using System.Collections.ObjectModel;
using System.Diagnostics;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Threading;

namespace BmsTool.Windows;

public partial class MainWindow
{
    private sealed record MosDisplayRow(string Time, string Source, string State, string Detail);
    private readonly ObservableCollection<MosDisplayRow> _mosRows = new();
    private readonly CheckBox _mosAuto = new() { Content = "自动监控并写盘", IsChecked = true, Margin = new Thickness(6) };
    private readonly TextBlock _mosStatus = new() { Text = "连接后自动记录；物理 Gate/Vgs 未测", TextWrapping = TextWrapping.Wrap, Margin = new Thickness(6) };
    private readonly TextBlock _mosPathText = new() { TextWrapping = TextWrapping.Wrap, Margin = new Thickness(6) };
    private BmsMosJournal? _mosJournal;
    private BmsClient? _mosClient;
    private DeviceIdentity? _mosIdentity;
    private string? _mosDirectory, _mosGap;
    private BatterySnapshot? _mosPrevious;
    private uint? _mosSequence, _mosBootTick, _mosBuildId;
    private long _mosSample;
    private DateTimeOffset _mosLastSampleUtc;
    private DateTimeOffset _mosLastDiagnosticUtc;
    private CancellationTokenSource? _mosCts;
    private bool _mosBusy, _mosClosed;
    private bool _mosDriverInvalid;
    private readonly (uint Tick, DateTimeOffset Seen)?[] _mosOffTicks = new (uint, DateTimeOffset)?[2];

    private void AddMosMonitorTab()
    {
        var root = new DockPanel { Margin = new Thickness(12) };
        var controls = new WrapPanel();
        var open = new Button { Content = "打开记录目录", Margin = new Thickness(6), Padding = new Thickness(12,5,12,5) };
        open.Click += (_, _) => {
            string path = _mosDirectory ?? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "BmsAssistant", "MosMonitor");
            try { Directory.CreateDirectory(path); Process.Start(new ProcessStartInfo(path) { UseShellExecute = true }); }
            catch (Exception ex) { _mosStatus.Text = "打开目录失败：" + ex.Message; }
        };
        var clear = new Button { Content = "清空列表（保留文件）", Margin = new Thickness(6) };
        clear.Click += (_, _) => _mosRows.Clear();
        _mosAuto.Unchecked += (_, _) => { _mosCts?.Cancel(); NoteMosGap("用户暂停监控"); };
        _mosAuto.Checked += (_, _) => _mosStatus.Text = "已启用，下次电池刷新时继续记录";
        controls.Children.Add(_mosAuto); controls.Children.Add(open); controls.Children.Add(clear);
        DockPanel.SetDock(controls, Dock.Top); root.Children.Add(controls);
        DockPanel.SetDock(_mosStatus, Dock.Top); root.Children.Add(_mosStatus);
        DockPanel.SetDock(_mosPathText, Dock.Top); root.Children.Add(_mosPathText);
        var note = new TextBlock { Text = "保存全部电池样本、指示边沿、固件变化及通信缺口。列表显示最近 300 条。请求、命令和 AFE 报告分别记录；不能据此证明实际负载中断。旧固件无法补回轮询间的短暂变化。", TextWrapping = TextWrapping.Wrap, Margin = new Thickness(6) };
        DockPanel.SetDock(note, Dock.Top); root.Children.Add(note);
        var grid = new DataGrid { IsReadOnly = true, AutoGenerateColumns = false, CanUserAddRows = false, ItemsSource = _mosRows };
        foreach (var column in new[] { ("PC 时间", "Time", 155d), ("证据来源", "Source", 140d), ("状态", "State", 200d) })
            grid.Columns.Add(new DataGridTextColumn { Header = column.Item1, Binding = new Binding(column.Item2), Width = column.Item3 });
        grid.Columns.Add(new DataGridTextColumn { Header = "原因 / 证据边界", Binding = new Binding("Detail"), Width = new DataGridLength(1, DataGridLengthUnitType.Star), MinWidth = 260 });
        root.Children.Add(grid);
        MainTabs.Items.Add(new TabItem { Header = "MOS 监控", Content = root });
        var timer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };
        timer.Tick += (_, _) => {
            if (_mosClosed || _mosAuto.IsChecked != true || _mosJournal is null) return;
            if (_bms is null) NoteMosGap("设备断开");
            else if (_otaRunning) NoteMosGap("OTA 期间暂停");
            else if (_mosLastSampleUtc != default && DateTimeOffset.UtcNow - _mosLastSampleUtc > TimeSpan.FromSeconds(5))
                NoteMosGap("电池数据超过 5 秒未刷新（其他操作或通信中断）");
        };
        timer.Start();
        Closed += (_, _) => {
            _mosClosed = true; timer.Stop(); _mosCts?.Cancel();
            TryMosWrite("end", new { reason = "窗口关闭" });
            _mosJournal?.Dispose(); _mosJournal = null;
        };
    }

    private void SetMosIdentity(BmsClient client, DeviceIdentity identity)
    {
        if (!ReferenceEquals(client, _bms)) return;
        if (!ReferenceEquals(client, _mosClient) || _mosIdentity != identity) {
            NoteMosGap("连接或设备身份变化，建立新监控分段");
            TryMosWrite("end", new { reason = "连接或身份变化" });
            _mosJournal?.Dispose(); _mosJournal = null;
            _mosClient = client; _mosIdentity = identity; _mosPrevious = null;
            _mosSequence = _mosBootTick = _mosBuildId = null;
            _mosLastDiagnosticUtc = default;
            _mosDriverInvalid = false;
            Array.Clear(_mosOffTicks);
        }
    }

    private bool EnsureMosJournal()
    {
        if (_mosClosed || _mosAuto.IsChecked != true || _bms is null) return false;
        if (!ReferenceEquals(_bms, _mosClient)) {
            SetMosIdentity(_bms, new DeviceIdentity("", "身份尚未读取", "", "", ""));
        }
        if (_mosJournal is not null) return true;
        try {
            _mosJournal = new BmsMosJournal(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "BmsAssistant", "MosMonitor"));
            _mosDirectory = _mosJournal.DirectoryPath;
            _mosPathText.Text = "自动保存：" + _mosDirectory;
            _mosGap = null;
            return TryMosWrite("start", new { identity = _mosIdentity, endpoint = ConnectionText.Text, protocolLog = _sessionLog.FilePath });
        }
        catch (Exception ex) { MosStorageFailure(ex); return false; }
    }

    private bool TryMosWrite(string kind, object data)
    {
        if (_mosJournal is null) return false;
        try { _mosJournal.Write(kind, data); return true; }
        catch (Exception ex) { MosStorageFailure(ex); return false; }
    }

    private void MosStorageFailure(Exception ex)
    {
        // 明显显示且停记，不能把磁盘失败伪装为仍在监控。
        var journal = _mosJournal; _mosJournal = null;
        try { journal?.Dispose(); } catch { }
        _mosAuto.IsChecked = false;
        _mosStatus.Text = "MOS 记录写盘失败，已停止：" + ex.Message;
        if (!_mosClosed) AppendLog(_mosStatus.Text, "MOS_MONITOR");
    }

    private void AddMosRow(string source, string state, string detail)
    {
        _mosRows.Insert(0, new MosDisplayRow(DateTime.Now.ToString("MM-dd HH:mm:ss.fff"), source, state, detail));
        if (_mosRows.Count > 300) _mosRows.RemoveAt(_mosRows.Count-1);
    }

    private void NoteMosGap(string reason)
    {
        _mosPrevious = null; Array.Clear(_mosOffTicks);
        if (_mosJournal is null || _mosGap == reason) return;
        _mosGap = reason;
        TryMosWrite("gap", new { reason, communication = _communicationStatistics.Snapshot() });
        AddMosRow("证据缺口", "未知", reason);
        _mosStatus.Text = "监控缺口：" + reason + "；不能认定 MOS 关闭";
    }

    private void RecordMosSnapshot(BatterySnapshot snapshot)
    {
        if (!EnsureMosJournal()) return;
        if (_mosGap is not null) { TryMosWrite("resume", new { previousGap = _mosGap }); _mosGap = null; }
        bool edge = _mosPrevious is not null &&
            (_mosPrevious.ChargeMosOn != snapshot.ChargeMosOn || _mosPrevious.DischargeMosOn != snapshot.DischargeMosOn);
        // 先落盘界面实际使用的源值，随后诊断失败也不丢本次边沿。
        if (!TryMosWrite("sample", new { sample = ++_mosSample, snapshot, previous = edge ? _mosPrevious : null,
            indicationChanged = edge, communication = _communicationStatistics.Snapshot() })) return;
        if (edge) AddMosRow("上位机指示", MosHistoryEntry.Bits((snapshot.ChargeMosOn ? 1 : 0) | (snapshot.DischargeMosOn ? 2 : 0)),
            $"SystemStatus 0x{_mosPrevious!.SystemStatus:X8} → 0x{snapshot.SystemStatus:X8}；等待固件证据，真实关断未确认");
        _mosPrevious = snapshot; _mosLastSampleUtc = DateTimeOffset.UtcNow;
    }

    private async Task RefreshMosMonitorAsync(BmsClient client)
    {
        if (_mosClosed || _mosBusy || _mosAuto.IsChecked != true || _mosJournal is null ||
            _otaRunning || !ReferenceEquals(client, _bms)) return;
        _mosBusy = true;
        using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(20));
        _mosCts = cts;
        try {
            var capture = await client.ReadMosHistoryAsync(_mosSequence, _mosBootTick, _mosBuildId, cts.Token);
            if (_mosClosed || !ReferenceEquals(client, _bms) || !ReferenceEquals(client, _mosClient)) return;
            if (capture.Reset) { NoteMosGap("固件重启或序号回退"); _mosPrevious = null; }
            if (capture.Missing != 0) NoteMosGap($"固件历史缺失 {capture.Missing} 条（覆盖或读取中变化）");
            if (!TryMosWrite("diagnostic", new { sample = _mosSample, capture.Header, current = capture.Current?.Words,
                capture.LegacyMosWords, capture.Missing, capture.Reset, capture.Availability })) return;
            _mosDriverInvalid = capture.Current is not null ? !capture.Current.DriverValid :
                capture.LegacyMosWords is { } legacy && legacy[5] == 0;
            foreach (var entry in capture.Entries) {
                bool continuousTime = _mosSequence.HasValue && !capture.Reset && capture.Missing == 0 &&
                    _mosLastDiagnosticUtc != default && DateTimeOffset.UtcNow - _mosLastDiagnosticUtc < TimeSpan.FromHours(36);
                string duration = continuousTime ? MosRecoveryDuration(entry) : "；历史回读或有缺口，不推断关断时长";
                if (!TryMosWrite("firmware", new { sample = _mosSample, entry.Words, entry.Sequence, entry.Tick32k, entry.State, entry.Detail, duration })) return;
                AddMosRow("固件 #" + entry.Sequence, entry.State, entry.Detail + duration);
            }
            if (capture.Enabled) { _mosSequence = capture.LatestSequence; _mosBootTick = capture.BootTick; _mosBuildId = capture.BuildId; }
            _mosLastDiagnosticUtc = DateTimeOffset.UtcNow;
            string currentText = capture.Current is not null ? capture.Current.State + " · " + capture.Current.Reasons :
                capture.LegacyMosWords is { } old ? $"AFE 缓存：{(old[5] != 0 ? MosHistoryEntry.Bits(old[4] & 3) : "未知")} · 充电原因：{BmsDiagnostics.Reasons(BmsDiagnostics.U32(old,8))} · 放电原因：{BmsDiagnostics.Reasons(BmsDiagnostics.U32(old,10))}" : "";
            _mosStatus.Text = "正在自动写盘 · " + capture.Availability + " · " + currentText;
        }
        catch (OperationCanceledException) { if (!_mosClosed) NoteMosGap("MOS 读取取消或超时"); }
        catch (Exception ex) { NoteMosGap("MOS 诊断读取失败：" + ex.Message); AppendLog(ex.Message, "MOS_MONITOR"); }
        finally { _mosCts = null; _mosBusy = false; }
    }

    private string MosRecoveryDuration(MosHistoryEntry entry)
    {
        string detail = "";
        for (int channel = 0; channel < 2; ++channel) {
            int bit = 1 << channel;
            if (!entry.DriverValid || !entry.HasPrevious || entry.Words[29] == 0) { _mosOffTicks[channel] = null; continue; }
            if ((entry.Words[28] & bit) != 0 && (entry.Driver & bit) == 0)
                _mosOffTicks[channel] = (entry.Tick32k, DateTimeOffset.UtcNow);
            if ((entry.Driver & bit) != 0 && _mosOffTicks[channel] is { } start) {
                if (DateTimeOffset.UtcNow - start.Seen < TimeSpan.FromHours(36))
                    detail += $"；{(channel == 0 ? "充电" : "放电")}恢复，固件采样计时差 {unchecked(entry.Tick32k-start.Tick)/32.768:F1} ms（物理关断时长未测）";
                else detail += "；长时间记录跨 tick 周期，关断时长无法确认";
                _mosOffTicks[channel] = null;
            }
        }
        return detail;
    }

    private async Task StopMosReadsForOtaAsync()
    {
        _mosCts?.Cancel(); NoteMosGap("OTA 期间暂停");
        for (int attempt = 0; _mosBusy && attempt < 100; ++attempt) await Task.Delay(50);
        if (_mosBusy) throw new IOException("MOS 监控读取尚未停止，请稍后重试 OTA");
    }
}
