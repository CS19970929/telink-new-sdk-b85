using System.Runtime.InteropServices.WindowsRuntime;
using TelinkOta.Core.Ota;
using Windows.Devices.Bluetooth;
using Windows.Devices.Bluetooth.GenericAttributeProfile;

namespace TelinkOta.App.Wpf.Ble;

/// <summary>
/// Windows BLE 传输适配器（Windows.Devices.Bluetooth）。
/// 只负责：连接、服务发现、通知订阅、Write Without Response、排空、断开、MTU/PDU 能力查询。
/// </summary>
public sealed class WindowsBleTransport : IBleTransport
{
    private readonly ulong _address;
    private BluetoothLEDevice? _device;
    private GattSession? _gattSession;
    private GattDeviceService? _otaService;
    private GattCharacteristic? _otaCharacteristic;
    private GattDeviceService? _sppService;
    private GattCharacteristic? _sppWrite;
    private GattCharacteristic? _sppNotify;

    public WindowsBleTransport(ulong address)
    {
        _address = address;
    }

    public event Action<byte[]>? OtaNotifyReceived;
    public event Action<byte[]>? SppNotifyReceived;
    public event Action? ConnectionLost;

    public ulong DeviceAddress => _address;

    /// <summary>Windows 不公开 ATT MTU；MaxPduSize 反映链路层能力，写负载上限取其 -7。</summary>
    public int MaxWriteLength { get; private set; } = 20;

    public bool IsConnected =>
        _device is not null &&
        _device.ConnectionStatus == BluetoothConnectionStatus.Connected &&
        _gattSession?.SessionStatus == GattSessionStatus.Active;

    public async Task<bool> ConnectAsync(TimeSpan timeout, CancellationToken ct)
    {
        try
        {
            _device = await BluetoothLEDevice.FromBluetoothAddressAsync(_address).AsTask().WaitAsync(timeout, ct);
            if (_device is null)
                return false;

            _gattSession = await GattSession.FromDeviceIdAsync(BluetoothDeviceId.FromId(_device.DeviceId))
                .AsTask().WaitAsync(timeout, ct);
            if (_gattSession is not null)
            {
                _gattSession.SessionStatusChanged += (_, args) =>
                {
                    if (args.Status != GattSessionStatus.Active)
                        ConnectionLost?.Invoke();
                };
                MaxWriteLength = Math.Max(20, _gattSession.MaxPduSize - 7);
            }

            // Windows 的 FromBluetoothAddressAsync 不会真正建立连接：
            // 必须先发起一次 GATT 操作（GetGattServicesAsync）触发连接建立。
            var services = await _device.GetGattServicesAsync(BluetoothCacheMode.Uncached)
                .AsTask().WaitAsync(timeout, ct);
            _ = services; // 触发连接；结果在 Discover 步骤使用

            var deadline = DateTime.UtcNow + timeout;
            while (DateTime.UtcNow < deadline)
            {
                ct.ThrowIfCancellationRequested();
                if (_device.ConnectionStatus == BluetoothConnectionStatus.Connected)
                    return true;
                await Task.Delay(100, ct);
            }
            return _device.ConnectionStatus == BluetoothConnectionStatus.Connected;
        }
        catch (OperationCanceledException)
        {
            throw;
        }
        catch
        {
            return false;
        }
    }

    public async Task<bool> DiscoverOtaServiceAsync(TimeSpan timeout, CancellationToken ct)
    {
        try
        {
            var services = await _device!.GetGattServicesAsync(BluetoothCacheMode.Uncached).AsTask()
                .WaitAsync(timeout, ct);
            foreach (var svc in services.Services)
            {
                if (svc.Uuid == OtaConstants.OtaServiceUuid)
                {
                    _otaService = svc;
                    var chars = await svc.GetCharacteristicsAsync(BluetoothCacheMode.Uncached).AsTask()
                        .WaitAsync(timeout, ct);
                    foreach (var ch in chars.Characteristics)
                    {
                        if (ch.Uuid == OtaConstants.OtaCharacteristicUuid)
                        {
                            _otaCharacteristic = ch;
                            _otaCharacteristic.ValueChanged += OnOtaValueChanged;
                            return true;
                        }
                    }
                }
            }
            return false;
        }
        catch
        {
            return false;
        }
    }

    public async Task<bool> DiscoverSppServiceAsync(TimeSpan timeout, CancellationToken ct)
    {
        try
        {
            var services = await _device!.GetGattServicesAsync(BluetoothCacheMode.Uncached).AsTask()
                .WaitAsync(timeout, ct);
            foreach (var svc in services.Services)
            {
                if (svc.Uuid == OtaConstants.SppServiceUuid)
                {
                    _sppService = svc;
                    var chars = await svc.GetCharacteristicsAsync(BluetoothCacheMode.Uncached).AsTask()
                        .WaitAsync(timeout, ct);
                    foreach (var ch in chars.Characteristics)
                    {
                        if (ch.Uuid == OtaConstants.SppWriteUuid) _sppWrite = ch;
                        if (ch.Uuid == OtaConstants.SppNotifyUuid)
                        {
                            _sppNotify = ch;
                            _sppNotify.ValueChanged += OnSppValueChanged;
                        }
                    }
                    return _sppWrite is not null && _sppNotify is not null;
                }
            }
            return false;
        }
        catch
        {
            return false;
        }
    }

    public async Task<bool> EnableOtaNotificationsAsync(TimeSpan timeout, CancellationToken ct)
    {
        if (_otaCharacteristic is null) return false;
        try
        {
            var status = await _otaCharacteristic
                .WriteClientCharacteristicConfigurationDescriptorAsync(
                    GattClientCharacteristicConfigurationDescriptorValue.Notify)
                .AsTask().WaitAsync(timeout, ct);
            return status == GattCommunicationStatus.Success;
        }
        catch
        {
            return false;
        }
    }

    public async Task<bool> EnableSppNotificationsAsync(TimeSpan timeout, CancellationToken ct)
    {
        if (_sppNotify is null) return false;
        try
        {
            var status = await _sppNotify
                .WriteClientCharacteristicConfigurationDescriptorAsync(
                    GattClientCharacteristicConfigurationDescriptorValue.Notify)
                .AsTask().WaitAsync(timeout, ct);
            return status == GattCommunicationStatus.Success;
        }
        catch
        {
            return false;
        }
    }

    public Task<int> NegotiateMtuAsync(TimeSpan timeout, CancellationToken ct)
    {
        // Windows 由系统在连接时自动协商 MTU/DLE，无显式 API；MaxPduSize 即协商结果。
        return Task.FromResult(MaxWriteLength + 7);
    }

    public async Task<bool> WriteWithoutResponseAsync(byte[] data, CancellationToken ct)
    {
        if (_otaCharacteristic is null) return false;
        try
        {
            var result = await _otaCharacteristic
                .WriteValueWithResultAsync(data.AsBuffer(), GattWriteOption.WriteWithoutResponse)
                .AsTask().WaitAsync(TimeSpan.FromSeconds(8), ct);
            return result.Status == GattCommunicationStatus.Success;
        }
        catch
        {
            return false;
        }
    }

    public async Task<bool> WriteSppAsync(byte[] frame, CancellationToken ct)
    {
        if (_sppWrite is null) return false;
        try
        {
            var result = await _sppWrite
                .WriteValueWithResultAsync(frame.AsBuffer(), GattWriteOption.WriteWithoutResponse)
                .AsTask().WaitAsync(TimeSpan.FromSeconds(5), ct);
            return result.Status == GattCommunicationStatus.Success;
        }
        catch
        {
            return false;
        }
    }

    public Task<bool> WaitForTxQueueDrainedAsync(TimeSpan timeout, CancellationToken ct)
    {
        // Windows GATT 栈按提交顺序串行发送，WriteValueWithResultAsync 返回即已被栈接受；
        // 有界窗口已在会话层保证排空。此处直接返回成功。
        return Task.FromResult(true);
    }

    public async Task DisconnectAsync()
    {
        try
        {
            if (_otaCharacteristic is not null)
            {
                _otaCharacteristic.ValueChanged -= OnOtaValueChanged;
                _otaCharacteristic = null;
            }
            if (_sppNotify is not null)
            {
                _sppNotify.ValueChanged -= OnSppValueChanged;
                _sppNotify = null;
            }
            _otaService?.Dispose();
            _otaService = null;
            _sppService?.Dispose();
            _sppService = null;
            _gattSession?.Dispose();
            _gattSession = null;
            _device?.Dispose();
            _device = null;
        }
        catch { /* 忽略 */ }
        await Task.CompletedTask;
    }

    private void OnOtaValueChanged(GattCharacteristic sender, GattValueChangedEventArgs args)
    {
        var data = args.CharacteristicValue.ToArray();
        OtaNotifyReceived?.Invoke(data);
    }

    private void OnSppValueChanged(GattCharacteristic sender, GattValueChangedEventArgs args)
    {
        var data = args.CharacteristicValue.ToArray();
        SppNotifyReceived?.Invoke(data);
    }

    public async ValueTask DisposeAsync()
    {
        await DisconnectAsync();
    }
}
