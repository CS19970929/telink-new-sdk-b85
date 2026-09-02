using System.IO;
using Windows.Devices.Bluetooth;
using Windows.Devices.Bluetooth.Advertisement;
using Windows.Devices.Bluetooth.GenericAttributeProfile;
using Windows.Security.Cryptography;

namespace BmsTool.Windows;

public sealed record DiscoveredDevice(ulong Address, string Name, short Rssi)
{
    public override string ToString() => $"{Name}  RSSI {Rssi} dBm  [{Address:X12}]";
}

public sealed class BmsBleTransport : IAsyncDisposable
{
    public static readonly Guid ServiceUuid = Guid.Parse("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
    public static readonly Guid RequestUuid = Guid.Parse("6E400002-B5A3-F393-E0A9-E50E24DCCA9E");
    public static readonly Guid ResponseUuid = Guid.Parse("6E400003-B5A3-F393-E0A9-E50E24DCCA9E");

    private BluetoothLEDevice? _device;
    private GattDeviceService? _service;
    private GattCharacteristic? _request;
    private GattCharacteristic? _response;
    private GattSession? _session;

    public bool IsConnected => _device is not null && _request is not null && _response is not null;
    public int? NegotiatedMtu => _session?.MaxPduSize;
    public string DiscoveryDescription { get; private set; } = "BMS SPP not connected";
    public event Action<ReadOnlyMemory<byte>>? DataReceived;

    public async Task ConnectAsync(ulong address)
    {
        await DisposeConnectionAsync();
        _device = await BluetoothLEDevice.FromBluetoothAddressAsync(address) ?? throw new IOException("Windows could not open BLE device.");
        var services = await _device.GetGattServicesForUuidAsync(ServiceUuid, BluetoothCacheMode.Uncached);
        if (services.Status != GattCommunicationStatus.Success || services.Services.Count == 0) throw new IOException($"BMS SPP service not found: {ServiceUuid}");
        _service = services.Services[0];
        foreach (var extra in services.Services.Skip(1)) extra.Dispose();

        var reqResult = await _service.GetCharacteristicsForUuidAsync(RequestUuid, BluetoothCacheMode.Uncached);
        var rspResult = await _service.GetCharacteristicsForUuidAsync(ResponseUuid, BluetoothCacheMode.Uncached);
        _request = reqResult.Characteristics.FirstOrDefault(c => c.CharacteristicProperties.HasFlag(GattCharacteristicProperties.WriteWithoutResponse) || c.CharacteristicProperties.HasFlag(GattCharacteristicProperties.Write));
        _response = rspResult.Characteristics.FirstOrDefault(c => c.CharacteristicProperties.HasFlag(GattCharacteristicProperties.Notify));
        if (_request is null || _response is null) throw new IOException("BMS request/response characteristics are not available with expected properties.");

        try
        {
            _session = await GattSession.FromDeviceIdAsync(_device.BluetoothDeviceId);
            if (_session is not null && _session.CanMaintainConnection) _session.MaintainConnection = true;
        }
        catch { _session = null; }

        _response.ValueChanged += OnValueChanged;
        var notifyStatus = await _response.WriteClientCharacteristicConfigurationDescriptorAsync(GattClientCharacteristicConfigurationDescriptorValue.Notify);
        if (notifyStatus != GattCommunicationStatus.Success) throw new IOException($"Failed to subscribe BMS response notifications: {notifyStatus}");

        DiscoveryDescription = $"SPP service={ServiceUuid}; TX={RequestUuid}; RX={ResponseUuid}; MTU={NegotiatedMtu?.ToString() ?? "unknown"}";
    }

    public async Task WriteAsync(ReadOnlyMemory<byte> data, CancellationToken ct = default)
    {
        ct.ThrowIfCancellationRequested();
        var characteristic = _request ?? throw new IOException("BMS SPP write characteristic is not connected.");
        var option = characteristic.CharacteristicProperties.HasFlag(GattCharacteristicProperties.WriteWithoutResponse) ? GattWriteOption.WriteWithoutResponse : GattWriteOption.WriteWithResponse;
        var buffer = CryptographicBuffer.CreateFromByteArray(data.ToArray());
        var status = await characteristic.WriteValueAsync(buffer, option);
        if (status != GattCommunicationStatus.Success) throw new IOException($"BMS BLE write failed: {status}");
    }

    private void OnValueChanged(GattCharacteristic sender, GattValueChangedEventArgs args)
    {
        CryptographicBuffer.CopyToByteArray(args.CharacteristicValue, out byte[] bytes);
        DataReceived?.Invoke(bytes);
    }

    public static BluetoothLEAdvertisementWatcher CreateWatcher(Action<DiscoveredDevice> callback)
    {
        var watcher = new BluetoothLEAdvertisementWatcher { ScanningMode = BluetoothLEScanningMode.Active };
        watcher.Received += (_, args) =>
        {
            string name = args.Advertisement.LocalName ?? string.Empty;
            if (!name.StartsWith("BT_", StringComparison.OrdinalIgnoreCase)) return;
            callback(new DiscoveredDevice(args.BluetoothAddress, name, args.RawSignalStrengthInDBm));
        };
        return watcher;
    }

    public async ValueTask DisposeAsync() => await DisposeConnectionAsync();

    private Task DisposeConnectionAsync()
    {
        if (_response is not null) _response.ValueChanged -= OnValueChanged;
        _response = null; _request = null;
        _session?.Dispose(); _session = null;
        _service?.Dispose(); _service = null;
        _device?.Dispose(); _device = null;
        DiscoveryDescription = "BMS SPP not connected";
        return Task.CompletedTask;
    }
}
