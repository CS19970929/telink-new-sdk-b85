using System.Collections.ObjectModel;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Threading;

namespace BmsTool.Windows;

public partial class MainWindow
{
    private sealed record OpenWireRow(string Time, string State, string Detail);
    private readonly ObservableCollection<OpenWireRow> _openWireRows = new();
    private readonly TextBlock _openWireStatus = new() { Text = "连接后自动读取；旧固件会显示不支持", TextWrapping = TextWrapping.Wrap, Margin = new Thickness(6) };
    private readonly TextBlock _openWirePath = new() { TextWrapping = TextWrapping.Wrap, Margin = new Thickness(6) };
    private BmsOpenWireStatus? _openWirePrevious;
    private BmsClient? _openWireClient;
    private DateTimeOffset _openWireReceived;
    private bool _openWireStale;

    private void AddOpenWireTab()
    {
        var root = new DockPanel { Margin = new Thickness(12) };
        var auto = new CheckBox { Content = "自动监控并写盘（与 MOS 监控共用）", Margin = new Thickness(6) };
        auto.SetBinding(CheckBox.IsCheckedProperty, new Binding(nameof(CheckBox.IsChecked)) { Source = _mosAuto, Mode = BindingMode.TwoWay });
        DockPanel.SetDock(auto, Dock.Top); root.Children.Add(auto);
        DockPanel.SetDock(_openWireStatus, Dock.Top); root.Children.Add(_openWireStatus);
        DockPanel.SetDock(_openWirePath, Dock.Top); root.Children.Add(_openWirePath);
        var note = new TextBlock { Text = "检测过程不主动关 MOS，确认故障仍保护关断。检测仅在无充放电电流且门禁允许时运行。SH 奇偶通道映射待逐路实板确认，保留两阶段原始掩码。记录与 MOS 监控保存在同一目录，可从 MOS 监控页打开。界面显示最近 300 次变化；每次读取的原始字均保存为 openwire。计数可发现两次轮询间发生检测，但不能补回已覆盖的历史原因。", TextWrapping = TextWrapping.Wrap, Margin = new Thickness(6) };
        DockPanel.SetDock(note, Dock.Top); root.Children.Add(note);
        var grid = new DataGrid { IsReadOnly = true, AutoGenerateColumns = false, CanUserAddRows = false, ItemsSource = _openWireRows };
        grid.Columns.Add(new DataGridTextColumn { Header = "PC 时间", Binding = new Binding("Time"), Width = 155 });
        grid.Columns.Add(new DataGridTextColumn { Header = "检测状态", Binding = new Binding("State"), Width = 230 });
        var style = new Style(typeof(TextBlock)); style.Setters.Add(new Setter(TextBlock.TextWrappingProperty, TextWrapping.Wrap));
        grid.Columns.Add(new DataGridTextColumn { Header = "结果 / 原因", Binding = new Binding("Detail"), ElementStyle = style, Width = new DataGridLength(1, DataGridLengthUnitType.Star), MinWidth = 260 });
        root.Children.Add(grid); MainTabs.Items.Add(new TabItem { Header = "断线检测", Content = root });
        var timer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };
        timer.Tick += (_, _) => {
            if (_openWireReceived == default || _openWireStale) return;
            if (_bms is null || _otaRunning || _mosAuto.IsChecked != true || DateTimeOffset.UtcNow - _openWireReceived > TimeSpan.FromSeconds(5)) {
                _openWireStale = true;
                _openWireStatus.Text = "监控暂停或通信数据过期，当前检测状态未知\n最后一次接收：" + _openWireReceived.ToLocalTime().ToString("yyyy-MM-dd HH:mm:ss");
                TryMosWrite("openwire-gap", new { reason = _openWireStatus.Text });
            }
        };
        timer.Start(); Closed += (_, _) => timer.Stop();
    }

    private async Task RefreshOpenWireAsync(BmsClient client, CancellationToken ct)
    {
        if (!ReferenceEquals(client, _openWireClient)) {
            _openWireClient = client; _openWirePrevious = null; _openWireReceived = default;
        }
        try {
            var status = await client.ReadOpenWireStatusAsync(ct);
            if (_mosClosed || !ReferenceEquals(client, _bms)) return;
            if (status is null) { _openWireStatus.Text = "当前固件不支持独立断线检测快照；升级配套固件后显示"; return; }
            bool reset = _openWirePrevious is { } previous && (previous.BuildId != status.BuildId ||
                unchecked(status.Sequence - previous.Sequence) >= 0x80000000u ||
                unchecked(status.ReadTick - previous.ReadTick) >= 0x80000000u);
            uint completedBetween = !reset && _openWirePrevious is { } old ? unchecked(status.Completed - old.Completed) : 0;
            if (!TryMosWrite("openwire", new { sample = _mosSample, status.Words, status.Sequence,
                status.Attempts, status.Completed, status.Failed, reset, completedBetween, summary = status.Summary })) {
                _openWireStatus.Text = "记录写盘失败，监控已停止"; return;
            }
            _openWirePath.Text = "自动保存：" + _mosDirectory + "（mos_*.jsonl 中 kind=openwire）";
            _openWireStatus.Text = status.Summary;
            if (_openWirePrevious is null || reset || status.Sequence != _openWirePrevious.Sequence ||
                status.Words[4] != _openWirePrevious.Words[4] || status.Words[39] != _openWirePrevious.Words[39]) {
                string detail = status.Summary + (reset ? "\n检测到重启/序号回退，建立新观察段" : "") +
                    (completedBetween > 1 ? $"\n两次轮询间完成 {completedBetween} 次，只能读取最近结果" : "");
                _openWireRows.Insert(0, new OpenWireRow(DateTime.Now.ToString("HH:mm:ss.fff"), status.StateText, detail));
                while (_openWireRows.Count > 300) _openWireRows.RemoveAt(_openWireRows.Count - 1);
            }
            _openWirePrevious = status; _openWireReceived = DateTimeOffset.UtcNow; _openWireStale = false;
        } catch (OperationCanceledException) { throw; }
        catch (Exception ex) {
            _openWireStatus.Text = "断线检测读取失败，当前状态未知：" + ex.Message;
            TryMosWrite("openwire-gap", new { reason = ex.Message });
        }
    }
}
