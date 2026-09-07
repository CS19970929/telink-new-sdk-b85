namespace BmsTool.Windows;

/// <summary>
/// BMS Modbus 字节通道。BLE 和直连串口只在这一层适配，协议、页面和测试流程不区分物理链路。
/// </summary>
public interface IBmsTransport : IAsyncDisposable
{
    bool IsConnected { get; }
    string DiscoveryDescription { get; }
    event Action<ReadOnlyMemory<byte>>? DataReceived;
    event Action<string>? ConnectionProgress;
    Task ReconnectAsync(CancellationToken ct = default);
    Task WriteAsync(ReadOnlyMemory<byte> data, CancellationToken ct = default);
}
