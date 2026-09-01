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

    public bool IsConnected => _device is not null && _characteristic is not null;
    public string DiscoveryDescription { get; private set; } = "OTA GATT not discovered";

    public async Task ConnectAsync(ulong address)
    {
        await DisposeConnectionAsync();
        DiscoveryDescription = "OTA GATT not discovered";

        _device = await BluetoothLEDevice.FromBluetoothAddressAsync(address);
        if (_device is null)
            throw new InvalidOperationException("Windows could not open the selected BLE device.");

        // Preferred path: Telink's official OTA service UUID.
        var preferredServices = await _device.GetGattServicesForUuidAsync(TelinkOtaServiceUuid, BluetoothCacheMode.Uncached);
        if (preferredServices.Status == GattCommunicationStatus.Success)
        {
            foreach (var service in preferredServices.Services)
            {
                var characteristic = await FindWritableOtaCharacteristicAsync(service);
                if (characteristic is not null)
                {
                    Select(service, characteristic, "official Telink OTA service + characteristic");
                    DisposeServicesExcept(preferredServices.Services, service);
                    return;
                }
                service.Dispose();
            }
        }

        // Compatibility fallback: some products customize the service while retaining
        // Telink's fixed OTA data characteristic UUID. Never hard-code an attribute handle.
        var allServices = await _device.GetGattServicesAsync(BluetoothCacheMode.Uncached);
        if (allServices.Status != GattCommunicationStatus.Success)
            throw new InvalidOperationException($"GATT service discovery failed: {allServices.Status}");

        foreach (var service in allServices.Services)
        {
            var characteristic = await FindWritableOtaCharacteristicAsync(service);
            if (characteristic is not null)
            {
                Select(service, characteristic, $"OTA characteristic fallback under service {service.Uuid}");
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

    private void Select(GattDeviceService service, GattCharacteristic characteristic, string description)
    {
        _service = service;
        _characteristic = characteristic;
        DiscoveryDescription = $"{description}; service={service.Uuid}; characteristic={characteristic.Uuid}";
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
        _characteristic = null;
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
            onDevice(new DiscoveredDevice(args.BluetoothAddress, name, args.RawSignalStrengthInDBm));
        };
        return watcher;
    }
}
