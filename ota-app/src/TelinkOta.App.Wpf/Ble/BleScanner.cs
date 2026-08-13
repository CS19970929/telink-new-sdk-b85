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
    private readonly Dictionary<string, DeviceInformation> _systemDevices = new();
    private BluetoothLEAdvertisementWatcher? _watcher;
    private DeviceWatcher? _deviceWatcher;
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
                return _watcher?.Status == BluetoothLEAdvertisementWatcherStatus.Started ||
                       _deviceWatcher?.Status is DeviceWatcherStatus.Started or
                           DeviceWatcherStatus.EnumerationCompleted;
        }
    }

    public void Start(Action<BleDeviceInfo> onUpdated, bool clearPrevious = true)
    {
        ArgumentNullException.ThrowIfNull(onUpdated);

        BluetoothLEAdvertisementWatcher watcher;
        DeviceWatcher deviceWatcher;
        int generation;
        lock (_lifecycleGate)
        {
            ObjectDisposedException.ThrowIf(_disposed, this);

            // Windows 的 Stop 是异步的，旧实例可能长时间停在 Stopping。每轮扫描创建新实例，
            // 避免快速重扫被排队到旧实例停止之后，也避免旧 Stopped 事件覆盖新扫描状态。
            StopCurrentWatchersLocked();
            _onUpdated = onUpdated;
            generation = ++_generation;
            watcher = CreateWatcher();
            deviceWatcher = CreateDeviceWatcher();
            _watcher = watcher;
            _deviceWatcher = deviceWatcher;
        }

        if (clearPrevious)
        {
            lock (_gate)
                _devices.Clear();
        }

        try
        {
            // 两条 Windows 官方发现通道并行：原始广播 + 未配对设备枚举。
            watcher.Start();
            deviceWatcher.Start();
        }
        catch
        {
            lock (_lifecycleGate)
            {
                if (ReferenceEquals(_watcher, watcher))
                    _watcher = null;
                if (ReferenceEquals(_deviceWatcher, deviceWatcher))
                    _deviceWatcher = null;
                DetachWatcher(watcher);
                DetachDeviceWatcher(deviceWatcher);
                StopDeviceWatcher(deviceWatcher);
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
            StopCurrentWatchersLocked();
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

    private DeviceWatcher CreateDeviceWatcher()
    {
        string[] properties =
        {
            DeviceAddressProperty,
            IsPresentProperty,
            "System.Devices.Aep.SignalStrength",
        };
        var watcher = DeviceInformation.CreateWatcher(
            BluetoothLEDevice.GetDeviceSelectorFromPairingState(false),
            properties,
            DeviceInformationKind.AssociationEndpoint);
        watcher.Added += HandleDeviceAdded;
        watcher.Updated += HandleDeviceUpdated;
        watcher.Removed += HandleDeviceRemoved;
        watcher.Stopped += HandleDeviceWatcherStopped;
        return watcher;
    }

    private void StopCurrentWatchersLocked()
    {
        var watcher = _watcher;
        _watcher = null;
        if (watcher is not null)
        {
            // 先解绑，旧实例的异步 Stopped 不得污染下一轮扫描。
            DetachWatcher(watcher);
            if (watcher.Status == BluetoothLEAdvertisementWatcherStatus.Started)
                watcher.Stop();
        }

        var deviceWatcher = _deviceWatcher;
        _deviceWatcher = null;
        if (deviceWatcher is not null)
        {
            DetachDeviceWatcher(deviceWatcher);
            StopDeviceWatcher(deviceWatcher);
        }

        lock (_gate)
            _systemDevices.Clear();
    }

    private void DetachWatcher(BluetoothLEAdvertisementWatcher watcher)
    {
        watcher.Received -= HandleReceived;
        watcher.Stopped -= HandleStopped;
    }

    private void DetachDeviceWatcher(DeviceWatcher watcher)
    {
        watcher.Added -= HandleDeviceAdded;
        watcher.Updated -= HandleDeviceUpdated;
        watcher.Removed -= HandleDeviceRemoved;
        watcher.Stopped -= HandleDeviceWatcherStopped;
    }

    private static void StopDeviceWatcher(DeviceWatcher watcher)
    {
        if (watcher.Status is DeviceWatcherStatus.Started or DeviceWatcherStatus.EnumerationCompleted)
            watcher.Stop();
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
        bool noFallback;
        lock (_lifecycleGate)
        {
            if (!ReferenceEquals(sender, _watcher))
                return;
            _watcher = null;
            DetachWatcher(sender);
            noFallback = _deviceWatcher?.Status is not (DeviceWatcherStatus.Started or
                DeviceWatcherStatus.EnumerationCompleted);
        }
        if (noFallback)
            ScanStopped?.Invoke($"BLE 广播扫描被系统停止：{args.Error}");
    }

    private void HandleDeviceAdded(DeviceWatcher sender, DeviceInformation deviceInfo)
    {
        lock (_lifecycleGate)
        {
            if (!ReferenceEquals(sender, _deviceWatcher))
                return;
        }

        lock (_gate)
            _systemDevices[deviceInfo.Id] = deviceInfo;
        PublishSystemDevice(deviceInfo);
    }

    private void HandleDeviceUpdated(DeviceWatcher sender, DeviceInformationUpdate update)
    {
        lock (_lifecycleGate)
        {
            if (!ReferenceEquals(sender, _deviceWatcher))
                return;
        }

        DeviceInformation? deviceInfo;
        string oldName;
        bool hadAddress;
        lock (_gate)
        {
            if (!_systemDevices.TryGetValue(update.Id, out deviceInfo))
                return;
            oldName = deviceInfo.Name;
            hadAddress = TryGetBluetoothAddress(deviceInfo, out _);
            deviceInfo.Update(update);
        }

        // DeviceWatcher 会高频发送纯 RSSI 更新；原始广播通道已经负责 RSSI，不能把这些
        // 更新全部投递到 UI。仅在地址首次补齐或名称变化时刷新设备列表。
        if (!hadAddress || !string.Equals(oldName, deviceInfo.Name, StringComparison.Ordinal))
            PublishSystemDevice(deviceInfo);
    }

    private void HandleDeviceRemoved(DeviceWatcher sender, DeviceInformationUpdate update)
    {
        lock (_lifecycleGate)
        {
            if (!ReferenceEquals(sender, _deviceWatcher))
                return;
        }

        // 设备可能只是瞬时漏包；本轮扫描列表保留最后一次发现，避免界面反复闪烁。
        lock (_gate)
            _systemDevices.Remove(update.Id);
    }

    private void HandleDeviceWatcherStopped(DeviceWatcher sender, object args)
    {
        bool noFallback;
        lock (_lifecycleGate)
        {
            if (!ReferenceEquals(sender, _deviceWatcher))
                return;
            _deviceWatcher = null;
            DetachDeviceWatcher(sender);
            noFallback = _watcher?.Status != BluetoothLEAdvertisementWatcherStatus.Started;
        }
        if (noFallback)
            ScanStopped?.Invoke("Windows BLE 设备枚举被系统停止");
    }

    private void PublishSystemDevice(DeviceInformation deviceInfo)
    {
        if (!TryGetBluetoothAddress(deviceInfo, out ulong address))
            return;
        if (deviceInfo.Properties.TryGetValue(IsPresentProperty, out object? present) &&
            present is false)
            return;

        Action<BleDeviceInfo>? callback;
        lock (_lifecycleGate)
            callback = _onUpdated;
        if (callback is null)
            return;

        BleDeviceInfo info;
        lock (_gate)
        {
            _devices.TryGetValue(address, out var existing);
            string name = !string.IsNullOrWhiteSpace(deviceInfo.Name)
                ? deviceInfo.Name
                : existing?.Name ?? _nameCache.GetValueOrDefault(address, "");
            if (!string.IsNullOrWhiteSpace(name))
                _nameCache[address] = name;

            short rssi = GetSignalStrength(deviceInfo) ?? existing?.Rssi ?? -127;
            info = new BleDeviceInfo
            {
                Address = address,
                Name = name,
                LocalName = string.IsNullOrEmpty(name) ? existing?.LocalName : name,
                Rssi = rssi,
                Connectable = existing?.Connectable ?? true,
                ServiceUuids = existing?.ServiceUuids ?? Array.Empty<Guid>(),
                FirstSeen = existing?.FirstSeen ?? DateTime.Now,
            };
            _devices[address] = info;
        }
        callback(info);
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

    private static short? GetSignalStrength(DeviceInformation deviceInfo)
    {
        const string propertyName = "System.Devices.Aep.SignalStrength";
        if (!deviceInfo.Properties.TryGetValue(propertyName, out object? value) || value is null)
            return null;
        try
        {
            return Convert.ToInt16(value, CultureInfo.InvariantCulture);
        }
        catch (Exception) when (value is not short)
        {
            return null;
        }
    }

    public void Dispose()
    {
        lock (_lifecycleGate)
        {
            if (_disposed)
                return;
            _disposed = true;
            ++_generation;
            StopCurrentWatchersLocked();
            _onUpdated = null;
        }
    }
}
