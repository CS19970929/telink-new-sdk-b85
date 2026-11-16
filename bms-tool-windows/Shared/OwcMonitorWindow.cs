using System.ComponentModel;
using System.IO.Ports;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Media;

namespace BmsTool.Windows;

public sealed class OwcMonitorWindow : Window
{
    private readonly ComboBox _ports = new() { Width = 140, Margin = new Thickness(4), IsEditable = true };
    private readonly ComboBox _protocol = new() { Width = 150, Margin = new Thickness(4),
        ItemsSource = new[] { "自动识别", "A1.3.3", "锂神 V1.1.1" }, SelectedIndex = 0 };
    private bool _restartAfterStop;
    private readonly Button _refresh = new() { Content = "刷新串口", Margin = new Thickness(4), Padding = new Thickness(10, 4, 10, 4) };
    private readonly Button _start = new() { Content = "开始监控", Margin = new Thickness(4), Padding = new Thickness(10, 4, 10, 4) };
    private readonly Button _stop = new() { Content = "停止监控", Margin = new Thickness(4), Padding = new Thickness(10, 4, 10, 4), IsEnabled = false };
    private readonly TextBlock _status = new() { Text = "请选择Comm Tool串口后开始监控", TextWrapping = TextWrapping.Wrap, Margin = new Thickness(4) };
    private readonly DataGrid _fields = new()
    {
        IsReadOnly = true, AutoGenerateColumns = false, CanUserAddRows = false,
        CanUserDeleteRows = false, HeadersVisibility = DataGridHeadersVisibility.Column
    };
    private readonly TextBox _raw = new()
    {
        IsReadOnly = true, AcceptsReturn = true, TextWrapping = TextWrapping.Wrap,
        VerticalScrollBarVisibility = ScrollBarVisibility.Auto, FontFamily = new FontFamily("Consolas")
    };
    private CancellationTokenSource? _stopSource;
    private Task? _running;
    private bool _closing;

    public OwcMonitorWindow()
    {
        Title = "一线通监控";
        Width = 1000; Height = 760; MinWidth = 740; MinHeight = 520;
        WindowStartupLocation = WindowStartupLocation.CenterOwner;
        var layout = new Grid { Margin = new Thickness(12) };
        layout.RowDefinitions.Add(new() { Height = GridLength.Auto });
        layout.RowDefinitions.Add(new() { Height = GridLength.Auto });
        layout.RowDefinitions.Add(new() { Height = GridLength.Auto });
        layout.RowDefinitions.Add(new() { Height = new GridLength(1, GridUnitType.Star) });
        layout.RowDefinitions.Add(new() { Height = new GridLength(160) });
        var controls = new WrapPanel();
        controls.Children.Add(new TextBlock { Text = "Comm Tool / 115200 8N1", VerticalAlignment = VerticalAlignment.Center });
        foreach (var control in new UIElement[] { _ports, _refresh, _protocol, _start, _stop }) controls.Children.Add(control);
        layout.Children.Add(controls);
        Grid.SetRow(_status, 1); layout.Children.Add(_status);
        var note = new TextBlock
        {
            Text = "选择接收协议；切换后自动重新接收。锂神模式只提供SOC、总压和电池温度。\n仅连接Comm Tool串口；接收模式不会改变BMS发送协议。换电池后请停止再开始。",
            TextWrapping = TextWrapping.Wrap, Margin = new Thickness(4, 6, 4, 8), Foreground = Brushes.DimGray
        };
        Grid.SetRow(note, 2); layout.Children.Add(note);
        foreach (var (title, path, width) in new[]
        {
            ("报文", nameof(OwcField.Packet), 85.0), ("字段", nameof(OwcField.Name), 225.0),
            ("值", nameof(OwcField.Value), 260.0), ("单位", nameof(OwcField.Unit), 90.0)
        })
            _fields.Columns.Add(new DataGridTextColumn { Header = title, Binding = new Binding(path), Width = width });
        Grid.SetRow(_fields, 3); layout.Children.Add(_fields);
        Grid.SetRow(_raw, 4); layout.Children.Add(_raw);
        Content = layout;
        _refresh.Click += (_, _) => RefreshPorts();
        _start.Click += Start_Click;
        _stop.Click += (_, _) => Stop();
        _protocol.SelectionChanged += (_, _) => { ClearData(); if (_running is not null) Stop(true); };
        Closing += ClosingWindow;
        RefreshPorts();
    }

    private void RefreshPorts()
    {
        string selected = _ports.Text;
        _ports.ItemsSource = SerialPort.GetPortNames().OrderBy(p => p, StringComparer.OrdinalIgnoreCase).ToArray();
        _ports.Text = selected;
        if (string.IsNullOrWhiteSpace(selected) && _ports.Items.Count != 0) _ports.SelectedIndex = 0;
    }

    private async void Start_Click(object sender, RoutedEventArgs e)
    {
        if (_running is not null) return;
        string port = _ports.Text.Trim();
        if (string.IsNullOrWhiteSpace(port)) { _status.Text = "请先选择Comm Tool串口"; return; }
        ClearData(); SetRunning(true);
        _status.Text = "正在连接 " + port;
        _stopSource = new CancellationTokenSource();
        _running = MonitorAsync(port, (OwcReceiveMode)_protocol.SelectedIndex, _stopSource.Token);
        try { await _running; }
        finally
        {
            _running = null;
            _stopSource.Dispose(); _stopSource = null;
            SetRunning(false);
            if (_status.Text == "正在停止监控") _status.Text = "已停止监控";
            if (_closing) Close();
            else if (_restartAfterStop)
            {
                _restartAfterStop = false;
                Start_Click(this, new RoutedEventArgs());
            }
        }
    }

    private Task MonitorAsync(string port, OwcReceiveMode mode, CancellationToken token) => Task.Run(async () =>
    {
        try
        {
            using var client = new CommToolOwcClient(port);
            try
            {
                client.Start(mode, token);
                while (true)
                {
                    token.ThrowIfCancellationRequested();
                    var snapshots = client.ReadSnapshots(token);
                    if (!snapshots[0].Enabled)
                    {
                        await Dispatcher.InvokeAsync(() => { ClearData(); _status.Text = "接收器租约结束，等待重新接入"; });
                        await Task.Delay(2000, token);
                        client.SetEnabled(true, token);
                    }
                    else
                        await Dispatcher.InvokeAsync(() => { if (!token.IsCancellationRequested) ShowSnapshots(snapshots); });
                    await Task.Delay(1000, token);
                }
            }
            finally
            {
                // 取消读取后仍单独给停止命令1s上限；失败则由设备10s租约释放上拉。
                try { client.SetEnabled(false, CancellationToken.None); }
                catch (Exception ex) when (ex is System.IO.IOException or TimeoutException or InvalidOperationException)
                {
                    await Dispatcher.InvokeAsync(() => _status.Text = $"停止命令未确认：{ex.Message}；设备由10s租约释放上拉");
                }
            }
        }
        catch (OperationCanceledException) when (token.IsCancellationRequested) { }
        catch (Exception ex)
        {
            await Dispatcher.InvokeAsync(() => { ClearData(); _status.Text = "通信异常：" + ex.Message; });
        }
    });

    // 主线程显示入口；仅展示经长度、SUM、范围及有效期检查的字段。
    public void ShowSnapshots(IReadOnlyDictionary<byte, OwcSnapshot> snapshots)
    {
        bool online = CommToolOwcProtocol.Online(snapshots);
        var rows = new List<OwcField>();
        var raw = new StringBuilder();
        var latestSnapshot = snapshots.GetValueOrDefault((byte)0);
        OwcWireProtocol protocol = latestSnapshot?.Protocol ?? OwcWireProtocol.A133;
        byte[] visibleIds = protocol == OwcWireProtocol.LishenV111 ? [0x80, 0] :
            protocol == OwcWireProtocol.Unknown ? [0] : CommToolOwcProtocol.PacketIds;
        foreach (byte id in visibleIds)
        {
            if (!snapshots.TryGetValue(id, out var snapshot)) continue;
            var fields = id == 0 || latestSnapshot?.MismatchProtocol != OwcWireProtocol.Unknown
                ? new List<OwcField>() : CommToolOwcProtocol.VisibleFields(snapshot, snapshots);
            string packetName = id == 0x80 ? "锂神实时" : $"0x{id:X2}";
            string state = !snapshot.Enabled ? "接收关闭" :
                snapshot.Raw.Length == 0 ? "等待" : !snapshot.ChecksumOk ? "校验失败" :
                id == 1 ? (online ? "接入信息" : "接入信息（离线）") :
                !online ? "已失联" : !snapshot.Fresh ? "等待或过期" :
                id == 0x3B && fields.Count == 0 ? "测量不可用" : "有效";
            string age = snapshot.Raw.Length == 0 ? "--" : $"{snapshot.AgeMs / 1000.0:g}s";
            string session = snapshot.Session == 0 ? "--" : snapshot.Session.ToString();
            raw.AppendLine($"{packetName} | {state} | age={age} | snapshot=v{snapshot.Version} | session={session} | seq={snapshot.Sequence}");
            raw.AppendLine(Convert.ToHexString(snapshot.Raw));
            if (id != 0) { rows.Add(new(packetName, "状态", state, age)); rows.AddRange(fields); }
        }
        if (protocol == OwcWireProtocol.LishenV111)
            rows.Add(new("锂神", "协议范围", "仅提供SOC、总压、电池温度", ""));
        _fields.ItemsSource = rows; _raw.Text = raw.ToString();
        if (snapshots.TryGetValue(0, out var latest))
            _status.Text = $"{CommToolOwcProtocol.ProtocolName(latest.Protocol)} · {(online ? "在线" : "等待有效数据")}  snapshot=v{latest.Version}  session={(latest.Session == 0 ? "--" : latest.Session.ToString())}  有效帧={latest.Good}  SUM错误={latest.ChecksumErrors}  时序错误={latest.TimingErrors}  溢出={latest.Overflows}";
        if (latestSnapshot?.MismatchProtocol is { } other && other != OwcWireProtocol.Unknown)
            _status.Text = $"检测到{CommToolOwcProtocol.ProtocolName(other)}，请切换接收模式；旧测量已隐藏";
        else if (latestSnapshot?.Version is 1 or 2)
            _status.Text += "；此Comm Tool仅支持A1.3.3，锂神接收需升级App";
    }

    private void SetRunning(bool running)
    {
        _ports.IsEnabled = _refresh.IsEnabled = _start.IsEnabled = !running;
        _stop.IsEnabled = running;
    }
    private void ClearData() { _fields.ItemsSource = null; _raw.Clear(); }
    private void Stop(bool restart = false)
    {
        _restartAfterStop = restart;
        _stopSource?.Cancel();
        ClearData(); _stop.IsEnabled = false;
        _status.Text = "正在停止监控";
    }
    private void ClosingWindow(object? sender, CancelEventArgs e)
    {
        if (_running is null) return;
        e.Cancel = true; _closing = true; Stop();
    }
}
