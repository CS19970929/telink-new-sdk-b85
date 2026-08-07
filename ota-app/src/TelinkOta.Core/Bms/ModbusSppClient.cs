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
    /// 从累积区中寻找匹配当前请求的完整帧：
    /// 开头可能混有迟到的上一帧（重复通知），逐一按帧长跳过，直到找到 byteCount 与请求匹配且 CRC 正确的帧。
    /// </summary>
    private void TryComplete()
    {
        while (_acc.Length >= 3)
        {
            if (_acc[0] != _slaveAddr || _acc[1] != ModbusRtu.FuncReadHolding)
            {
                // 帧头不符：非本次响应的残留，逐字节前移（1 字节对齐前进，安全）
                _acc = _acc[1..];
                continue;
            }

            int byteCount = _acc[2];
            int total = 3 + byteCount + 2;

            if (byteCount != _expectedPayload)
            {
                // 长度不匹配：这是上一请求的迟到帧。若整帧已到则丢弃；否则说明交错异常，整体清空等待重新累积。
                if (_acc.Length >= total)
                {
                    _acc = _acc[total..];
                    continue;
                }
                _acc = Array.Empty<byte>();
                return;
            }

            if (_acc.Length < total)
                return; // 分片未收齐，继续等

            ushort crc = Crc16.Compute(_acc.AsSpan(0, total - 2));
            ushort stored = (ushort)(_acc[total - 2] | (_acc[total - 1] << 8));
            if (crc != stored)
            {
                // CRC 错：跳过该帧继续找
                _acc = _acc[total..];
                continue;
            }

            var payload = new byte[byteCount];
            Array.Copy(_acc, 3, payload, 0, byteCount);
            _acc = _acc[total..];
            _tcs.TrySetResult(payload);
            return;
        }
    }

    public void Dispose()
    {
        _transport.SppNotifyReceived -= OnNotify;
    }
}
