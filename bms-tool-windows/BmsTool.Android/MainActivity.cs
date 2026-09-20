using Android;
using Android.App;
using Android.Content;
using Android.Content.PM;
using Android.OS;
using Android.Util;
using BmsTool.Windows;
using System.Text;

namespace BmsTool.Android;

[Activity(Label = "BMS Tool", MainLauncher = true, Exported = true, ScreenOrientation = ScreenOrientation.Portrait)]
public sealed partial class MainActivity : Activity
{
    private const string LogTag = "BmsTool.Android";
    private const int BluetoothPermissionRequest = 1001;
    private const int FirmwarePickerRequest = 1002;

    private readonly SemaphoreSlim _operationGate = new(1, 1);
    private readonly object _logFileGate = new();
    private readonly List<MonitorRecord> _monitorRecords = new();
    private AndroidBmsBleTransport? _transport;
    private BmsClient? _client;
    private CancellationTokenSource? _operationCts;
    private CancellationTokenSource? _refreshCts;
    private CancellationTokenSource? _monitorCts;
    private string? _logFilePath;
    private bool _intentStarted;

    protected override void OnCreate(Bundle? savedInstanceState)
    {
        base.OnCreate(savedInstanceState);
        string logDirectory = GetExternalFilesDir(null)?.AbsolutePath ?? FilesDir!.AbsolutePath;
        _logFilePath = Path.Combine(logDirectory, $"bms-tool-{DateTimeOffset.Now:yyyyMMdd-HHmmss}.log");
        BuildUi();
        ApplyIntentValues();
        _ = RefreshFirmwareInboxAsync();
        RequestBluetoothPermissions();
        AppendLog($"LOG_FILE path={_logFilePath}");
        _refreshCts = new CancellationTokenSource();
        _ = RefreshLoopAsync(_refreshCts.Token);
    }

    protected override void OnResume()
    {
        base.OnResume();
        if (_intentStarted || !HasBluetoothPermissions()) return;
        string? operation = Intent?.GetStringExtra("operation");
        if (operation is not ("ota" or "info")) return;
        _intentStarted = true;
        _ = RunIntentOperationAsync(operation);
    }

    public override void OnRequestPermissionsResult(int requestCode, string[] permissions, Permission[] grantResults)
    {
        base.OnRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode != BluetoothPermissionRequest) return;
        if (HasBluetoothPermissions()) OnResume();
        else SetStatus("蓝牙权限被拒绝，无法扫描或连接", true);
    }

    protected override void OnActivityResult(int requestCode, Result resultCode, Intent? data)
    {
        base.OnActivityResult(requestCode, resultCode, data);
        if (requestCode != FirmwarePickerRequest || resultCode != Result.Ok || data?.Data is null) return;
        _ = ImportFirmwareAsync(data.Data);
    }

    private void ApplyIntentValues()
    {
        if (Intent is null) return;
        string? mac = Intent.GetStringExtra("mac");
        string? firmware = Intent.GetStringExtra("firmware");
        string? firmwareInboxName = Intent.GetStringExtra("firmware_inbox_name");
        string? expectedSerial = Intent.GetStringExtra("expected_serial");
        if (!string.IsNullOrWhiteSpace(mac)) _macInput!.Text = mac;
        if (!string.IsNullOrWhiteSpace(firmwareInboxName) && AndroidFirmwareInbox.IsValidFileName(firmwareInboxName))
            _firmwareInput!.Text = Path.Combine(FirmwareInboxDirectory(), firmwareInboxName);
        else if (!string.IsNullOrWhiteSpace(firmware))
            _firmwareInput!.Text = firmware;
        if (!string.IsNullOrWhiteSpace(expectedSerial)) _expectedSerialInput!.Text = expectedSerial;
    }

    private async Task RunIntentOperationAsync(string operation)
    {
        await ConnectSelectedAsync();
        if (_client is null)
        {
            AppendLog("TEST_RESULT FAIL type=ConnectionError message=Unable to connect and probe the selected BMS.");
            return;
        }
        if (operation == "ota") await StartOtaAsync();
        else AppendLog("TEST_RESULT INFO_OK");
    }

    private async Task WithClientAsync(string title, Func<BmsClient, CancellationToken, Task> action,
        TimeSpan? timeout = null)
    {
        if (_client is null)
        {
            SetStatus("请先扫描并连接 BMS", true);
            return;
        }
        SetBusy(true);
        SetStatus(title + "（等待当前刷新结束）…");
        await _operationGate.WaitAsync();
        _operationCts?.Cancel();
        _operationCts?.Dispose();
        _operationCts = new CancellationTokenSource(timeout ?? TimeSpan.FromSeconds(60));
        SetStatus(title + "…");
        try { await action(_client, _operationCts.Token); }
        catch (System.OperationCanceledException)
        {
            SetStatus(title + "已取消");
            AppendLog($"{title} CANCELLED");
        }
        catch (Exception ex)
        {
            SetStatus(title + "失败：" + ex.Message, true);
            AppendLog($"{title} FAIL type={ex.GetType().Name} message={ex.Message}");
            Log.Error(LogTag, ex.ToString());
        }
        finally
        {
            SetBusy(false);
            _operationGate.Release();
        }
    }

    private async Task<T?> WithClientResultAsync<T>(string title, Func<BmsClient, CancellationToken, Task<T>> action,
        TimeSpan? timeout = null)
    {
        T? value = default;
        await WithClientAsync(title, async (client, ct) => value = await action(client, ct), timeout);
        return value;
    }

    private void CancelCurrentOperation() => _operationCts?.Cancel();

    private void AppendLog(string message)
    {
        string line = $"{DateTimeOffset.Now:HH:mm:ss.fff} {message}";
        Log.Info(LogTag, line);
        if (_logFilePath is not null)
        {
            try
            {
                lock (_logFileGate)
                    File.AppendAllText(_logFilePath, line + System.Environment.NewLine, Encoding.UTF8);
            }
            catch (IOException ex) { Log.Warn(LogTag, $"log write failed: {ex.Message}"); }
        }
        RunOnUiThread(() => _logView?.Append(line + System.Environment.NewLine));
    }

    private void SetStatus(string text, bool error = false) => RunOnUiThread(() =>
    {
        if (_statusView is null) return;
        _statusView.Text = text;
        _statusView.SetTextColor(global::Android.Graphics.Color.ParseColor(error ? "#B3261E" : "#325A43"));
    });

    private bool HasBluetoothPermissions()
    {
        if (OperatingSystem.IsAndroidVersionAtLeast(31))
            return CheckSelfPermission(Manifest.Permission.BluetoothScan) == Permission.Granted &&
                   CheckSelfPermission(Manifest.Permission.BluetoothConnect) == Permission.Granted;
        if (OperatingSystem.IsAndroidVersionAtLeast(23))
            return CheckSelfPermission(Manifest.Permission.AccessFineLocation) == Permission.Granted;
        return true;
    }

    private void RequestBluetoothPermissions()
    {
        if (HasBluetoothPermissions()) return;
        if (OperatingSystem.IsAndroidVersionAtLeast(31))
            RequestPermissions(new[] { Manifest.Permission.BluetoothScan, Manifest.Permission.BluetoothConnect }, BluetoothPermissionRequest);
        else
            RequestPermissions(new[] { Manifest.Permission.AccessFineLocation }, BluetoothPermissionRequest);
    }

    protected override void OnDestroy()
    {
        _refreshCts?.Cancel();
        _monitorCts?.Cancel();
        _operationCts?.Cancel();
        _ = DisposeConnectionAsync();
        _refreshCts?.Dispose();
        _monitorCts?.Dispose();
        _operationCts?.Dispose();
        base.OnDestroy();
    }

    private sealed record MonitorRecord(DateTimeOffset Time, BatterySnapshot Snapshot);
}
