using Plugin.BLE;
using Plugin.BLE.Abstractions.Contracts;
using Plugin.BLE.Abstractions.EventArgs;

namespace TelinkOta.Mobile.Ble;

public sealed class MobileBleManager : IDisposable
{
    private readonly IBluetoothLE _ble = CrossBluetoothLE.Current;
    private readonly IAdapter _adapter = CrossBluetoothLE.Current.Adapter;
    private readonly Dictionary<Guid, MobileBleDevice> _devices = new();
    private readonly object _devicesGate = new();
    private CancellationTokenSource? _scanCts;

    public event Action<MobileBleDevice>? DeviceUpdated;
    public event Action<string>? StatusChanged;

    public MobileBleManager()
    {
        _adapter.DeviceDiscovered += OnDevice;
        _adapter.DeviceAdvertised += OnDevice;
        _ble.StateChanged += (_, e) => StatusChanged?.Invoke($"蓝牙状态：{e.NewState}");
    }

    public IReadOnlyList<MobileBleDevice> Devices
    {
        get
        {
            lock (_devicesGate)
            {
                return _devices.Values
                    .OrderByDescending(d => d.Name.StartsWith("BT_", StringComparison.OrdinalIgnoreCase))
                    .ThenByDescending(d => d.Rssi)
                    .ToList();
            }
        }
    }

    public async Task ScanAsync(CancellationToken ct)
    {
        if (!await BlePermission.EnsureAsync())
            throw new InvalidOperationException("没有蓝牙扫描/连接权限。");
        if (_ble.State != BluetoothState.On)
            throw new InvalidOperationException($"系统蓝牙不可用：{_ble.State}");

        await StopScanAsync();
        _scanCts = CancellationTokenSource.CreateLinkedTokenSource(ct);
        _scanCts.CancelAfter(TimeSpan.FromSeconds(20));
        _adapter.ScanMode = ScanMode.LowLatency;
        _adapter.ScanTimeout = 20_000;
        StatusChanged?.Invoke("正在扫描附近 BLE 设备…");

        try
        {
            await _adapter.StartScanningForDevicesAsync(
                Array.Empty<Guid>(),
                device => device.IsConnectable || !device.SupportsIsConnectable,
                allowDuplicatesKey: true,
                _scanCts.Token);
        }
        catch (OperationCanceledException) when (!ct.IsCancellationRequested)
        {
            // 20 秒扫描窗口正常结束。
        }
        finally
        {
            int count;
            lock (_devicesGate) count = _devices.Count;
            StatusChanged?.Invoke($"扫描结束，共发现 {count} 个设备。");
        }
    }

    public async Task StopScanAsync()
    {
        try { _scanCts?.Cancel(); } catch { }
        if (_adapter.IsScanning)
        {
            try { await _adapter.StopScanningForDevicesAsync(); } catch { }
        }
        _scanCts?.Dispose();
        _scanCts = null;
    }

    public MobileBleTransport CreateTransport(MobileBleDevice device) =>
        new(_adapter, device.Native);

    private void OnDevice(object? sender, DeviceEventArgs args)
    {
        var item = new MobileBleDevice(args.Device);
        lock (_devicesGate) _devices[item.Id] = item;
        DeviceUpdated?.Invoke(item);
    }

    public void Dispose()
    {
        _adapter.DeviceDiscovered -= OnDevice;
        _adapter.DeviceAdvertised -= OnDevice;
        try { _scanCts?.Cancel(); } catch { }
        _scanCts?.Dispose();
    }
}

internal static class BlePermission
{
    public static async Task<bool> EnsureAsync()
    {
#if ANDROID
        var status = await Permissions.RequestAsync<BluetoothPermission>();
        return status == PermissionStatus.Granted;
#else
        await Task.CompletedTask;
        return true; // iOS 在首次扫描时由 CoreBluetooth 弹出系统授权框。
#endif
    }
}

#if ANDROID
internal sealed class BluetoothPermission : Permissions.BasePlatformPermission
{
    public override (string androidPermission, bool isRuntime)[] RequiredPermissions =>
        Android.OS.Build.VERSION.SdkInt >= Android.OS.BuildVersionCodes.S
            ? new[]
            {
                (Android.Manifest.Permission.BluetoothScan, true),
                (Android.Manifest.Permission.BluetoothConnect, true),
            }
            : new[] { (Android.Manifest.Permission.AccessFineLocation, true) };
}
#endif
