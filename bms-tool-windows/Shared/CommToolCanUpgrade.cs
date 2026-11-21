using System.Buffers.Binary;
using System.Diagnostics;
using System.IO;

namespace BmsTool.Windows;

internal static class CommToolCanUpgrade
{
    internal static void Run(byte[] packageBytes, Func<byte, byte[], CancellationToken, byte[]> command,
        IProgress<(int Percent, string Message)> progress, CancellationToken token)
    {
        var package = SerialFirmwarePackage.Parse(packageBytes);
        if (package.BoardId != 0xC073000D || package.AppAddress != 0x08003000)
            throw new InvalidDataException("CAN 升级需要 APP 起点为 0x08003000 的 C073 固件。");
        byte[] app = packageBytes[SerialFirmwarePackage.HeaderBytes..];
        byte[] config = new byte[8];
        BinaryPrimitives.WriteUInt32LittleEndian(config, 250000);
        config[4] = 1;
        void Ack(byte cmd, byte[] data)
        {
            if (command(cmd, data, token).Length != 0)
                throw new InvalidDataException($"Comm Tool 命令 {cmd:X2} 应答长度错误。");
        }
        byte[] status = command(0x32, [], token);
        if (status.Length != 13 || status[0] == 1)
            throw new IOException("Comm Tool 状态不可用或已有升级正在进行。");
        Ack(0x02, config);
        byte[] meta = new byte[14];
        BinaryPrimitives.WriteUInt32LittleEndian(meta, package.AppAddress);
        BinaryPrimitives.WriteUInt32LittleEndian(meta.AsSpan(4), (uint)app.Length);
        BinaryPrimitives.WriteUInt16LittleEndian(meta.AsSpan(8), ModbusRtu.Crc16(app));
        BinaryPrimitives.WriteUInt32LittleEndian(meta.AsSpan(10), package.PayloadCrc);
        Ack(0x20, meta);
        for (int pos = 0; pos < app.Length;)
        {
            int count = Math.Min(480, app.Length - pos);
            byte[] data = new byte[count + 4];
            BinaryPrimitives.WriteUInt32LittleEndian(data, (uint)pos);
            Array.Copy(app, pos, data, 4, count);
            Ack(0x21, data);
            pos += count;
            progress.Report((pos * 30 / app.Length, "正在写入 Comm Tool 缓存"));
        }
        Ack(0x22, meta[4..]);
        byte[] stored = command(0x23, [], token);
        if (stored.Length != 15 || stored[14] != 1 || !stored.AsSpan(0, 14).SequenceEqual(meta))
            throw new InvalidDataException("Comm Tool 缓存回读与固件不一致。");

        bool transferring = true;
        try
        {
            Ack(0x31, []);
            var timer = Stopwatch.StartNew();
            while (true)
            {
                token.ThrowIfCancellationRequested();
                if (timer.Elapsed > TimeSpan.FromMinutes(3)) throw new TimeoutException("CAN 升级超时。");
                status = command(0x32, [], token);
                if (status.Length != 13 || status[1] > 100 ||
                    BinaryPrimitives.ReadUInt32LittleEndian(status.AsSpan(7)) != app.Length)
                    throw new InvalidDataException("Comm Tool 返回的升级进度无效。");
                if (status[0] == 2) break;
                if (status[0] != 1) throw new IOException($"CAN 升级终止，状态 {status[0]}，错误 {status[2]:X2}。");
                progress.Report((30 + status[1] * 65 / 100, "正在通过 CAN 写入 C073"));
                token.WaitHandle.WaitOne(200);
            }
            transferring = false;
        }
        finally
        {
            if (transferring)
            {
                using var stop = new CancellationTokenSource(TimeSpan.FromSeconds(2));
                try { command(0x33, [], stop.Token); }
                catch (Exception ex) when (ex is IOException or TimeoutException or OperationCanceledException) { }
            }
        }
        progress.Report((96, "等待 C073 重启并校验运行镜像"));
        var verify = Stopwatch.StartNew();
        while (verify.Elapsed < TimeSpan.FromSeconds(25))
        {
            token.ThrowIfCancellationRequested();
            try
            {
                byte[] info = command(0x10, [0x70, 0xD1, 12, 0], token);
                if (info.Length != 24) throw new InvalidDataException("C073 升级能力回读长度错误。");
                ushort Word(byte[] data, int index) => BinaryPrimitives.ReadUInt16LittleEndian(data.AsSpan(2 * index));
                ushort[] expected = [0x5550, 1, 0xC073, 0x000D, 2, 2, 0x0800, 0x3000, 0, 0xB800, 0, 0];
                for (int i = 0; i < expected.Length; ++i)
                    if (Word(info, i) != expected[i]) throw new InvalidDataException("运行设备的身份、分区或模式不符。");
                byte[] image = command(0x10, [0x7C, 0xD1, 8, 0], token);
                uint Pair(int index) => ((uint)Word(image, index) << 16) | Word(image, index+1);
                if (image.Length != 16 || Pair(0) != package.Version || Pair(2) != app.Length ||
                    Pair(4) != package.PayloadCrc || Word(image, 6) != 1)
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
