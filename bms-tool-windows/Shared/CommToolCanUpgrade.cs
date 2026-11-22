using System.Buffers.Binary;
using System.Diagnostics;
using System.IO;

namespace BmsTool.Windows;

internal static class CommToolCanUpgrade
{
    private const int CacheBlockBytes = 48;
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

    internal static void Run(byte[] packageBytes, Func<byte, byte[], CancellationToken, byte[]> command,
        IProgress<(int Percent, string Message)> progress, CancellationToken token)
    {
        var package = SerialFirmwarePackage.Parse(packageBytes);
        if (!SerialFirmwarePackage.IsC073Board(package.BoardId) || package.AppAddress != SerialFirmwarePackage.C073AppBase)
            throw new InvalidDataException("CAN 升级需要 APP 起点为 0x08003000 的 C073 固件。");
        bool compact = SerialFirmwarePackage.C073ImageCapacity(package.BoardId, package.AppAddress, package.Version)
            == SerialFirmwarePackage.C073CompactCapacity;
        var minimumAdapter = package.BoardId == SerialFirmwarePackage.C073GuardedBoardId
            ? GuardedLayoutAdapterVersion : CompactLayoutAdapterVersion;
        if (compact && (CommToolOwcProtocol.TryReadFirmwareVersion(command, token) is not { } version ||
                        version < minimumAdapter))
            throw new InvalidDataException($"此 44 KiB CAN 升级要求 Comm Tool {minimumAdapter} 或更新版本。");
        byte[] app = packageBytes[SerialFirmwarePackage.HeaderBytes..];
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
        byte[] meta = new byte[14];
        BinaryPrimitives.WriteUInt32LittleEndian(meta, package.AppAddress);
        BinaryPrimitives.WriteUInt32LittleEndian(meta.AsSpan(4), (uint)app.Length);
        BinaryPrimitives.WriteUInt16LittleEndian(meta.AsSpan(8), ModbusRtu.Crc16(app));
        BinaryPrimitives.WriteUInt32LittleEndian(meta.AsSpan(10), package.PayloadCrc);
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

        // RTC 的第一帧可能只唤醒设备。Boot 中的 C073 由转接器通过 CAN-IAP Hello 校验，
        // APP 有回复时必须通过完整身份校验。
        progress.Report((30, "正在通过 CAN 确认 C073 身份"));
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            try
            {
                ValidateAppIdentity(command(0x10, [0x70, 0xD1, 12, 0], token), package);
                break;
            }
            catch (CommToolCommandException ex) when (ex.Status == 6)
            {
                if (attempt == 0)
                    token.WaitHandle.WaitOne(250);
                else if (compact)
                    throw new IOException("CAN 未读到 44 KiB APP 布局。设备若已留在 Boot，请通过直连串口或 BLE 恢复。", ex);
                else
                    progress.Report((30, "未收到 APP 应答，转入 CAN Boot 身份检查"));
                token.ThrowIfCancellationRequested();
            }
        }

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
                    if (attempt != 0) throw new IOException(where + "。C073 升级尚未启动。", ex);
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
                    throw new IOException("Comm Tool 已校验缓存，CAN 未确认 C073 身份（0x23）。请确认钥匙合上、设备已唤醒及 CAN 连线。");
                if (!startConfirmed && status[0] != 1)
                    throw new IOException("Comm Tool 启动应答丢失，未观察到本次 CAN 传输。请重连核对运行镜像。");
                startConfirmed = true;
                if (status[0] != 1) throw new IOException($"CAN 升级终止，状态 {status[0]}，错误 {status[2]:X2}。");
                lastPercent = 30 + status[1] * 65 / 100;
                progress.Report((lastPercent, "正在通过 CAN 写入 C073"));
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
}
