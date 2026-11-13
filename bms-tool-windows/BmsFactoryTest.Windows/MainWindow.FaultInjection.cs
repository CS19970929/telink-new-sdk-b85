using System.Globalization;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;

namespace BmsTool.Windows;

public partial class MainWindow
{
    private C11Connection? _faultSession;
    private BmsClient? _faultClient;
    private readonly SemaphoreSlim _faultGate = new(1, 1);
    private DispatcherTimer? _faultTimer;
    private readonly TextBlock _faultStatus = new() { Text = "连接设备后读取能力。", TextWrapping = TextWrapping.Wrap };
    private readonly TextBlock _faultLive = new() { TextWrapping = TextWrapping.Wrap };
    private readonly TextBox _faultCurrent = new() { Text = "1", Width = 100 };
    private readonly TextBox _faultTemperature = new() { Text = "25", Width = 100 };
    private readonly CheckBox _faultUseCurrent = new() { Content = "注入电流 A（正充、负放）", IsChecked = true };
    private readonly CheckBox _faultUseTemperature = new() { Content = "注入有效温度通道 ℃", IsChecked = true };
    private readonly List<CheckBox> _faultMcuChecks = new();
    private readonly List<Control> _faultControls = new();
    private bool _faultOperationPending;

    private void AddFaultInjectionTab()
    {
        var panel = new StackPanel { Margin = new Thickness(16) };
        panel.Children.Add(new TextBlock { Text = "故障注入与 MOS 联动", FontSize = 22, FontWeight = FontWeights.SemiBold });
        panel.Children.Add(new TextBlock {
            Text = "使用 controlled 固件。温度、电流覆盖保护测试输入；首页电池温度同步显示设备确认的测试值，真实采样保留。清除全部注入后恢复显示真实温度。AFE 通信失效和 MCU 故障关闭充、放电 MOS，通信继续运行。",
            TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 10, 0, 10) });
        var actions = new WrapPanel();
        void Button(Panel parent, string text, Func<Task> action, bool blockWhileApplying = true)
        {
            var button = new Button { Content = text, Margin = new Thickness(4), Padding = new Thickness(12, 6, 12, 6), HorizontalAlignment = HorizontalAlignment.Left };
            button.Click += async (_, _) => {
                try { await action(); }
                catch (Exception ex) { _faultStatus.Text = ex.Message; AppendLog(ex.Message, "INJECTION"); }
            };
            parent.Children.Add(button);
            if (blockWhileApplying) _faultControls.Add(button);
        }
        Button(actions, "读取能力/状态", async () => {
            var client = _bms ?? throw new IOException("请先连接设备。");
            var state = await client.TryReadFaultInjectionAsync(forceProbe: true);
            if (!ReferenceEquals(client, _bms)) return;
            _faultStatus.Text = state is null ? "设备不支持此故障注入接口，请使用配套固件。" :
                $"故障注入能力：{(state.Enabled ? "已开启" : "未开启，请编译 controlled 配置")}；MCU：{(state.McuFaults == 0 ? "无" : state.McuText)}；安全保持：{state.Hold}";
        });
        Button(actions, "开始注入会话", StartFaultInjectionAsync);
        Button(actions, "启用 MOS 联动", () => FaultOperationAsync(async (c, ct) => {
            await EnableFaultMosAsync(c, ct);
            _faultStatus.Text = "设备已确认 MOS 联动。";
        }));
        Button(actions, "清除全部注入", ClearAllFaultInjectionAsync);
        Button(actions, "结束会话", CloseFaultInjectionAsync, false);
        Button(actions, "回到首页查看", () => { MainTabs.SelectedIndex = 0; return Task.CompletedTask; }, false);
        panel.Children.Add(actions);
        panel.Children.Add(_faultStatus);
        var inputs = new WrapPanel { Margin = new Thickness(0, 16, 0, 0) };
        inputs.Children.Add(_faultUseCurrent); inputs.Children.Add(_faultCurrent);
        inputs.Children.Add(_faultUseTemperature); inputs.Children.Add(_faultTemperature);
        _faultControls.AddRange(new Control[] { _faultUseCurrent, _faultCurrent, _faultUseTemperature, _faultTemperature });
        Button(inputs, "同时应用温度/电流", ApplyFaultInputsAsync);
        panel.Children.Add(inputs);
        var presets = new WrapPanel();
        var cases = new[] { ("充电过流", "5.1", "25"), ("放电过流", "-40.1", "25"),
            ("充电过温", "1", "56"), ("充电低温", "1", "-8"),
            ("放电过温", "-1", "76"), ("放电低温", "-1", "-21"), ("正常输入", "1", "25") };
        foreach (var entry in cases)
            Button(presets, entry.Item1, async () => {
                _faultUseCurrent.IsChecked = _faultUseTemperature.IsChecked = true;
                _faultCurrent.Text = entry.Item2; _faultTemperature.Text = entry.Item3;
                await ApplyFaultInputsAsync();
            });
        Button(presets, "AFE通信失效", () => FaultOperationAsync(async (c, ct) => {
            if ((await c.StatusAsync(ct)).Mode != 1) throw new IOException("请先启用 MOS 联动。");
            var ack = await c.Command(0x22, new byte[] { 1 }, ct);
            var state = await _faultClient!.TryReadFaultInjectionAsync(ct);
            if (ack.Length != 1 || ack[0] != 1 || state?.AfeCommunicationFault != true)
                throw new IOException("AFE 通信失效故障结果未知，请读取状态确认。");
            _faultStatus.Text = "设备已确认 AFE 通信失效保护，充、放电 MOS 禁止；请回首页查看。";
        }));
        panel.Children.Add(presets);
        var diagnostics = new WrapPanel();
        Button(diagnostics, "电流采样电阻漂移失效", () => InjectSafetyFaultAsync(1));
        Button(diagnostics, "一级关断电路失效", () => InjectSafetyFaultAsync(2));
        panel.Children.Add(diagnostics);
        panel.Children.Add(new TextBlock {
            Text = "独立 MOS 控制：开启表示解除本项关闭请求，其他保护仍可关闭 MOS；同口反向导通仍由真实电流判定。",
            TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 12, 0, 4) });
        var mos = new WrapPanel();
        Button(mos, "关闭充电 MOS", () => SetFaultMosAsync(true, false));
        Button(mos, "开启充电 MOS", () => SetFaultMosAsync(true, true));
        Button(mos, "关闭放电 MOS", () => SetFaultMosAsync(false, false));
        Button(mos, "开启放电 MOS", () => SetFaultMosAsync(false, true));
        panel.Children.Add(mos);
        panel.Children.Add(new TextBlock { Text = "过流触发延时 >1秒；温度触发延时 >2秒。点击“清除全部注入”即可解除测试故障，继续下一项。", Margin = new Thickness(4) });
        panel.Children.Add(new TextBlock { Text = "MCU 故障（可多选，仅注入故障判定，不破坏实际寄存器或停止 CPU）", Margin = new Thickness(0, 16, 0, 8) });
        var mcu = new WrapPanel();
        foreach (string name in BmsFaultInjectionStatus.McuNames)
        {
            var check = new CheckBox { Content = name, Margin = new Thickness(6) };
            _faultMcuChecks.Add(check); mcu.Children.Add(check);
            _faultControls.Add(check);
        }
        panel.Children.Add(mcu);
        Button(panel, "应用所选 MCU 故障", () => FaultOperationAsync(async (c, ct) => {
            if ((await c.StatusAsync(ct)).Mode != 1) throw new IOException("请先启用 MOS 联动。");
            byte mask = 0;
            for (int i = 0; i < _faultMcuChecks.Count; i++)
                if (_faultMcuChecks[i].IsChecked == true) mask |= (byte)(1 << i);
            if (mask == 0) throw new IOException("请至少选择一个 MCU 故障；解除请使用“清除全部注入”。");
            await c.Command(0x21, new[] { mask }, ct);
            var state = await _faultClient!.TryReadFaultInjectionAsync(ct);
            if (state is null || (state.McuFaults & mask) != mask)
                throw new IOException("MCU 故障注入结果未知，请停止操作并确认清理。");
            _faultStatus.Text = "设备已确认 MCU 故障：" + state.McuText + "；请回首页查看三级保护及 MOS。";
        }));
        panel.Children.Add(_faultLive);
        MainTabs.Items.Add(new TabItem { Header = "故障注入", Content = new ScrollViewer {
            Content = panel, VerticalScrollBarVisibility = ScrollBarVisibility.Auto } });
        _faultTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(500) };
        _faultTimer.Tick += async (_, _) => await PollFaultInjectionAsync();
    }

    private async Task StartFaultInjectionAsync()
    {
        if (_faultSession is not null || _c11Session is not null || _shBusy || _parameterBusy || _otaRunning)
            throw new IOException("请先结束其他测试或参数操作。");
        var client = _bms ?? throw new IOException("请先连接设备。");
        if (!await _faultGate.WaitAsync(0)) throw new IOException("正在读取注入状态，请稍后操作。");
        _shBusy = true; _pollTimer.Stop();
        var session = new C11Connection(client);
        try
        {
            var capability = await client.TryReadFaultInjectionAsync(forceProbe: true);
            if (capability?.Enabled != true) throw new IOException("请使用配套 controlled 固件。");
            await WaitForCommunicationIdleAsync();
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(8));
            await session.OpenAsync(timeout.Token);
            if (!ReferenceEquals(client, _bms)) throw new IOException("设备连接已改变。");
            _faultSession = session; _faultClient = client;
            _faultUseTemperature.Content = $"注入 {session.Capabilities.Temperatures} 路温度 ℃";
            _faultTimer!.Start();
            if (capability.Hold)
                _faultStatus.Text = "会话已建立，设备保留测试保持。点击“清除全部注入”即可解除。";
            else
            {
                await EnableFaultMosAsync(session, timeout.Token);
                _faultStatus.Text = "注入会话与 MOS 联动已开启，尚未注入故障；切换首页仍持续续租和刷新。";
            }
        }
        catch
        {
            if (_faultSession is null)
            {
                try { await session.DisposeAsync(); }
                finally { _shBusy = false; StartAutomaticRefresh(); }
            }
            throw;
        }
        finally { _faultGate.Release(); }
    }

    private static string FaultDiagnosticsText(ushort mask) => mask == 0 ? "无" :
        string.Join("、", Enumerable.Range(0, 7).Where(i => (mask & (1 << i)) != 0)
            .Select(i => C11ValidationRunner.Names[8 + i]));

    private static async Task EnableFaultMosAsync(C11Connection session, CancellationToken ct)
    {
        var status = await session.StatusAsync(ct);
        if (status.RealFaults != 0 || status.RealDiagnostics != 0 || status.Hold || status.WriteFailed)
            throw new IOException($"MOS 联动未启用：真实保护=0x{status.RealFaults:X4}；" +
                $"真实诊断={FaultDiagnosticsText(status.RealDiagnostics)}；安全保持={status.Hold}；AFE 写入失败={status.WriteFailed}。" +
                "请处理真实故障；测试保持可点击“清除全部注入”解除。");
        await session.Command(0x14, new byte[] { 1 }, ct);
        if ((await session.StatusAsync(ct)).Mode != 1) throw new IOException("设备未确认 MOS 联动。");
    }

    private async Task ClearAllFaultInjectionAsync()
    {
        await FaultOperationAsync(async (c, ct) => {
            await c.Command(0x23, Array.Empty<byte>(), ct);
            var status = await c.StatusAsync(ct);
            var state = await _faultClient!.TryReadFaultInjectionAsync(ct);
            if (status.InjectionActive || state is null || state.HasInjectedFault ||
                (state.Hold && !state.WriteFailed))
                throw new IOException("设备尚未确认全部注入解除，请更新配套固件并读取状态确认。");
            foreach (var check in _faultMcuChecks) check.IsChecked = false;
            _faultUseCurrent.IsChecked = _faultUseTemperature.IsChecked = false;
            if (status.Mode == 0 && !state.Hold)
            {
                try { await EnableFaultMosAsync(c, ct); }
                catch (IOException ex)
                {
                    _faultStatus.Text = "全部注入已清除；MOS 联动未启用：" + ex.Message;
                    return;
                }
            }
            _faultStatus.Text = state.WriteFailed
                ? "全部注入已清除；真实 AFE 驱动写入故障仍保持关断。"
                : "设备已确认全部注入、独立 MOS 关闭请求和测试保持解除；可继续下一项，真实保护仍有效。";
        });
        await PollFaultInjectionAsync();
    }

    private async Task InjectSafetyFaultAsync(byte mask)
    {
        await FaultOperationAsync(async (c, ct) => {
            if ((await c.StatusAsync(ct)).Mode != 1) throw new IOException("请先启用 MOS 联动。");
            var ack = await c.Command(0x25, new[] { mask }, ct);
            var state = await _faultClient!.TryReadFaultInjectionAsync(ct);
            if (ack.Length != 1 || ack[0] > 3 || (ack[0] & mask) != mask || state is null ||
                ((mask & 1) != 0 && !state.CurrentShuntDriftFault) ||
                ((mask & 2) != 0 && !state.PrimaryCutoffFault))
                throw new IOException("安全故障注入结果未知，请读取状态并清除全部注入后重试。");
            _faultStatus.Text = (mask == 1 ? "设备已确认电流采样电阻漂移失效。" : "设备已确认一级关断电路失效。") +
                "请回首页查看双路 MOS 及设备上报的关断状态。";
        });
        await PollFaultInjectionAsync();
    }

    private async Task SetFaultMosAsync(bool charge, bool allowed)
    {
        byte channel = charge ? (byte)1 : (byte)2;
        await FaultOperationAsync(async (c, ct) => {
            if ((await c.StatusAsync(ct)).Mode != 1) throw new IOException("请先启用 MOS 联动。");
            var ack = await c.Command(0x24, new[] { channel, allowed ? (byte)1 : (byte)0 }, ct);
            var readback = await c.Command(0x24, Array.Empty<byte>(), ct);
            if (ack.Length != 1 || ack[0] > 3 || readback.Length != 1 || ack[0] != readback[0] ||
                ((readback[0] & channel) == 0) != allowed)
                throw new IOException("独立 MOS 请求结果未知，请读取状态并清除全部注入后重试。");
            var state = await c.StatusAsync(ct);
            string driver = state.ReadbackValid
                ? $"充电{OnOff((state.Readback & 1) != 0)}、放电{OnOff((state.Readback & 2) != 0)}"
                : "未知";
            _faultStatus.Text = $"{(charge ? "充电" : "放电")} MOS {(allowed ? "开启" : "关闭")}请求已确认；AFE 回读：{driver}。其他保护仍参与仲裁。";
        });
        await PollFaultInjectionAsync();
    }

    private async Task FaultOperationAsync(Func<C11Connection, CancellationToken, Task> action)
    {
        var expectedSession = _faultSession ?? throw new IOException("请先开始注入会话。");
        if (_faultOperationPending) throw new IOException("上一项操作尚未完成，请等待结果。");
        _faultOperationPending = true;
        foreach (var control in _faultControls) control.IsEnabled = false;
        _faultStatus.Text = "正在等待当前读取结束并提交操作…";
        bool acquired = false;
        try
        {
            using var waiting = new CancellationTokenSource(TimeSpan.FromSeconds(8));
            await _faultGate.WaitAsync(waiting.Token);
            acquired = true;
            var session = _faultSession ?? throw new IOException("请先开始注入会话。");
            if (!ReferenceEquals(session, expectedSession)) throw new IOException("注入会话已改变，已取消排队操作。");
            if (_faultTimer?.IsEnabled != true) throw new IOException("会话状态未知，请先结束会话并重新连接。");
            if (!ReferenceEquals(_faultClient, _bms)) throw new IOException("连接已改变，禁止沿用注入会话。");
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(12));
            await session.KeepAliveAsync(timeout.Token);
            await action(session, timeout.Token);
        }
        catch (OperationCanceledException ex)
        {
            throw new IOException(acquired
                ? "本次操作超时，提交结果未知；请读取状态并清除注入后再继续。"
                : "等待当前读取超时，本次操作尚未执行。", ex);
        }
        finally
        {
            if (acquired) _faultGate.Release();
            _faultOperationPending = false;
            foreach (var control in _faultControls) control.IsEnabled = true;
        }
    }

    private Task ApplyFaultInputsAsync()
    {
        var expectedSession = _faultSession ?? throw new IOException("请先开始注入会话。");
        // Capture the click's values before waiting for background reading.
        var fields = new List<(byte Kind, byte Index, int Value)>();
        if (_faultUseCurrent.IsChecked == true)
        {
            decimal amps = decimal.Parse(_faultCurrent.Text, CultureInfo.InvariantCulture);
            if (amps < -200 || amps > 200) throw new IOException("电流范围为 -200～200 A。");
            fields.Add((3, 0, checked((int)(amps * 1000))));
        }
        if (_faultUseTemperature.IsChecked == true)
        {
            decimal degrees = decimal.Parse(_faultTemperature.Text, CultureInfo.InvariantCulture);
            if (degrees < -40 || degrees > 125) throw new IOException("温度范围为 -40～125 ℃。");
            for (byte i = 0; i < expectedSession.Capabilities.Temperatures; i++)
                fields.Add((4, i, checked((int)(degrees * 10))));
        }
        if (fields.Count == 0) throw new IOException("请至少启用一种输入。");
        return FaultOperationAsync(async (c, ct) => {
            if (!ReferenceEquals(c, expectedSession)) throw new IOException("注入会话已改变，本次输入已取消。");
            if ((await c.StatusAsync(ct)).Mode != 1) throw new IOException("请先启用 MOS 联动。");
            await c.ApplyAsync(fields, ct);
            _faultStatus.Text = "设备已确认原子应用温度/电流；保护延时由 MCU 计时，首页显示设备上报结果。";
        });
    }

    private async Task PollFaultInjectionAsync()
    {
        if (_faultOperationPending || _faultSession is null || !await _faultGate.WaitAsync(0)) return;
        try
        {
            var session = _faultSession;
            var client = _faultClient!;
            if (!ReferenceEquals(client, _bms)) throw new IOException("连接已改变。");
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(8));
            var status = await session.KeepAliveAsync(timeout.Token);
            if (_faultOperationPending) return;
            var snapshot = await client.ReadBatteryAsync(timeout.Token);
            if (!ReferenceEquals(client, _bms)) throw new IOException("连接已改变，当前数据作废。");
            status = await session.KeepAliveAsync(timeout.Token);
            if (_faultOperationPending) return;
            var test = await session.SampleAsync(true, timeout.Token);
            var confirmed = await session.StatusAsync(timeout.Token);
            if (_faultOperationPending) return;
            if (!ReferenceEquals(client, _bms)) throw new IOException("连接已改变，当前数据作废。");
            // Only project the device-confirmed effective sample onto the display.
            // BatterySnapshot, recordings, MOS temperature and real safety inputs stay real.
            bool injectedTemperature = confirmed.Mode == 1 && confirmed.InjectionActive &&
                (confirmed.InjectionMask & 8u) != 0;
            double maxTempC = snapshot.MaxTempC, minTempC = snapshot.MinTempC;
            if (injectedTemperature)
            {
                if (!status.InjectionActive || status.AppliedSequence != confirmed.AppliedSequence ||
                    test.Sequence != confirmed.AppliedSequence || (test.Valid & 2u) == 0 ||
                    test.Temperatures is < 1 or > 3 ||
                    test.Temperatures != session.Capabilities.Temperatures)
                    throw new IOException("温度注入回读无效或序号已改变，当前温度未知。");
                var temperatures = test.TemperatureDecic.Take(test.Temperatures).ToArray();
                if (temperatures.Any(t => t is < -400 or > 1250))
                    throw new IOException("温度注入回读超出范围，当前温度未知。");
                maxTempC = temperatures.Max() / 10.0;
                minTempC = temperatures.Min() / 10.0;
            }
            ApplyBatterySnapshot(snapshot);
            TempsText.Text = $"最高温度：{maxTempC:F1} °C\n最低温度：{minTempC:F1} °C\nMOS 温度：{snapshot.MosTempC:F1} °C";
            FactoryCellTempText.Text = $"单体最高/最低：{maxTempC:F1} / {minTempC:F1} ℃";
            TempsText.ToolTip = FactoryCellTempText.ToolTip = injectedTemperature
                ? $"设备确认的温度测试输入；真实采样最高/最低：{snapshot.MaxTempC:F1} / {snapshot.MinTempC:F1} ℃"
                : null;
            _faultLive.Text = $"测试电流：{test.CurrentMa / 1000.0:F3} A；测试温度：{string.Join(" / ", test.TemperatureDecic.Take(test.Temperatures).Select(t => $"{t/10.0:F1}℃"))}\n" +
                $"设备三级保护：{snapshot.ProtectionLevel3Text}\n真实诊断：{FaultDiagnosticsText(status.RealDiagnostics)}；测试诊断：{FaultDiagnosticsText(status.TestDiagnostics)}；安全保持：{status.Hold}；MOS 联动：{status.Mode == 1}\n" +
                $"最终 MOS 许可：{status.Final}；AFE 状态：{status.Readback}；回读有效：{status.ReadbackValid}；物理导通需外部测量。";
        }
        catch (Exception ex)
        {
            _faultTimer?.Stop();
            _faultStatus.Text = "通信或会话状态未知，已停止续租；设备超时后保持关断。请结束会话并重新连接。" + ex.Message;
            _customerLastDataUtc = DateTime.MinValue;
            MosStateText.Text = "充电 MOS：未知\n放电 MOS：未知";
            ProtectionLevelsText.Text = "一级：未知\n二级：未知\n三级：通信中断，故障状态未知";
            TempsText.Text = "温度状态未知，等待设备回读确认";
            FactoryCellTempText.Text = "单体温度：未知";
            TempsText.ToolTip = FactoryCellTempText.ToolTip = null;
            AppendLog(_faultStatus.Text, "INJECTION");
        }
        finally { _faultGate.Release(); }
    }

    private async Task CloseFaultInjectionAsync()
    {
        var closing = _faultSession;
        if (closing is null) return;
        _faultTimer?.Stop();
        await _faultGate.WaitAsync();
        try
        {
            if (!ReferenceEquals(closing, _faultSession)) return;
            await closing.DisposeAsync();
            _faultStatus.Text = "会话已结束；设备保留安全保持及故障原因，不自动恢复输出。";
        }
        finally
        {
            if (ReferenceEquals(closing, _faultSession))
            {
                _faultSession = null; _faultClient = null;
                _shBusy = false;
            }
            _faultGate.Release();
            StartAutomaticRefresh();
        }
    }
}
