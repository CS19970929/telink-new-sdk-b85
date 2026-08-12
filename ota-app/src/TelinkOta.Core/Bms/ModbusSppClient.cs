using TelinkOta.Core.Ota;

namespace TelinkOta.Core.Bms;

/// <summary>
/// Modbus RTU over SPP 客户端：严格单飞、自动重组 20 字节通知分片。
/// 每次请求结束后保留一个短静默窗，使设备迟到的重复通知在下一请求开始前被丢弃。
/// </summary>
public sealed class ModbusSppClient : IDisposable
{
    private static readonly TimeSpan DuplicateGuard = TimeSpan.FromMilliseconds(40);

    private readonly IBleTransport _transport;
    private readonly byte _slaveAddr;
    private readonly object _gate = new();
    private readonly SemaphoreSlim _singleFlight = new(1, 1);
    private byte[] _acc = Array.Empty<byte>();
    private PendingRequest? _pending;
    private TaskCompletionSource<byte[]?>? _tcs;
    private bool _disposed;

    public ModbusSppClient(IBleTransport transport, byte slaveAddr = ModbusRtu.DefaultSlaveAddr)
    {
        _transport = transport ?? throw new ArgumentNullException(nameof(transport));
        _slaveAddr = slaveAddr;
        _transport.SppNotifyReceived += OnNotify;
    }

    /// <summary>读取保持寄存器（功能码 0x03），返回数据区；超时/异常/写入失败返回 null。</summary>
    public async Task<byte[]?> ReadRegistersAsync(ushort startRegister, ushort quantity,
        TimeSpan timeout, CancellationToken ct)
    {
        byte[] request = ModbusRtu.BuildReadRequest(startRegister, quantity, _slaveAddr);
        var pending = new PendingRequest(ModbusRtu.FuncReadHolding, startRegister, quantity, quantity * 2);
        return await ExecuteAsync(request, pending, timeout, ct);
    }

    /// <summary>写多个保持寄存器（功能码 0x10）；仅在设备回包与请求地址/数量完全一致时返回 true。</summary>
    public async Task<bool> WriteMultipleRegistersAsync(ushort startRegister, byte[] registerData,
        TimeSpan timeout, CancellationToken ct)
    {
        byte[] request = ModbusRtu.BuildWriteMultipleRequest(startRegister, registerData, _slaveAddr);
        ushort quantity = (ushort)(registerData.Length / 2);
        var pending = new PendingRequest(ModbusRtu.FuncWriteMultiple, startRegister, quantity, 0);
        return await ExecuteAsync(request, pending, timeout, ct) is not null;
    }

    private async Task<byte[]?> ExecuteAsync(byte[] request, PendingRequest pending,
        TimeSpan timeout, CancellationToken ct)
    {
        await _singleFlight.WaitAsync(ct);
        TaskCompletionSource<byte[]?> tcs;
        try
        {
            ThrowIfDisposed();
            lock (_gate)
            {
                _acc = Array.Empty<byte>();
                _pending = pending;
                tcs = _tcs = new TaskCompletionSource<byte[]?>(TaskCreationOptions.RunContinuationsAsynchronously);
            }

            if (!await _transport.WriteSppAsync(request, ct))
                return null;

            try
            {
                return await tcs.Task.WaitAsync(timeout, ct);
            }
            catch (TimeoutException)
            {
                return null;
            }
            catch (OperationCanceledException)
            {
                return null;
            }
        }
        finally
        {
            lock (_gate)
            {
                _pending = null;
                _tcs = null;
                _acc = Array.Empty<byte>();
            }

            // Telink Notify 偶发重复投递；在锁仍被占用时留出静默窗，迟到副本会因无人等待而被忽略。
            await Task.Delay(DuplicateGuard, CancellationToken.None);
            _singleFlight.Release();
        }
    }

    private void OnNotify(byte[] data)
    {
        if (data is null || data.Length == 0)
            return;

        lock (_gate)
        {
            if (_tcs is null || _tcs.Task.IsCompleted || _pending is null)
                return;

            // 合法最大 0x03 回包为 255 字节；留出混杂/错位空间但限制异常通知导致的无限增长。
            if (_acc.Length + data.Length > 1024)
                _acc = Array.Empty<byte>();

            var buf = new byte[_acc.Length + data.Length];
            _acc.CopyTo(buf, 0);
            data.CopyTo(buf, _acc.Length);
            _acc = buf;
            TryComplete();
        }
    }

    private void TryComplete()
    {
        PendingRequest pending = _pending!;
        while (_acc.Length >= 3)
        {
            if (_acc[0] != _slaveAddr)
            {
                _acc = _acc[1..];
                continue;
            }

            // Modbus 异常响应：[addr][func|0x80][exception][crc16]
            if (_acc[1] == (byte)(pending.Function | 0x80))
            {
                if (_acc.Length < 5)
                    return;
                if (HasValidCrc(_acc, 5))
                {
                    _acc = _acc[5..];
                    _tcs!.TrySetResult(null);
                    return;
                }
                _acc = _acc[1..];
                continue;
            }

            if (pending.Function == ModbusRtu.FuncReadHolding)
            {
                int byteCount = _acc[2];
                int total = 3 + byteCount + 2;
                if (_acc[1] == pending.Function && byteCount == pending.ExpectedPayloadBytes)
                {
                    if (_acc.Length < total)
                        return;
                    if (HasValidCrc(_acc, total))
                    {
                        var payload = new byte[byteCount];
                        Array.Copy(_acc, 3, payload, 0, byteCount);
                        _acc = _acc[total..];
                        _tcs!.TrySetResult(payload);
                        return;
                    }
                    _acc = _acc[1..];
                    continue;
                }
            }
            else if (pending.Function == ModbusRtu.FuncWriteMultiple)
            {
                const int total = 8;
                if (_acc[1] == pending.Function)
                {
                    if (_acc.Length < total)
                        return;
                    if (ModbusRtu.TryParseWriteMultipleResponse(
                            _acc.AsSpan(0, total).ToArray(), pending.StartRegister, pending.Quantity, _slaveAddr))
                    {
                        _acc = _acc[total..];
                        _tcs!.TrySetResult(Array.Empty<byte>());
                        return;
                    }
                    _acc = _acc[1..];
                    continue;
                }
            }

            _acc = _acc[1..];
        }
    }

    private static bool HasValidCrc(byte[] frame, int total)
    {
        ushort crc = Crc16.Compute(frame.AsSpan(0, total - 2));
        ushort stored = (ushort)(frame[total - 2] | (frame[total - 1] << 8));
        return crc == stored;
    }

    private void ThrowIfDisposed()
    {
        if (_disposed)
            throw new ObjectDisposedException(nameof(ModbusSppClient));
    }

    public void Dispose()
    {
        if (_disposed)
            return;
        _disposed = true;
        _transport.SppNotifyReceived -= OnNotify;
        lock (_gate)
        {
            _tcs?.TrySetResult(null);
            _tcs = null;
            _pending = null;
            _acc = Array.Empty<byte>();
        }
    }

    private sealed record PendingRequest(byte Function, ushort StartRegister, ushort Quantity,
        int ExpectedPayloadBytes);
}
