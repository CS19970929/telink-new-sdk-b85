using System.Collections.ObjectModel;
using System.Globalization;
using System.IO;
using System.Text.Json;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Shapes;
using System.Windows.Threading;

namespace BmsTool.Windows;

public partial class MainWindow
{
    private readonly ObservableCollection<C11CaseResult> _c11Results = new();
    private readonly List<(double Raw, double Test)> _c11Points = new();
    private C11Connection? _c11Session;
    private BmsClient? _c11BoundClient;
    private CancellationTokenSource? _c11Cts;
    private DispatcherTimer? _c11Timer;
    private bool _c11Polling, _c11Running;
    private readonly SemaphoreSlim _c11ManualGate = new(1, 1);
    private TaskCompletionSource? _c11RunFinished;
    private readonly TextBlock _c11State =
        new() { Text = "尚未探测 C11 能力。", TextWrapping = TextWrapping.Wrap,
                Margin = new Thickness(6) };
    private readonly TextBlock _c11Input =
        new() { Text = "真实采样 / 测试输入：未读取", TextWrapping = TextWrapping.Wrap,
                Margin = new Thickness(6) };
    private readonly TextBlock _c11SelfTest =
        new() { Text = "MCU 自检：未读取", Margin = new Thickness(6) };
    private readonly TextBox _c11Log =
        new() { IsReadOnly = true, AcceptsReturn = true,
                VerticalScrollBarVisibility = ScrollBarVisibility.Auto, MinHeight = 120 };
    private readonly Canvas _c11Plot =
        new() { Height = 150, Background = Brushes.White, ClipToBounds = true };
    private readonly ComboBox _c11Case = new() { MinWidth = 220, SelectedIndex = 0 };
    private readonly ComboBox _c11Kind = new() { MinWidth = 180, SelectedIndex = 0 };
    private readonly TextBox _c11Channel = new() { Text = "1", Width = 45 };
    private readonly TextBox _c11Value = new() { Text = "4260", Width = 90 };
    private readonly CheckBox _c11Output =
        new() { Content = "受控输出（需要专用固件和限流台架）", IsEnabled = false,
                VerticalAlignment = VerticalAlignment.Center };
    private readonly ComboBox _c11Rule = new() { MinWidth = 140, SelectedIndex = 0 };
    private readonly ComboBox _c11Parameter = new() { MinWidth = 120, SelectedIndex = 0 };
    private readonly TextBox _c11ParameterValue = new() { Text = "4250", Width = 90 };
    private readonly CheckBox _c11Fail =
        new() { Content = "注入比较/输入失败", VerticalAlignment = VerticalAlignment.Center };
    private readonly CheckBox _c11Fatal =
        new() { Content = "实际 fail-safe（专用输出固件；执行后停止业务，需外部复位）",
                VerticalAlignment = VerticalAlignment.Center };

    private void AddC11ValidationTab()
    {
        var panel = new StackPanel { Margin = new Thickness(10) };
        panel.Children.Add(new TextBlock { Text = "C11 软件验证 · SW-VD-001～022", FontSize = 22,
                                           FontWeight = FontWeights.SemiBold });
        panel.Children.Add(
            new TextBlock { Text = "默认影子测试。软件判据、AFE " +
                                   "回读与物理关断分别记录；保险丝实物验证使用独立证明试验。",
                            TextWrapping = TextWrapping.Wrap, Margin = new Thickness(6) });
        var controls = new WrapPanel();
        Button Add(string title, Func<Task> action)
        {
            var b = new Button { Content = title, Margin = new Thickness(4),
                                 Padding = new Thickness(10, 5, 10, 5) };
            b.Click += async (_, _) =>
            {
                try
                {
                    await action();
                }
                catch (Exception ex)
                {
                    C11Log(ex.Message);
                    _c11State.Text = "操作未完成：" + ex.Message;
                }
            };
            controls.Children.Add(b);
            return b;
        }
        Add("读取能力/状态", C11ProbeAsync);
        Add("开始手动会话", C11OpenManualAsync);
        Add("停止并清理", CloseC11Async);
        Add("申请输出方式",
            () => C11OperateAsync(
                async (c, ct) =>
                {
                    await c.Command(0x14,
                                    new[] { _c11Output.IsChecked == true ? (byte)1 : (byte)0 }, ct);
                }));
        Add("申请安全解除",
            () => C11OperateAsync(
                async (c, ct) =>
                {
                    await c.Command(0x1a, Array.Empty<byte>(), ct);
                    await Task.Delay(100, ct);
                    var s = await c.StatusAsync(ct);
                    C11DisplayStatus(s);
                    C11Log(s.Hold
                               ? "设备保持关断：检查拔充电器、关钥匙、真实零电流、诊断及自检条件。"
                               : "设备已确认安全保持解除；真实保护结果仍有效。");
                }));
        controls.Children.Add(_c11Output);
        panel.Children.Add(controls);
        panel.Children.Add(_c11State);
        _c11Kind.ItemsSource = new[] { "单体电压 mV",   "电流 A（正充负放）",
                                       "温度 ℃",        "诊断输入故障（通道1～7）",
                                       "NTC 原始码",    "有效性位掩码",
                                       "独立电流参考 A" };
        var input = new WrapPanel();
        input.Children.Add(_c11Kind);
        input.Children.Add(
            new TextBlock { Text = " 通道：", VerticalAlignment = VerticalAlignment.Center });
        input.Children.Add(_c11Channel);
        input.Children.Add(
            new TextBlock { Text = " 值：", VerticalAlignment = VerticalAlignment.Center });
        input.Children.Add(_c11Value);
        var apply = new Button { Content = "原子应用输入", Margin = new Thickness(6) };
        apply.Click += async (_, _) =>
        {
            try
            {
                await C11ApplyManualAsync();
            }
            catch (Exception ex)
            {
                C11Log(ex.Message);
            }
        };
        input.Children.Add(apply);
        panel.Children.Add(input);
        panel.Children.Add(_c11Input);
        panel.Children.Add(
            new TextBlock { Text =
                                "单体最大电压曲线：蓝色真实 / 橙色测试，纵轴0～5000 mV，最近120点",
                            Margin = new Thickness(6) });
        panel.Children.Add(_c11Plot);
        _c11Rule.ItemsSource = C11ValidationRunner.Names.Take(8).ToArray();
        _c11Parameter.ItemsSource =
            new[] { "触发阈值", "恢复阈值", "触发延时 ms", "恢复延时 ms", "锁存0/1" };
        var parameters = new WrapPanel();
        parameters.Children.Add(new TextBlock { Text = "测试 RAM 参数（电压mV/电流mA/温度0.1℃）：",
                                                VerticalAlignment = VerticalAlignment.Center });
        parameters.Children.Add(_c11Rule);
        parameters.Children.Add(_c11Parameter);
        parameters.Children.Add(_c11ParameterValue);
        var read = new Button { Content = "读取参数", Margin = new Thickness(4) };
        read.Click += async (_, _) =>
        {
            try
            {
                await C11OperateAsync(async (c, ct) =>
                                      {
                                          var r = await c.RuleAsync(_c11Rule.SelectedIndex, ct);
                                          C11Log(JsonSerializer.Serialize(r));
                                      });
            }
            catch (Exception ex)
            {
                C11Log(ex.Message);
            }
        };
        parameters.Children.Add(read);
        var save = new Button { Content = "应用并读回", Margin = new Thickness(4) };
        save.Click += async (_, _) =>
        {
            try
            {
                await C11WriteParameterAsync();
            }
            catch (Exception ex)
            {
                C11Log(ex.Message);
            }
        };
        parameters.Children.Add(save);
        panel.Children.Add(parameters);
        var execution = new WrapPanel();
        _c11Case.ItemsSource = Enumerable.Range(0, 22)
                                   .Select(i => $"SW-VD-{i+1:000} {C11ValidationRunner.Names[i]}")
                                   .ToArray();
        execution.Children.Add(_c11Case);
        var single = new Button { Content = "运行选中", Margin = new Thickness(4) };
        single.Click += async (_, _) => await C11RunAsync(_c11Case.SelectedIndex);
        execution.Children.Add(single);
        var all = new Button { Content = "运行22项", Margin = new Thickness(4) };
        all.Click += async (_, _) => await C11RunAsync(null);
        execution.Children.Add(all);
        var stop = new Button { Content = "中止", Margin = new Thickness(4) };
        stop.Click += (_, _) => _c11Cts?.Cancel();
        execution.Children.Add(stop);
        var self = new Button { Content = "执行选中自检", Margin = new Thickness(4) };
        self.Click += async (_, _) =>
        {
            try
            {
                int item = _c11Case.SelectedIndex - 14;
                if (item < 1 || item > 7)
                    throw new InvalidOperationException("请选择016～022的MCU自检项。");
                await C11OperateAsync(
                    async (c, ct) =>
                    {
                        await c.Command(0x19,
                                        new[] { (byte)item, _c11Fatal.IsChecked == true ? (byte)2
                                                            : _c11Fail.IsChecked == true
                                                                ? (byte)1
                                                                : (byte)0 },
                                        ct);
                    });
                if (_c11Fatal.IsChecked == true)
                    C11Log(
                        "已请求真实失败路径；失联不作为PASS，需外部波形确认CTL/实际输出，并复位。");
            }
            catch (Exception ex)
            {
                C11Log(ex.Message);
            }
        };
        execution.Children.Add(self);
        execution.Children.Add(_c11Fail);
        execution.Children.Add(_c11Fatal);
        panel.Children.Add(execution);
        panel.Children.Add(_c11SelfTest);
        panel.Children.Add(new DataGrid { ItemsSource = _c11Results, IsReadOnly = true,
                                          AutoGenerateColumns = true, Height = 220 });
        panel.Children.Add(_c11Log);
        MainTabs.Items.Add(
            new TabItem { Header = "C11 软件验证", Content = new ScrollViewer {
                             Content = panel, VerticalScrollBarVisibility = ScrollBarVisibility.Auto
                         } });
        _c11Timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(500) };
        _c11Timer.Tick += async (_, _) => await C11PollAsync();
    }

    private void C11Log(string text)
    {
        AppendLog(text, "C11");
        if (Dispatcher.HasShutdownStarted)
            return;
        Dispatcher.Invoke(() =>
                          {
                              _c11Log.AppendText(
                                  $"{DateTime.Now:HH:mm:ss} {text}{Environment.NewLine}");
                              _c11Log.ScrollToEnd();
                          });
    }
    private C11Connection C11Manual()
    {
        if (_c11Running || _c11Session is null || !ReferenceEquals(_c11BoundClient, _bms))
            throw new InvalidOperationException("请先为当前设备开始手动会话。");
        return _c11Session;
    }
    private async Task C11OperateAsync(Func<C11Connection, CancellationToken, Task> operation)
    {
        if (!await _c11ManualGate.WaitAsync(0))
            throw new InvalidOperationException("正在读取或应用，请稍后操作。");
        try
        {
            await operation(C11Manual(), _c11Cts!.Token);
        }
        finally
        {
            _c11ManualGate.Release();
        }
    }
    private async Task C11ProbeAsync()
    {
        if (_c11Running)
            throw new InvalidOperationException("自动测试期间请查看实时状态。");
        var bms = _bms ?? throw new InvalidOperationException("请先连接设备。");
        var c = C11Capabilities.Parse(
            await bms.C11ExchangeAsync(0x10, Array.Empty<byte>(), CancellationToken.None));
        if (!ReferenceEquals(bms, _bms))
            return;
        _c11State.Text =
            $"C11 协议v{c.Version} · 参数0x{c.Profile:X4} · {c.Cells}串/{c.Temperatures}温度 · 主动注入{c.InjectionEnabled} · 输出测试{c.ControlledOutputEnabled} · boot={c.BootId:X8}";
        _c11Output.IsEnabled = c.ControlledOutputEnabled;
        var status = C11Status.Parse(await bms.C11ExchangeAsync(
            0x15, C11Wire.Token(_c11Session?.Token ?? 0, 0), CancellationToken.None));
        if (ReferenceEquals(bms, _bms))
            C11DisplayStatus(status);
    }
    private async Task C11OpenManualAsync()
    {
        if (_c11Session is not null || _c11Running || _shBusy || _parameterBusy || _otaRunning)
            throw new InvalidOperationException("另一个操作尚未完成。");
        var bms = _bms ?? throw new InvalidOperationException("请先连接设备。");
        _shBusy = true;
        _pollTimer.Stop();
        _c11Cts = new CancellationTokenSource();
        var c = new C11Connection(bms);
        try
        {
            await WaitForCommunicationIdleAsync();
            await c.OpenAsync(_c11Cts.Token);
            if (!ReferenceEquals(bms, _bms))
                throw new IOException("连接已改变。");
            _c11Session = c;
            _c11BoundClient = bms;
            _c11Output.IsEnabled = c.Capabilities.ControlledOutputEnabled;
            _c11Timer!.Start();
            C11Log("手动会话建立；输入有效期独立续租，停止/断线后终止。");
        }
        catch
        {
            try
            {
                await c.DisposeAsync();
            }
            finally
            {
                _shBusy = false;
                _c11Cts?.Dispose();
                _c11Cts = null;
                StartAutomaticRefresh();
            }
            throw;
        }
    }
    private async Task C11ApplyManualAsync()
    {
        await C11OperateAsync(
            async (c, ct) =>
            {
                int channel = int.Parse(_c11Channel.Text, CultureInfo.InvariantCulture) - 1;
                decimal value = decimal.Parse(_c11Value.Text, CultureInfo.InvariantCulture);
                byte kind = _c11Kind.SelectedIndex switch {
                    0 => 1,
                    1 => 3,
                    2 => 4,
                    3 => 7,
                    4 => 9,
                    5 => 8,
                    _ => 10
                };
                int scaled = checked((int)(value * (kind is 3 or 10 ? 1000 : kind == 4 ? 10 : 1)));
                await c.ApplyAsync(new[] { (kind, checked((byte)channel), scaled) }, ct);
                C11Log("设备已确认应用；未改写正式参数或原始采样。");
            });
    }
    private async Task C11WriteParameterAsync()
    {
        await C11OperateAsync(async (c, ct) =>
                              {
                                  await c.ClearAsync(ct);
                                  int value = int.Parse(_c11ParameterValue.Text,
                                                        CultureInfo.InvariantCulture);
                                  byte[] payload = new byte[6];
                                  payload[0] = (byte)_c11Rule.SelectedIndex;
                                  payload[1] = (byte)_c11Parameter.SelectedIndex;
                                  System.Buffers.Binary.BinaryPrimitives.WriteInt32BigEndian(
                                      payload.AsSpan(2), value);
                                  await c.Command(0x17, payload, ct);
                                  var r = await c.RuleAsync(_c11Rule.SelectedIndex, ct);
                                  int actual = _c11Parameter.SelectedIndex switch {
                                      0 => r.Trip,
                                      1 => r.Recover,
                                      2 => checked((int)r.TripMs),
                                      3 => checked((int)r.RecoverMs),
                                      _ => r.Latched ? 1 : 0
                                  };
                                  if (actual != value)
                                      throw new IOException("参数读回不一致；不能报告成功。");
                                  C11Log("RAM 参数已读回确认：" + JsonSerializer.Serialize(r));
                              });
    }
    private async Task C11PollAsync()
    {
        if (_c11Polling || _c11Session is null || _c11Running || !await _c11ManualGate.WaitAsync(0))
            return;
        _c11Polling = true;
        try
        {
            var c = C11Manual();
            await c.HeartbeatAsync(_c11Cts!.Token);
            var status = await c.StatusAsync(_c11Cts.Token);
            if (status.InjectionActive)
                await c.Command(0x1b,
                                C11Wire.Sequence(0, status.AppliedSequence).AsSpan(2).ToArray(),
                                _c11Cts.Token);
            var raw = await c.SampleAsync(false, _c11Cts.Token);
            var test = await c.SampleAsync(true, _c11Cts.Token);
            if (!ReferenceEquals(_c11BoundClient, _bms))
                throw new IOException("连接已改变，当前结果作废。");
            C11DisplayStatus(status);
            _c11Input.Text =
                $"真实电流 {raw.CurrentMa/1000.0:F3} A / 测试 {test.CurrentMa/1000.0:F3} A；真实温度 {string.Join(", ",raw.TemperatureDecic.Take(raw.Temperatures).Select(x=>$"{x/10.0:F1}℃"))}；测试温度 {string.Join(", ",test.TemperatureDecic.Take(test.Temperatures).Select(x=>$"{x/10.0:F1}℃"))}\n真实Cell：{string.Join(",",raw.CellMv.Take(raw.Cells))}\n测试Cell：{string.Join(",",test.CellMv.Take(test.Cells))}";
            var self = await c.Command(0x15, new byte[] { 3 }, _c11Cts.Token);
            var diagnostic = await c.Command(0x15, new byte[] { 5 }, _c11Cts.Token);
            if (diagnostic.Length != 40)
                throw new IOException("诊断输入长度不匹配。");
            _c11Input.Text +=
                $"\n测试NTC原码：{C11Wire.U16(diagnostic,12)},{C11Wire.U16(diagnostic,14)},{C11Wire.U16(diagnostic,16)}；ADC mV：{C11Wire.U16(diagnostic,18)},{C11Wire.U16(diagnostic,20)},{C11Wire.U16(diagnostic,22)}；独立参考mA：{unchecked((int)C11Wire.U32(diagnostic,24))}";
            _c11SelfTest.Text =
                $"实际自检 PASS=0x{C11Wire.U32(self,4):X2} FAIL=0x{C11Wire.U32(self,8):X2} 周期={C11Wire.U32(self,12)}；测试自检 PASS=0x{C11Wire.U32(self,24):X2} 检出失败=0x{C11Wire.U32(self,28):X2}";
            _c11Points.Add((raw.CellMv.Take(raw.Cells).DefaultIfEmpty().Max(),
                            test.CellMv.Take(test.Cells).DefaultIfEmpty().Max()));
            if (_c11Points.Count > 120)
                _c11Points.RemoveAt(0);
            C11DrawPlot();
        }
        catch (Exception ex)
        {
            _c11Timer?.Stop();
            _c11State.Text = "会话状态不确定，停止后续注入：" + ex.Message;
            C11Log(ex.Message);
        }
        finally
        {
            _c11Polling = false;
            _c11ManualGate.Release();
        }
    }
    private void C11DisplayStatus(C11Status s)
    {
        _c11State.Text =
            $"MCU tick {s.NowMs} · 应用序号{s.AppliedSequence} · 软件保护 真实0x{s.RealFaults:X2}/测试0x{s.TestFaults:X2} · 诊断 真实0x{s.RealDiagnostics:X2}/测试0x{s.TestDiagnostics:X2}\n禁止请求 真实{s.RealInhibit}/测试{s.TestInhibit} · 业务请求{s.Requested} · 最终许可{s.Final} · AFE状态{s.Readback}（回读有效{s.ReadbackValid}，物理导通未验证）· 反向{s.Reverse} · 安全保持{s.Hold}";
    }
    private void C11DrawPlot()
    {
        double width = Math.Max(300, _c11Plot.ActualWidth);
        _c11Plot.Children.Clear();
        foreach (bool raw in new[] { true, false })
        {
            var line = new Polyline { Stroke = raw ? Brushes.SteelBlue : Brushes.DarkOrange,
                                      StrokeThickness = 2 };
            for (int i = 0; i < _c11Points.Count; i++)
            {
                double v = raw ? _c11Points[i].Raw : _c11Points[i].Test;
                line.Points.Add(
                    new Point(i * width / 119, 145 - Math.Clamp(v, 0, 5000) * 140 / 5000));
            }
            _c11Plot.Children.Add(line);
        }
    }
    private async Task CloseC11Async()
    {
        _c11Cts?.Cancel();
        _c11Timer?.Stop();
        if (_c11Running)
        {
            if (_c11RunFinished is not null)
                await _c11RunFinished.Task;
            return;
        }
        if (_c11Session is null)
            return;
        await _c11ManualGate.WaitAsync();
        var c = _c11Session;
        try
        {
            if (c is not null)
                await c.DisposeAsync();
            _c11State.Text = "会话已关闭、注入已清除；真实锁存/安全保持以设备状态为准。";
        }
        finally
        {
            _c11Session = null;
            _c11BoundClient = null;
            _shBusy = false;
            _c11Cts?.Dispose();
            _c11Cts = null;
            _c11ManualGate.Release();
            StartAutomaticRefresh();
        }
    }
    private async Task C11RunAsync(int? index)
    {
        if (_c11Session is not null || _c11Running || _shBusy || _parameterBusy || _otaRunning)
        {
            C11Log("请先关闭手动会话或等待其他操作完成。");
            return;
        }
        var client = _bms;
        if (client is null)
        {
            C11Log("请先连接设备。");
            return;
        }
        _c11Running = true;
        _shBusy = true;
        _pollTimer.Stop();
        _c11Results.Clear();
        _c11Cts = new CancellationTokenSource();
        _c11RunFinished = new(TaskCreationOptions.RunContinuationsAsynchronously);
        try
        {
            await WaitForCommunicationIdleAsync();
            string folder = System.IO.Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments),
                "BmsFactoryReports", "C11_" + DateTime.Now.ToString("yyyyMMdd_HHmmss_fff"));
            Directory.CreateDirectory(folder);
            await using var trace = new StreamWriter(System.IO.Path.Combine(folder, "trace.jsonl"));
            await using var curves = new StreamWriter(System.IO.Path.Combine(folder, "curves.csv"));
            using var rawLog = new StreamWriter(System.IO.Path.Combine(folder, "transport.log"));
            void Protocol(string line)
            {
                lock (rawLog)
                {
                    rawLog.WriteLine(DateTimeOffset.Now.ToString("O") + " " + line);
                    rawLog.Flush();
                }
            }
            client.Log += Protocol;
            try
            {
                var runner = new C11ValidationRunner(
                    row => Dispatcher.Invoke(() => _c11Results.Add(row)),
                    status => Dispatcher.Invoke(() =>
                                                {
                                                    if (ReferenceEquals(client, _bms))
                                                        C11DisplayStatus(status);
                                                }),
                    C11Log, trace, curves: curves);
                var report = await runner.RunAsync(client, _connectedName + " " + IdentityText.Text,
                                                   index, _c11Cts.Token);
                await C11ValidationRunner.SaveReportAsync(folder, report);
                C11Log(report.Overall + "；证据：" + folder);
            }
            catch (Exception ex)
            {
                C11Log("测试或报告保存未完成：" + ex.Message);
            }
            finally
            {
                client.Log -= Protocol;
            }
        }
        catch (Exception ex)
        {
            C11Log("无法创建或写入证据文件：" + ex.Message);
        }
        finally
        {
            _c11Running = false;
            _shBusy = false;
            _c11Cts?.Dispose();
            _c11Cts = null;
            _c11RunFinished.TrySetResult();
            StartAutomaticRefresh();
        }
    }
}
