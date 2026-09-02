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

    private const int ConnectAttempts = 4;
    private BluetoothLEDevice? _device;
    private GattDeviceService? _service;
    private GattCharacteristic? _request;
    private GattCharacteristic? _response;
    private GattSession? _session;

    public bool IsConnected => _device is not null && _request is not null && _response is not null;
    public int? NegotiatedMtu => _session?.MaxPduSize;
    public string DiscoveryDescription { get; private set; } = "BMS SPP not connected";
    public event Action<ReadOnlyMemory<byte>>? DataReceived;
    public event Action<string>? ConnectionProgress;

    public async Task ConnectAsync(ulong address, CancellationToken ct = default)
    {
        Exception? last = null;
        for (int attempt = 1; attempt <= ConnectAttempts; attempt++)
        {
            ct.ThrowIfCancellationRequested();
            try
            {
                ConnectionProgress?.Invoke($"BLE connect attempt {attempt}/{ConnectAttempts}");
                await ConnectOnceAsync(address, ct);
                ConnectionProgress?.Invoke($"BLE GATT ready on attempt {attempt}/{ConnectAttempts}");
                return;
            }
            catch (OperationCanceledException)
            {
                throw;
            }
            catch (Exception ex)
            {
                last = ex;
                ConnectionProgress?.Invoke($"BLE attempt {attempt} failed: {ex.Message}");
                await DisposeConnectionAsync();
                if (attempt < ConnectAttempts)
                    await Task.Delay(250 + attempt * 250, ct);
            }
        }

        throw new IOException($"BMS BLE connection failed after {ConnectAttempts} automatic attempts. Last error: {last?.Message}", last);
    }

    private async Task ConnectOnceAsync(ulong address, CancellationToken ct)
    {
        await DisposeConnectionAsync();
        ct.ThrowIfCancellationRequested();

        // Give Windows Bluetooth stack a short settling interval after active scanning stops.
        await Task.Delay(180, ct);
        _device = await BluetoothLEDevice.FromBluetoothAddressAsync(address)
            ?? throw new IOException("Windows could not open BLE device.");

        // Ask Windows to keep the physical link alive before doing GATT discovery. This avoids
        // repeated connect/disconnect churn that is especially visible with Uncached discovery.
        try
        {
            _session = await GattSession.FromDeviceIdAsync(_device.BluetoothDeviceId);
            if (_session is not null && _session.CanMaintainConnection)
                _session.MaintainConnection = true;
        }
        catch (Exception ex)
        {
            ConnectionProgress?.Invoke("GattSession warning: " + ex.Message);
            _session = null;
        }

        await Task.Delay(100, ct);
        var services = await GetServicesRobustAsync(_device, ct);
        _service = services.Services[0];
        foreach (var extra in services.Services.Skip(1)) extra.Dispose();

        var reqResult = await GetCharacteristicsRobustAsync(_service, RequestUuid, ct);
        var rspResult = await GetCharacteristicsRobustAsync(_service, ResponseUuid, ct);

        _request = reqResult.Characteristics.FirstOrDefault(c =>
            c.CharacteristicProperties.HasFlag(GattCharacteristicProperties.WriteWithoutResponse) ||
            c.CharacteristicProperties.HasFlag(GattCharacteristicProperties.Write));
        _response = rspResult.Characteristics.FirstOrDefault(c =>
            c.CharacteristicProperties.HasFlag(GattCharacteristicProperties.Notify));

        if (_request is null || _response is null)
            throw new IOException("BMS request/response characteristics are not available with expected properties.");

        _response.ValueChanged += OnValueChanged;
        var notifyStatus = await _response.WriteClientCharacteristicConfigurationDescriptorAsync(
            GattClientCharacteristicConfigurationDescriptorValue.Notify);
        if (notifyStatus != GattCommunicationStatus.Success)
        {
            await Task.Delay(180, ct);
            notifyStatus = await _response.WriteClientCharacteristicConfigurationDescriptorAsync(
                GattClientCharacteristicConfigurationDescriptorValue.Notify);
        }
        if (notifyStatus != GattCommunicationStatus.Success)
            throw new IOException($"Failed to subscribe BMS response notifications: {notifyStatus}");

        DiscoveryDescription =
            $"SPP service={ServiceUuid}; TX={RequestUuid}; RX={ResponseUuid}; MTU={NegotiatedMtu?.ToString() ?? "unknown"}";
    }

    private static async Task<GattDeviceServicesResult> GetServicesRobustAsync(BluetoothLEDevice device, CancellationToken ct)
    {
        ct.ThrowIfCancellationRequested();
        var cached = await device.GetGattServicesForUuidAsync(ServiceUuid, BluetoothCacheMode.Cached);
        if (cached.Status == GattCommunicationStatus.Success && cached.Services.Count > 0)
            return cached;

        ct.ThrowIfCancellationRequested();
        var uncached = await device.GetGattServicesForUuidAsync(ServiceUuid, BluetoothCacheMode.Uncached);
        if (uncached.Status != GattCommunicationStatus.Success || uncached.Services.Count == 0)
            throw new IOException($"BMS SPP service not found: {ServiceUuid}; cached={cached.Status}, uncached={uncached.Status}");
        return uncached;
    }

    private static async Task<GattCharacteristicsResult> GetCharacteristicsRobustAsync(
        GattDeviceService service,
        Guid uuid,
        CancellationToken ct)
    {
        ct.ThrowIfCancellationRequested();
        var cached = await service.GetCharacteristicsForUuidAsync(uuid, BluetoothCacheMode.Cached);
        if (cached.Status == GattCommunicationStatus.Success && cached.Characteristics.Count > 0)
            return cached;

        ct.ThrowIfCancellationRequested();
        var uncached = await service.GetCharacteristicsForUuidAsync(uuid, BluetoothCacheMode.Uncached);
        if (uncached.Status != GattCommunicationStatus.Success || uncached.Characteristics.Count == 0)
            throw new IOException($"GATT characteristic {uuid} not found; cached={cached.Status}, uncached={uncached.Status}");
        return uncached;
    }

    public async Task WriteAsync(ReadOnlyMemory<byte> data, CancellationToken ct = default)
    {
        ct.ThrowIfCancellationRequested();
        var characteristic = _request ?? throw new IOException("BMS SPP write characteristic is not connected.");
        var option = characteristic.CharacteristicProperties.HasFlag(GattCharacteristicProperties.WriteWithoutResponse)
            ? GattWriteOption.WriteWithoutResponse
            : GattWriteOption.WriteWithResponse;
        var buffer = CryptographicBuffer.CreateFromByteArray(data.ToArray());
        var status = await characteristic.WriteValueAsync(buffer, option);
        if (status != GattCommunicationStatus.Success)
            throw new IOException($"BMS BLE write failed: {status}");
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
        _response = null;
        _request = null;
        _session?.Dispose();
        _session = null;
        _service?.Dispose();
        _service = null;
        _device?.Dispose();
        _device = null;
        DiscoveryDescription = "BMS SPP not connected";
        return Task.CompletedTask;
    }
}
