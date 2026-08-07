using Windows.Devices.Bluetooth;
using Windows.Devices.Bluetooth.Advertisement;

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
        _watcher.Stopped += (_, _) => { };
    }

    public IReadOnlyList<BleDeviceInfo> Devices
    {
        get { lock (_gate) { return _devices.Values.ToList(); } }
    }

    public bool IsScanning => _watcher.Status == BluetoothLEAdvertisementWatcherStatus.Started;

    public void Start(Action<BleDeviceInfo> onUpdated)
    {
        _onUpdated = onUpdated;
        lock (_gate) { _devices.Clear(); }
        _watcher.Start();
    }

    public void Stop()
    {
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

    public void Dispose()
    {
        Stop();
    }
}
