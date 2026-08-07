using TelinkOta.Core.Ota;

namespace TelinkOta.Core.Bms;

/// <summary>
/// Modbus RTU over SPP 客户端：单飞请求/响应，自动重组 20 字节分片通知。
/// 与 OTA 会话解耦（BatteryMonitor 专用，OTA 期间不得并发使用同一传输）。
/// </summary>
public sealed class ModbusSppClient : IDisposable
{
    private readonly IBleTransport _transport;
    private readonly object _gate = new();
    private byte[] _acc = Array.Empty<byte>();
    private TaskCompletionSource<byte[]>? _tcs;

    public ModbusSppClient(IBleTransport transport)
    {
        _transport = transport ?? throw new ArgumentNullException(nameof(transport));
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
            tcs = _tcs = new TaskCompletionSource<byte[]>(TaskCreationOptions.RunContinuationsAsynchronously);
        }

        var req = ModbusRtu.BuildReadRequest(startRegister, quantity);
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

            if (ModbusRtu.TryParseReadResponse(_acc, out var payload))
            {
                _tcs.TrySetResult(payload);
            }
        }
    }

    public void Dispose()
    {
        _transport.SppNotifyReceived -= OnNotify;
    }
}
