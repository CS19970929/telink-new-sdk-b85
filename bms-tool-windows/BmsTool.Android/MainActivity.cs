using Android;
using Android.App;
using Android.Content.PM;
using Android.OS;
using Android.Util;
using Android.Views;
using Android.Widget;
using BmsTool.Windows;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace BmsTool.Android;

[Activity(Label = "BMS Tool Android", MainLauncher = true, Exported = true)]
public sealed class MainActivity : Activity
{
    private const string LogTag = "BmsTool.Android";
    private EditText? _macInput;
    private EditText? _firmwareInput;
    private EditText? _expectedSerialInput;
    private TextView? _logView;
    private Button? _infoButton;
    private Button? _otaButton;
    private CancellationTokenSource? _operationCts;
    private bool _intentStarted;
    private string? _logFilePath;
    private readonly object _logFileGate = new();

    protected override void OnCreate(Bundle? savedInstanceState)
    {
        base.OnCreate(savedInstanceState);
        string logDirectory = GetExternalFilesDir(null)?.AbsolutePath ?? FilesDir!.AbsolutePath;
        _logFilePath = Path.Combine(logDirectory, $"bms-tool-{DateTimeOffset.Now:yyyyMMdd-HHmmss}.log");
        BuildUi();
        ApplyIntentValues();
        RequestBluetoothPermissions();
        AppendLog($"LOG_FILE path={_logFilePath}");
    }

    protected override void OnResume()
    {
        base.OnResume();
        if (!_intentStarted && HasBluetoothPermissions() && string.Equals(Intent?.GetStringExtra("operation"), "ota", StringComparison.OrdinalIgnoreCase))
        {
            _intentStarted = true;
            _ = StartOperationAsync(runOta: true);
        }
        else if (!_intentStarted && HasBluetoothPermissions() && string.Equals(Intent?.GetStringExtra("operation"), "info", StringComparison.OrdinalIgnoreCase))
        {
            _intentStarted = true;
            _ = StartOperationAsync(runOta: false);
        }
    }

    public override void OnRequestPermissionsResult(int requestCode, string[] permissions, Permission[] grantResults)
    {
        base.OnRequestPermissionsResult(requestCode, permissions, grantResults);
        if (HasBluetoothPermissions())
            OnResume();
        else
            AppendLog("ERROR Bluetooth permission denied.");
    }

    private void BuildUi()
    {
        var scroll = new ScrollView(this);
        var panel = new LinearLayout(this) { Orientation = Orientation.Vertical };
        int padding = (int)(16 * Resources!.DisplayMetrics!.Density);
        panel.SetPadding(padding, padding, padding, padding);

        panel.AddView(new TextView(this) { Text = "BMS Tool Android · shared C# protocol core", TextSize = 20 });
        _macInput = AddInput(panel, "BLE MAC", "A4:C1:38:00:30:CF");
        _firmwareInput = AddInput(panel, "Telink firmware path", string.Empty);
        _expectedSerialInput = AddInput(panel, "Expected Serial after OTA", string.Empty);

        _infoButton = new Button(this) { Text = "Read info" };
        _infoButton.Click += async (_, _) => await StartOperationAsync(runOta: false);
        panel.AddView(_infoButton);

        _otaButton = new Button(this) { Text = "OTA + Serial verify" };
        _otaButton.Click += async (_, _) => await StartOperationAsync(runOta: true);
        panel.AddView(_otaButton);

        var cancelButton = new Button(this) { Text = "Cancel" };
        cancelButton.Click += (_, _) => _operationCts?.Cancel();
        panel.AddView(cancelButton);

        _logView = new TextView(this) { TextSize = 12 };
        _logView.SetTextIsSelectable(true);
        panel.AddView(_logView, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MatchParent, ViewGroup.LayoutParams.WrapContent));
        scroll.AddView(panel);
        SetContentView(scroll);
    }

    private EditText AddInput(LinearLayout panel, string hint, string value)
    {
        var input = new EditText(this) { Hint = hint, Text = value };
        input.SetSingleLine(true);
        panel.AddView(input);
        return input;
    }

    private void ApplyIntentValues()
    {
        if (Intent is null) return;
        string? mac = Intent.GetStringExtra("mac");
        string? firmware = Intent.GetStringExtra("firmware");
        string? expectedSerial = Intent.GetStringExtra("expected_serial");
        if (!string.IsNullOrWhiteSpace(mac)) _macInput!.Text = mac;
        if (!string.IsNullOrWhiteSpace(firmware)) _firmwareInput!.Text = firmware;
        if (!string.IsNullOrWhiteSpace(expectedSerial)) _expectedSerialInput!.Text = expectedSerial;
    }

    private async Task StartOperationAsync(bool runOta)
    {
        if (!HasBluetoothPermissions())
        {
            RequestBluetoothPermissions();
            return;
        }

        string mac = _macInput?.Text?.Trim() ?? string.Empty;
        string firmware = _firmwareInput?.Text?.Trim() ?? string.Empty;
        string expectedSerial = _expectedSerialInput?.Text?.Trim() ?? string.Empty;
        if (string.IsNullOrWhiteSpace(mac))
        {
            AppendLog("ERROR MAC is required.");
            return;
        }

        SetBusy(true);
        _operationCts?.Cancel();
        _operationCts?.Dispose();
        _operationCts = new CancellationTokenSource(TimeSpan.FromMinutes(6));
        try
        {
            AndroidDeviceInfo before = await ReadDeviceInfoAsync(mac, _operationCts.Token);
            AppendJson("INFO_BEFORE", before);
            if (!runOta)
            {
                AppendLog("TEST_RESULT INFO_OK");
                return;
            }

            FirmwareImage image = FirmwareImage.LoadStrictTelink(firmware);
            string sha256 = Convert.ToHexString(SHA256.HashData(image.Bytes));
            AppendLog($"PREFLIGHT file={image.FileName} bytes={image.ImageSize} marker={image.HasD008TlnkStartupMarker} crcTrailer={image.HasValidTelinkCrcTrailer} sha256={sha256}");

            bool otaResult;
            await using (var transport = new AndroidOtaBleTransport(this, mac))
            {
                transport.Log += AppendLog;
                await transport.ConnectAsync(_operationCts.Token);
                var ota = new TelinkOtaClient(transport);
                ota.Log += AppendLog;
                ota.Progress += progress => AppendLog($"PROGRESS percent={progress.Percent:F1} sent={progress.SentBytes}/{progress.TotalBytes} rate={progress.BytesPerSecond:F0}");
                otaResult = await ota.UpgradeAsync(image, OtaTransferMode.LegacyFast, _operationCts.Token);
            }

            AppendLog($"OTA_RESULT_CONFIRMED={otaResult}");
            AndroidDeviceInfo after = await ReadIdentityAfterOtaAsync(mac, _operationCts.Token);
            AppendJson("INFO_AFTER", after);
            bool serialMatch = string.IsNullOrWhiteSpace(expectedSerial) || string.Equals(after.Identity.Serial, expectedSerial, StringComparison.Ordinal);
            AppendLog($"SERIAL_VERIFY expected='{expectedSerial}' actual='{after.Identity.Serial}' match={serialMatch}");
            if (!otaResult)
                throw new InvalidOperationException("OTA server did not report OTA_SUCCESS.");
            if (!serialMatch)
                throw new InvalidOperationException("OTA completed but Serial readback does not match the expected firmware.");
            AppendLog("TEST_RESULT OTA_OK");
        }
        catch (System.OperationCanceledException)
        {
            AppendLog("TEST_RESULT CANCELLED");
        }
        catch (Exception ex)
        {
            AppendLog($"TEST_RESULT FAIL type={ex.GetType().Name} message={ex.Message}");
            Log.Error(LogTag, ex.ToString());
        }
        finally
        {
            SetBusy(false);
        }
    }

    private async Task<AndroidDeviceInfo> ReadIdentityAfterOtaAsync(string mac, CancellationToken ct)
    {
        Exception? last = null;
        for (int attempt = 1; attempt <= 8; attempt++)
        {
            ct.ThrowIfCancellationRequested();
            try
            {
                AppendLog($"POST_OTA_RECONNECT attempt={attempt}/8");
                return await ReadDeviceInfoAsync(mac, ct);
            }
            catch (Exception ex) when (attempt < 8 && ex is not System.OperationCanceledException)
            {
                last = ex;
                AppendLog($"POST_OTA_RECONNECT_RETRY type={ex.GetType().Name} message={ex.Message}");
                await Task.Delay(TimeSpan.FromSeconds(2), ct);
            }
        }
        throw new IOException("BMS did not reconnect after OTA.", last);
    }

    private async Task<AndroidDeviceInfo> ReadDeviceInfoAsync(string mac, CancellationToken ct)
    {
        await using var transport = new AndroidBmsBleTransport(this, mac);
        transport.ConnectionProgress += AppendLog;
        await transport.ConnectAsync(ct);
        await using var client = new BmsClient(transport);
        client.Log += AppendLog;
        await client.ProbeAsync(ct);
        DeviceIdentity identity = await client.ReadIdentityAsync(mac, "BT_DEFAULT", ct);
        uint? buildId = await client.TryReadFirmwareBuildIdAsync(ct);
        return new AndroidDeviceInfo(identity, buildId?.ToString("x8"));
    }

    private void AppendJson(string prefix, object value) =>
        AppendLog($"{prefix} {JsonSerializer.Serialize(value)}");

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
            catch (IOException ex)
            {
                Log.Warn(LogTag, $"log file write failed: {ex.Message}");
            }
        }
        RunOnUiThread(() =>
        {
            if (_logView is null) return;
            _logView.Append(line + System.Environment.NewLine);
        });
    }

    private void SetBusy(bool busy) => RunOnUiThread(() =>
    {
        if (_infoButton is not null) _infoButton.Enabled = !busy;
        if (_otaButton is not null) _otaButton.Enabled = !busy;
    });

    private bool HasBluetoothPermissions()
    {
        if (!OperatingSystem.IsAndroidVersionAtLeast(31)) return true;
        return CheckSelfPermission(Manifest.Permission.BluetoothScan) == Permission.Granted &&
               CheckSelfPermission(Manifest.Permission.BluetoothConnect) == Permission.Granted;
    }

    private void RequestBluetoothPermissions()
    {
        if (OperatingSystem.IsAndroidVersionAtLeast(31) && !HasBluetoothPermissions())
            RequestPermissions(new[] { Manifest.Permission.BluetoothScan, Manifest.Permission.BluetoothConnect }, 1001);
    }

    protected override void OnDestroy()
    {
        _operationCts?.Cancel();
        _operationCts?.Dispose();
        base.OnDestroy();
    }

    private sealed record AndroidDeviceInfo(DeviceIdentity Identity, string? FirmwareBuildId);
}
