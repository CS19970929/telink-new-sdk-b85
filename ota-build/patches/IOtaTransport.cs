namespace Bms.Ota.Core.Transport;

public interface IOtaTransport : IAsyncDisposable
{
    bool IsConnected { get; }
    bool NotificationsEnabled { get; }
    int? NegotiatedMtu { get; }
    event Action<ReadOnlyMemory<byte>>? NotificationReceived;

    Task WriteAsync(ReadOnlyMemory<byte> data, CancellationToken cancellationToken);
}
