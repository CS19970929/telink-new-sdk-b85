using System.Globalization;
using Windows.Devices.Bluetooth;
using Windows.Devices.Bluetooth.Advertisement;
using Windows.Devices.Enumeration;

namespace TelinkOta.App.Wpf.Ble;

/// <summary>
/// BLE 扫描器（Windows.Devices.Bluetooth.Advertisement）。
/// </summary>
public sealed class BleScanner : IDisposable
{
    private const string DeviceAddressProperty = "System.Devices.Aep.DeviceAddress";
    private const string IsPresentProperty = "System.Devices.Aep.IsPresent";

    private readonly object _gate = new();
    private readonly object _lifecycleGate = new();
    private readonly Dictionary<ulong, BleDeviceInfo> _devices = new();
    private readonly Dictionary<ulong, string> _nameCache = new();
    private BluetoothLEAdvertisementWatcher? _watcher;
    private Action<BleDeviceInfo>? _onUpdated;
    private int _generation;
    private bool _disposed;

    public event Action<string>? ScanStopped;

    public IReadOnlyList<BleDeviceInfo> Devices
    {
        get { lock (_gate) { return _devices.Values.ToList(); } }
    }

    public bool IsScanning
    {
        get
        {
            lock (_lifecycleGate)
                return _watcher?.Status == BluetoothLEAdvertisementWatcherStatus.Started;
        }
    }

    public void Start(Action<BleDeviceInfo> onUpdated, bool clearPrevious = true)
    {
        ArgumentNullException.ThrowIfNull(onUpdated);

        BluetoothLEAdvertisementWatcher watcher;
        int generation;
        lock (_lifecycleGate)
        {
            ObjectDisposedException.ThrowIf(_disposed, this);

            // Windows 的 Stop 是异步的，旧实例可能长时间停在 Stopping。每轮扫描创建新实例，
            // 避免快速重扫被排队到旧实例停止之后，也避免旧 Stopped 事件覆盖新扫描状态。
            StopCurrentWatcherLocked();
            _onUpdated = onUpdated;
            generation = ++_generation;
            watcher = CreateWatcher();
            _watcher = watcher;
        }

        if (clearPrevious)
        {
            lock (_gate)
                _devices.Clear();
        }

        try
        {
            // 先启动空口扫描，不能让 Windows 缓存枚举阻塞实时广播接收。
            watcher.Start();
        }
        catch
        {
            lock (_lifecycleGate)
            {
                if (ReferenceEquals(_watcher, watcher))
                    _watcher = null;
                DetachWatcher(watcher);
            }
            throw;
        }

        // 同一轮自动增强重试时重放已发现设备；用户发起的新扫描则只显示本轮结果。
        if (!clearPrevious)
        {
            foreach (var cached in Devices)
                onUpdated(cached);
        }

        _ = LoadKnownDevicesAsync(generation);
    }

    public void Stop()
    {
        lock (_lifecycleGate)
        {
            ++_generation; // 使仍在运行的缓存枚举结果失效。
            StopCurrentWatcherLocked();
        }
    }

    private BluetoothLEAdvertisementWatcher CreateWatcher()
    {
        var watcher = new BluetoothLEAdvertisementWatcher
        {
            // 固件把 BT_* 名称放在 Scan Response，必须主动扫描。
            ScanningMode = BluetoothLEScanningMode.Active,
        };

        // 发现阶段不设置 SignalStrengthFilter。RSSI 门限是“进入/离开范围”状态过滤，
        // 不是简单的数据降噪；在弱信号与部分 USB 蓝牙适配器上会显著漏报广播。
        watcher.Received += HandleReceived;
        watcher.Stopped += HandleStopped;
        return watcher;
    }

    private void StopCurrentWatcherLocked()
    {
        var watcher = _watcher;
        _watcher = null;
        if (watcher is null)
            return;

        // 先解绑，旧实例的异步 Stopped 不得污染下一轮扫描。
        DetachWatcher(watcher);
        if (watcher.Status == BluetoothLEAdvertisementWatcherStatus.Started)
            watcher.Stop();
    }

    private void DetachWatcher(BluetoothLEAdvertisementWatcher watcher)
    {
        watcher.Received -= HandleReceived;
        watcher.Stopped -= HandleStopped;
    }

    private void HandleReceived(BluetoothLEAdvertisementWatcher sender,
        BluetoothLEAdvertisementReceivedEventArgs args)
    {
        Action<BleDeviceInfo>? callback;
        lock (_lifecycleGate)
        {
            if (!ReferenceEquals(sender, _watcher))
                return;
            callback = _onUpdated;
        }

        ulong addr = args.BluetoothAddress;
        string name = args.Advertisement.LocalName ?? "";
        var uuids = args.Advertisement.ServiceUuids.Select(u => u).ToList();
        bool connectable = args.AdvertisementType == BluetoothLEAdvertisementType.ConnectableUndirected ||
                           args.AdvertisementType == BluetoothLEAdvertisementType.ConnectableDirected;

        BleDeviceInfo info;
        lock (_gate)
        {
            if (!string.IsNullOrWhiteSpace(name))
                _nameCache[addr] = name;
            else if (_nameCache.TryGetValue(addr, out string? cachedName))
                name = cachedName;

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
        callback?.Invoke(info);
    }

    private void HandleStopped(BluetoothLEAdvertisementWatcher sender,
        BluetoothLEAdvertisementWatcherStoppedEventArgs args)
    {
        lock (_lifecycleGate)
        {
            if (!ReferenceEquals(sender, _watcher))
                return;
            _watcher = null;
            DetachWatcher(sender);
        }
        ScanStopped?.Invoke($"BLE 扫描被系统停止：{args.Error}");
    }

    private async Task LoadKnownDevicesAsync(int generation)
    {
        try
        {
            // 只读取 DeviceInformation 属性，不再并发 FromIdAsync 打开所有 BLE 设备句柄。
            // 后者会与实时扫描争用 Windows 蓝牙栈，且对单连接外设存在误触发连接的风险。
            string[] properties = { DeviceAddressProperty, IsPresentProperty };
            var known = await DeviceInformation.FindAllAsync(
                BluetoothLEDevice.GetDeviceSelector(), properties);

            foreach (var deviceInfo in known)
            {
                if (!IsCurrentGeneration(generation))
                    return;
                if (!TryGetBluetoothAddress(deviceInfo, out ulong address))
                    continue;

                bool isPresent = TryGetBoolean(deviceInfo, IsPresentProperty);
                BleDeviceInfo? existing;
                lock (_gate)
                    _devices.TryGetValue(address, out existing);

                // Windows 会长期保留配对缓存。只有“当前存在”或本轮已收到广播的设备才加入 UI。
                if (!isPresent && existing is null)
                    continue;

                string name = !string.IsNullOrWhiteSpace(deviceInfo.Name)
                    ? deviceInfo.Name
                    : existing?.Name ?? "";
                if (!string.IsNullOrWhiteSpace(name))
                {
                    lock (_gate)
                        _nameCache[address] = name;
                }
                var info = new BleDeviceInfo
                {
                    Address = address,
                    Name = name,
                    LocalName = string.IsNullOrEmpty(name) ? existing?.LocalName : name,
                    Rssi = existing?.Rssi ?? -127,
                    Connectable = existing?.Connectable ?? true,
                    ServiceUuids = existing?.ServiceUuids ?? Array.Empty<Guid>(),
                    FirstSeen = existing?.FirstSeen ?? DateTime.Now,
                };
                lock (_gate)
                    _devices[address] = info;

                Action<BleDeviceInfo>? callback;
                lock (_lifecycleGate)
                    callback = generation == _generation ? _onUpdated : null;
                callback?.Invoke(info);
            }
        }
        catch
        {
            // 缓存枚举只是名称补全路径；失败时仍由 AdvertisementWatcher 发现设备。
        }
    }

    private bool IsCurrentGeneration(int generation)
    {
        lock (_lifecycleGate)
            return !_disposed && generation == _generation;
    }

    private static bool TryGetBluetoothAddress(DeviceInformation deviceInfo, out ulong address)
    {
        address = 0;
        if (!deviceInfo.Properties.TryGetValue(DeviceAddressProperty, out object? value) ||
            value is not string text)
            return false;

        string hex = new(text.Where(Uri.IsHexDigit).ToArray());
        return hex.Length == 12 &&
               ulong.TryParse(hex, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out address);
    }

    private static bool TryGetBoolean(DeviceInformation deviceInfo, string propertyName) =>
        deviceInfo.Properties.TryGetValue(propertyName, out object? value) && value is true;

    public void Dispose()
    {
        lock (_lifecycleGate)
        {
            if (_disposed)
                return;
            _disposed = true;
            ++_generation;
            StopCurrentWatcherLocked();
            _onUpdated = null;
        }
    }
}
