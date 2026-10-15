using System.Globalization;
using System.IO;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
using Microsoft.Win32;

namespace BmsTool.Windows;

public partial class MainWindow
{
    private readonly TextBlock _currentCalibrationStatus = new()
    { Text = "校准需配套固件和外部电流表。先采零点，再采一个稳定电流点。", TextWrapping = TextWrapping.Wrap, Margin = new Thickness(4) };
    private readonly TextBox _currentCalibrationReference = new() { Width = 160, Margin = new Thickness(4) };
    private readonly ComboBox _currentCalibrationDirection = new() { Width = 100, Margin = new Thickness(4) };
    private readonly Button _currentCalibrationZero = new() { Content = "① 采集零点" };
    private readonly Button _currentCalibrationLoad = new() { Content = "② 采集电流点" };
    private readonly Button _currentCalibrationSave = new() { Content = "③ 保存校准" };
    private readonly Button _currentCalibrationCancel = new() { Content = "取消" };
    private readonly Button _currentCalibrationRestore = new() { Content = "恢复本次校准前参数" };
    private CurrentCalibrationSession? _currentCalibrationSession, _currentCalibrationRecord;
    private CancellationTokenSource? _currentCalibrationCts;
    private BmsClient? _currentCalibrationOwner;
    private bool _currentCalibrationBusy, _currentCalibrationFinishing, _currentCalibrationWindowClosed;
    private DateTime _currentCalibrationHeartbeatUtc;

    private void AddCurrentCalibrationControls(StackPanel root)
    {
        root.Children.Add(new TextBlock { Text = "电流两点校准", FontWeight = FontWeights.Bold, Margin = new Thickness(4,12,4,4) });
        root.Children.Add(new TextBlock { Text = "零点：断开充电器和负载，确认电流表为零。加载点：在产品允许范围内接稳定电流，输入电流表实测值，不能输入设备设定值。采集期间不要重启。", TextWrapping = TextWrapping.Wrap, Margin = new Thickness(4) });
        root.Children.Add(_currentCalibrationStatus);
        _currentCalibrationDirection.Items.Add("放电"); _currentCalibrationDirection.Items.Add("充电");
        _currentCalibrationDirection.SelectedIndex = 0;
        var input = new StackPanel { Orientation = Orientation.Horizontal };
        input.Children.Add(_currentCalibrationDirection);
        input.Children.Add(new TextBlock { Text = "电流表实测 / A", VerticalAlignment = VerticalAlignment.Center });
        input.Children.Add(_currentCalibrationReference); root.Children.Add(input);
        var buttons = new WrapPanel();
        foreach (var button in new[] { _currentCalibrationZero, _currentCalibrationLoad, _currentCalibrationSave,
            _currentCalibrationCancel, _currentCalibrationRestore })
        { button.Margin = new Thickness(4); button.Padding = new Thickness(8,4,8,4); buttons.Children.Add(button); }
        root.Children.Add(buttons);
        var export = new Button { Content = "导出校准记录", HorizontalAlignment = HorizontalAlignment.Left, Margin = new Thickness(4) };
        export.Click += (_,_) => ExportCurrentCalibration(); root.Children.Add(export);
        _currentCalibrationZero.Click += async (_,_) => await StartCurrentCalibrationAsync();
        _currentCalibrationLoad.Click += async (_,_) => await CaptureLoadedCurrentAsync();
        _currentCalibrationSave.Click += async (_,_) => await CommitCurrentCalibrationAsync();
        _currentCalibrationRestore.Click += async (_,_) => await RestoreCurrentCalibrationAsync();
        _currentCalibrationCancel.Click += async (_,_) => {
            _currentCalibrationCts?.Cancel();
            if (!_currentCalibrationBusy) { _currentCalibrationStatus.Text = "校准已取消，未保存的候选作废。"; await FinishCurrentCalibrationAsync(); }
        };
        void Changed()
        {
            if (_currentCalibrationSession is not { } session || _currentCalibrationBusy) return;
            session.Loaded = null; session.Candidate = null;
            _currentCalibrationStatus.Text = "参考值或方向已改变，请重新采集电流点。";
            UpdateCurrentCalibrationControls();
        }
        _currentCalibrationReference.TextChanged += (_,_) => Changed();
        _currentCalibrationDirection.SelectionChanged += (_,_) => Changed();
        var timer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };
        timer.Tick += async (_,_) => {
            if (_currentCalibrationFinishing) return;
            if (_currentCalibrationOwner is not null && (!ReferenceEquals(_bms, _currentCalibrationOwner) || _autoReconnectRunning || _currentCalibrationWindowClosed))
            {
                _currentCalibrationCts?.Cancel();
                if (!_currentCalibrationBusy) { _currentCalibrationStatus.Text = "连接已改变，候选作废，请重新采集。"; await FinishCurrentCalibrationAsync(); }
            }
            else if (_currentCalibrationSession is { } session && !_currentCalibrationBusy)
            {
                if (_currentCalibrationCts?.IsCancellationRequested == true)
                { _currentCalibrationStatus.Text = "校准已超时，请重新开始。"; await FinishCurrentCalibrationAsync(); }
                else if (DateTime.UtcNow >= _currentCalibrationHeartbeatUtc)
                    await RunCurrentCalibrationStepAsync(async ct => await session.Client.CheckCurrentCalibrationAsync(session, ct));
            }
            UpdateCurrentCalibrationControls();
        };
        timer.Start();
        Closed += async (_,_) => { _currentCalibrationWindowClosed = true; timer.Stop(); _currentCalibrationCts?.Cancel(); if (!_currentCalibrationBusy) await FinishCurrentCalibrationAsync(); };
        UpdateCurrentCalibrationControls();
    }

    private bool CurrentCalibrationAllowed()
    {
#if !BMS_FACTORY_APP
        if (!_protectedFeaturesUnlocked) { _currentCalibrationStatus.Text = "请先解锁高级功能，再执行电流校准。"; return false; }
#endif
        return true;
    }
    private async Task<bool> AcquireCurrentCalibrationAsync()
    {
        if (!CurrentCalibrationAllowed()) return false;
        var client = _bms;
        if (client is null || _currentCalibrationWindowClosed || _parameterBusy || _diagBusy || _eventLogReadInProgress ||
            _otaRunning || _shBusy || ShFactoryBusy() || _autoReconnectRunning || _mosControlBusy)
        { _currentCalibrationStatus.Text = "请连接设备并等待其他操作结束。"; return false; }
        _currentCalibrationOwner = client;
        _currentCalibrationCts = new CancellationTokenSource(TimeSpan.FromMinutes(10));
        _parameterBusy = true; _diagBusy = true; _eventLogReadInProgress = true;
        _pollTimer.Stop(); UpdateBusinessParameterControls(); ApplyMosControlState(_mosControlState);
        try { await WaitForCommunicationIdleAsync(); }
        catch (Exception ex) { _currentCalibrationStatus.Text = "无法开始校准：" + ex.Message; await FinishCurrentCalibrationAsync(); return false; }
        if (!ReferenceEquals(client, _bms)) { await FinishCurrentCalibrationAsync(); return false; }
        return true;
    }
    private async Task StartCurrentCalibrationAsync()
    {
        if (!await AcquireCurrentCalibrationAsync()) return;
        await RunCurrentCalibrationStepAsync(async ct => {
            var client = _currentCalibrationOwner ?? throw new IOException("连接已改变。");
            var session = await client.BeginCurrentCalibrationAsync(ct);
            _currentCalibrationSession = _currentCalibrationRecord = session;
            session.Zero = await client.CollectCurrentCalibrationAsync(session, true, CurrentCalibrationPolicy.Default,
                (done,total) => _currentCalibrationStatus.Text = $"采集零点 {done}/{total}…", ct);
            _currentCalibrationStatus.Text = $"零点已采集。选择充电或放电，输入电流表实测值，再采集电流点。SN：{session.Identity.Serial}";
        });
    }
    private int CurrentCalibrationReferenceMa()
    {
        if (!decimal.TryParse(_currentCalibrationReference.Text,
            NumberStyles.AllowDecimalPoint | NumberStyles.AllowLeadingWhite | NumberStyles.AllowTrailingWhite,
            CultureInfo.CurrentCulture, out decimal amperes) || amperes < CurrentCalibrationPolicy.Default.MinLoadedMa / 1000m || amperes > 1000m ||
            amperes * 1000m != decimal.Truncate(amperes * 1000m))
            throw new ArgumentException("输入正的电流表实测值，至少1 A，最多三位小数；不得超过产品允许电流。");
        int ma = checked((int)(amperes * 1000m));
        return _currentCalibrationDirection.SelectedIndex == 1 ? -ma : ma;
    }
    private async Task CaptureLoadedCurrentAsync()
    {
        var session = _currentCalibrationSession;
        if (session?.Zero is null || _currentCalibrationBusy) return;
        int reference;
        try { reference = CurrentCalibrationReferenceMa(); }
        catch (ArgumentException ex) { _currentCalibrationStatus.Text = ex.Message; return; }
        session.Loaded = null; session.Candidate = null;
        await RunCurrentCalibrationStepAsync(async ct => {
            session.ReferenceMa = reference;
            session.Loaded = await session.Client.CollectCurrentCalibrationAsync(session, false, CurrentCalibrationPolicy.Default,
                (done,total) => _currentCalibrationStatus.Text = $"采集电流点 {done}/{total}…", ct);
            session.Candidate = CurrentCalibrationPolicy.Default.Calculate(session.Zero, session.Loaded, reference);
            double before = session.Original.Apply(session.Loaded.MeanMa) / 1000;
            _currentCalibrationStatus.Text = $"候选已生成，尚未保存。外部参考 {Math.Abs(reference)/1000d:F3} A；按旧参数约 {Math.Abs(before):F3} A；按候选约 {Math.Abs(session.Candidate.Apply(session.Loaded.MeanMa))/1000:F3} A。点击保存。";
#if BMS_FACTORY_APP
            _currentCalibrationStatus.Text += $"\n原始零点 {session.Zero.MeanMa:F3} mA；加载 {session.Loaded.MeanMa:F3} mA，范围 {session.Loaded.MinMa}～{session.Loaded.MaxMa}；offset={session.Candidate.OffsetMa} mA，gain={session.Candidate.GainPpm} ppm。";
#endif
        });
    }
    private async Task CommitCurrentCalibrationAsync()
    {
        var session = _currentCalibrationSession;
        if (session?.Candidate is null || _currentCalibrationBusy || !CurrentCalibrationAllowed()) return;
        await RunCurrentCalibrationStepAsync(async ct => {
            if (CurrentCalibrationReferenceMa() != session.ReferenceMa) throw new IOException("参考值已改变，请重新采集。");
            session.Outcome = "开始保存；若中断须重新读取确认，不能盲目重写";
            int final = await session.Client.SaveCurrentCalibrationAsync(session, session.Candidate, ct);
            session.Outcome = $"参数保存、读回及新样本公式核对完成；当前电流 {final} mA。多点、温升和重新上电精度尚待验证。";
            _currentCalibrationStatus.Text = $"已保存并读回，当前电流 {Math.Abs(final)/1000d:F3} A。请另行检查小电流、相反方向及重新上电后的结果。";
            await FinishCurrentCalibrationAsync();
        });
    }
    private async Task RestoreCurrentCalibrationAsync()
    {
        var previous = _currentCalibrationRecord;
        if (previous?.Candidate is null || !previous.WriteAttempted || !ReferenceEquals(previous.Client, _bms)) return;
        if (!await AcquireCurrentCalibrationAsync()) return;
        await RunCurrentCalibrationStepAsync(async ct => {
            var session = await previous.Client.BeginCurrentCalibrationAsync(ct);
            _currentCalibrationSession = session;
            if (session.Identity != previous.Identity ||
                (session.Original != previous.Candidate && session.Original != previous.Original))
                throw new IOException("设备身份或校准参数已改变，未恢复旧值；请重新读取核对。");
            if (session.Original != previous.Original)
                await session.Client.SaveCurrentCalibrationAsync(session, previous.Original, ct);
            previous.Outcome += "\n已恢复并读回本次校准前参数。";
            previous.WriteAttempted = false;
            _currentCalibrationStatus.Text = "已恢复本次校准前参数。";
            await FinishCurrentCalibrationAsync();
        });
    }
    private async Task RunCurrentCalibrationStepAsync(Func<CancellationToken,Task> action)
    {
        if (_currentCalibrationBusy || _currentCalibrationCts is null) return;
        _currentCalibrationBusy = true; UpdateCurrentCalibrationControls();
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(_currentCalibrationCts.Token);
        timeout.CancelAfter(TimeSpan.FromSeconds(45));
        try
        {
            await action(timeout.Token);
            if (_currentCalibrationOwner is not null && !ReferenceEquals(_currentCalibrationOwner, _bms))
                throw new IOException("连接已改变，候选作废。");
            _currentCalibrationHeartbeatUtc = DateTime.UtcNow.AddSeconds(15);
        }
        catch (Exception ex)
        {
            string result = "本次未完成：" + ex.Message;
            if (_currentCalibrationSession?.WriteAttempted == true) result += "；可能已写入，请重新读取确认。";
            if (_currentCalibrationRecord is { } record) record.Outcome += "\n" + result;
            _currentCalibrationStatus.Text = result;
            await FinishCurrentCalibrationAsync();
        }
        finally { _currentCalibrationBusy = false; UpdateCurrentCalibrationControls(); }
    }
    private async Task FinishCurrentCalibrationAsync()
    {
        if (_currentCalibrationFinishing) return;
        _currentCalibrationFinishing = true;
        var session = _currentCalibrationSession; _currentCalibrationSession = null;
        _currentCalibrationCts?.Cancel();
        try { if (session is not null) await session.Client.EndCurrentCalibrationAsync(session); }
        finally
        {
            _currentCalibrationCts?.Dispose(); _currentCalibrationCts = null; _currentCalibrationOwner = null;
            _parameterBusy = false; _diagBusy = false; _eventLogReadInProgress = false;
            _currentCalibrationFinishing = false;
            UpdateBusinessParameterControls(); ApplyMosControlState(_mosControlState);
            if (!_currentCalibrationWindowClosed) StartAutomaticRefresh();
        }
    }
    private void UpdateCurrentCalibrationControls()
    {
        bool active = _currentCalibrationOwner is not null, ready = !_currentCalibrationBusy && !_currentCalibrationFinishing && !_currentCalibrationWindowClosed;
        _currentCalibrationZero.IsEnabled = ready && !active && _bms is not null && !_parameterBusy && !_diagBusy;
        _currentCalibrationLoad.IsEnabled = ready && _currentCalibrationSession?.Zero is not null;
        _currentCalibrationSave.IsEnabled = ready && _currentCalibrationSession?.Candidate is not null;
        _currentCalibrationCancel.IsEnabled = active;
        _currentCalibrationReference.IsEnabled = ready && active;
        _currentCalibrationDirection.IsEnabled = ready && active;
        _currentCalibrationRestore.IsEnabled = ready && !active && _currentCalibrationRecord?.WriteAttempted == true &&
            ReferenceEquals(_currentCalibrationRecord.Client, _bms);
    }
    private void ExportCurrentCalibration()
    {
        if (_currentCalibrationRecord is not { } record) { _currentCalibrationStatus.Text = "尚无校准记录。"; return; }
        var dialog = new SaveFileDialog { Filter = "校准记录 (*.txt)|*.txt", FileName = $"current_calibration_{DateTime.Now:yyyyMMdd_HHmmss}.txt" };
        if (dialog.ShowDialog(this) != true) return;
        try { File.WriteAllText(dialog.FileName, record.Report, new UTF8Encoding(false)); }
        catch (Exception ex) { ShowError("校准记录导出失败", ex); }
    }
}
