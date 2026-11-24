using System.Text;
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
    private const string BatteryLogCsvMime = "text/csv";
    private static readonly int[] BatteryLogIntervals = [1, 2, 5, 10, 30, 60];

    private Uri? _batteryLogFolder;
    private string? _batteryLogShareUri;
    private TextView? _batteryLogFolderView, _batteryLogStatus;
    private Spinner? _batteryLogInterval;
    private Button? _batteryLogChoose, _batteryLogStart, _batteryLogStop, _batteryLogShare;
    private CancellationTokenSource? _batteryLogCts;
    private Task? _batteryLogTask;
    private string? _batteryLogStopReason;

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
        RefreshBatteryLogControls();
    }

    private Task PickBatteryLogFolderAsync()
    {
        if (_batteryLogCts is not null) return Task.CompletedTask;
        using var picker = new Intent(Intent.ActionOpenDocumentTree);
        picker.AddFlags(ActivityFlags.GrantReadUriPermission | ActivityFlags.GrantWriteUriPermission);
        StartActivityForResult(picker, BatteryLogFolderRequest);
        return Task.CompletedTask;
    }

    private void SelectBatteryLogFolder(Intent? selection)
    {
        if (_batteryLogCts is not null || selection?.Data is not Uri folder) return;
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
        if (_batteryLogCts is not null || _uiBusy) return Task.CompletedTask;
        if (_batteryLogShareUri is string uri && Uri.Parse(uri) is Uri document)
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
        if (_batteryLogCts is not null || _uiBusy) return;
        try
        {
            string? name = await Task.Run(() => ReadDisplayName(document));
            if (IsDestroyed || _batteryLogCts is not null || _uiBusy) return;
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
        send.SetType(BatteryLogCsvMime);
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
        bool running = _batteryLogCts is not null;
        _batteryLogChoose!.Enabled = _batteryLogInterval!.Enabled = !running && !_uiBusy;
        _batteryLogStart.Enabled = !running && !_uiBusy && _mobileCalibrationSession is null &&
            _client is not null && _batteryLogFolder is not null;
        _batteryLogStop!.Enabled = running && _batteryLogCts?.IsCancellationRequested == false;
        _batteryLogShare!.Enabled = !running && !_uiBusy;
        _batteryLogShare.Text = _batteryLogShareUri is null ? "选择并分享 CSV" : "分享本次 CSV";
        foreach (var button in new[] { _batteryLogChoose, _batteryLogStart, _batteryLogStop, _batteryLogShare })
            button!.Alpha = button.Enabled ? 1f : 0.4f;
    }

    private Task StartBatteryLogAsync()
    {
        if (_batteryLogCts is not null || _uiBusy || _mobileCalibrationSession is not null) return Task.CompletedTask;
        if (_client is not BmsClient client || _connectedMac is not string mac || _transport?.IsConnected != true)
        {
            _batteryLogStatus!.Text = "请先连接电池";
            return Task.CompletedTask;
        }
        if (_batteryLogFolder is not Uri folder)
        {
            _batteryLogStatus!.Text = "请先选择存储文件夹";
            return Task.CompletedTask;
        }

        int seconds = BatteryLogIntervals[Math.Max(0, _batteryLogInterval!.SelectedItemPosition)];
        string name = $"BMS_{mac.Replace(":", "")}_{DateTime.Now:yyyyMMdd_HHmmss_fff}.csv";
        var operation = new CancellationTokenSource();
        _batteryLogCts = operation;
        _batteryLogShareUri = null;
        _batteryLogStopReason = null;
        _batteryLogStatus!.Text = $"正在创建 {name}";
        if (Window?.DecorView is View decor) decor.KeepScreenOn = true;
        RefreshBatteryLogControls();
        _batteryLogTask = Task.Run(() => WriteBatteryLogAsync(client, mac, folder, name, seconds, operation));
        return Task.CompletedTask;
    }

    private async Task StopBatteryLogAsync()
    {
        CancelBatteryLog("已停止");
        RefreshBatteryLogControls();
        if (_batteryLogTask is Task task)
        {
            _batteryLogStatus!.Text = "正在完成最后的写入…";
            await task;
        }
    }

    private void CancelBatteryLog(string reason)
    {
        if (_batteryLogCts is not { IsCancellationRequested: false } operation) return;
        _batteryLogStopReason = reason;
        operation.Cancel();
    }

    private async Task WriteBatteryLogAsync(BmsClient client, string mac, Uri folder, string name, int seconds,
        CancellationTokenSource operation)
    {
        CancellationToken ct = operation.Token;
        long samples = 0, gaps = 0;
        string? error = null;
        string? documentUri = null;
        try
        {
            ct.ThrowIfCancellationRequested();
            using var parent = DocumentsContract.BuildDocumentUriUsingTree(folder, DocumentsContract.GetTreeDocumentId(folder));
            using var document = DocumentsContract.CreateDocument(ContentResolver!, parent!, BatteryLogCsvMime, name)
                ?? throw new IOException("无法在所选文件夹创建 CSV");
            Stream stream = ContentResolver!.OpenOutputStream(document)
                ?? throw new IOException("无法打开所选文件夹中的 CSV");
            await using var writer = new StreamWriter(stream, new UTF8Encoding(true));
            await writer.WriteLineAsync(BatteryLogCsv.Header);
            await writer.FlushAsync();
            documentUri = document.ToString();

            using var timer = new PeriodicTimer(TimeSpan.FromSeconds(seconds));
            do
            {
                BatterySnapshot? sample = null;
                string status = "ok";
                await _operationGate.WaitAsync(ct);
                try
                {
                    if (!ReferenceEquals(client, _client) || _transport?.IsConnected != true)
                        throw new IOException("电池连接已断开");
                    if (_mobileCalibrationSession is not null)
                        status = "calibration";
                    else
                    {
                        try { sample = await client.ReadBatteryAsync(ct); }
                        catch (Exception ex) when ((ex is IOException or TimeoutException or InvalidDataException) &&
                            !ct.IsCancellationRequested && _transport?.IsConnected == true)
                        {
                            status = "read_error";
                            AppendLog("BATTERY_LOG_READ_FAIL " + ex.Message);
                        }
                    }
                }
                finally { _operationGate.Release(); }

                await writer.WriteLineAsync(BatteryLogCsv.Row(DateTimeOffset.Now, mac, status, sample));
                await writer.FlushAsync();
                if (sample is null) gaps++; else samples++;
                string progress = $"记录中 · {samples} 条 · 缺口 {gaps} · 每 {seconds} 秒\n{name}";
                RunOnUiThread(() =>
                {
                    if (!IsDestroyed && ReferenceEquals(operation, _batteryLogCts) && !operation.IsCancellationRequested)
                        _batteryLogStatus!.Text = progress;
                });
            }
            while (await timer.WaitForNextTickAsync(ct));
        }
        catch (OperationCanceledException) when (ct.IsCancellationRequested) { }
        catch (Exception ex)
        {
            error = ex.Message;
            AppendLog("BATTERY_LOG_STOP " + ex.Message);
        }
        finally
        {
            RunOnUiThread(() =>
            {
                if (!ReferenceEquals(operation, _batteryLogCts)) return;
                _batteryLogCts = null;
                _batteryLogTask = null;
                // Processing reaches here only after the writer has closed the document.
                _batteryLogShareUri = error is null && samples + gaps > 0 ? documentUri : null;
                operation.Dispose();
                if (IsDestroyed) return;
                if (Window?.DecorView is View decor) decor.KeepScreenOn = false;
                string reason = error is null ? _batteryLogStopReason ?? "已停止" : "记录中断 · " + error;
                _batteryLogStatus!.Text = $"{reason} · {samples} 条 · 缺口 {gaps}\n{name}";
                RefreshBatteryLogControls();
            });
        }
    }
}
