using System.Buffers.Binary;
using System.Diagnostics;
using System.IO;

namespace BmsTool.Windows;

internal static class CommToolCanUpgrade
{
    private const int CacheBlockBytes = 48;
    private const uint LegacyCacheAppAddress = 0x08004800;
    private static readonly Version CompactLayoutAdapterVersion = new(0, 2, 10);
    private static readonly Version GuardedLayoutAdapterVersion = new(0, 2, 11);

    private static ushort Word(byte[] data, int index) => BinaryPrimitives.ReadUInt16LittleEndian(data.AsSpan(2 * index));

    private static void ValidateAppIdentity(byte[] info, SerialFirmwarePackage package)
    {
        if (info.Length != 24)
            throw new InvalidDataException("C073 升级能力回读长度错误。");
        var words = new ushort[12];
        for (int i = 0; i < words.Length; ++i) words[i] = Word(info, i);
        if (!SerialFirmwarePackage.IsC073RunningIdentity(words))
            throw new InvalidDataException("运行设备的身份、分区或模式不符。");
        UpgradeDetection.CheckPackage(UpgradeDetection.ParseCapabilities(words)!, package);
    }

    private const uint LegacyAppLimit = 0x0801C000; // First persistent page of the verified 103+309 firmware.
    internal sealed record LegacyIdentity(string Serial, string Hardware, string Software);

    internal static byte[] LoadLegacyBin(string path)
    {
        if (!Path.GetExtension(path).Equals(".bin", StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("旧项目 CAN 升级请选择该项目的 APP BIN。");
        if (new FileInfo(path).Length > LegacyAppLimit - LegacyCacheAppAddress)
            throw new InvalidDataException("旧项目 APP 镜像超出 0x0801C000 参数区边界。");
        byte[] app = File.ReadAllBytes(path);
        ValidateLegacyBin(app);
        return app;
    }

    private static void ValidateLegacyBin(byte[] app)
    {
        if (app.Length < 8 || app.Length > LegacyAppLimit - LegacyCacheAppAddress ||
            SerialFirmwarePackage.HasHeader(app))
            throw new InvalidDataException("旧项目 BIN 的大小或格式不适用。");
        if (app.Length >= 0x10C &&
            (SerialFirmwarePackage.IsC073Board(BinaryPrimitives.ReadUInt32LittleEndian(app.AsSpan(0x104))) ||
             BinaryPrimitives.ReadUInt32LittleEndian(app.AsSpan(0x108)) == SerialFirmwarePackage.C073AppBase))
            throw new InvalidDataException("此文件带 C073 身份，请使用 C073 升级流程。");
        uint stack = BinaryPrimitives.ReadUInt32LittleEndian(app);
        uint reset = BinaryPrimitives.ReadUInt32LittleEndian(app.AsSpan(4));
        if (stack < 0x20000000 || stack >= 0x20005000 || (reset & 1) == 0 ||
            reset < LegacyCacheAppAddress || reset >= LegacyCacheAppAddress + app.Length)
            throw new InvalidDataException("旧项目 APP 向量无效，请确认 BIN 从 0x08004800 链接。");
    }

    internal static LegacyIdentity RunLegacy(byte[] app, string expectedSoftware,
        Func<byte, byte[], CancellationToken, byte[]> command, IProgress<(int Percent, string Message)> progress,
        CancellationToken token, Func<LegacyIdentity, bool> confirm)
    {
        ValidateLegacyBin(app);
        if (CommToolOwcProtocol.TryReadFirmwareVersion(command, token) is null)
            throw new IOException("未读到 Comm Tool 版本，请重新连接后重试。");

        LegacyIdentity? target = null;
        LegacyIdentity ReadLegacyApp()
        {
            try
            {
                byte[] capabilities = command(0x10, [0x70, 0xD1, 12, 0], token);
                if (capabilities.Length != 24 || capabilities.Any(value => value != 0))
                    throw new InvalidDataException("设备提供独立升级身份，请使用该板对应的升级包。");
            }
            catch (CommToolCommandException ex) when (ex.Status == 7)
            {
                // 旧网关将不支持的寄存器映射为 BMS 错误。仍须取得 C002 和 D034 的正向证据。
            }

            byte[] info = command(0x10, [0x02, 0xC0, 48, 0], token);
            if (info.Length != 96 || command(0x10, [0x34, 0xD0, 2, 0], token).Length != 4)
                throw new InvalidDataException("旧板身份或 APP 状态回读格式错误。");
            string Field(int start) => ModbusRtu.DecodeAscii(Enumerable.Range(start, 16).Select(i => Word(info, i))).TrimEnd('�');
            var identity = new LegacyIdentity(Field(0), Field(16), Field(32));
            if (string.IsNullOrWhiteSpace(identity.Hardware) || string.IsNullOrWhiteSpace(identity.Software))
                throw new InvalidDataException("旧板未提供完整的硬件和软件版本，无法启动 CAN 升级。");
            return identity;
        }

        void ConfirmBeforeCache()
        {
            progress.Report((0, "正在读取旧项目板身份"));
            try { target = ReadLegacyApp(); }
            catch (CommToolCommandException ex) when (ex.Status is 6 or 7)
            {
                token.WaitHandle.WaitOne(250);
                token.ThrowIfCancellationRequested();
                try { target = ReadLegacyApp(); }
                catch (CommToolCommandException retry) when (retry.Status is 6 or 7)
                {
                    throw new IOException("旧板 APP 尚未确认。若设备已经停在 Boot，请按该项目的已知工装流程恢复。", retry);
                }
            }
            if (!confirm(target))
                throw new OperationCanceledException("已取消旧项目单板升级。");
            token.ThrowIfCancellationRequested();
        }

        Transfer(app, SerialFirmwarePackage.Crc32(app), LegacyCacheAppAddress, command, progress, token,
            ConfirmBeforeCache, () =>
            {
                progress.Report((30, "正在重新核对旧项目板身份"));
                if (ReadLegacyApp() != target)
                    throw new InvalidDataException("设备身份在缓存写入后发生变化，升级尚未启动。");
            }, "旧项目 BMS");

        progress.Report((96, "等待旧板重启并读回 APP"));
        var timer = Stopwatch.StartNew();
        while (timer.Elapsed < TimeSpan.FromSeconds(25))
        {
            token.ThrowIfCancellationRequested();
            try
            {
                var current = ReadLegacyApp();
                if (current.Serial != target!.Serial || current.Hardware != target.Hardware)
                    throw new InvalidDataException("重启后的硬件版本或序列号不一致。");
                if (expectedSoftware.Length != 0 && !string.Equals(current.Software, expectedSoftware, StringComparison.OrdinalIgnoreCase))
                    throw new InvalidDataException($"旧板已响应，读到软件版本 {current.Software}，与预期 {expectedSoftware} 不一致。");
                progress.Report((100, "旧板已重启，APP 已读回"));
                return current;
            }
            catch (Exception ex) when (ex is CommToolCommandException or TimeoutException)
            { token.WaitHandle.WaitOne(350); }
        }
        throw new TimeoutException("Comm Tool 已完成旧板传输，APP 尚未确认。");
    }

    internal static void Run(byte[] packageBytes, Func<byte, byte[], CancellationToken, byte[]> command,
        IProgress<(int Percent, string Message)> progress, CancellationToken token,
        Func<Version, bool>? confirmLegacySingleBoard)
    {
        var package = SerialFirmwarePackage.Parse(packageBytes);
        if (!SerialFirmwarePackage.IsC073Board(package.BoardId) || package.AppAddress != SerialFirmwarePackage.C073AppBase)
            throw new InvalidDataException("CAN 升级需要 APP 起点为 0x08003000 的 C073 固件。");
        bool compact = SerialFirmwarePackage.C073ImageCapacity(package.BoardId, package.AppAddress, package.Version)
            == SerialFirmwarePackage.C073CompactCapacity;
        var minimumAdapter = package.BoardId == SerialFirmwarePackage.C073GuardedBoardId
            ? GuardedLayoutAdapterVersion : CompactLayoutAdapterVersion;
        var version = CommToolOwcProtocol.TryReadFirmwareVersion(command, token);
        bool legacy = version is { Major: 0, Minor: 2, Build: 3 or 4 or 6 or 7 or 8 };
        if (compact && version is null)
            throw new IOException("未读到 Comm Tool 固件版本，无法选择 CAN 升级流程。请重新连接转接器后重试。");
        if (compact && !legacy && version < minimumAdapter)
            throw new InvalidDataException($"检测到 Comm Tool {version}。该版本升级此 C073 需要更新至 {minimumAdapter} 或使用直连串口、BLE。");
        byte[] app = packageBytes[SerialFirmwarePackage.HeaderBytes..];
        Transfer(app, package.PayloadCrc, legacy ? LegacyCacheAppAddress : package.AppAddress, command, progress, token,
            legacy ? () =>
            {
                ConfirmRunningApp(0);
                if (confirmLegacySingleBoard?.Invoke(version!) != true)
                    throw new OperationCanceledException("已取消旧版 Comm Tool 单板升级。");
                token.ThrowIfCancellationRequested();
                progress.Report((0, $"已确认 C073，使用 Comm Tool {version} 单板兼容升级"));
            } : null, () => ConfirmRunningApp(30), "C073");

        void ConfirmRunningApp(int percent)
        {
            progress.Report((percent, "正在通过 CAN 确认 C073 身份"));
            for (int attempt = 0; attempt < 2; ++attempt)
            {
                try
                {
                    ValidateAppIdentity(command(0x10, [0x70, 0xD1, 12, 0], token), package);
                    return;
                }
                catch (CommToolCommandException ex) when (ex.Status == 6)
                {
                    if (attempt == 0)
                    {
                        token.WaitHandle.WaitOne(250);
                        token.ThrowIfCancellationRequested();
                        continue;
                    }
                    if (legacy || compact)
                        throw new IOException("CAN 未读到匹配的 C073 APP 身份。请确认钥匙与连线。设备若已在 Boot，请通过直连串口或 BLE 恢复。", ex);
                    progress.Report((percent, "未收到 APP 应答，转入 CAN Boot 身份检查"));
                }
            }
        }

        progress.Report((96, "等待 C073 重启并校验运行镜像"));
        var verify = Stopwatch.StartNew();
        while (verify.Elapsed < TimeSpan.FromSeconds(25))
        {
            token.ThrowIfCancellationRequested();
            try
            {
                ValidateAppIdentity(command(0x10, [0x70, 0xD1, 12, 0], token), package);
                byte[] image = command(0x10, [0x7C, 0xD1, 8, 0], token);
                uint Pair(int index) => ((uint)Word(image, index) << 16) | Word(image, index+1);
                if (image.Length != 16 || Pair(0) != package.Version || Pair(2) != app.Length ||
                    Pair(4) != package.PayloadCrc || Word(image, 6) != 1 || Word(image, 7) != 0)
                    throw new InvalidDataException("运行 APP 的版本、长度、CRC32 或有效标记不符。");
                progress.Report((100, "C073 运行镜像校验通过"));
                return;
            }
            catch (Exception ex) when (ex is CommToolCommandException or TimeoutException)
            { token.WaitHandle.WaitOne(350); }
        }
        throw new TimeoutException("Comm Tool 已完成传输，C073 APP 尚未确认。");
    }
    private static void Transfer(byte[] app, uint payloadCrc, uint cacheAddress,
        Func<byte, byte[], CancellationToken, byte[]> command, IProgress<(int Percent, string Message)> progress,
        CancellationToken token, Action? confirmBeforeCache, Action confirmBeforeStart, string targetName)
    {
        byte[] config = new byte[8];
        BinaryPrimitives.WriteUInt32LittleEndian(config, 250000);
        config[4] = 1;
        void Ack(byte cmd, byte[] data)
        {
            if (command(cmd, data, token).Length != 0)
                throw new InvalidDataException($"Comm Tool 命令 {cmd:X2} 应答长度错误。");
        }
        byte[] Query(byte cmd)
        {
            try { return command(cmd, [], token); }
            catch (TimeoutException)
            {
                // 0.2.9 没有半帧超时。GET_INFO 忽略请求体，用填充帧消耗遗留长度；
                // 它本身的应答也可能被吞掉，随后以新序号读取真实状态。
                byte[] padding = new byte[512];
                Array.Fill(padding, (byte)0xff);
                try { command(0x01, padding, token); }
                catch (TimeoutException) { }
                return command(cmd, [], token);
            }
        }
        byte[] status = Query(0x32);
        if (status.Length != 13 || status[0] == 1)
            throw new IOException("Comm Tool 状态不可用或已有升级正在进行。");
        Ack(0x02, config);
        confirmBeforeCache?.Invoke();
        byte[] meta = new byte[14];
        // 旧转接器将 0x08004800 用作缓存分类。CAN START 只发长度与 CRC16，C073 Boot 按自身分区写入。
        BinaryPrimitives.WriteUInt32LittleEndian(meta, cacheAddress);
        BinaryPrimitives.WriteUInt32LittleEndian(meta.AsSpan(4), (uint)app.Length);
        BinaryPrimitives.WriteUInt16LittleEndian(meta.AsSpan(8), ModbusRtu.Crc16(app));
        BinaryPrimitives.WriteUInt32LittleEndian(meta.AsSpan(10), payloadCrc);
        bool Matches(byte[] stored, byte valid) => stored.Length == 15 && stored[14] == valid &&
            stored.AsSpan(0, 14).SequenceEqual(meta);

        // 转接器的 Flash 缓存跨串口会话保留，只有包身份不匹配才重新写入。
        byte[] stored = Query(0x23);
        if (stored.Length != 15 || stored[14] > 1)
            throw new InvalidDataException("Comm Tool 缓存状态回读格式错误。");

        if (Matches(stored, 1))
        {
            progress.Report((30, "已复用 Comm Tool 中校验通过的缓存"));
        }
        else
        {
            StageCache();
        }

        confirmBeforeStart();

        void StageCache()
        {
            for (int attempt = 0; ; ++attempt)
            {
                token.ThrowIfCancellationRequested();
                Ack(0x20, meta);
                int pos = 0;
                try
                {
                    while (pos < app.Length)
                    {
                        int count = Math.Min(CacheBlockBytes, app.Length - pos);
                        byte[] data = new byte[count + 4];
                        BinaryPrimitives.WriteUInt32LittleEndian(data, (uint)pos);
                        Array.Copy(app, pos, data, 4, count);
                        Ack(0x21, data);
                        pos += count;
                        progress.Report((pos * 30 / app.Length, "正在写入 Comm Tool 缓存"));
                    }
                }
                catch (Exception ex) when (ex is TimeoutException or FormatException)
                {
                    string where = $"Comm Tool 缓存写入 0x21 在偏移 {pos}/{app.Length}、每块 {CacheBlockBytes} 字节处应答异常";
                    if (attempt != 0) throw new IOException(where + "。BMS 升级尚未启动。", ex);
                    progress.Report((pos * 30 / app.Length, where + "，正在核对缓存"));
                    // 旧转接器没有分块写入游标，收到超时后只能重新开一次缓存事务。
                    byte[] partial = Query(0x23);
                    byte[] idle = Query(0x32);
                    if (!Matches(partial, 0) || idle.Length != 13 || idle[0] == 1)
                        throw new IOException(where + "，缓存身份或状态无法确认。", ex);
                    continue;
                }
                try { Ack(0x22, meta[4..]); }
                catch (Exception ex) when (ex is TimeoutException or FormatException)
                {
                    progress.Report((30, "Comm Tool 缓存提交应答缺失，正在回读校验结果"));
                }
                if (!Matches(Query(0x23), 1))
                    throw new InvalidDataException("Comm Tool 缓存未通过固件长度、地址及 CRC 回读校验。");
                return;
            }
        }

        bool startConfirmed = true;
        bool previousJobComplete = status[0] == 2;
        try
        {
            try { Ack(0x31, []); }
            catch (Exception ex) when (ex is TimeoutException or FormatException)
            {
                startConfirmed = false;
                progress.Report((30, "启动应答缺失，正在查询 Comm Tool 的本次传输"));
            }
            var timer = Stopwatch.StartNew();
            int lastPercent = 30;
            while (true)
            {
                token.ThrowIfCancellationRequested();
                if (timer.Elapsed > TimeSpan.FromMinutes(3)) throw new TimeoutException("CAN 升级超时。");
                try { status = command(0x32, [], token); }
                catch (Exception ex) when (ex is TimeoutException or FormatException)
                {
                    progress.Report((lastPercent, "等待 Comm Tool 恢复进度应答"));
                    token.WaitHandle.WaitOne(200);
                    continue;
                }
                if (status.Length != 13 || status[1] > 100 ||
                    BinaryPrimitives.ReadUInt32LittleEndian(status.AsSpan(7)) != app.Length)
                    throw new InvalidDataException("Comm Tool 返回的升级进度无效。");
                if (status[0] == 2 && (startConfirmed || !previousJobComplete))
                {
                    if (status[1] != 100 || status[2] != 0 ||
                        BinaryPrimitives.ReadUInt32LittleEndian(status.AsSpan(3)) != app.Length)
                        throw new InvalidDataException("Comm Tool 返回的 CAN 刷写完成状态不完整。");
                    break;
                }
                if (status[0] == 3 && status[2] == 0x23)
                    throw new IOException($"Comm Tool 已校验缓存，CAN 未确认 {targetName} 身份（0x23）。请核对设备、钥匙及 CAN 连线。");
                if (!startConfirmed && status[0] != 1)
                    throw new IOException("Comm Tool 启动应答丢失，未观察到本次 CAN 传输。请重连核对运行镜像。");
                startConfirmed = true;
                if (status[0] != 1) throw new IOException($"CAN 升级终止，状态 {status[0]}，错误 {status[2]:X2}。");
                lastPercent = 30 + status[1] * 65 / 100;
                progress.Report((lastPercent, $"正在通过 CAN 写入 {targetName}"));
                token.WaitHandle.WaitOne(200);
            }
        }
        catch (OperationCanceledException) when (token.IsCancellationRequested)
        {
            using var stop = new CancellationTokenSource(TimeSpan.FromSeconds(2));
            try { command(0x33, [], stop.Token); }
            catch (Exception ex) when (ex is IOException or TimeoutException or OperationCanceledException or FormatException) { }
            throw;
        }
    }
}
