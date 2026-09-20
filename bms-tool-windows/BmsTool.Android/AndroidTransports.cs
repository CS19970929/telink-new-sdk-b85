using Android.Content;
using BmsTool.Windows;

namespace BmsTool.Android;

public sealed class AndroidBmsBleTransport : IBmsTransport, IBmsMtuTransport
{
    public static readonly Guid ServiceUuid = Guid.Parse("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
    public static readonly Guid RequestUuid = Guid.Parse("6E400002-B5A3-F393-E0A9-E50E24DCCA9E");
    public static readonly Guid ResponseUuid = Guid.Parse("6E400003-B5A3-F393-E0A9-E50E24DCCA9E");
    private readonly AndroidGattChannel _channel;

    public bool IsConnected => _channel.IsConnected;
    public int? NegotiatedMtu => _channel.NegotiatedMtu;
    public string DiscoveryDescription => IsConnected ? $"Android BLE 已连接 · MTU {NegotiatedMtu}" : "Android BLE 未连接";
    public event Action<ReadOnlyMemory<byte>>? DataReceived;
    public event Action<string>? ConnectionProgress;

    public AndroidBmsBleTransport(Context context, string mac)
    {
        _channel = new AndroidGattChannel(context, mac, ServiceUuid, RequestUuid, ResponseUuid);
        _channel.NotificationReceived += data => DataReceived?.Invoke(data);
        _channel.Log += message => ConnectionProgress?.Invoke(message);
    }

    public Task ConnectAsync(CancellationToken ct = default) => _channel.ConnectAsync(ct);
    public Task ReconnectAsync(CancellationToken ct = default) => _channel.ConnectAsync(ct);
    public Task WriteAsync(ReadOnlyMemory<byte> data, CancellationToken ct = default) => _channel.WriteAsync(data, withResponse: true, ct);
    public ValueTask DisposeAsync() => _channel.DisposeAsync();
}

public sealed class AndroidOtaBleTransport : ITelinkOtaTransport
{
    public static readonly Guid ServiceUuid = Guid.Parse("00010203-0405-0607-0809-0a0b0c0d1912");
    public static readonly Guid CharacteristicUuid = Guid.Parse("00010203-0405-0607-0809-0a0b0c0d2b12");
    private readonly AndroidGattChannel _channel;

    public bool IsConnected => _channel.IsConnected;
    public bool NotificationsEnabled => _channel.NotificationsEnabled;
    public int? NegotiatedMtu => _channel.NegotiatedMtu;
    public OtaTransportPolicy Policy => OtaTransportPolicy.AndroidGatt;
    public event Action<ReadOnlyMemory<byte>>? NotificationReceived;
    public event Action<string>? Log;

    public AndroidOtaBleTransport(Context context, string mac)
    {
        _channel = new AndroidGattChannel(context, mac, ServiceUuid, CharacteristicUuid, CharacteristicUuid);
        _channel.NotificationReceived += data => NotificationReceived?.Invoke(data);
        _channel.Log += message => Log?.Invoke(message);
    }

    public Task ConnectAsync(CancellationToken ct = default) => _channel.ConnectAsync(ct);
    public Task WriteAsync(ReadOnlyMemory<byte> data, CancellationToken ct) => _channel.WriteAsync(data, withResponse: false, ct);
    public Task WriteWithResponseAsync(ReadOnlyMemory<byte> data, CancellationToken ct) => _channel.WriteAsync(data, withResponse: true, ct);
    public ValueTask DisposeAsync() => _channel.DisposeAsync();
}
