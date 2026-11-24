using Android.App;
using Android.Content;
using Android.Provider;
using Android.Views;
using Android.Widget;
using BmsTool.Windows;
using Uri = Android.Net.Uri;

namespace BmsTool.Android;

public sealed partial class MainActivity
{
    private const int BatteryLogFolderRequest = 1006;
    private const int BatteryLogShareRequest = 1007;
    private const int BatteryLogNotificationRequest = 1008;
    private static readonly int[] BatteryLogIntervals = [1, 2, 5, 10, 30, 60];

    private Uri? _batteryLogFolder;
    private TextView? _batteryLogFolderView, _batteryLogStatus;
    private Spinner? _batteryLogInterval;
    private Button? _batteryLogChoose, _batteryLogStart, _batteryLogStop, _batteryLogShare;

    private void BuildBatteryLogControls(LinearLayout page)
    {
        page.AddView(SectionTitle("电池数据记录"));
        _batteryLogChoose = ActionButton("选择存储文件夹", PickBatteryLogFolderAsync, false);
        page.AddView(_batteryLogChoose);
        _batteryLogFolderView = DataBlock("尚未选择文件夹");
        page.AddView(_batteryLogFolderView);
        page.AddView(Note("记录间隔"));
        _batteryLogInterval = new Spinner(this) { ContentDescription = "记录间隔" };
        var intervals = BatteryLogIntervals.Select(s => $"{s} 秒").ToArray();
        var adapter = new ArrayAdapter<string>(this, global::Android.Resource.Layout.SimpleSpinnerItem, intervals);
        adapter.SetDropDownViewResource(global::Android.Resource.Layout.SimpleSpinnerDropDownItem);
        _batteryLogInterval.Adapter = adapter;
        _batteryLogInterval.SetSelection(0);
        page.AddView(_batteryLogInterval);
        var controls = Row();
        _batteryLogStart = ActionButton("开始记录", StartBatteryLogAsync, false);
        _batteryLogStop = ActionButton("停止记录", StopBatteryLogAsync, false);
        AddEqual(controls, _batteryLogStart);
        AddEqual(controls, _batteryLogStop);
        page.AddView(controls);
        _batteryLogStatus = DataBlock("未开始");
        page.AddView(_batteryLogStatus);
        _batteryLogShare = ActionButton("选择并分享 CSV", ShareBatteryLogAsync, false);
        page.AddView(_batteryLogShare);
        page.AddView(ActionButton("手机省电设置", OpenBatteryRecordingPowerSettings, false));
        RefreshBatteryLogControls();
    }

    private Task PickBatteryLogFolderAsync()
    {
        if (BatteryRecordingService.Status.Active) return Task.CompletedTask;
        using var picker = new Intent(Intent.ActionOpenDocumentTree);
        picker.AddFlags(ActivityFlags.GrantReadUriPermission | ActivityFlags.GrantWriteUriPermission);
        StartActivityForResult(picker, BatteryLogFolderRequest);
        return Task.CompletedTask;
    }

    private void SelectBatteryLogFolder(Intent? selection)
    {
        if (BatteryRecordingService.Status.Active || selection?.Data is not Uri folder) return;
        if (!selection.Flags.HasFlag(ActivityFlags.GrantWriteUriPermission))
        {
            _batteryLogStatus!.Text = "该文件夹未授权写入，请另选文件夹";
            return;
        }
        string folderId = DocumentsContract.GetTreeDocumentId(folder) ?? folder.ToString() ?? "已选文件夹";
        _batteryLogFolder = folder;
        _batteryLogFolderView!.Text = folderId;
        _batteryLogStatus!.Text = "文件夹已选择";
        RefreshBatteryLogControls();
    }

    private Task ShareBatteryLogAsync()
    {
        if (BatteryRecordingService.Status.Active || _uiBusy) return Task.CompletedTask;
        if (BatteryRecordingService.Status.DocumentUri is string uri && Uri.Parse(uri) is Uri document)
        {
            using (document) OpenBatteryLogShareSheet(document);
        }
        else
        {
            using var picker = new Intent(Intent.ActionOpenDocument);
            picker.AddCategory(Intent.CategoryOpenable);
            // Providers disagree on the MIME type for CSV. Check the selected file name.
            picker.SetType("*/*");
            picker.AddFlags(ActivityFlags.GrantReadUriPermission);
            StartActivityForResult(picker, BatteryLogShareRequest);
        }
        return Task.CompletedTask;
    }

    private async Task ShareSelectedBatteryLogAsync(Uri document)
    {
        if (BatteryRecordingService.Status.Active || _uiBusy) return;
        try
        {
            string? name = await Task.Run(() => ReadDisplayName(document));
            if (IsDestroyed || BatteryRecordingService.Status.Active || _uiBusy) return;
            if (name?.EndsWith(".csv", StringComparison.OrdinalIgnoreCase) != true)
            {
                SetStatus("请选择 CSV 日志文件", true);
                return;
            }
            OpenBatteryLogShareSheet(document);
        }
        catch (Exception ex)
        {
            SetStatus("分享 CSV 失败：" + ex.Message, true);
            AppendLog("BATTERY_LOG_SHARE_FAIL " + ex.Message);
        }
    }

    private void OpenBatteryLogShareSheet(Uri document)
    {
        if (document.Scheme != "content") throw new IOException("请选择系统文件选择器中的 CSV");
        using var send = new Intent(Intent.ActionSend);
        send.SetType(BatteryRecordingService.CsvMime);
        send.PutExtra(Intent.ExtraStream, (global::Android.OS.IParcelable)document);
        using var clip = ClipData.NewUri(ContentResolver!, "电池数据 CSV", document);
        send.ClipData = clip;
        send.AddFlags(ActivityFlags.GrantReadUriPermission);
        using var chooser = Intent.CreateChooser(send, "分享电池数据 CSV");
        StartActivity(chooser);
    }

    private void RefreshBatteryLogControls()
    {
        if (_batteryLogStart is null) return;
        var recording = BatteryRecordingService.Status;
        bool running = recording.Active;
        _batteryLogChoose!.Enabled = _batteryLogInterval!.Enabled = !running && !_uiBusy;
        _batteryLogStart.Enabled = !running && !_uiBusy && _mobileCalibrationSession is null &&
            _client is not null && _batteryLogFolder is not null;
        _batteryLogStop!.Enabled = running && !recording.Stopping;
        _batteryLogShare!.Enabled = !running && !_uiBusy;
        _batteryLogShare.Text = recording.DocumentUri is null ? "选择并分享 CSV" : "分享本次 CSV";
        foreach (var button in new[] { _batteryLogChoose, _batteryLogStart, _batteryLogStop, _batteryLogShare })
            button!.Alpha = button.Enabled ? 1f : 0.4f;
    }

    private Task StartBatteryLogAsync()
    {
        if (BatteryRecordingService.Status.Active || _uiBusy || _mobileCalibrationSession is not null)
            return Task.CompletedTask;
        if (_client is not BmsClient client || _connectedMac is not string mac ||
            _transport is not AndroidBmsBleTransport { IsConnected: true } transport)
        {
            _batteryLogStatus!.Text = "请先连接电池";
            return Task.CompletedTask;
        }
        if (_batteryLogFolder?.ToString() is not string folder)
        {
            _batteryLogStatus!.Text = "请先选择存储文件夹";
            return Task.CompletedTask;
        }

        int seconds = BatteryLogIntervals[Math.Max(0, _batteryLogInterval!.SelectedItemPosition)];
        BatteryRecordingService.Start(this,
            new(client, transport, _operationGate, mac, folder, seconds, BatteryLogCalibrationCheck()));
        if (OperatingSystem.IsAndroidVersionAtLeast(33) &&
            CheckSelfPermission(global::Android.Manifest.Permission.PostNotifications) != global::Android.Content.PM.Permission.Granted &&
            !PreferencesForBatteryNotification())
        {
            RequestPermissions(new[] { global::Android.Manifest.Permission.PostNotifications }, BatteryLogNotificationRequest);
        }
        return Task.CompletedTask;
    }

    private bool PreferencesForBatteryNotification()
    {
        using var settings = GetSharedPreferences("battery-recording", FileCreationMode.Private);
        if (settings?.GetBoolean("notification-requested", false) == true) return true;
        using var edit = settings?.Edit();
        edit?.PutBoolean("notification-requested", true)?.Apply();
        return false;
    }

    private Task StopBatteryLogAsync()
    {
        BatteryRecordingService.Stop("已停止");
        return Task.CompletedTask;
    }

    private Task OpenBatteryRecordingPowerSettings()
    {
        using var settings = new Intent(Settings.ActionIgnoreBatteryOptimizationSettings);
        StartActivity(settings);
        return Task.CompletedTask;
    }

    private Func<bool> BatteryLogCalibrationCheck()
    {
        // The service must not keep a destroyed Activity or its UI alive.
        var activity = new WeakReference<MainActivity>(this);
        return () => activity.TryGetTarget(out var owner) && owner._mobileCalibrationSession is not null;
    }

    private void ResumeBatteryLogConnection()
    {
        if (BatteryRecordingService.AttachActivity(BatteryLogCalibrationCheck()) is not BatteryRecordingSession session)
            return;
        _operationGate = session.OperationGate;
        _client = session.Client;
        _transport = session.Transport;
        _client.Log += AppendLog;
        _transport.ConnectionProgress += AppendLog;
        _selectedMac = _connectedMac = session.Mac;
        _batteryLogFolder = Uri.Parse(session.FolderUri);
        if (_batteryLogFolder is Uri folder)
            _batteryLogFolderView!.Text = DocumentsContract.GetTreeDocumentId(folder) ?? session.FolderUri;
        _batteryLogInterval!.SetSelection(Array.IndexOf(BatteryLogIntervals, session.IntervalSeconds));
        _connectionView!.Text = _transport.IsConnected ? "已连接 · 后台记录中" : "后台记录中 · 正在重连";
        RefreshActionAvailability();
    }

    protected override void OnStart()
    {
        base.OnStart();
        BatteryRecordingService.StatusChanged += UpdateBatteryLogStatus;
        UpdateBatteryLogStatus();
    }

    private void UpdateBatteryLogStatus()
    {
        if (_batteryLogStatus is not null) _batteryLogStatus.Text = BatteryRecordingService.Status.Text;
        RefreshBatteryLogControls();
    }
}
