using System.IO;
using Windows.Devices.Bluetooth;
using Windows.Devices.Bluetooth.GenericAttributeProfile;
using Windows.Security.Cryptography;

namespace BmsTool.Windows;

public static class OtaTargetDetector
{
    public static async Task<OtaTargetKind> DetectAsync(ulong address, OtaTargetKind requested, CancellationToken ct = default)
    {
        if (requested is OtaTargetKind.Telink or OtaTargetKind.Stm32SerialIap)
            return requested;

        ct.ThrowIfCancellationRequested();
        using BluetoothLEDevice device = await BluetoothLEDevice.FromBluetoothAddressAsync(address)
            ?? throw new IOException("Could not open BLE device for OTA architecture detection.");
        GattDeviceServicesResult result = await device.GetGattServicesAsync(BluetoothCacheMode.Uncached);
        if (result.Status != GattCommunicationStatus.Success)
            throw new IOException($"BLE service discovery failed during OTA architecture detection: {result.Status}.");

        try
        {
            if (result.Services.Any(service => service.Uuid == OtaBleTransport.ServiceUuid))
                return OtaTargetKind.Telink;
            if (result.Services.Any(service => service.Uuid == BmsBleTransport.ServiceUuid))
                return OtaTargetKind.Stm32SerialIap;
        }
        finally
        {
            foreach (GattDeviceService service in result.Services)
                service.Dispose();
        }

        throw new IOException("未识别 OTA 架构：未发现 Telink OTA service 或 BMS Nordic UART service。");
    }
}

public sealed class OtaBleTransport : ITelinkOtaTransport
{
    public static readonly Guid ServiceUuid = Guid.Parse("00010203-0405-0607-0809-0a0b0c0d1912");
    public static readonly Guid CharacteristicUuid = Guid.Parse("00010203-0405-0607-0809-0a0b0c0d2b12");

    private BluetoothLEDevice? _device;
    private GattDeviceService? _service;
    private GattCharacteristic? _characteristic;
    private GattSession? _session;

    public bool IsConnected => _characteristic is not null;
    public bool NotificationsEnabled { get; private set; }
    public int? NegotiatedMtu => _session?.MaxPduSize;
    public OtaTransportPolicy Policy => OtaTransportPolicy.WindowsWinRt;
    public event Action<ReadOnlyMemory<byte>>? NotificationReceived;

    public async Task ConnectAsync(ulong address, CancellationToken ct = default)
    {
        ct.ThrowIfCancellationRequested();
        await DisposeConnectionAsync();
        _device = await BluetoothLEDevice.FromBluetoothAddressAsync(address)
            ?? throw new IOException("Could not open BLE device for OTA.");

        GattDeviceServicesResult preferred = await _device.GetGattServicesForUuidAsync(ServiceUuid, BluetoothCacheMode.Uncached);
        ct.ThrowIfCancellationRequested();
        if (preferred.Status == GattCommunicationStatus.Success)
        {
            foreach (GattDeviceService service in preferred.Services)
            {
                GattCharacteristic? characteristic = await FindCharacteristicAsync(service);
                if (characteristic is not null)
                {
                    await SelectAsync(service, characteristic);
                    foreach (GattDeviceService extra in preferred.Services)
                        if (!ReferenceEquals(extra, service)) extra.Dispose();
                    return;
                }
                service.Dispose();
            }
        }

        GattDeviceServicesResult all = await _device.GetGattServicesAsync(BluetoothCacheMode.Uncached);
        ct.ThrowIfCancellationRequested();
        if (all.Status == GattCommunicationStatus.Success)
        {
            foreach (GattDeviceService service in all.Services)
            {
                GattCharacteristic? characteristic = await FindCharacteristicAsync(service);
                if (characteristic is not null)
                {
                    await SelectAsync(service, characteristic);
                    foreach (GattDeviceService extra in all.Services)
                        if (!ReferenceEquals(extra, service)) extra.Dispose();
                    return;
                }
                service.Dispose();
            }
        }

        throw new IOException("Telink OTA characteristic was not found.");
    }

    private static async Task<GattCharacteristic?> FindCharacteristicAsync(GattDeviceService service)
    {
        GattCharacteristicsResult result = await service.GetCharacteristicsForUuidAsync(CharacteristicUuid, BluetoothCacheMode.Uncached);
        return result.Status == GattCommunicationStatus.Success
            ? result.Characteristics.FirstOrDefault(characteristic =>
                characteristic.CharacteristicProperties.HasFlag(GattCharacteristicProperties.WriteWithoutResponse) ||
                characteristic.CharacteristicProperties.HasFlag(GattCharacteristicProperties.Write))
            : null;
    }

    private async Task SelectAsync(GattDeviceService service, GattCharacteristic characteristic)
    {
        _service = service;
        _characteristic = characteristic;
        try
        {
            _session = await GattSession.FromDeviceIdAsync(_device!.BluetoothDeviceId);
            if (_session is not null && _session.CanMaintainConnection)
                _session.MaintainConnection = true;
        }
        catch
        {
            _session = null;
        }

        NotificationsEnabled = false;
        if (characteristic.CharacteristicProperties.HasFlag(GattCharacteristicProperties.Notify))
        {
            characteristic.ValueChanged += OnValueChanged;
            GattCommunicationStatus status = await characteristic.WriteClientCharacteristicConfigurationDescriptorAsync(
                GattClientCharacteristicConfigurationDescriptorValue.Notify);
            NotificationsEnabled = status == GattCommunicationStatus.Success;
            if (!NotificationsEnabled)
                characteristic.ValueChanged -= OnValueChanged;
        }
    }

    private void OnValueChanged(GattCharacteristic sender, GattValueChangedEventArgs args)
    {
        CryptographicBuffer.CopyToByteArray(args.CharacteristicValue, out byte[] bytes);
        NotificationReceived?.Invoke(bytes);
    }

    public async Task WriteAsync(ReadOnlyMemory<byte> data, CancellationToken ct)
    {
        ct.ThrowIfCancellationRequested();
        GattCharacteristic characteristic = _characteristic ?? throw new IOException("OTA characteristic is not connected.");
        GattWriteOption option = characteristic.CharacteristicProperties.HasFlag(GattCharacteristicProperties.WriteWithoutResponse)
            ? GattWriteOption.WriteWithoutResponse
            : GattWriteOption.WriteWithResponse;
        GattCommunicationStatus status = await characteristic.WriteValueAsync(
            CryptographicBuffer.CreateFromByteArray(data.ToArray()), option);
        if (status != GattCommunicationStatus.Success)
            throw new IOException($"OTA BLE write failed: {status}");
    }

    public async Task WriteWithResponseAsync(ReadOnlyMemory<byte> data, CancellationToken ct)
    {
        ct.ThrowIfCancellationRequested();
        GattCharacteristic characteristic = _characteristic ?? throw new IOException("OTA characteristic is not connected.");
        GattWriteOption option = characteristic.CharacteristicProperties.HasFlag(GattCharacteristicProperties.Write)
            ? GattWriteOption.WriteWithResponse
            : GattWriteOption.WriteWithoutResponse;
        GattCommunicationStatus status = await characteristic.WriteValueAsync(
            CryptographicBuffer.CreateFromByteArray(data.ToArray()), option);
        if (status != GattCommunicationStatus.Success)
            throw new IOException($"OTA BLE write-with-response failed: {status}");
    }

    public async ValueTask DisposeAsync() => await DisposeConnectionAsync();

    private Task DisposeConnectionAsync()
    {
        if (_characteristic is not null)
            _characteristic.ValueChanged -= OnValueChanged;
        _characteristic = null;
        NotificationsEnabled = false;
        _session?.Dispose();
        _session = null;
        _service?.Dispose();
        _service = null;
        _device?.Dispose();
        _device = null;
        return Task.CompletedTask;
    }
}
