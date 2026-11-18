using System.Globalization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace BmsTool.Windows;

public partial class MainWindow
{
    private bool _shortCircuitWindowOpened;

    private async void OpenShortCircuitSettings_Click(object sender, RoutedEventArgs e)
    {
        if (_currentCalibrationOwner is not null || _shortCircuitWindowOpened || _parameterBusy || _diagBusy ||
            _eventLogReadInProgress || _otaRunning) return;
        _shortCircuitWindowOpened = _parameterBusy = true;
        try
        {
            _pollTimer.Stop();
            await WaitForCommunicationIdleAsync();
            BmsClient bms = _bms ?? throw new InvalidOperationException("请先连接 BMS。");
            Window editor = bms.UsesC060Protocol ? new PackShortCircuitWindow(bms) : new ShortCircuitWindow(bms);
            editor.Owner = this;
            editor.ShowDialog();
        }
        catch (Exception ex) { ShowError("打开硬件短路保护失败", ex); }
        finally
        {
            _shortCircuitWindowOpened = _parameterBusy = false;
            StartAutomaticRefresh();
        }
    }
}

// 两版软件保护页共用；只编辑短路电流和延时，BmsClient 的连接仍由主窗口拥有。
public sealed class ShortCircuitWindow : Window
{
    private readonly AfeHardwareClient _client;
    private readonly AfeHardwareParameterModel _model = new();
    private AfeHardwareSnapshot? _snapshot;
    private bool _busy;
    private readonly TextBlock _device = new() { TextWrapping = TextWrapping.Wrap };
    private readonly TextBlock _ocd2 = new() { TextWrapping = TextWrapping.Wrap };
    private readonly TextBlock _range = new() { TextWrapping = TextWrapping.Wrap };
    private readonly TextBlock _status = new() { Text = "未读取", TextWrapping = TextWrapping.Wrap };
    private readonly ComboBox _current = new() { DisplayMemberPath = nameof(AfeShortCircuitChoice.Label), MinWidth = 400, MaxDropDownHeight = 300 };
    private readonly TextBox _delay = new() { Width = 120, HorizontalAlignment = HorizontalAlignment.Left };
    private readonly Button _read = new() { Content = "重新读取", MinWidth = 110, Height = 32 };
    private readonly Button _save = new() { Content = "保存并读回确认", MinWidth = 150, Height = 32, Margin = new Thickness(10, 0, 0, 0) };
    private static readonly ushort[] ShDelays = { 0,32,64,96,128,192,224,256,288,320,384,448,480,512,544,576 };

    public ShortCircuitWindow(BmsClient bms)
    {
        _client = new AfeHardwareClient(bms);
        Title = "硬件短路保护";
        Width = 800; Height = 470; MinWidth = 680; MinHeight = 420;
        WindowStartupLocation = WindowStartupLocation.CenterOwner;
        Background = Brushes.White;
        FontSize = 14;
        var panel = new StackPanel { Margin = new Thickness(20) };
        panel.Children.Add(new TextBlock {
            Text = "短路由 AFE 硬件执行。此处只修改短路电流和延时，放电过流2（OCD2）及其它参数保持。",
            TextWrapping = TextWrapping.Wrap, FontWeight = FontWeights.SemiBold, Margin = new Thickness(0, 0, 0, 14)
        });
        foreach (var text in new[] { _device, _ocd2, _range }) {
            text.Margin = new Thickness(0, 0, 0, 10); panel.Children.Add(text);
        }
        panel.Children.Add(new TextBlock { Text = "短路电流档位（A）", Margin = new Thickness(0, 4, 0, 5) });
        panel.Children.Add(_current);
        var delayRow = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 14, 0, 16) };
        delayRow.Children.Add(new TextBlock { Text = "短路延时", Width = 100, VerticalAlignment = VerticalAlignment.Center });
        delayRow.Children.Add(_delay);
        delayRow.Children.Add(new TextBlock { Text = "µs", Margin = new Thickness(8, 0, 0, 0), VerticalAlignment = VerticalAlignment.Center });
        panel.Children.Add(delayRow);
        var buttons = new StackPanel { Orientation = Orientation.Horizontal };
        buttons.Children.Add(_read); buttons.Children.Add(_save); panel.Children.Add(buttons);
        _status.Margin = new Thickness(0, 14, 0, 0); _status.Foreground = Brushes.DimGray; panel.Children.Add(_status);
        Content = new ScrollViewer { Content = panel, Background = Brushes.White, VerticalScrollBarVisibility = ScrollBarVisibility.Auto };
        _read.Click += async (_, _) => await ReadAsync();
        _save.Click += async (_, _) => await SaveAsync();
        Loaded += async (_, _) => await ReadAsync();
        Closing += (_, e) => { if (_busy) e.Cancel = true; };
        SetBusy(false);
    }

    private void SetBusy(bool busy)
    {
        _busy = busy;
        bool editable = !busy && _snapshot is not null &&
            (_snapshot.Requested[34] & AfeHardwareProtocolMap.CapSc) != 0 && _current.Items.Count != 0;
        _read.IsEnabled = !busy;
        _save.IsEnabled = _current.IsEnabled = _delay.IsEnabled = editable;
    }

    private void ApplySnapshot(AfeHardwareSnapshot snapshot)
    {
        _model.Load(snapshot);
        _snapshot = snapshot;
        _device.Text = $"AFE：{snapshot.Info.BackendName} · 分流器 {snapshot.Info.ShuntDisplay} · 短路保护" +
            (((snapshot.Requested[34] & AfeHardwareProtocolMap.CapSc) != 0) ? "已启用" : "已关闭（本页保持开关状态）");
        bool sh = AfeHardwareProtocolMap.IsSh(snapshot.Info.BackendModel);
        _ocd2.Text = sh
            ? $"OCD2 只读基准：请求 {snapshot.Requested[12] / 10m:0.0} A，有效 {snapshot.Effective[12] / 10m:0.0} A。修改短路不会改变此项。"
            : "本芯片短路阈值独立于 OCD2，修改短路不会改变过流参数。";
        var row = _model.Rows.FirstOrDefault(r => r.WireIndex == 22);
        _range.Text = row is null ? "设备不支持短路参数设置。" :
            $"电流允许请求范围：{row.AllowedCurrentRange}。短路延时允许 0～{(sh ? 576 : 1992)} µs，按芯片档位量化。";
        var choices = _model.GetShortCircuitChoices().ToList();
        var selected = choices.FirstOrDefault(c => c.RequestedA10 == snapshot.Requested[22] && c.EffectiveA10 == snapshot.Effective[22]);
        if (selected is null && choices.Count != 0) {
            selected = new(snapshot.Requested[22], snapshot.Effective[22],
                $"当前：请求 {snapshot.Requested[22] / 10m:0.0} A，有效 {snapshot.Effective[22] / 10m:0.0} A");
            choices.Insert(0, selected);
        }
        _current.ItemsSource = choices; _current.SelectedItem = selected;
        _delay.Text = snapshot.Requested[23].ToString(CultureInfo.InvariantCulture);
        _status.Text = choices.Count == 0 ? "无法取得可选短路档位，请核对芯片能力及分流器信息。" :
            $"读取完成 · 当前有效短路电流 {snapshot.Effective[22] / 10m:0.0} A，延时 {snapshot.Effective[23]} µs。";
    }

    private async Task ReadAsync()
    {
        if (_busy) return;
        _snapshot = null; SetBusy(true); _status.Text = "正在读取当前短路配置及 OCD2 基准…";
        try { ApplySnapshot(await _client.ReadAllAsync()); }
        catch (Exception ex) {
            _snapshot = null; _status.Text = "读取失败，请重新读取。";
            MessageBox.Show(this, ex.Message, "读取短路参数失败", MessageBoxButton.OK, MessageBoxImage.Error);
        }
        finally { SetBusy(false); }
    }

    private async Task SaveAsync()
    {
        if (_busy || _snapshot is null) return;
        if (_current.SelectedItem is not AfeShortCircuitChoice choice ||
            !ushort.TryParse(_delay.Text.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out ushort delay)) {
            MessageBox.Show(this, "请选择短路电流档位，并输入整数延时（µs）。", "参数未发送", MessageBoxButton.OK, MessageBoxImage.Warning); return;
        }
        if (!_model.TryBuildShortCircuitCandidate(choice.RequestedA10, delay, out var candidate, out string error)) {
            MessageBox.Show(this, error, "参数未发送", MessageBoxButton.OK, MessageBoxImage.Warning); return;
        }
        if (candidate.SequenceEqual(_snapshot.Requested)) { _status.Text = "没有待保存的修改。"; return; }
        ushort effectiveDelay = AfeHardwareProtocolMap.IsSh(_snapshot.Info.BackendModel)
            ? ShDelays.First(d => d >= delay) : (ushort)(((delay * 100u / 781u) * 781u + 50u) / 100u);
        if (MessageBox.Show(this,
                $"确认保存短路保护？\n\n电流请求 {choice.RequestedA10 / 10m:0.0} A，有效目标 {choice.EffectiveA10 / 10m:0.0} A。\n" +
                $"延时请求 {delay} µs，预计设备报告 {effectiveDelay} µs。\nOCD2、保护开关及其它参数保持。",
                "确认修改短路保护", MessageBoxButton.YesNo, MessageBoxImage.Question) != MessageBoxResult.Yes) return;
        var expected = _snapshot; SetBusy(true); _status.Text = "正在保存并读回确认…";
        try {
            ApplySnapshot(await _client.WriteShortCircuitAsync(expected, choice.RequestedA10, delay));
            _status.Text = $"保存成功 · 有效电流 {_snapshot!.Effective[22] / 10m:0.0} A，延时 {_snapshot.Effective[23]} µs；OCD2 已核对。";
            MessageBox.Show(this, _status.Text, "保存成功", MessageBoxButton.OK, MessageBoxImage.Information);
        }
        catch (Exception ex) {
            _snapshot = null; _status.Text = "保存未确认，请重新读取当前设备；不要直接重复提交。";
            MessageBox.Show(this, ex.Message + "\n\n" + _status.Text, "保存短路参数失败", MessageBoxButton.OK, MessageBoxImage.Error);
        }
        finally { SetBusy(false); }
    }
}
