using Android.Bluetooth;
using Android.Bluetooth.LE;
using Android.Content;
using Java.Lang;

namespace BmsTool.Android;

internal sealed record AndroidScanDevice(string Name, string Mac, int Rssi);

internal sealed class AndroidBleScanner : ScanCallback
{
    private readonly Dictionary<string, AndroidScanDevice> _devices = new(StringComparer.OrdinalIgnoreCase);
    private readonly Action<AndroidScanDevice> _found;

    public AndroidBleScanner(Action<AndroidScanDevice> found) => _found = found;

    public override void OnScanResult(ScanCallbackType callbackType, ScanResult? result)
    {
        base.OnScanResult(callbackType, result);
        if (result?.Device?.Address is not string mac) return;
        string name = result.ScanRecord?.DeviceName ?? result.Device.Name ?? string.Empty;
        var device = new AndroidScanDevice(name, mac, result.Rssi);
        lock (_devices) _devices[mac] = device;
        _found(device);
    }

    public static async Task ScanAsync(Context context, TimeSpan duration,
        Action<AndroidScanDevice> found, CancellationToken ct)
    {
        BluetoothManager manager = (BluetoothManager?)context.GetSystemService(Context.BluetoothService)
            ?? throw new IOException("Android BluetoothManager is unavailable.");
        BluetoothAdapter adapter = manager.Adapter ?? throw new IOException("Android BluetoothAdapter is unavailable.");
        if (!adapter.IsEnabled) throw new IOException("Android Bluetooth is disabled.");
        BluetoothLeScanner scanner = adapter.BluetoothLeScanner
            ?? throw new IOException("Android BLE scanner is unavailable.");
        var callback = new AndroidBleScanner(found);
        using var settingsBuilder = new ScanSettings.Builder();
        settingsBuilder.SetScanMode(global::Android.Bluetooth.LE.ScanMode.LowLatency);
        ScanSettings settings = settingsBuilder.Build() ?? throw new IOException("Unable to build Android BLE scan settings.");
        scanner.StartScan(Array.Empty<ScanFilter>(), settings, callback);
        try { await Task.Delay(duration, ct); }
        finally { scanner.StopScan(callback); }
    }
}
