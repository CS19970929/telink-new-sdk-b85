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
    private readonly string? _deviceId;
    private readonly BluetoothAddressType? _addressType;
    private readonly LogCallback _log;
    private BluetoothLEDevice? _device;
    private GattSession? _gattSession;
    private GattDeviceService? _otaService;
    private GattCharacteristic? _otaCharacteristic;
    private GattDeviceService? _sppService;
    private GattCharacteristic? _sppWrite;
    private GattCharacteristic? _sppNotify;

    public WindowsBleTransport(ulong address, string? deviceId = null,
        BluetoothAddressType? addressType = null, LogCallback? log = null)
    {
        _address = address;
        _deviceId = deviceId;
        _addressType = addressType;
        _log = log ?? ((_, _) => { });
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
        var started = DateTime.UtcNow;
        try
        {
            for (int attempt = 1; attempt <= 2; attempt++)
            {
                await DisconnectAsync();
                var result = await ConnectOnceAsync(timeout, started, ct);
                if (result.Success)
                {
                    _log(LogLevel.Info,
                        $"[BLE] GATT 已连接，耗时 {(DateTime.UtcNow - started).TotalSeconds:F1}s，PDU={_gattSession?.MaxPduSize ?? 23}");
                    return true;
                }

                bool canRetry = attempt == 1 && result.Status == GattCommunicationStatus.Unreachable &&
                                Remaining(timeout, started) > TimeSpan.FromSeconds(1);
                if (!canRetry)
                {
                    _log(LogLevel.Error,
                        $"[BLE] GATT 连接失败：{Describe(result.Status ?? GattCommunicationStatus.Unreachable, result.ProtocolError)}");
                    return false;
                }

                _log(LogLevel.Warn, "[BLE] 设备暂时不可达，释放旧句柄后重试一次");
                await Task.Delay(250, ct);
            }
            return false;
        }
        catch (OperationCanceledException)
        {
            throw;
        }
        catch (TimeoutException)
        {
            _log(LogLevel.Error, $"[BLE] 连接总超时（{timeout.TotalSeconds:F0}s）");
            return false;
        }
        catch (Exception ex)
        {
            _log(LogLevel.Error, $"[BLE] 连接异常：{ex.GetType().Name}: {ex.Message}");
            return false;
        }
    }

    private async Task<(bool Success, GattCommunicationStatus? Status, byte? ProtocolError)>
        ConnectOnceAsync(TimeSpan timeout, DateTime started, CancellationToken ct)
    {
        // 实机验证：本机 DeviceWatcher 的 AssociationEndpoint Id 可返回 Unreachable，而同一时刻
        // FromBluetoothAddressAsync 能在 1.4 秒内连接成功，因此地址入口必须优先。
        if (_addressType is BluetoothAddressType.Public or BluetoothAddressType.Random)
        {
            _log(LogLevel.Info, $"[BLE] 使用地址 {_address:X12}/{_addressType} 建立连接");
            _device = await BluetoothLEDevice.FromBluetoothAddressAsync(_address, _addressType.Value)
                .AsTask().WaitAsync(Remaining(timeout, started), ct);
        }
        else
        {
            _log(LogLevel.Info, $"[BLE] 使用 Windows 地址缓存入口 {_address:X12} 建立连接");
            _device = await BluetoothLEDevice.FromBluetoothAddressAsync(_address).AsTask()
                .WaitAsync(Remaining(timeout, started), ct);
        }

        if (_device is null && !string.IsNullOrWhiteSpace(_deviceId))
        {
            _log(LogLevel.Warn, "[BLE] 地址入口无设备对象，改用 Windows 设备 ID 兜底");
            _device = await BluetoothLEDevice.FromIdAsync(_deviceId).AsTask()
                .WaitAsync(Remaining(timeout, started), ct);
        }
        if (_device is null)
            return (false, GattCommunicationStatus.Unreachable, null);

        _device.ConnectionStatusChanged += OnConnectionStatusChanged;

        // 创建设备对象不等于连接；Uncached GATT 操作才真正触发链路建立。
        // 必须先完成它，再创建 GattSession；反过来会在部分 Windows 驱动上形成额外排队请求。
        var services = await _device.GetGattServicesAsync(BluetoothCacheMode.Uncached)
            .AsTask().WaitAsync(Remaining(timeout, started), ct);
        var status = services.Status;
        var protocolError = services.ProtocolError;
        foreach (var service in services.Services)
            service.Dispose();
        if (status != GattCommunicationStatus.Success)
            return (false, status, protocolError);

        try
        {
            _gattSession = await GattSession.FromDeviceIdAsync(BluetoothDeviceId.FromId(_device.DeviceId))
                .AsTask().WaitAsync(Remaining(timeout, started), ct);
            if (_gattSession is not null)
            {
                _gattSession.SessionStatusChanged += OnSessionStatusChanged;
                _gattSession.MaintainConnection = true;
                MaxWriteLength = Math.Max(20, _gattSession.MaxPduSize - 7);
            }
        }
        catch (OperationCanceledException)
        {
            throw;
        }
        catch (Exception ex)
        {
            // GATT 已成功，GattSession 仅用于维持连接/PDU 能力；获取失败不应推翻已建立的链路。
            _log(LogLevel.Warn, $"[BLE] GATT 已连接，但会话能力查询失败：{ex.Message}");
        }

        var stateDeadline = DateTime.UtcNow + TimeSpan.FromMilliseconds(500);
        while (DateTime.UtcNow < stateDeadline &&
               _device.ConnectionStatus != BluetoothConnectionStatus.Connected)
        {
            ct.ThrowIfCancellationRequested();
            await Task.Delay(25, ct);
        }
        bool connected = _device.ConnectionStatus == BluetoothConnectionStatus.Connected;
        return (connected, connected ? status : GattCommunicationStatus.Unreachable, protocolError);
    }

    private static TimeSpan Remaining(TimeSpan timeout, DateTime started)
    {
        TimeSpan remaining = timeout - (DateTime.UtcNow - started);
        if (remaining <= TimeSpan.Zero)
            throw new TimeoutException();
        return remaining;
    }

    private void OnConnectionStatusChanged(BluetoothLEDevice sender, object args)
    {
        if (sender.ConnectionStatus == BluetoothConnectionStatus.Disconnected)
            ConnectionLost?.Invoke();
    }

    private void OnSessionStatusChanged(GattSession sender, GattSessionStatusChangedEventArgs args)
    {
        if (args.Status != GattSessionStatus.Active)
            ConnectionLost?.Invoke();
    }

    public async Task<bool> DiscoverOtaServiceAsync(TimeSpan timeout, CancellationToken ct)
    {
        try
        {
            var services = await _device!.GetGattServicesAsync(BluetoothCacheMode.Uncached).AsTask()
                .WaitAsync(timeout, ct);
            if (services.Status != GattCommunicationStatus.Success)
            {
                _log(LogLevel.Warn, $"[BLE] OTA 服务发现失败：{Describe(services.Status, services.ProtocolError)}");
                return false;
            }
            foreach (var svc in services.Services)
            {
                if (svc.Uuid == OtaConstants.OtaServiceUuid)
                {
                    _otaService = svc;
                    var chars = await svc.GetCharacteristicsAsync(BluetoothCacheMode.Uncached).AsTask()
                        .WaitAsync(timeout, ct);
                    if (chars.Status != GattCommunicationStatus.Success)
                    {
                        _log(LogLevel.Warn, $"[BLE] OTA 特征发现失败：{Describe(chars.Status, chars.ProtocolError)}");
                        return false;
                    }
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
        catch (Exception ex)
        {
            _log(LogLevel.Warn, $"[BLE] OTA 服务发现异常：{ex.Message}");
            return false;
        }
    }

    public async Task<bool> DiscoverSppServiceAsync(TimeSpan timeout, CancellationToken ct)
    {
        try
        {
            var services = await _device!.GetGattServicesAsync(BluetoothCacheMode.Uncached).AsTask()
                .WaitAsync(timeout, ct);
            if (services.Status != GattCommunicationStatus.Success)
            {
                _log(LogLevel.Warn, $"[BLE] SPP 服务发现失败：{Describe(services.Status, services.ProtocolError)}");
                return false;
            }
            foreach (var svc in services.Services)
            {
                if (svc.Uuid == OtaConstants.SppServiceUuid)
                {
                    _sppService = svc;
                    var chars = await svc.GetCharacteristicsAsync(BluetoothCacheMode.Uncached).AsTask()
                        .WaitAsync(timeout, ct);
                    if (chars.Status != GattCommunicationStatus.Success)
                    {
                        _log(LogLevel.Warn, $"[BLE] SPP 特征发现失败：{Describe(chars.Status, chars.ProtocolError)}");
                        return false;
                    }
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
        catch (Exception ex)
        {
            _log(LogLevel.Warn, $"[BLE] SPP 服务发现异常：{ex.Message}");
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
            if (status != GattCommunicationStatus.Success)
                _log(LogLevel.Warn, $"[BLE] OTA 通知订阅失败：Status={status}");
            return status == GattCommunicationStatus.Success;
        }
        catch (Exception ex)
        {
            _log(LogLevel.Warn, $"[BLE] OTA 通知订阅异常：{ex.Message}");
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
            if (status != GattCommunicationStatus.Success)
                _log(LogLevel.Warn, $"[BLE] SPP 通知订阅失败：Status={status}");
            return status == GattCommunicationStatus.Success;
        }
        catch (Exception ex)
        {
            _log(LogLevel.Warn, $"[BLE] SPP 通知订阅异常：{ex.Message}");
            return false;
        }
    }

    private static string Describe(GattCommunicationStatus status, byte? protocolError) =>
        $"Status={status}, ProtocolError={(protocolError is byte error ? $"0x{error:X2}" : "无")}";

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
            // 实测：本设备（Telink 固件）会丢弃 WriteWithoutResponse 的 SPP 写，
            // 必须用 WriteWithResponse（ATT 写请求触发固件 attribute 写回调；与 Qt 上位机一致）。
            var result = await _sppWrite
                .WriteValueWithResultAsync(frame.AsBuffer(), GattWriteOption.WriteWithResponse)
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
            if (_gattSession is not null)
            {
                _gattSession.MaintainConnection = false;
                _gattSession.SessionStatusChanged -= OnSessionStatusChanged;
                _gattSession.Dispose();
            }
            _gattSession = null;
            if (_device is not null)
            {
                _device.ConnectionStatusChanged -= OnConnectionStatusChanged;
                _device.Dispose();
            }
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
