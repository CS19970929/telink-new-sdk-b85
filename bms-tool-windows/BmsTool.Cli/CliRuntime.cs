using System.Collections.Concurrent;
using BmsTool.Windows;
using Windows.Devices.Bluetooth.Advertisement;

namespace BmsTool.Cli;

internal sealed record CliEndpoint(
    bool IsSerial,
    string Display,
    string? PortName,
    int BaudRate,
    ulong? Address,
    string? Name);

internal sealed record DeviceSnapshot(
    CliEndpoint Endpoint,
    DeviceIdentity Identity,
    BatterySnapshot Battery,
    uint? FirmwareBuildId,
    bool IsD008);

internal sealed class CliBmsConnection : IAsyncDisposable
{
    public CliEndpoint Endpoint { get; }
    public IBmsTransport Transport { get; }
    public BmsClient Client { get; }

    public CliBmsConnection(CliEndpoint endpoint, IBmsTransport transport, BmsClient client)
    {
        Endpoint = endpoint;
        Transport = transport;
        Client = client;
    }

    public async ValueTask DisposeAsync()
    {
        await Client.DisposeAsync();
        await Transport.DisposeAsync();
    }
}

internal static class CliRuntime
{
    public static async Task<IReadOnlyList<DiscoveredDevice>> ScanAsync(
        int seconds,
        CliReporter reporter,
        CancellationToken ct)
    {
        var devices = new ConcurrentDictionary<ulong, DiscoveredDevice>();
        BluetoothLEAdvertisementWatcher watcher = BmsBleTransport.CreateWatcher(
            d => devices.AddOrUpdate(d.Address, d, (_, old) => d.Rssi >= old.Rssi ? d : old),
            reporter.VerboseLog);

        reporter.Status($"Scanning BLE for {seconds}s...");
        watcher.Start();
        try
        {
            await Task.Delay(TimeSpan.FromSeconds(seconds), ct);
        }
        finally
        {
            try { watcher.Stop(); } catch { }
        }

        await Task.Delay(100, CancellationToken.None);
        return devices.Values
            .OrderByDescending(d => d.Rssi)
            .ThenBy(d => d.Name, StringComparer.OrdinalIgnoreCase)
            .ToArray();
    }

    public static async Task<CliEndpoint> ResolveEndpointAsync(
        CliOptions options,
        CliReporter reporter,
        CancellationToken ct)
    {
        string? serial = options.Get("serial");
        bool hasBleSelector = options.Has("mac") || options.Has("name") || options.Has("auto");
        if (!string.IsNullOrWhiteSpace(serial))
        {
            if (hasBleSelector)
                throw new CliException(ExitCodes.Usage, "usage", "--serial cannot be combined with --mac, --name or --auto.");
            int baud = options.GetInt("baud", BmsSerialTransport.DefaultBaudRate, 300, 4_000_000);
            return new CliEndpoint(true, $"{serial} @ {baud} 8N1", serial, baud, null, null);
        }

        string? mac = options.Get("mac");
        if (!string.IsNullOrWhiteSpace(mac))
        {
            ulong address = ParseMac(mac);
            return new CliEndpoint(false, BmsBleTransport.FormatBluetoothAddress(address), null, 0, address, null);
        }

        string? name = options.Get("name");
        if (!options.Has("auto") && string.IsNullOrWhiteSpace(name))
            throw new CliException(ExitCodes.Usage, "usage", "Specify one target selector: --mac, --name, --auto or --serial.");

        int seconds = options.GetInt("scan-seconds", 4, 1, 30);
        IReadOnlyList<DiscoveredDevice> devices = await ScanAsync(seconds, reporter, ct);

        if (!string.IsNullOrWhiteSpace(name))
        {
            var matches = devices.Where(d => string.Equals(d.Name, name, StringComparison.OrdinalIgnoreCase)).ToArray();
            if (matches.Length == 0)
                throw new CliException(ExitCodes.DeviceNotFound, "device_not_found", $"No BMS named '{name}' was found.");
            if (matches.Length > 1)
                throw new CliException(ExitCodes.MultipleDevices, "multiple_devices", $"Found {matches.Length} devices named '{name}'. Use --mac.",
                    matches.Select(ToScanRow).ToArray());
            var d = matches[0];
            return new CliEndpoint(false, $"{d.Name} / {BmsBleTransport.FormatBluetoothAddress(d.Address)}", null, 0, d.Address, d.Name);
        }

        if (devices.Count == 0)
            throw new CliException(ExitCodes.DeviceNotFound, "device_not_found", "No compatible BMS was found.");
        if (devices.Count > 1)
            throw new CliException(ExitCodes.MultipleDevices, "multiple_devices",
                $"Found {devices.Count} compatible BMS devices. --auto refuses to choose one arbitrarily; use --mac or --name.",
                devices.Select(ToScanRow).ToArray());

        DiscoveredDevice only = devices[0];
        return new CliEndpoint(false, $"{only.Name} / {BmsBleTransport.FormatBluetoothAddress(only.Address)}", null, 0, only.Address, only.Name);
    }

    public static async Task<CliBmsConnection> ConnectBmsAsync(
        CliEndpoint endpoint,
        CliReporter reporter,
        CancellationToken ct)
    {
        IBmsTransport transport;
        if (endpoint.IsSerial)
        {
            var serial = new BmsSerialTransport();
            serial.ConnectionProgress += reporter.VerboseLog;
            try
            {
                await serial.ConnectAsync(endpoint.PortName!, endpoint.BaudRate, ct);
            }
            catch (Exception ex) when (ex is not OperationCanceledException)
            {
                await serial.DisposeAsync();
                throw new CliException(ExitCodes.ConnectFailed, "connect_failed", $"Serial connection failed: {ex.Message}", inner: ex);
            }
            transport = serial;
        }
        else
        {
            var ble = new BmsBleTransport();
            ble.ConnectionProgress += reporter.VerboseLog;
            try
            {
                await ble.ConnectAsync(endpoint.Address!.Value, ct);
            }
            catch (Exception ex) when (ex is not OperationCanceledException)
            {
                await ble.DisposeAsync();
                throw new CliException(ExitCodes.ConnectFailed, "connect_failed", $"BLE connection failed: {ex.Message}", inner: ex);
            }
            transport = ble;
        }

        var client = new BmsClient(transport);
        client.Log += reporter.VerboseLog;
        try
        {
            await client.ProbeAsync(ct);
            return new CliBmsConnection(endpoint, transport, client);
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            await client.DisposeAsync();
            await transport.DisposeAsync();
            throw new CliException(ExitCodes.ConnectFailed, "bms_probe_failed", $"Transport connected but BMS Modbus probe failed: {ex.Message}", inner: ex);
        }
    }

    public static async Task<DeviceSnapshot> ReadSnapshotAsync(CliBmsConnection connection, CancellationToken ct)
    {
        string fallbackMac = connection.Endpoint.IsSerial
            ? string.Empty
            : BmsBleTransport.FormatBluetoothAddress(connection.Endpoint.Address!.Value);
        string fallbackName = connection.Endpoint.IsSerial
            ? $"Serial {connection.Endpoint.PortName}"
            : connection.Endpoint.Name ?? string.Empty;

        DeviceIdentity identity = await connection.Client.ReadIdentityAsync(fallbackMac, fallbackName, ct);
        BatterySnapshot battery = await connection.Client.ReadBatteryAsync(ct);
        uint? buildId = await TryReadFirmwareBuildIdAsync(connection.Client, ct);
        bool isD008 = await TryDetectD008Async(connection.Client, ct);
        return new DeviceSnapshot(connection.Endpoint, identity, battery, buildId, isD008);
    }

    public static async Task<DeviceSnapshot?> TryReadSnapshotAsync(
        CliEndpoint endpoint,
        CliReporter reporter,
        CancellationToken ct)
    {
        try
        {
            await using CliBmsConnection connection = await ConnectBmsAsync(endpoint, reporter, ct);
            return await ReadSnapshotAsync(connection, ct);
        }
        catch (OperationCanceledException) { throw; }
        catch (Exception ex)
        {
            reporter.VerboseLog("Preflight snapshot unavailable: " + ex.Message);
            return null;
        }
    }

    public static async Task<DeviceSnapshot> VerifyAfterOtaAsync(
        CliEndpoint endpoint,
        CliReporter reporter,
        CancellationToken ct)
    {
        Exception? last = null;
        for (int attempt = 1; attempt <= 12; attempt++)
        {
            ct.ThrowIfCancellationRequested();
            try
            {
                await Task.Delay(attempt == 1 ? 1200 : 900, ct);
                reporter.Status($"Post-OTA reconnect {attempt}/12...");
                await using CliBmsConnection connection = await ConnectBmsAsync(endpoint, reporter, ct);
                return await ReadSnapshotAsync(connection, ct);
            }
            catch (OperationCanceledException) { throw; }
            catch (Exception ex)
            {
                last = ex;
                reporter.VerboseLog($"Post-OTA reconnect {attempt}/12 failed: {ex.Message}");
            }
        }

        throw new CliException(
            ExitCodes.ReconnectFailed,
            "post_ota_reconnect_failed",
            "OTA data transfer finished, but the device did not return to a readable BMS application state.",
            inner: last);
    }

    public static async Task<uint?> TryReadFirmwareBuildIdAsync(BmsClient client, CancellationToken ct)
    {
        try
        {
            ushort[] words = await client.ReadRegistersAsync(BmsDiagnostics.Base, 24, ct);
            if (words.Length < 24 || words[0] != BmsDiagnostics.Magic || words[1] != BmsDiagnostics.Schema)
                return null;
            uint value = BmsDiagnostics.U32(words, 22);
            return value == 0 ? null : value;
        }
        catch (OperationCanceledException) { throw; }
        catch { return null; }
    }

    public static async Task<bool> TryDetectD008Async(BmsClient client, CancellationToken ct)
    {
        try
        {
            ushort[] words = await client.ReadRegistersAsync(0x2E00, 2, ct);
            return words.Length >= 2 && words[0] == 0xD008 && words[1] == 1;
        }
        catch (OperationCanceledException) { throw; }
        catch { return false; }
    }

    public static object ToScanRow(DiscoveredDevice d) => new
    {
        name = d.Name,
        mac = BmsBleTransport.FormatBluetoothAddress(d.Address),
        address = d.Address.ToString("X12"),
        rssiDbm = d.Rssi
    };

    public static ulong ParseMac(string value)
    {
        string hex = new(value.Where(Uri.IsHexDigit).ToArray());
        if (hex.Length != 12 || !ulong.TryParse(hex, System.Globalization.NumberStyles.HexNumber, null, out ulong address))
            throw new CliException(ExitCodes.Usage, "usage", $"Invalid BLE MAC '{value}'. Expected 12 hex digits, e.g. A4:C1:38:12:34:56.");
        return address;
    }

    public static bool IsKnownVersion(string? value) =>
        !string.IsNullOrWhiteSpace(value) &&
        !string.Equals(value, "未知", StringComparison.OrdinalIgnoreCase) &&
        !string.Equals(value, "unknown", StringComparison.OrdinalIgnoreCase);
}
