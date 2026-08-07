namespace TelinkOta.Core.Ota;

/// <summary>平台 BLE 传输抽象。实现不得关心协议语义，只负责收发与连接状态。</summary>
public interface IBleTransport : IAsyncDisposable
{
    /// <summary>OTA Characteristic 收到 Notify（原始字节）。</summary>
    event Action<byte[]>? OtaNotifyReceived;

    /// <summary>SPP/业务 Characteristic 收到 Notify（原始字节，用于升级后版本复核）。</summary>
    event Action<byte[]>? SppNotifyReceived;

    /// <summary>连接被终止（设备侧或链路丢失）。</summary>
    event Action? ConnectionLost;

    /// <summary>设备广播地址（用于重启后重连）。</summary>
    ulong DeviceAddress { get; }

    /// <summary>建立连接（含连接超时）。</summary>
    Task<bool> ConnectAsync(TimeSpan timeout, CancellationToken ct);

    /// <summary>发现 OTA 服务与 Characteristic。返回是否成功。</summary>
    Task<bool> DiscoverOtaServiceAsync(TimeSpan timeout, CancellationToken ct);

    /// <summary>发现 SPP 业务服务（尽力而为，可选）。</summary>
    Task<bool> DiscoverSppServiceAsync(TimeSpan timeout, CancellationToken ct);

    /// <summary>订阅 OTA Characteristic Notify（CCCD=0x0100）。</summary>
    Task<bool> EnableOtaNotificationsAsync(TimeSpan timeout, CancellationToken ct);

    /// <summary>订阅 SPP 业务 Notify（尽力而为）。</summary>
    Task<bool> EnableSppNotificationsAsync(TimeSpan timeout, CancellationToken ct);

    /// <summary>协商 MTU（尽力而为，返回生效 MTU，未知时返回 23）。</summary>
    Task<int> NegotiateMtuAsync(TimeSpan timeout, CancellationToken ct);

    /// <summary>当前允许的最大单次写入字节数（= MTU-3，Windows 无法查询时返回探测/配置值）。</summary>
    int MaxWriteLength { get; }

    /// <summary>Write Without Response。成功返回 true；缓冲区超限等可恢复错误返回 false（不抛异常）。</summary>
    Task<bool> WriteWithoutResponseAsync(byte[] data, CancellationToken ct);

    /// <summary>等待已提交的写入被平台 BLE 栈消化（发送队列排空）。</summary>
    Task<bool> WaitForTxQueueDrainedAsync(TimeSpan timeout, CancellationToken ct);

    /// <summary>主动断开。</summary>
    Task DisconnectAsync();

    /// <summary>向 SPP 写特征发送一帧（尽力而为），供版本复核使用。</summary>
    Task<bool> WriteSppAsync(byte[] frame, CancellationToken ct);
}
