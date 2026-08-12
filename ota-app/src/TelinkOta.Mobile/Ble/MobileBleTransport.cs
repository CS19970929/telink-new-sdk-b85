using Plugin.BLE.Abstractions;
using Plugin.BLE.Abstractions.Contracts;
using Plugin.BLE.Abstractions.EventArgs;
using TelinkOta.Core.Ota;

namespace TelinkOta.Mobile.Ble;

/// <summary>Plugin.BLE 到平台无关协议核心的薄适配层；Android 使用 BluetoothGatt，iOS 使用 CoreBluetooth。</summary>
public sealed class MobileBleTransport : IBleTransport
{
    private readonly IAdapter _adapter;
    private readonly IDevice _device;
    private readonly SemaphoreSlim _writeGate = new(1, 1);
    private IService? _otaService;
    private IService? _sppService;
    private ICharacteristic? _ota;
    private ICharacteristic? _sppWrite;
    private ICharacteristic? _sppNotify;
    private bool _intentionalDisconnect;
    private bool _disposed;

    public MobileBleTransport(IAdapter adapter, IDevice device)
    {
        _adapter = adapter;
        _device = device;
        _adapter.DeviceConnectionLost += OnConnectionLost;
        _adapter.DeviceDisconnected += OnDisconnected;
    }

    public event Action<byte[]>? OtaNotifyReceived;
    public event Action<byte[]>? SppNotifyReceived;
    public event Action? ConnectionLost;

    public ulong DeviceAddress => BitConverter.ToUInt64(_device.Id.ToByteArray(), 0);
    public int MaxWriteLength { get; private set; } = 20;

    public async Task<bool> ConnectAsync(TimeSpan timeout, CancellationToken ct)
    {
        ThrowIfDisposed();
        if (!await BlePermission.EnsureAsync()) return false;
        _intentionalDisconnect = false;
        if (_device.State == DeviceState.Connected) return true;
        using var cts = CancellationTokenSource.CreateLinkedTokenSource(ct);
        cts.CancelAfter(timeout);
        try
        {
            await _adapter.ConnectToDeviceAsync(_device, new ConnectParameters(autoConnect: false), cts.Token);
            return _device.State == DeviceState.Connected;
        }
        catch { return false; }
    }

    public async Task<bool> DiscoverOtaServiceAsync(TimeSpan timeout, CancellationToken ct)
    {
        try
        {
            _otaService = await WithTimeout(_device.GetServiceAsync(OtaConstants.OtaServiceUuid, ct), timeout, ct);
            if (_otaService is null) return false;
            _ota = await WithTimeout(_otaService.GetCharacteristicAsync(OtaConstants.OtaCharacteristicUuid), timeout, ct);
            return _ota is not null;
        }
        catch { return false; }
    }

    public async Task<bool> DiscoverSppServiceAsync(TimeSpan timeout, CancellationToken ct)
    {
        try
        {
            _sppService = await WithTimeout(_device.GetServiceAsync(OtaConstants.SppServiceUuid, ct), timeout, ct);
            if (_sppService is null) return false;
            _sppWrite = await WithTimeout(_sppService.GetCharacteristicAsync(OtaConstants.SppWriteUuid), timeout, ct);
            _sppNotify = await WithTimeout(_sppService.GetCharacteristicAsync(OtaConstants.SppNotifyUuid), timeout, ct);
            return _sppWrite is not null && _sppNotify is not null;
        }
        catch { return false; }
    }

    public async Task<bool> EnableOtaNotificationsAsync(TimeSpan timeout, CancellationToken ct)
    {
        if (_ota is null || !_ota.CanUpdate) return false;
        try
        {
            _ota.ValueUpdated -= OnOtaUpdated;
            _ota.ValueUpdated += OnOtaUpdated;
            await WithTimeout(_ota.StartUpdatesAsync(ct), timeout, ct);
            return true;
        }
        catch { return false; }
    }

    public async Task<bool> EnableSppNotificationsAsync(TimeSpan timeout, CancellationToken ct)
    {
        if (_sppNotify is null || !_sppNotify.CanUpdate) return false;
        try
        {
            _sppNotify.ValueUpdated -= OnSppUpdated;
            _sppNotify.ValueUpdated += OnSppUpdated;
            await WithTimeout(_sppNotify.StartUpdatesAsync(ct), timeout, ct);
            return true;
        }
        catch { return false; }
    }

    public async Task<int> NegotiateMtuAsync(TimeSpan timeout, CancellationToken ct)
    {
        try
        {
            int mtu = await WithTimeout(_device.RequestMtuAsync(247), timeout, ct);
            if (mtu < 23) mtu = 23;
            MaxWriteLength = Math.Max(20, mtu - 3);
            return mtu;
        }
        catch
        {
            MaxWriteLength = 20;
            return 23;
        }
    }

    public Task<bool> WriteWithoutResponseAsync(byte[] data, CancellationToken ct) =>
        WriteAsync(_ota, data, CharacteristicWriteType.WithoutResponse, ct);

    public Task<bool> WriteSppAsync(byte[] frame, CancellationToken ct) =>
        WriteAsync(_sppWrite, frame, CharacteristicWriteType.WithResponse, ct);

    private async Task<bool> WriteAsync(ICharacteristic? characteristic, byte[] data,
        CharacteristicWriteType writeType, CancellationToken ct)
    {
        if (characteristic is null || data.Length > MaxWriteLength) return false;
        await _writeGate.WaitAsync(ct);
        try
        {
            characteristic.WriteType = writeType;
            int code = await characteristic.WriteAsync(data, ct);
            return code == 0;
        }
        catch { return false; }
        finally { _writeGate.Release(); }
    }

    public Task<bool> WaitForTxQueueDrainedAsync(TimeSpan timeout, CancellationToken ct) =>
        Task.FromResult(true); // 每次 Plugin.BLE WriteAsync 已串行等待平台提交完成。

    public async Task DisconnectAsync()
    {
        _intentionalDisconnect = true;
        try
        {
            if (_ota is not null) { _ota.ValueUpdated -= OnOtaUpdated; await _ota.StopUpdatesAsync(); }
            if (_sppNotify is not null) { _sppNotify.ValueUpdated -= OnSppUpdated; await _sppNotify.StopUpdatesAsync(); }
        }
        catch { }
        if (_device.State == DeviceState.Connected)
        {
            try { await _adapter.DisconnectDeviceAsync(_device); } catch { }
        }
        ClearGattObjects();
    }

    private void OnOtaUpdated(object? sender, CharacteristicUpdatedEventArgs e) =>
        OtaNotifyReceived?.Invoke(e.Characteristic.Value?.ToArray() ?? Array.Empty<byte>());

    private void OnSppUpdated(object? sender, CharacteristicUpdatedEventArgs e) =>
        SppNotifyReceived?.Invoke(e.Characteristic.Value?.ToArray() ?? Array.Empty<byte>());

    private void OnConnectionLost(object? sender, DeviceErrorEventArgs e)
    {
        if (e.Device.Id == _device.Id && !_intentionalDisconnect) ConnectionLost?.Invoke();
    }

    private void OnDisconnected(object? sender, DeviceEventArgs e)
    {
        if (e.Device.Id == _device.Id && !_intentionalDisconnect) ConnectionLost?.Invoke();
    }

    private void ClearGattObjects()
    {
        _ota = null; _sppWrite = null; _sppNotify = null;
        _otaService?.Dispose(); _sppService?.Dispose();
        _otaService = null; _sppService = null;
    }

    private static async Task<T> WithTimeout<T>(Task<T> task, TimeSpan timeout, CancellationToken ct) =>
        await task.WaitAsync(timeout, ct);
    private static async Task WithTimeout(Task task, TimeSpan timeout, CancellationToken ct) =>
        await task.WaitAsync(timeout, ct);

    private void ThrowIfDisposed()
    {
        if (_disposed) throw new ObjectDisposedException(nameof(MobileBleTransport));
    }

    public async ValueTask DisposeAsync()
    {
        if (_disposed) return;
        await DisconnectAsync();
        _adapter.DeviceConnectionLost -= OnConnectionLost;
        _adapter.DeviceDisconnected -= OnDisconnected;
        _disposed = true;
    }
}
