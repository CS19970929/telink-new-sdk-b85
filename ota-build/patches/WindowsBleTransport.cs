using System.IO;
using Bms.Ota.Core.Transport;
using Windows.Devices.Bluetooth;
using Windows.Devices.Bluetooth.Advertisement;
using Windows.Devices.Bluetooth.GenericAttributeProfile;
using Windows.Security.Cryptography;

namespace Bms.Ota.Windows.Ble;

public sealed class WindowsBleTransport : IOtaTransport
{
    public static readonly Guid TelinkOtaServiceUuid = Guid.Parse("00010203-0405-0607-0809-0a0b0c0d1912");
    public static readonly Guid TelinkOtaCharacteristicUuid = Guid.Parse("00010203-0405-0607-0809-0a0b0c0d2b12");

    private BluetoothLEDevice? _device;
    private GattDeviceService? _service;
    private GattCharacteristic? _characteristic;
    private GattSession? _session;

    public bool IsConnected => _device is not null && _characteristic is not null;
    public bool NotificationsEnabled { get; private set; }
    public int? NegotiatedMtu => _session?.MaxPduSize;
    public string DiscoveryDescription { get; private set; } = "OTA GATT not discovered";
    public event Action<ReadOnlyMemory<byte>>? NotificationReceived;

    public async Task ConnectAsync(ulong address)
    {
        await DisposeConnectionAsync();
        DiscoveryDescription = "OTA GATT not discovered";

        _device = await BluetoothLEDevice.FromBluetoothAddressAsync(address);
        if (_device is null)
            throw new InvalidOperationException("Windows could not open the selected BLE device.");

        var preferredServices = await _device.GetGattServicesForUuidAsync(TelinkOtaServiceUuid, BluetoothCacheMode.Uncached);
        if (preferredServices.Status == GattCommunicationStatus.Success)
        {
            foreach (var service in preferredServices.Services)
            {
                var characteristic = await FindWritableOtaCharacteristicAsync(service);
                if (characteristic is not null)
                {
                    await SelectAsync(service, characteristic, "official Telink OTA service + characteristic");
                    DisposeServicesExcept(preferredServices.Services, service);
                    return;
                }
                service.Dispose();
            }
        }

        var allServices = await _device.GetGattServicesAsync(BluetoothCacheMode.Uncached);
        if (allServices.Status != GattCommunicationStatus.Success)
            throw new InvalidOperationException($"GATT service discovery failed: {allServices.Status}");

        foreach (var service in allServices.Services)
        {
            var characteristic = await FindWritableOtaCharacteristicAsync(service);
            if (characteristic is not null)
            {
                await SelectAsync(service, characteristic, $"OTA characteristic fallback under service {service.Uuid}");
                DisposeServicesExcept(allServices.Services, service);
                return;
            }
            service.Dispose();
        }

        throw new InvalidOperationException($"Telink OTA characteristic not found: {TelinkOtaCharacteristicUuid}");
    }

    private static async Task<GattCharacteristic?> FindWritableOtaCharacteristicAsync(GattDeviceService service)
    {
        var result = await service.GetCharacteristicsForUuidAsync(TelinkOtaCharacteristicUuid, BluetoothCacheMode.Uncached);
        if (result.Status != GattCommunicationStatus.Success)
            return null;

        foreach (var characteristic in result.Characteristics)
        {
            var props = characteristic.CharacteristicProperties;
            if (props.HasFlag(GattCharacteristicProperties.WriteWithoutResponse) ||
                props.HasFlag(GattCharacteristicProperties.Write))
                return characteristic;
        }
        return null;
    }

    private async Task SelectAsync(GattDeviceService service, GattCharacteristic characteristic, string description)
    {
        _service = service;
        _characteristic = characteristic;

        try
        {
            var device = _device;
            if (device is not null)
            {
                _session = await GattSession.FromDeviceIdAsync(device.BluetoothDeviceId);
                if (_session is not null && _session.CanMaintainConnection)
                    _session.MaintainConnection = true;
            }
        }
        catch
        {
            _session = null;
        }

        NotificationsEnabled = false;
        var props = characteristic.CharacteristicProperties;
        if (props.HasFlag(GattCharacteristicProperties.Notify))
        {
            characteristic.ValueChanged += Characteristic_ValueChanged;
            try
            {
                var status = await characteristic.WriteClientCharacteristicConfigurationDescriptorAsync(
                    GattClientCharacteristicConfigurationDescriptorValue.Notify);
                NotificationsEnabled = status == GattCommunicationStatus.Success;
                if (!NotificationsEnabled)
                    characteristic.ValueChanged -= Characteristic_ValueChanged;
            }
            catch
            {
                characteristic.ValueChanged -= Characteristic_ValueChanged;
                NotificationsEnabled = false;
            }
        }

        DiscoveryDescription =
            $"{description}; service={service.Uuid}; characteristic={characteristic.Uuid}; " +
            $"MTU={NegotiatedMtu?.ToString() ?? "unknown"}; notify={(NotificationsEnabled ? "on" : "off")}; " +
            $"write={(props.HasFlag(GattCharacteristicProperties.WriteWithoutResponse) ? "without-response" : "with-response")}";
    }

    private void Characteristic_ValueChanged(GattCharacteristic sender, GattValueChangedEventArgs args)
    {
        CryptographicBuffer.CopyToByteArray(args.CharacteristicValue, out byte[] bytes);
        NotificationReceived?.Invoke(bytes);
    }

    private static void DisposeServicesExcept(IReadOnlyList<GattDeviceService> services, GattDeviceService selected)
    {
        foreach (var service in services)
            if (!ReferenceEquals(service, selected))
                service.Dispose();
    }

    public async Task WriteAsync(ReadOnlyMemory<byte> data, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var characteristic = _characteristic ?? throw new InvalidOperationException("BLE OTA characteristic is not connected.");
        byte[] bytes = data.ToArray();
        var buffer = CryptographicBuffer.CreateFromByteArray(bytes);
        var props = characteristic.CharacteristicProperties;
        var option = props.HasFlag(GattCharacteristicProperties.WriteWithoutResponse)
            ? GattWriteOption.WriteWithoutResponse
            : GattWriteOption.WriteWithResponse;

        var status = await characteristic.WriteValueAsync(buffer, option);
        if (status != GattCommunicationStatus.Success)
            throw new IOException($"BLE write failed: {status}");
    }

    public async ValueTask DisposeAsync() => await DisposeConnectionAsync();

    private Task DisposeConnectionAsync()
    {
        if (_characteristic is not null)
            _characteristic.ValueChanged -= Characteristic_ValueChanged;
        _characteristic = null;
        NotificationsEnabled = false;

        _session?.Dispose();
        _session = null;
        _service?.Dispose();
        _service = null;
        _device?.Dispose();
        _device = null;
        DiscoveryDescription = "OTA GATT not discovered";
        return Task.CompletedTask;
    }

    public static BluetoothLEAdvertisementWatcher CreateWatcher(Action<DiscoveredDevice> onDevice)
    {
        var watcher = new BluetoothLEAdvertisementWatcher
        {
            ScanningMode = BluetoothLEScanningMode.Active
        };
        watcher.Received += (_, args) =>
        {
            string name = args.Advertisement.LocalName ?? string.Empty;
            if (!name.StartsWith("BT_", StringComparison.OrdinalIgnoreCase))
                return;

            onDevice(new DiscoveredDevice(args.BluetoothAddress, name, args.RawSignalStrengthInDBm));
        };
        return watcher;
    }
}
