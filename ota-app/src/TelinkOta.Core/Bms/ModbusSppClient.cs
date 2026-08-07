using TelinkOta.Core.Ota;

namespace TelinkOta.Core.Bms;

/// <summary>
/// Modbus RTU over SPP 客户端：单飞请求/响应，自动重组 20 字节分片通知。
/// 鲁棒性处理：
///  - 按"期望数据长度 + 从机地址"匹配响应帧，迟到的上一帧（重复通知）会被逐帧丢弃，绝不返回旧数据；
///  - 与 OTA 会话解耦（BatteryMonitor 专用，OTA 期间不得并发使用同一传输）。
/// </summary>
public sealed class ModbusSppClient : IDisposable
{
    private readonly IBleTransport _transport;
    private readonly byte _slaveAddr;
    private readonly object _gate = new();
    private byte[] _acc = Array.Empty<byte>();
    private int _expectedPayload = -1;
    private TaskCompletionSource<byte[]>? _tcs;

    public ModbusSppClient(IBleTransport transport, byte slaveAddr = ModbusRtu.DefaultSlaveAddr)
    {
        _transport = transport ?? throw new ArgumentNullException(nameof(transport));
        _slaveAddr = slaveAddr;
        _transport.SppNotifyReceived += OnNotify;
    }

    /// <summary>读取保持寄存器（功能码 0x03），返回数据区；超时/写入失败返回 null。</summary>
    public async Task<byte[]?> ReadRegistersAsync(ushort startRegister, ushort quantity,
        TimeSpan timeout, CancellationToken ct)
    {
        TaskCompletionSource<byte[]> tcs;
        lock (_gate)
        {
            _acc = Array.Empty<byte>();
            _expectedPayload = quantity * 2;
            tcs = _tcs = new TaskCompletionSource<byte[]>(TaskCreationOptions.RunContinuationsAsynchronously);
        }

        var req = ModbusRtu.BuildReadRequest(startRegister, quantity, _slaveAddr);
        if (!await _transport.WriteSppAsync(req, ct))
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
        finally
        {
            lock (_gate)
            {
                if (ReferenceEquals(_tcs, tcs))
                    _tcs = null;
            }
        }
    }

    private void OnNotify(byte[] data)
    {
        lock (_gate)
        {
            if (_tcs is null)
                return; // 无人等待，忽略

            var buf = new byte[_acc.Length + data.Length];
            _acc.CopyTo(buf, 0);
            data.CopyTo(buf, _acc.Length);
            _acc = buf;
            TryComplete();
        }
    }

    /// <summary>
    /// 从累积区中寻找匹配当前请求的完整帧。
    /// 累积区可能混有迟到的上一帧（重复通知）或其它请求的响应：
    ///  - 头部不符或长度不匹配 → 逐字节跳过（绝不整体清空，避免丢失在途的当前响应分片）；
    ///  - 找到 byteCount 与请求匹配且 CRC 正确的帧 → 交付。
    /// </summary>
    private void TryComplete()
    {
        while (_acc.Length >= 3)
        {
            bool headerOk = _acc[0] == _slaveAddr && _acc[1] == ModbusRtu.FuncReadHolding;
            int byteCount = _acc[2];
            int total = 3 + byteCount + 2;

            if (headerOk && byteCount == _expectedPayload)
            {
                if (_acc.Length < total)
                    return; // 当前请求的帧头已确认，等待剩余分片
                ushort crc = Crc16.Compute(_acc.AsSpan(0, total - 2));
                ushort stored = (ushort)(_acc[total - 2] | (_acc[total - 1] << 8));
                if (crc == stored)
                {
                    var payload = new byte[byteCount];
                    Array.Copy(_acc, 3, payload, 0, byteCount);
                    _acc = _acc[total..];
                    _tcs.TrySetResult(payload);
                    return;
                }
                // CRC 错：跳过该候选帧继续找
                _acc = _acc[total..];
                continue;
            }

            // 头部不符 / 长度不匹配：迟到的错帧，逐字节前移对齐（在途的真实分片保留在累积区中）
            _acc = _acc[1..];
        }
    }

    public void Dispose()
    {
        _transport.SppNotifyReceived -= OnNotify;
    }
}
