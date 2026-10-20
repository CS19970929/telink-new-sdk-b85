using System.Buffers.Binary;
using System.Diagnostics;
using System.IO;
using System.Security.Cryptography;

namespace BmsTool.Windows;

public sealed record TelinkSerialImage(byte Product, uint BuildId, uint DescriptorOffset, uint Crc)
{
    public static TelinkSerialImage Inspect(FirmwareImage image)
    {
        byte[] bytes = image.Bytes;
        if (image.TargetKind != OtaTargetKind.Telink || !image.HasValidTelinkCrcTrailer ||
            !image.HasD008TlnkStartupMarker || bytes.Length > 124 * 1024)
            throw new InvalidDataException("串口升级要求有效的 Telink 正式 BIN。");
        byte[] magic = System.Text.Encoding.ASCII.GetBytes("BMSU8251");
        TelinkSerialImage? found = null;
        for (int i = 0; i <= bytes.Length - 20; i++)
        {
            if (!bytes.AsSpan(i, 8).SequenceEqual(magic)) continue;
            if (bytes[i + 8] != 1 || bytes[i + 10] != 1 || bytes[i + 11] != 0 ||
                BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(i + 16, 4)) != 124 * 1024 ||
                bytes[i + 9] is not (8 or 11 or 13 or 14)) continue;
            if (found is not null) throw new InvalidDataException("BIN 含重复的串口升级描述，已停止。");
            found = new(bytes[i + 9], BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(i + 12, 4)),
                (uint)i, BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(bytes.Length - 4)));
        }
        return found ?? throw new InvalidDataException("BIN 没有 TLSR8251 串口升级描述；请先通过 BLE 或既有烧录方式安装支持版本。");
    }
}

public sealed record TelinkSerialStatus(byte State, byte Error, byte Product, uint Session,
    uint NextOffset, uint Size, uint BuildId, uint Crc);

/// <summary>私有0x46串口协议；每次写入有确认，未知结果停止，不自动重发写命令。</summary>
public sealed class TelinkSerialOtaClient : IDisposable
{
    private readonly IBmsTransport _transport;
    private readonly object _sync = new();
    private readonly List<byte> _rx = new();
    private TaskCompletionSource<TelinkSerialStatus>? _pending;
    private ushort _sequence, _expectedSequence;
    private byte _expectedCommand;
    private uint _expectedSession;
    private bool _disposed;
    public event Action<string>? Log;
    public event Action<OtaProgress>? Progress;
    public TimeSpan RequestTimeout { get; set; } = TimeSpan.FromSeconds(3);

    public TelinkSerialOtaClient(IBmsTransport transport)
    {
        _transport = transport;
        _transport.DataReceived += Receive;
    }
    public static byte[] BuildRequest(byte command, ushort sequence, uint session, byte[] payload)
    {
        if (payload.Length > 132) throw new ArgumentOutOfRangeException(nameof(payload));
        byte[] frame = new byte[11 + payload.Length];
        frame[0] = 1; frame[1] = 0x46; frame[2] = command;
        BinaryPrimitives.WriteUInt16LittleEndian(frame.AsSpan(3, 2), sequence);
        BinaryPrimitives.WriteUInt32LittleEndian(frame.AsSpan(5, 4), session);
        payload.CopyTo(frame, 9);
        BinaryPrimitives.WriteUInt16LittleEndian(frame.AsSpan(frame.Length - 2), ModbusRtu.Crc16(frame.AsSpan(0, frame.Length - 2)));
        return frame;
    }
    public async Task<TelinkSerialStatus> QueryAsync(CancellationToken ct)
        => await ExchangeAsync(0, 0, Array.Empty<byte>(), ct);

    public async Task<bool> UpgradeAsync(FirmwareImage image, CancellationToken ct)
    {
        TelinkSerialImage info = TelinkSerialImage.Inspect(image);
        TelinkSerialStatus device = await ProbeAsync(ct);
        if (device.Product != info.Product) throw new InvalidDataException($"BIN 产品 D{info.Product:000} 与设备 D{device.Product:000} 不匹配。");
        if (device.State is >= 1 and <= 5) throw new IOException("设备存在升级会话，请等待其结束或超时后再操作。");
        uint session;
        using (RandomNumberGenerator random = RandomNumberGenerator.Create())
        {
            byte[] nonce = new byte[4]; random.GetBytes(nonce);
            session = BinaryPrimitives.ReadUInt32LittleEndian(nonce) | 1u;
        }
        byte[] begin = new byte[17]; begin[0] = info.Product;
        BinaryPrimitives.WriteUInt32LittleEndian(begin.AsSpan(1), (uint)image.ImageSize);
        BinaryPrimitives.WriteUInt32LittleEndian(begin.AsSpan(5), info.BuildId);
        BinaryPrimitives.WriteUInt32LittleEndian(begin.AsSpan(9), info.DescriptorOffset);
        BinaryPrimitives.WriteUInt32LittleEndian(begin.AsSpan(13), info.Crc);
        bool committed = false;
        bool commitStarted = false;
        bool began = false;
        try
        {
            Log?.Invoke($"UART OTA BEGIN D{info.Product:000}; bytes={image.ImageSize}; Build={info.BuildId:x8}; CRC={info.Crc:x8}");
            await ExchangeAsync(1, session, begin, ct); began = true;
            await WaitStateAsync(session, 2, ct);
            var elapsed = Stopwatch.StartNew();
            for (int offset = 0; offset < image.ImageSize;)
            {
                int count = Math.Min(128, image.ImageSize - offset);
                byte[] data = new byte[4 + count];
                BinaryPrimitives.WriteUInt32LittleEndian(data, (uint)offset);
                image.Bytes.AsSpan(offset, count).CopyTo(data.AsSpan(4));
                TelinkSerialStatus ack = await ExchangeAsync(2, session, data, ct);
                if (ack.State != 2 || ack.NextOffset != offset + count)
                    throw new IOException("串口升级的设备确认偏移不匹配，已停止。");
                offset += count;
                double speed = offset / Math.Max(0.001, elapsed.Elapsed.TotalSeconds);
                Progress?.Invoke(new OtaProgress(offset * 100.0 / image.ImageSize, offset, image.ImageSize,
                    speed, TimeSpan.FromSeconds((image.ImageSize - offset) / speed), OtaTransferMode.Auto));
            }
            await ExchangeAsync(3, session, Array.Empty<byte>(), ct);
            await WaitStateAsync(session, 4, ct);
            Log?.Invoke("UART OTA 镜像读回及 CRC 已通过，提交启动标志。");
            commitStarted = true;
            TelinkSerialStatus result = await ExchangeAsync(4, session, Array.Empty<byte>(), ct);
            if (result.State != 5) throw new IOException("启动标志提交结果未知，已停止；请重新读取设备身份。");
            committed = true;
            await Task.Delay(1500, ct);
            await VerifyBootAsync(info, ct);
            Log?.Invoke("UART OTA 新程序 Build ID 与镜像 CRC 已确认，继续检查 BMS 通信。");
            return true;
        }
        catch (OperationCanceledException ex) when (commitStarted)
        {
            throw new IOException("提交期间已取消等待，实际升级结果未知；请重新核对设备镜像。", ex);
        }
        finally
        {
            if (began && !committed)
            {
                // ABORT只取消未提交会话；固件拒绝撤销COMMITTED。自身有界，不重发写命令。
                try { await ExchangeAsync(5, session, Array.Empty<byte>(), CancellationToken.None); }
                catch (Exception ex) { Log?.Invoke("UART OTA 退出未确认：" + ex.Message); }
            }
        }
    }
    private async Task<TelinkSerialStatus> ProbeAsync(CancellationToken ct)
    {
        // 仅QUERY可重试，用于D008/D014唤醒首包；没有应答时绝不进入BEGIN。
        for (int i = 0; ; i++)
        {
            try { return await QueryAsync(ct); }
            catch (TimeoutException) when (i < 2) { await Task.Delay(150, ct); }
        }
    }
    private async Task WaitStateAsync(uint session, byte wanted, CancellationToken ct)
    {
        var watch = Stopwatch.StartNew();
        while (watch.Elapsed < TimeSpan.FromSeconds(30))
        {
            await Task.Delay(30, ct);
            TelinkSerialStatus status = await ExchangeAsync(0, session, Array.Empty<byte>(), ct);
            if (status.Error != 0) throw new IOException($"设备升级失败：error={status.Error}; state={status.State}。");
            if (status.State == wanted) return;
            if (status.State != 1 && status.State != 3) throw new IOException($"升级阶段异常：state={status.State}。");
        }
        throw new TimeoutException("等待设备擦除或校验超时，已停止。");
    }
    private async Task VerifyBootAsync(TelinkSerialImage info, CancellationToken ct)
    {
        Exception? last = null;
        for (int i = 0; i < 12; i++)
        {
            ct.ThrowIfCancellationRequested();
            try
            {
                TelinkSerialStatus status = await QueryAsync(ct);
                if (status.State == 0 && status.Product == info.Product &&
                    status.BuildId == info.BuildId && status.Crc == info.Crc) return;
                last = new IOException($"设备镜像未匹配：state={status.State}; Build={status.BuildId:x8}; CRC={status.Crc:x8}。");
            }
            catch (TimeoutException ex) { last = ex; }
            await Task.Delay(500, ct);
        }
        throw new IOException("固件已提交，但重启后镜像未确认，升级结果未知。", last);
    }
    private async Task<TelinkSerialStatus> ExchangeAsync(byte command, uint session, byte[] payload, CancellationToken ct)
    {
        ct.ThrowIfCancellationRequested();
        TaskCompletionSource<TelinkSerialStatus> completion;
        ushort sequence;
        lock (_sync)
        {
            if (_disposed) throw new ObjectDisposedException(nameof(TelinkSerialOtaClient));
            if (_pending is not null) throw new InvalidOperationException("串口升级命令不能并发。");
            _sequence++; sequence = _sequence;
            _expectedSequence = sequence; _expectedCommand = command; _expectedSession = session;
            _rx.Clear();
            completion = _pending = new(TaskCreationOptions.RunContinuationsAsynchronously);
        }
        try
        {
            await _transport.WriteAsync(BuildRequest(command, sequence, session, payload), ct);
            Task delay = Task.Delay(RequestTimeout, ct);
            if (await Task.WhenAny(completion.Task, delay) != completion.Task)
            {
                ct.ThrowIfCancellationRequested();
                throw new TimeoutException(command == 0 ? "TLSR8251 串口升级查询无响应。" : "升级命令结果未知，已停止，不自动重发。");
            }
            TelinkSerialStatus status = await completion.Task;
            if (status.Error != 0 && command != 0) throw new IOException($"设备拒绝升级命令 {command}：error={status.Error}; state={status.State}。提交阶段错误须重新核对实际启动镜像。");
            return status;
        }
        finally { lock (_sync) { if (ReferenceEquals(_pending, completion)) _pending = null; } }
    }
    private void Receive(ReadOnlyMemory<byte> bytes)
    {
        lock (_sync)
        {
            if (_disposed || _pending is null) return;
            foreach (byte value in bytes.Span)
            {
                _rx.Add(value);
                if (_rx.Count > 512) _rx.RemoveAt(0);
            }
            while (_rx.Count >= 30)
            {
                byte[] p = _rx.Take(30).ToArray();
                if (p[0] != 1 || p[1] != 0x46 || ModbusRtu.Crc16(p.AsSpan(0, 28)) != BinaryPrimitives.ReadUInt16LittleEndian(p.AsSpan(28)))
                { _rx.RemoveAt(0); continue; }
                _rx.RemoveRange(0, 30);
                if (p[2] != _expectedCommand || BinaryPrimitives.ReadUInt16LittleEndian(p.AsSpan(3)) != _expectedSequence) continue;
                uint session = BinaryPrimitives.ReadUInt32LittleEndian(p.AsSpan(8));
                if (p[6] == 0 && _expectedSession != 0 && session != _expectedSession) continue;
                _pending.TrySetResult(new(p[5], p[6], p[7], session,
                    BinaryPrimitives.ReadUInt32LittleEndian(p.AsSpan(12)), BinaryPrimitives.ReadUInt32LittleEndian(p.AsSpan(16)),
                    BinaryPrimitives.ReadUInt32LittleEndian(p.AsSpan(20)), BinaryPrimitives.ReadUInt32LittleEndian(p.AsSpan(24))));
            }
        }
    }
    public void Dispose()
    {
        _transport.DataReceived -= Receive;
        lock (_sync)
        {
            _disposed = true;
            _pending?.TrySetException(new ObjectDisposedException(nameof(TelinkSerialOtaClient)));
            _rx.Clear();
        }
    }
}
