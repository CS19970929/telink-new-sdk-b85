using Android;
using Android.App;
using Android.Content;
using Android.Content.PM;
using Android.OS;
using Android.Util;
using BmsTool.Windows;
using System.Text;

namespace BmsTool.Android;

[Activity(Label = "BMS Tool", MainLauncher = true, Exported = true,
    LaunchMode = LaunchMode.SingleTask, ScreenOrientation = ScreenOrientation.Portrait)]
[IntentFilter(new[] { Intent.ActionView },
    Categories = new[] { Intent.CategoryDefault, Intent.CategoryBrowsable },
    DataMimeTypes = new[] { "application/octet-stream", "application/x-binary", "application/macbinary" })]
[IntentFilter(new[] { Intent.ActionSend },
    Categories = new[] { Intent.CategoryDefault },
    DataMimeTypes = new[] { "application/octet-stream", "application/x-binary", "application/macbinary" })]
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
    private string? _connectedMac;
    private bool _intentStarted;
    private bool _connectedAutoOtaRunning;

    protected override void OnCreate(Bundle? savedInstanceState)
    {
        base.OnCreate(savedInstanceState);
        string logDirectory = GetExternalFilesDir(null)?.AbsolutePath ?? FilesDir!.AbsolutePath;
        _logFilePath = Path.Combine(logDirectory, $"bms-tool-{DateTimeOffset.Now:yyyyMMdd-HHmmss}.log");
        BuildUi();
        ApplyIntentValues();
        _ = RefreshFirmwareInboxAsync();
        _ = HandleIncomingFirmwareIntentAsync(Intent);
        RequestBluetoothPermissions();
        AppendLog($"LOG_FILE path={_logFilePath}");
        _refreshCts = new CancellationTokenSource();
        _ = RefreshLoopAsync(_refreshCts.Token);
        _ = HandleConnectedAutoOtaIntentAsync(Intent);
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

    protected override void OnNewIntent(Intent? intent)
    {
        base.OnNewIntent(intent);
        if (intent is null) return;
        Intent = intent;
        ApplyIntentValues();
        _ = HandleIncomingFirmwareIntentAsync(intent);
        _ = HandleConnectedAutoOtaIntentAsync(intent);
    }

    private async Task HandleConnectedAutoOtaIntentAsync(Intent? intent)
    {
        if (intent is null || !intent.GetBooleanExtra("auto_ota_if_connected", false)) return;
        intent.RemoveExtra("auto_ota_if_connected");
        if (_connectedAutoOtaRunning) return;

        string uploadId = intent.GetStringExtra("auto_ota_upload_id") ?? string.Empty;
        string sha256 = intent.GetStringExtra("auto_ota_sha256") ?? string.Empty;
        string fileName = intent.GetStringExtra("firmware_inbox_name") ?? string.Empty;
        ISharedPreferences? authorization = GetSharedPreferences(
            AndroidFirmwareInbox.AutomationAuthorizationPreferences, FileCreationMode.Private);
        string authorizedUploadId = authorization?.GetString("upload_id", string.Empty) ?? string.Empty;
        string authorizedFileName = authorization?.GetString("file_name", string.Empty) ?? string.Empty;
        string authorizedSha256 = authorization?.GetString("sha256", string.Empty) ?? string.Empty;
        long expiresUtcMs = authorization?.GetLong("expires_utc_ms", 0) ?? 0;
        authorization?.Edit()?.Clear()?.Apply();
        bool authorized = expiresUtcMs >= DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() &&
            string.Equals(uploadId, authorizedUploadId, StringComparison.Ordinal) &&
            string.Equals(fileName, authorizedFileName, StringComparison.Ordinal) &&
            string.Equals(sha256, authorizedSha256, StringComparison.OrdinalIgnoreCase);
        if (!authorized)
        {
            SetStatus("已拒绝自动 OTA：本次固件导入授权无效或已过期", true);
            AppendLog("AUTO_OTA_SKIPPED reason=invalid_or_expired_import_authorization");
            return;
        }

        string requestedMac = _macInput?.Text?.Trim() ?? string.Empty;
        if (_client is null || _transport is null || string.IsNullOrWhiteSpace(_connectedMac) ||
            !string.Equals(requestedMac, _connectedMac, StringComparison.OrdinalIgnoreCase))
        {
            SetStatus("固件已导入；当前没有保持连接的目标 BMS，已跳过自动 OTA", true);
            AppendLog($"AUTO_OTA_SKIPPED reason=no_matching_active_bms_connection requested='{requestedMac}' connected='{_connectedMac}'");
            return;
        }

        _connectedAutoOtaRunning = true;
        try
        {
            AppendLog($"AUTO_OTA_ACCEPTED mac={_connectedMac}");
            await StartOtaAsync(skipConfirmation: true, requireExistingConnection: true);
        }
        finally { _connectedAutoOtaRunning = false; }
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
        string rememberedMac = GetPreferences(FileCreationMode.Private)?.GetString("last_bms_mac", string.Empty)
            ?? string.Empty;
        _macInput!.Text = !string.IsNullOrWhiteSpace(mac)
            ? mac
            : rememberedMac;
        if (!string.IsNullOrWhiteSpace(firmwareInboxName) && AndroidFirmwareInbox.IsValidFileName(firmwareInboxName))
            _firmwareInput!.Text = Path.Combine(FirmwareInboxDirectory(), firmwareInboxName);
        else if (!string.IsNullOrWhiteSpace(firmware))
            _firmwareInput!.Text = firmware;
        if (!string.IsNullOrWhiteSpace(expectedSerial)) _expectedSerialInput!.Text = expectedSerial;
        if (Intent.GetBooleanExtra("show_tools", false)) ShowPage(4);
    }

    private async Task HandleIncomingFirmwareIntentAsync(Intent? intent)
    {
        global::Android.Net.Uri? uri = null;
        if (intent?.Action == Intent.ActionView)
            uri = intent.Data;
        else if (intent?.Action == Intent.ActionSend)
        {
            if (intent.ClipData?.ItemCount > 0)
                uri = intent.ClipData.GetItemAt(0)?.Uri;
            if (uri is null && OperatingSystem.IsAndroidVersionAtLeast(33))
                uri = intent.GetParcelableExtra(Intent.ExtraStream,
                    Java.Lang.Class.FromType(typeof(global::Android.Net.Uri))) as global::Android.Net.Uri;
            else if (uri is null)
            {
#pragma warning disable CA1422
                uri = intent.GetParcelableExtra(Intent.ExtraStream) as global::Android.Net.Uri;
#pragma warning restore CA1422
            }
        }
        if (uri is null) return;

        string? name = ReadDisplayName(uri);
        if (string.IsNullOrWhiteSpace(name) || !name.EndsWith(".bin", StringComparison.OrdinalIgnoreCase))
        {
            SetStatus("已拒绝外部文件：只接受 .bin 固件", true);
            return;
        }

        ShowPage(4);
        await ImportFirmwareAsync(uri);
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
