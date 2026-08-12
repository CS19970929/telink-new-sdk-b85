using Windows.Devices.Bluetooth;
using Windows.Devices.Bluetooth.Advertisement;
using Windows.Devices.Enumeration;

namespace TelinkOta.App.Wpf.Ble;

/// <summary>
/// BLE 扫描器（Windows.Devices.Bluetooth.Advertisement）。
/// </summary>
public sealed class BleScanner : IDisposable
{
    private readonly BluetoothLEAdvertisementWatcher _watcher = new();
    private readonly object _gate = new();
    private readonly Dictionary<ulong, BleDeviceInfo> _devices = new();
    private Action<BleDeviceInfo>? _onUpdated;
    private bool _stopRequested;

    public event Action<string>? ScanStopped;

    public BleScanner()
    {
        _watcher.ScanningMode = BluetoothLEScanningMode.Active; // 主动扫描以获取 Scan Response（设备名在 Scan Response 中）
        _watcher.SignalStrengthFilter = new BluetoothSignalStrengthFilter
        {
            InRangeThresholdInDBm = -100,
            OutOfRangeThresholdInDBm = -110,
            OutOfRangeTimeout = TimeSpan.FromSeconds(10),
        };
        _watcher.Received += OnReceived;
        _watcher.Stopped += OnStopped;
    }

    public IReadOnlyList<BleDeviceInfo> Devices
    {
        get { lock (_gate) { return _devices.Values.ToList(); } }
    }

    public bool IsScanning => _watcher.Status == BluetoothLEAdvertisementWatcherStatus.Started;

    public void Start(Action<BleDeviceInfo> onUpdated)
    {
        _onUpdated = onUpdated;
        _stopRequested = false;

        // 先重放本进程缓存，避免“停止后立即重扫”时列表空白；同时异步加载 Windows 已知设备。
        foreach (var cached in Devices)
            _onUpdated(cached);

        _watcher.Start();
        _ = LoadKnownDevicesAsync();
    }

    public void Stop()
    {
        _stopRequested = true;
        if (IsScanning)
            _watcher.Stop();
        _onUpdated = null;
    }

    private void OnReceived(BluetoothLEAdvertisementWatcher sender, BluetoothLEAdvertisementReceivedEventArgs args)
    {
        ulong addr = args.BluetoothAddress;
        string name = args.Advertisement.LocalName ?? "";
        var uuids = args.Advertisement.ServiceUuids.Select(u => u).ToList();
        bool connectable = args.AdvertisementType == BluetoothLEAdvertisementType.ConnectableUndirected ||
                           args.AdvertisementType == BluetoothLEAdvertisementType.ConnectableDirected;

        BleDeviceInfo info;
        lock (_gate)
        {
            if (_devices.TryGetValue(addr, out var existing))
            {
                info = new BleDeviceInfo
                {
                    Address = addr,
                    Name = !string.IsNullOrEmpty(name) ? name : existing.Name,
                    LocalName = !string.IsNullOrEmpty(name) ? name : existing.LocalName,
                    Rssi = args.RawSignalStrengthInDBm,
                    Connectable = existing.Connectable || connectable,
                    ServiceUuids = uuids.Count > 0 ? uuids : existing.ServiceUuids,
                    FirstSeen = existing.FirstSeen,
                };
                _devices[addr] = info;
            }
            else
            {
                info = new BleDeviceInfo
                {
                    Address = addr,
                    Name = name,
                    LocalName = string.IsNullOrEmpty(name) ? null : name,
                    Rssi = args.RawSignalStrengthInDBm,
                    Connectable = connectable,
                    ServiceUuids = uuids,
                };
                _devices[addr] = info;
            }
        }
        _onUpdated?.Invoke(info);
    }

    private async Task LoadKnownDevicesAsync()
    {
        try
        {
            var known = await DeviceInformation.FindAllAsync(BluetoothLEDevice.GetDeviceSelector());
            var tasks = known.Select(async deviceInfo =>
            {
                BluetoothLEDevice? device = null;
                try
                {
                    device = await BluetoothLEDevice.FromIdAsync(deviceInfo.Id);
                    if (device is null)
                        return;

                    BleDeviceInfo info;
                    lock (_gate)
                    {
                        _devices.TryGetValue(device.BluetoothAddress, out var existing);
                        string name = !string.IsNullOrWhiteSpace(device.Name)
                            ? device.Name
                            : !string.IsNullOrWhiteSpace(deviceInfo.Name) ? deviceInfo.Name : existing?.Name ?? "";
                        info = new BleDeviceInfo
                        {
                            Address = device.BluetoothAddress,
                            Name = name,
                            LocalName = string.IsNullOrEmpty(name) ? existing?.LocalName : name,
                            Rssi = existing?.Rssi ?? -127,
                            Connectable = existing?.Connectable ?? true,
                            ServiceUuids = existing?.ServiceUuids ?? Array.Empty<Guid>(),
                            FirstSeen = existing?.FirstSeen ?? DateTime.Now,
                        };
                        _devices[info.Address] = info;
                    }
                    _onUpdated?.Invoke(info);
                }
                catch
                {
                    // 单个 Windows 缓存项失效不应终止实时广播扫描。
                }
                finally
                {
                    device?.Dispose();
                }
            });
            await Task.WhenAll(tasks);
        }
        catch
        {
            // 已知设备枚举只是加速路径；失败时仍由 AdvertisementWatcher 正常发现。
        }
    }

    private void OnStopped(BluetoothLEAdvertisementWatcher sender,
        BluetoothLEAdvertisementWatcherStoppedEventArgs args)
    {
        if (!_stopRequested)
            ScanStopped?.Invoke($"BLE 扫描被系统停止：{args.Error}");
    }

    public void Dispose()
    {
        Stop();
        _watcher.Received -= OnReceived;
        _watcher.Stopped -= OnStopped;
    }
}
