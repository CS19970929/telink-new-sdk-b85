using System.Collections.Concurrent;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using Windows.Devices.Bluetooth;
using Windows.Devices.Bluetooth.Advertisement;
using Windows.Devices.Bluetooth.GenericAttributeProfile;
using Windows.Devices.Enumeration;
using Windows.Security.Cryptography;

namespace BmsTool.Windows;

public sealed class BmsBleScanner : IDisposable
{
    private const string AepSelector = "System.Devices.Aep.ProtocolId:=\"{bb7bb05e-5972-42b5-94fc-76eaa7084d49}\"";
    private const string AddressProperty = "System.Devices.Aep.DeviceAddress";
    private const string LastSeenProperty = "System.Devices.Aep.Bluetooth.LastSeenTime";
    private const string RssiProperty = "System.Devices.Aep.SignalStrength";
    private readonly BluetoothLEAdvertisementWatcher _advertisementWatcher;
    private readonly DeviceWatcher _deviceWatcher;
    private readonly Dictionary<string, DeviceInformation> _aepDevices = new();
    private readonly object _aepLock = new();
    private readonly BleDiscoveryCatalog _catalog = new();
    private readonly Action<DiscoveredDevice> _found;
    private readonly Action<string>? _diagnostics;
    private volatile bool _stopping = true;
    public event Action<string>? ScanFailed;
    public event Action? DevicesChanged;
    public bool IsBroadcastAvailable => !_stopping && _advertisementWatcher.Status == BluetoothLEAdvertisementWatcherStatus.Started;
    public bool IsDiscoveryAvailable => IsBroadcastAvailable || (!_stopping &&
        _deviceWatcher.Status is DeviceWatcherStatus.Started or DeviceWatcherStatus.EnumerationCompleted);
    public string Status => $"AEP={_deviceWatcher.Status}; advertisement={_advertisementWatcher.Status}";
    public BmsBleScanner(Action<DiscoveredDevice> found, Action<string>? diagnostics = null)
    {
        _found = found; _diagnostics = diagnostics;
        _advertisementWatcher = new() { ScanningMode = BluetoothLEScanningMode.Active };
        _advertisementWatcher.Received += OnAdvertisement;
        _advertisementWatcher.Stopped += OnAdvertisementStopped;
        _deviceWatcher = DeviceInformation.CreateWatcher(AepSelector, new[] { AddressProperty, LastSeenProperty, RssiProperty }, DeviceInformationKind.AssociationEndpoint);
        _deviceWatcher.Added += OnAdded;
        _deviceWatcher.Updated += OnUpdated;
        _deviceWatcher.Removed += OnRemoved;
        _deviceWatcher.Stopped += OnAepStopped;
    }
    public IReadOnlyList<DiscoveredDevice> Snapshot(bool includeHistory = false) => _catalog.Snapshot(includeHistory);
    public void Start()
    {
        _stopping = false;
        try { _deviceWatcher.Start(); } catch (Exception ex) { _diagnostics?.Invoke("[SCAN] AEP_START_FAIL " + ex.Message); }
        try { _advertisementWatcher.Start(); } catch (Exception ex) { ReportBroadcastFailure(ex.Message); }
    }
    public void Stop()
    {
        _stopping = true;
        if (_deviceWatcher.Status is DeviceWatcherStatus.Started or DeviceWatcherStatus.EnumerationCompleted) _deviceWatcher.Stop();
        if (_advertisementWatcher.Status == BluetoothLEAdvertisementWatcherStatus.Started) _advertisementWatcher.Stop();
    }
    private void OnAdded(DeviceWatcher sender, DeviceInformation item)
    {
        if (_stopping) return;
        lock (_aepLock) _aepDevices[item.Id] = item;
        ReportSystem(item, false, item.Properties);
    }
    private void OnUpdated(DeviceWatcher sender, DeviceInformationUpdate update)
    {
        if (_stopping) return;
        DeviceInformation? item;
        lock (_aepLock) { if (!_aepDevices.TryGetValue(update.Id, out item)) return; item.Update(update); }
        ReportSystem(item, true, update.Properties);
    }
    private void OnRemoved(DeviceWatcher sender, DeviceInformationUpdate update)
    {
        if (_stopping) return;
        DeviceInformation? item;
        lock (_aepLock) { if (!_aepDevices.Remove(update.Id, out item)) return; }
        if (TryAddress(item, out ulong address)) _catalog.RemoveSystem(address);
        DevicesChanged?.Invoke();
    }
    private static bool TryAddress(DeviceInformation item, out ulong address)
    {
        address = 0;
        if (!item.Properties.TryGetValue(AddressProperty, out var value) || value is not string text) return false;
        string hex = text.Replace(":", "");
        return hex.Length == 12 &&
            ulong.TryParse(hex, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out address);
    }
    private void ReportSystem(DeviceInformation item, bool isUpdate, IReadOnlyDictionary<string, object> changed)
    {
        string name;
        ulong address;
        DateTimeOffset? lastSeen = null;
        short? rssi = null;
        lock (_aepLock)
        {
            if (_stopping || !BmsBleTransport.IsBmsName(item.Name) || !TryAddress(item, out address)) return;
            name = item.Name;
            // 必须是本次事件携带的 LastSeenTime；缓存属性或仅名称/RSSI更新不能续期。
            if (changed.TryGetValue(LastSeenProperty, out var seen) && seen is DateTimeOffset stamp) lastSeen = stamp;
            if (item.Properties.TryGetValue(RssiProperty, out var signal) && signal is int dbm && dbm is >= -127 and < 0) rssi = (short)dbm;
        }
        bool recent = _catalog.ObserveSystem(address, name, lastSeen, isUpdate, rssi);
        if (recent) _diagnostics?.Invoke($"[SCAN] SYSTEM_BROADCAST_UPDATE {name} {BmsBleTransport.FormatBluetoothAddress(address)} RSSI={rssi}");
        if (_stopping) return;
        var found = _catalog.Snapshot(true).FirstOrDefault(d => d.Address == address);
        if (found is not null) _found(found);
        DevicesChanged?.Invoke();
    }
    private void OnAdvertisement(BluetoothLEAdvertisementWatcher sender, BluetoothLEAdvertisementReceivedEventArgs args)
    {
        if (_stopping) return;
        string name = args.Advertisement.LocalName ?? "";
        if (!BmsBleTransport.IsBmsName(name) && !_catalog.KnowsBms(args.BluetoothAddress) &&
            !args.Advertisement.ServiceUuids.Contains(BmsBleTransport.ServiceUuid)) return;
        _catalog.ObserveAdvertisement(args.BluetoothAddress, name, args.RawSignalStrengthInDBm);
        if (_stopping) return;
        _found(_catalog.Snapshot().First(d => d.Address == args.BluetoothAddress));
        DevicesChanged?.Invoke();
    }
    private void OnAdvertisementStopped(BluetoothLEAdvertisementWatcher sender, BluetoothLEAdvertisementWatcherStoppedEventArgs args)
    {
        _diagnostics?.Invoke($"[SCAN] ADVERTISEMENT_STOPPED status={sender.Status}; error={args.Error}");
        if (!_stopping) ReportBroadcastFailure($"{sender.Status}/{args.Error}");
    }
    private void ReportBroadcastFailure(string reason)
    {
        if (_stopping) return;
        string message = IsDiscoveryAvailable
            ? "原始广播接口已中止，继续等待Windows系统广播更新；历史记录不代表在线。"
            : "蓝牙搜索接口均不可用，请检查电脑蓝牙；历史记录不代表在线。";
        _diagnostics?.Invoke("[SCAN] " + message + " reason=" + reason);
        ScanFailed?.Invoke(message);
    }
    private void OnAepStopped(DeviceWatcher sender, object args)
    {
        _diagnostics?.Invoke($"[SCAN] AEP_STOPPED status={sender.Status}");
        if (!_stopping && !IsBroadcastAvailable) ReportBroadcastFailure($"AEP={sender.Status}");
    }
    public void Dispose()
    {
        Stop();
        _advertisementWatcher.Received -= OnAdvertisement;
        _advertisementWatcher.Stopped -= OnAdvertisementStopped;
        _deviceWatcher.Added -= OnAdded; _deviceWatcher.Updated -= OnUpdated;
        _deviceWatcher.Removed -= OnRemoved; _deviceWatcher.Stopped -= OnAepStopped;
    }
}

public sealed class BmsBleTransport : IBmsTransport, IBmsMtuTransport
{
    public static readonly Guid ServiceUuid = Guid.Parse("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
    public static readonly Guid RequestUuid = Guid.Parse("6E400002-B5A3-F393-E0A9-E50E24DCCA9E");
    public static readonly Guid ResponseUuid = Guid.Parse("6E400003-B5A3-F393-E0A9-E50E24DCCA9E");

    private const int ConnectAttempts = 2;
    private static readonly TimeSpan ConnectionBudget = TimeSpan.FromSeconds(20);
    private CancellationToken _connectToken;
    private BluetoothLEDevice? _device;
    private GattDeviceService? _service;
    private GattCharacteristic? _request;
    private GattCharacteristic? _response;
    private GattSession? _session;
    private int _attempt;
    private string _currentStage = "Idle";
    private ulong? _lastAddress;

    public bool IsConnected => _device?.ConnectionStatus == BluetoothConnectionStatus.Connected && _request is not null && _response is not null;
    public int? NegotiatedMtu => _session?.MaxPduSize;
    public int YmodemBlePayloadLimit => 120;
    public string DiscoveryDescription { get; private set; } = "BMS SPP not connected";
    public string CurrentStage => _currentStage;

    public event Action<ReadOnlyMemory<byte>>? DataReceived;
    public event Action<string>? ConnectionProgress;

    public async Task ConnectAsync(ulong address, CancellationToken ct = default)
    {
        _lastAddress = address;
        using var budget = CancellationTokenSource.CreateLinkedTokenSource(ct);
        budget.CancelAfter(ConnectionBudget);
        _connectToken = budget.Token;
        bool ready = false;
        var total = Stopwatch.StartNew();
        Exception? last = null;
        try
        {
            for (int attempt = 1; attempt <= ConnectAttempts; attempt++)
            {
                _attempt = attempt;
                _connectToken.ThrowIfCancellationRequested();
                Diag($"[CONNECT] ATTEMPT_BEGIN {attempt}/{ConnectAttempts}");
                try
                {
                    await ConnectOnceAsync(address, _connectToken);
                    _connectToken.ThrowIfCancellationRequested();
                    ready = true;
                    Diag($"[CONNECT] COMPLETE elapsed={total.ElapsedMilliseconds}ms");
                    return;
                }
                catch (OperationCanceledException) { throw; }
                catch (Exception ex)
                {
                    last = ex;
                    DiagException($"[CONNECT] ATTEMPT_FAIL {attempt}; stage={_currentStage}; elapsed={total.ElapsedMilliseconds}ms", ex);
                    await DisposeConnectionAsync();
                    if (attempt < ConnectAttempts) await Task.Delay(600, _connectToken);
                }
            }
            throw new IOException($"Windows BLE连接失败；阶段={_currentStage}；{last?.Message}", last);
        }
        catch (OperationCanceledException) when (!ct.IsCancellationRequested)
        {
            throw new TimeoutException($"蓝牙连接超过20秒预算；阶段={_currentStage}。Windows正在执行的查询结束后已释放本次连接。");
        }
        finally
        {
            if (!ready) await DisposeConnectionAsync();
            _connectToken = default;
        }
    }

    public async Task ReconnectAsync(CancellationToken ct = default)
    {
        ulong address = _lastAddress ?? throw new IOException("No previous BLE address is available for reconnect.");
        Diag($"[CONNECT] FULL_RECONNECT requested address={address:X12}");
        await DisposeConnectionAsync();
        await Task.Delay(300, ct);
        await ConnectAsync(address, ct);
    }

    private async Task ConnectOnceAsync(ulong address, CancellationToken ct)
    {
        await DisposeConnectionAsync();
        ct.ThrowIfCancellationRequested();

        await StepAsync("StackSettleAfterScan", async () =>
        {
            await Task.Delay(220, ct);
            return true;
        });

        _device = await StepAsync("BluetoothLEDevice.FromBluetoothAddressAsync", async () =>
        {
            var device = await BluetoothLEDevice.FromBluetoothAddressAsync(address).AsTask(ct);
            if (device is null) throw new IOException("Windows returned null BluetoothLEDevice.");
            return device;
        });
        _device.ConnectionStatusChanged += OnConnectionStatusChanged;
        Diag($"[CONNECT] DEVICE name='{_device.Name}'; status={_device.ConnectionStatus}; deviceId='{_device.DeviceId}'");

        try
        {
            _session = await StepAsync("GattSession.FromDeviceIdAsync", async () =>
                await GattSession.FromDeviceIdAsync(_device.BluetoothDeviceId).AsTask(ct));
            if (_session is not null)
            {
                Diag($"[CONNECT] GATT_SESSION canMaintain={_session.CanMaintainConnection}; mtu={_session.MaxPduSize}");
                if (_session.CanMaintainConnection)
                {
                    _session.MaintainConnection = false;
                    Diag("[CONNECT] GATT_SESSION MaintainConnection=false (until GATT ready)");
                }
            }
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            DiagException("[CONNECT] GATT_SESSION_WARNING continuing without session", ex);
            _session = null;
        }

        await StepAsync("PostGattSessionSettle", async () =>
        {
            await Task.Delay(120, ct);
            return true;
        });

        GattDeviceServicesResult services = await GetServicesRobustAsync(_device, ct);
        _service = services.Services[0];
        foreach (var extra in services.Services.Skip(1)) extra.Dispose();
        Diag($"[CONNECT] SERVICE_SELECTED uuid={_service.Uuid}; deviceAccess={_service.DeviceAccessInformation.CurrentStatus}");

        GattCharacteristicsResult reqResult = await GetCharacteristicsRobustAsync(_service, RequestUuid, "TX", ct);
        GattCharacteristicsResult rspResult = await GetCharacteristicsRobustAsync(_service, ResponseUuid, "RX", ct);

        _request = reqResult.Characteristics.FirstOrDefault(c =>
            c.CharacteristicProperties.HasFlag(GattCharacteristicProperties.Write) ||
            c.CharacteristicProperties.HasFlag(GattCharacteristicProperties.WriteWithoutResponse));
        _response = rspResult.Characteristics.FirstOrDefault(c =>
            c.CharacteristicProperties.HasFlag(GattCharacteristicProperties.Notify));

        if (_request is null || _response is null)
            throw new IOException("BMS request/response characteristics are not available with expected properties.");

        Diag($"[CONNECT] TX props={_request.CharacteristicProperties}; handle={_request.AttributeHandle}");
        Diag($"[CONNECT] RX props={_response.CharacteristicProperties}; handle={_response.AttributeHandle}");

        _response.ValueChanged += OnValueChanged;
        GattCommunicationStatus notifyStatus = await StepAsync("CCCD.Notify.Attempt1", async () =>
            await _response.WriteClientCharacteristicConfigurationDescriptorAsync(
                GattClientCharacteristicConfigurationDescriptorValue.Notify).AsTask(ct));
        Diag($"[CONNECT] CCCD attempt=1 status={notifyStatus}");

        if (notifyStatus != GattCommunicationStatus.Success)
        {
            await Task.Delay(220, ct);
            notifyStatus = await StepAsync("CCCD.Notify.Attempt2", async () =>
                await _response.WriteClientCharacteristicConfigurationDescriptorAsync(
                    GattClientCharacteristicConfigurationDescriptorValue.Notify).AsTask(ct));
            Diag($"[CONNECT] CCCD attempt=2 status={notifyStatus}");
        }

        if (notifyStatus != GattCommunicationStatus.Success)
            throw new IOException($"Failed to subscribe BMS response notifications: {notifyStatus}");

        await StepAsync("PostNotifyReadySettle", async () =>
        {
            await Task.Delay(350, ct);
            return true;
        });

        if (_session?.CanMaintainConnection == true) _session.MaintainConnection = true;
        DiscoveryDescription = $"BLE 已连接 · MTU {NegotiatedMtu?.ToString() ?? "未知"}";
        _currentStage = "GattReady";
    }

    private async Task<GattDeviceServicesResult> GetServicesRobustAsync(BluetoothLEDevice device, CancellationToken ct)
    {
        ct.ThrowIfCancellationRequested();
        // Uncached discovery initiates the actual BLE connection. Do not duplicate an
        // unreachable request with cached discovery plus another 15-second wait.
        var result = await StepAsync("ServiceDiscovery.Uncached", async () =>
            await device.GetGattServicesForUuidAsync(ServiceUuid, BluetoothCacheMode.Uncached).AsTask(ct));
        Diag($"[CONNECT] SERVICE status={result.Status}; count={result.Services.Count}");
        if (result.Status == GattCommunicationStatus.Success && result.Services.Count > 0) return result;
        foreach (var service in result.Services) service.Dispose();
        throw new IOException($"Windows未建立可用的BLE服务连接：{result.Status}；设备状态={device.ConnectionStatus}");
    }

    private async Task<GattCharacteristicsResult> GetCharacteristicsRobustAsync(
        GattDeviceService service,
        Guid uuid,
        string role,
        CancellationToken ct)
    {
        ct.ThrowIfCancellationRequested();
        var cached = await StepAsync($"Characteristic.{role}.Cached", async () =>
            await service.GetCharacteristicsForUuidAsync(uuid, BluetoothCacheMode.Cached).AsTask(ct));
        Diag($"[CONNECT] CHAR {role} cached status={cached.Status}; count={cached.Characteristics.Count}");
        if (cached.Status == GattCommunicationStatus.Success && cached.Characteristics.Count > 0)
            return cached;

        ct.ThrowIfCancellationRequested();
        var uncached = await StepAsync($"Characteristic.{role}.Uncached", async () =>
            await service.GetCharacteristicsForUuidAsync(uuid, BluetoothCacheMode.Uncached).AsTask(ct));
        Diag($"[CONNECT] CHAR {role} uncached status={uncached.Status}; count={uncached.Characteristics.Count}");
        if (uncached.Status != GattCommunicationStatus.Success || uncached.Characteristics.Count == 0)
            throw new IOException($"GATT characteristic {role} not found; cached={cached.Status}, uncached={uncached.Status}");
        return uncached;
    }

    private async Task<T> StepAsync<T>(string stage, Func<Task<T>> action)
    {
        _connectToken.ThrowIfCancellationRequested();
        _currentStage = stage;
        var sw = Stopwatch.StartNew();
        Diag($"[CONNECT] STEP_BEGIN attempt={_attempt}; stage={stage}");
        try
        {
            T value = await action();
            if (_connectToken.IsCancellationRequested)
            {
                if (value is GattDeviceServicesResult abandoned) foreach (var service in abandoned.Services) service.Dispose();
                if (value is BluetoothLEDevice abandonedDevice) abandonedDevice.Dispose();
                if (value is GattSession abandonedSession) abandonedSession.Dispose();
                _connectToken.ThrowIfCancellationRequested();
            }
            Diag($"[CONNECT] STEP_OK attempt={_attempt}; stage={stage}; elapsed={sw.ElapsedMilliseconds}ms");
            return value;
        }
        catch (Exception ex)
        {
            DiagException($"[CONNECT] STEP_FAIL attempt={_attempt}; stage={stage}; elapsed={sw.ElapsedMilliseconds}ms", ex);
            throw;
        }
    }

    public async Task WriteAsync(ReadOnlyMemory<byte> data, CancellationToken ct = default)
    {
        ct.ThrowIfCancellationRequested();
        var characteristic = _request ?? throw new IOException("BMS SPP write characteristic is not connected.");

        var option = characteristic.CharacteristicProperties.HasFlag(GattCharacteristicProperties.Write)
            ? GattWriteOption.WriteWithResponse
            : GattWriteOption.WriteWithoutResponse;

        var buffer = CryptographicBuffer.CreateFromByteArray(data.ToArray());
        var sw = Stopwatch.StartNew();
        try
        {
            var status = await characteristic.WriteValueAsync(buffer, option).AsTask(ct);
            if (status != GattCommunicationStatus.Success)
                throw new IOException($"BMS BLE write failed: {status}");
            Diag($"[GATT] WRITE_OK len={data.Length}; option={option}; status={status}; elapsed={sw.ElapsedMilliseconds}ms");
        }
        catch (Exception ex)
        {
            DiagException($"[GATT] WRITE_FAIL len={data.Length}; option={option}; elapsed={sw.ElapsedMilliseconds}ms", ex);
            if (ex is COMException or ObjectDisposedException)
                throw new IOException("BMS 蓝牙写入失败，GATT 链路可能已断开。", ex);
            throw;
        }
    }

    private void OnConnectionStatusChanged(BluetoothLEDevice sender, object args)
    {
        Diag($"[CONNECT] DEVICE_STATUS_CHANGED status={sender.ConnectionStatus}; name='{sender.Name}'");
    }

    // Use the standard GAP name only. The UART and the module's own OTA/control
    // characteristics are not interchangeable with a name-management channel.
    public Task<string> ReadDeviceNameAsync(CancellationToken ct = default) =>
        WithDeviceNameAsync(async characteristic =>
        {
            var result = await characteristic.ReadValueAsync(BluetoothCacheMode.Uncached).AsTask(ct);
            if (result.Status != GattCommunicationStatus.Success) throw new IOException($"读取蓝牙名称失败：{result.Status}");
            CryptographicBuffer.CopyToByteArray(result.Value, out byte[] bytes);
            return System.Text.Encoding.UTF8.GetString(bytes ?? Array.Empty<byte>()).TrimEnd('\0');
        }, ct);

    public static void ValidateDeviceName(string name)
    {
        if (string.IsNullOrWhiteSpace(name) || name.Any(char.IsControl) ||
            System.Text.Encoding.UTF8.GetByteCount(name) > 16)
            throw new ArgumentException("名称须为1～16个UTF-8字节，且不能含控制字符。");
        if (!IsBmsName(name)) throw new ArgumentException("名称请以 BT_ 或 BT- 开头，便于现有手机和上位机发现。");
    }

    public async Task<string> WriteDeviceNameAsync(string name, CancellationToken ct = default)
    {
        ValidateDeviceName(name);
        var originalDevice = _device;
        await WithDeviceNameAsync(async characteristic =>
        {
            if (!characteristic.CharacteristicProperties.HasFlag(GattCharacteristicProperties.Write))
                throw new IOException("当前模块未开放蓝牙名称写入，需要模块专用管理固件支持。");
            var buffer = CryptographicBuffer.CreateFromByteArray(System.Text.Encoding.UTF8.GetBytes(name));
            var result = await characteristic.WriteValueWithResultAsync(buffer, GattWriteOption.WriteWithResponse).AsTask(ct);
            if (result.Status != GattCommunicationStatus.Success)
                throw new IOException($"蓝牙名称写入被拒绝：{result.Status}，ATT={result.ProtocolError}。未发送UART AT命令。");
            return true;
        }, ct);
        // A failed readback is an unknown result; never replay the write.
        if (!ReferenceEquals(originalDevice, _device)) throw new IOException("蓝牙连接已改变，名称写入结果待重新读取确认。");
        string readback = await ReadDeviceNameAsync(ct);
        if (readback != name) throw new IOException("名称写入后读回不一致，请重新连接核对；没有自动重写。");
        return readback;
    }

    private async Task<T> WithDeviceNameAsync<T>(Func<GattCharacteristic, Task<T>> operation, CancellationToken ct)
    {
        var device = _device;
        if (device is null || !IsConnected) throw new IOException("请先连接蓝牙设备。");
        var services = await device.GetGattServicesForUuidAsync(Guid.Parse("00001800-0000-1000-8000-00805f9b34fb"), BluetoothCacheMode.Uncached).AsTask(ct);
        try
        {
            if (services.Status != GattCommunicationStatus.Success || services.Services.Count != 1)
                throw new IOException("当前模块未提供可访问的GAP名称服务。");
            var result = await services.Services[0].GetCharacteristicsForUuidAsync(Guid.Parse("00002a00-0000-1000-8000-00805f9b34fb"), BluetoothCacheMode.Uncached).AsTask(ct);
            if (result.Status != GattCommunicationStatus.Success || result.Characteristics.Count != 1)
                throw new IOException("当前模块未提供可访问的蓝牙名称特征。");
            if (!ReferenceEquals(_device, device) || !IsConnected) throw new IOException("蓝牙连接已改变，请重新读取名称。");
            ct.ThrowIfCancellationRequested();
            return await operation(result.Characteristics[0]);
        }
        finally { foreach (var service in services.Services) service.Dispose(); }
    }

    private void OnValueChanged(GattCharacteristic sender, GattValueChangedEventArgs args)
    {
        CryptographicBuffer.CopyToByteArray(args.CharacteristicValue, out byte[] bytes);
        Diag($"[GATT] NOTIFY len={bytes.Length}");
        DataReceived?.Invoke(bytes);
    }

    public static BluetoothLEAdvertisementWatcher CreateWatcher(Action<DiscoveredDevice> callback, Action<string>? diagnostics = null)
    {
        var watcher = new BluetoothLEAdvertisementWatcher { ScanningMode = BluetoothLEScanningMode.Active };
        watcher.Received += (_, args) =>
        {
            string name = args.Advertisement.LocalName ?? "";
            if (IsBmsName(name) || args.Advertisement.ServiceUuids.Contains(ServiceUuid))
                callback(new(args.BluetoothAddress, string.IsNullOrWhiteSpace(name) ? "未命名BMS（广播服务）" : name, args.RawSignalStrengthInDBm));
        };
        watcher.Stopped += (_, args) => diagnostics?.Invoke($"[SCAN] ADVERTISEMENT_STOPPED status={watcher.Status}; error={args.Error}");
        return watcher;
    }

    public async Task WriteChunkedAsync(ReadOnlyMemory<byte> data, CancellationToken ct = default)
    {
        int chunkSize = NegotiatedMtu is int mtu ? Math.Clamp(mtu - 3, 20, 244) : 20;
        for (int offset = 0; offset < data.Length; offset += chunkSize)
        {
            int length = Math.Min(chunkSize, data.Length - offset);
            await WriteAsync(data.Slice(offset, length), ct);
        }
    }

    internal static bool IsBmsName(string name) =>
        name.StartsWith("BT_", StringComparison.OrdinalIgnoreCase) ||
        name.StartsWith("BT-", StringComparison.OrdinalIgnoreCase);

    public static string FormatBluetoothAddress(ulong address)
    {
        Span<char> result = stackalloc char[17];
        int position = 0;
        for (int byteIndex = 5; byteIndex >= 0; byteIndex--)
        {
            if (byteIndex != 5) result[position++] = ':';
            byte value = (byte)(address >> (byteIndex * 8));
            result[position++] = GetHex(value >> 4);
            result[position++] = GetHex(value & 0x0F);
        }
        return new string(result);
    }

    private static char GetHex(int value) => (char)(value < 10 ? '0' + value : 'A' + value - 10);

    public async ValueTask DisposeAsync() => await DisposeConnectionAsync();

    private Task DisposeConnectionAsync()
    {
        if (_device is not null) _device.ConnectionStatusChanged -= OnConnectionStatusChanged;
        if (_response is not null) _response.ValueChanged -= OnValueChanged;
        _response = null;
        _request = null;
        if (_session is not null) { try { if (_session.CanMaintainConnection) _session.MaintainConnection = false; } catch (Exception ex) { Diag("[CONNECT] session cleanup: " + ex.Message); } }
        _session?.Dispose();
        _session = null;
        _service?.Dispose();
        _service = null;
        _device?.Dispose();
        _device = null;
        DiscoveryDescription = "BMS SPP not connected";
        return Task.CompletedTask;
    }

    private void Diag(string message) => ConnectionProgress?.Invoke(message);

    private void DiagException(string prefix, Exception ex) =>
        ConnectionProgress?.Invoke($"{prefix}; exception={ex.GetType().FullName}; hresult=0x{ex.HResult:X8}; message={ex.Message}");
}
