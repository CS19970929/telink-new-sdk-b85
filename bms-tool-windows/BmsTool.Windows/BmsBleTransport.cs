using System.Collections.Concurrent;
using System.Diagnostics;
using System.IO;
using Windows.Devices.Bluetooth;
using Windows.Devices.Bluetooth.Advertisement;
using Windows.Devices.Bluetooth.GenericAttributeProfile;
using Windows.Security.Cryptography;

namespace BmsTool.Windows;

public sealed record DiscoveredDevice(ulong Address, string Name, short Rssi)
{
    public override string ToString() => $"{Name}    MAC {BmsBleTransport.FormatBluetoothAddress(Address)}    RSSI {Rssi} dBm";
}

public sealed class BmsBleTransport : IAsyncDisposable
{
    public static readonly Guid ServiceUuid = Guid.Parse("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
    public static readonly Guid RequestUuid = Guid.Parse("6E400002-B5A3-F393-E0A9-E50E24DCCA9E");
    public static readonly Guid ResponseUuid = Guid.Parse("6E400003-B5A3-F393-E0A9-E50E24DCCA9E");

    private const int ConnectAttempts = 4;
    private BluetoothLEDevice? _device;
    private GattDeviceService? _service;
    private GattCharacteristic? _request;
    private GattCharacteristic? _response;
    private GattSession? _session;
    private int _attempt;
    private string _currentStage = "Idle";
    private ulong? _lastAddress;

    public bool IsConnected => _device is not null && _request is not null && _response is not null;
    public int? NegotiatedMtu => _session?.MaxPduSize;
    public bool SupportsWriteWithoutResponse => _request is not null &&
        _request.CharacteristicProperties.HasFlag(GattCharacteristicProperties.WriteWithoutResponse);
    public string DiscoveryDescription { get; private set; } = "BMS SPP not connected";
    public string CurrentStage => _currentStage;

    public event Action<ReadOnlyMemory<byte>>? DataReceived;
    public event Action<string>? ConnectionProgress;

    public async Task ConnectAsync(ulong address, CancellationToken ct = default)
    {
        _lastAddress = address;
        Exception? last = null;
        var total = Stopwatch.StartNew();
        Diag($"[CONNECT] START address={address:X12}; attempts={ConnectAttempts}");

        for (int attempt = 1; attempt <= ConnectAttempts; attempt++)
        {
            _attempt = attempt;
            ct.ThrowIfCancellationRequested();
            var attemptTimer = Stopwatch.StartNew();
            try
            {
                Diag($"[CONNECT] ATTEMPT_BEGIN {attempt}/{ConnectAttempts}");
                await ConnectOnceAsync(address, ct);
                Diag($"[CONNECT] ATTEMPT_OK {attempt}/{ConnectAttempts}; elapsed={attemptTimer.ElapsedMilliseconds}ms; mtu={NegotiatedMtu?.ToString() ?? "unknown"}");
                Diag($"[CONNECT] COMPLETE elapsed={total.ElapsedMilliseconds}ms");
                return;
            }
            catch (OperationCanceledException)
            {
                Diag($"[CONNECT] CANCELLED attempt={attempt}; stage={_currentStage}; elapsed={attemptTimer.ElapsedMilliseconds}ms");
                throw;
            }
            catch (Exception ex)
            {
                last = ex;
                DiagException($"[CONNECT] ATTEMPT_FAIL {attempt}/{ConnectAttempts}; stage={_currentStage}; elapsed={attemptTimer.ElapsedMilliseconds}ms", ex);
                await DisposeConnectionAsync();
                if (attempt < ConnectAttempts)
                {
                    int backoff = 300 + attempt * 350;
                    Diag($"[CONNECT] RETRY_WAIT {backoff}ms");
                    await Task.Delay(backoff, ct);
                }
            }
        }

        var final = new IOException($"BMS BLE connection failed after {ConnectAttempts} automatic attempts. Failed stage={_currentStage}. Last error: {last?.Message}", last);
        DiagException($"[CONNECT] FAILED total={total.ElapsedMilliseconds}ms; finalStage={_currentStage}", final);
        throw final;
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
            var device = await BluetoothLEDevice.FromBluetoothAddressAsync(address);
            if (device is null) throw new IOException("Windows returned null BluetoothLEDevice.");
            return device;
        });
        _device.ConnectionStatusChanged += OnConnectionStatusChanged;
        Diag($"[CONNECT] DEVICE name='{_device.Name}'; status={_device.ConnectionStatus}; deviceId='{_device.DeviceId}'");

        try
        {
            _session = await StepAsync("GattSession.FromDeviceIdAsync", async () =>
                await GattSession.FromDeviceIdAsync(_device.BluetoothDeviceId));
            if (_session is not null)
            {
                Diag($"[CONNECT] GATT_SESSION canMaintain={_session.CanMaintainConnection}; mtu={_session.MaxPduSize}");
                if (_session.CanMaintainConnection)
                {
                    _session.MaintainConnection = true;
                    Diag("[CONNECT] GATT_SESSION MaintainConnection=true");
                }
            }
        }
        catch (Exception ex)
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
                GattClientCharacteristicConfigurationDescriptorValue.Notify));
        Diag($"[CONNECT] CCCD attempt=1 status={notifyStatus}");

        if (notifyStatus != GattCommunicationStatus.Success)
        {
            await Task.Delay(220, ct);
            notifyStatus = await StepAsync("CCCD.Notify.Attempt2", async () =>
                await _response.WriteClientCharacteristicConfigurationDescriptorAsync(
                    GattClientCharacteristicConfigurationDescriptorValue.Notify));
            Diag($"[CONNECT] CCCD attempt=2 status={notifyStatus}");
        }

        if (notifyStatus != GattCommunicationStatus.Success)
            throw new IOException($"Failed to subscribe BMS response notifications: {notifyStatus}");

        await StepAsync("PostNotifyReadySettle", async () =>
        {
            await Task.Delay(350, ct);
            return true;
        });

        DiscoveryDescription = $"BLE 已连接 · MTU {NegotiatedMtu?.ToString() ?? "未知"}";
        _currentStage = "GattReady";
    }

    private async Task<GattDeviceServicesResult> GetServicesRobustAsync(BluetoothLEDevice device, CancellationToken ct)
    {
        ct.ThrowIfCancellationRequested();
        var cached = await StepAsync("ServiceDiscovery.Cached", async () =>
            await device.GetGattServicesForUuidAsync(ServiceUuid, BluetoothCacheMode.Cached));
        Diag($"[CONNECT] SERVICE cached status={cached.Status}; count={cached.Services.Count}");
        if (cached.Status == GattCommunicationStatus.Success && cached.Services.Count > 0)
            return cached;

        ct.ThrowIfCancellationRequested();
        var uncached = await StepAsync("ServiceDiscovery.Uncached", async () =>
            await device.GetGattServicesForUuidAsync(ServiceUuid, BluetoothCacheMode.Uncached));
        Diag($"[CONNECT] SERVICE uncached status={uncached.Status}; count={uncached.Services.Count}");
        if (uncached.Status != GattCommunicationStatus.Success || uncached.Services.Count == 0)
            throw new IOException($"BMS SPP service not found; cached={cached.Status}, uncached={uncached.Status}");
        return uncached;
    }

    private async Task<GattCharacteristicsResult> GetCharacteristicsRobustAsync(
        GattDeviceService service,
        Guid uuid,
        string role,
        CancellationToken ct)
    {
        ct.ThrowIfCancellationRequested();
        var cached = await StepAsync($"Characteristic.{role}.Cached", async () =>
            await service.GetCharacteristicsForUuidAsync(uuid, BluetoothCacheMode.Cached));
        Diag($"[CONNECT] CHAR {role} cached status={cached.Status}; count={cached.Characteristics.Count}");
        if (cached.Status == GattCommunicationStatus.Success && cached.Characteristics.Count > 0)
            return cached;

        ct.ThrowIfCancellationRequested();
        var uncached = await StepAsync($"Characteristic.{role}.Uncached", async () =>
            await service.GetCharacteristicsForUuidAsync(uuid, BluetoothCacheMode.Uncached));
        Diag($"[CONNECT] CHAR {role} uncached status={uncached.Status}; count={uncached.Characteristics.Count}");
        if (uncached.Status != GattCommunicationStatus.Success || uncached.Characteristics.Count == 0)
            throw new IOException($"GATT characteristic {role} not found; cached={cached.Status}, uncached={uncached.Status}");
        return uncached;
    }

    private async Task<T> StepAsync<T>(string stage, Func<Task<T>> action)
    {
        _currentStage = stage;
        var sw = Stopwatch.StartNew();
        Diag($"[CONNECT] STEP_BEGIN attempt={_attempt}; stage={stage}");
        try
        {
            T value = await action();
            Diag($"[CONNECT] STEP_OK attempt={_attempt}; stage={stage}; elapsed={sw.ElapsedMilliseconds}ms");
            return value;
        }
        catch (Exception ex)
        {
            DiagException($"[CONNECT] STEP_FAIL attempt={_attempt}; stage={stage}; elapsed={sw.ElapsedMilliseconds}ms", ex);
            throw;
        }
    }

    public Task WriteAsync(ReadOnlyMemory<byte> data, CancellationToken ct = default) =>
        WriteCoreAsync(data, preferWithoutResponse: false, ct);

    public Task WriteWithoutResponseAsync(ReadOnlyMemory<byte> data, CancellationToken ct = default) =>
        WriteCoreAsync(data, preferWithoutResponse: true, ct);

    private async Task WriteCoreAsync(ReadOnlyMemory<byte> data, bool preferWithoutResponse, CancellationToken ct)
    {
        ct.ThrowIfCancellationRequested();
        var characteristic = _request ?? throw new IOException("BMS SPP write characteristic is not connected.");

        var option = preferWithoutResponse && characteristic.CharacteristicProperties.HasFlag(GattCharacteristicProperties.WriteWithoutResponse)
            ? GattWriteOption.WriteWithoutResponse
            : characteristic.CharacteristicProperties.HasFlag(GattCharacteristicProperties.Write)
                ? GattWriteOption.WriteWithResponse
                : GattWriteOption.WriteWithoutResponse;

        var buffer = CryptographicBuffer.CreateFromByteArray(data.ToArray());
        var sw = Stopwatch.StartNew();
        try
        {
            var status = await characteristic.WriteValueAsync(buffer, option);
            if (status != GattCommunicationStatus.Success)
                throw new IOException($"BMS BLE write failed: {status}");
            Diag($"[GATT] WRITE_OK len={data.Length}; option={option}; status={status}; elapsed={sw.ElapsedMilliseconds}ms");
        }
        catch (Exception ex)
        {
            DiagException($"[GATT] WRITE_FAIL len={data.Length}; option={option}; elapsed={sw.ElapsedMilliseconds}ms", ex);
            throw;
        }
    }

    private void OnConnectionStatusChanged(BluetoothLEDevice sender, object args)
    {
        Diag($"[CONNECT] DEVICE_STATUS_CHANGED status={sender.ConnectionStatus}; name='{sender.Name}'");
    }

    private void OnValueChanged(GattCharacteristic sender, GattValueChangedEventArgs args)
    {
        CryptographicBuffer.CopyToByteArray(args.CharacteristicValue, out byte[] bytes);
        Diag($"[GATT] NOTIFY len={bytes.Length}");
        DataReceived?.Invoke(bytes);
    }

    public static BluetoothLEAdvertisementWatcher CreateWatcher(Action<DiscoveredDevice> callback, Action<string>? diagnostics = null)
    {
        var pendingServiceChecks = new ConcurrentDictionary<ulong, byte>();
        var watcher = new BluetoothLEAdvertisementWatcher { ScanningMode = BluetoothLEScanningMode.Active };
        watcher.Received += async (_, args) =>
        {
            string name = args.Advertisement.LocalName ?? string.Empty;
            // Existing products use BT_, while the currently deployed BMS also uses BT-.
            if (IsBmsName(name))
            {
                callback(new DiscoveredDevice(args.BluetoothAddress, name, args.RawSignalStrengthInDBm));
                return;
            }

            // STM32 IAP may advertise without a local name. Do not expose every
            // unnamed BLE peripheral; confirm the actual BMS Nordic UART service
            // before adding it to the BMS list.
            ulong address = args.BluetoothAddress;
            if (!pendingServiceChecks.TryAdd(address, 0)) return;
            await ConfirmBmsServiceAsync(address, args.RawSignalStrengthInDBm, name, callback, diagnostics, pendingServiceChecks);
        };
        watcher.Stopped += (_, args) => diagnostics?.Invoke($"[SCAN] STOPPED status={watcher.Status}; error={args.Error}");
        return watcher;
    }

    private static async Task ConfirmBmsServiceAsync(
        ulong address,
        short rssi,
        string advertisementName,
        Action<DiscoveredDevice> callback,
        Action<string>? diagnostics,
        ConcurrentDictionary<ulong, byte> pendingServiceChecks)
    {
        try
        {
            using BluetoothLEDevice? device = await BluetoothLEDevice.FromBluetoothAddressAsync(address);
            if (device is null) return;

            bool hasService = await HasBmsServiceAsync(device, BluetoothCacheMode.Cached);
            if (!hasService)
                hasService = await HasBmsServiceAsync(device, BluetoothCacheMode.Uncached);
            if (hasService)
            {
                string name = string.IsNullOrWhiteSpace(advertisementName) ? device.Name ?? string.Empty : advertisementName;
                callback(new DiscoveredDevice(address, name, rssi));
            }
        }
        catch (Exception ex)
        {
            diagnostics?.Invoke($"[SCAN] SERVICE_CONFIRM address={address:X12}; result=ignored; message={ex.Message}");
        }
        finally
        {
            pendingServiceChecks.TryRemove(address, out _);
        }
    }

    private static async Task<bool> HasBmsServiceAsync(BluetoothLEDevice device, BluetoothCacheMode mode)
    {
        GattDeviceServicesResult result = await device.GetGattServicesForUuidAsync(ServiceUuid, mode);
        try
        {
            return result.Status == GattCommunicationStatus.Success && result.Services.Count > 0;
        }
        finally
        {
            foreach (GattDeviceService service in result.Services)
                service.Dispose();
        }
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
