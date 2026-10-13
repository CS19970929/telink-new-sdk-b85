// 两个 WPF 入口共用运行日志界面；默认不轮询，避免调试流量持续唤醒设备。
using Microsoft.Win32;
using System.Collections.ObjectModel;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Threading;

namespace BmsTool.Windows;
public partial class MainWindow
{
    private readonly ObservableCollection<RuntimeLogEntry> _runtimeLogs = new();
    private RuntimeLogCursor? _runtimeLogCursor;
    private BmsClient? _runtimeLogClient;
    private bool _runtimeLogBusy, _runtimeLogClosed;
    private CancellationTokenSource? _runtimeLogCts;
    private readonly CheckBox _runtimeLogAuto = new() { Content = "每秒采集（会增加通信功耗）", Margin = new Thickness(6) };
    private readonly TextBlock _runtimeLogStatus = new() { Text = "连接串口或 BLE 后读取。需要固件开启 BMS_DEBUG_LOG_ENABLE；日志不写 Flash。", TextWrapping = TextWrapping.Wrap, Margin = new Thickness(6) };

    private void AddRuntimeLogTab()
    {
#if BMS_FACTORY_APP
        var root = new DockPanel { Margin = new Thickness(10) };
        var controls = new WrapPanel();
        var read = new Button { Content = "读取运行日志", Margin = new Thickness(4) };
        read.Click += async (_, _) => await CaptureRuntimeLogsAsync();
        var stop = new Button { Content = "停止", Margin = new Thickness(4) };
        stop.Click += (_, _) => { _runtimeLogAuto.IsChecked = false; _runtimeLogCts?.Cancel(); };
        var clear = new Button { Content = "清空显示", Margin = new Thickness(4) };
        clear.Click += (_, _) => _runtimeLogs.Clear();
        var export = new Button { Content = "导出当前记录", Margin = new Thickness(4) };
        export.Click += (_, _) => {
            var dialog = new SaveFileDialog { Filter = "运行日志 (*.txt)|*.txt", FileName = $"BMS_runtime_{DateTime.Now:yyyyMMdd_HHmmss}.txt" };
            if (dialog.ShowDialog(this) != true) return;
            try {
                File.WriteAllLines(dialog.FileName, new[] { _runtimeLogStatus.Text,
                    "Sequence\tTick32k\tLevel\tModule\tEvent\tArg0\tArg1\tDetail" }.Concat(_runtimeLogs.Select(x => x.ExportLine)));
            } catch (Exception ex) { ShowError("运行日志导出失败", ex); }
        };
        var level = new ComboBox { ItemsSource = new[] { "全部等级", "ERROR", "WARN及以上", "INFO及以上" }, SelectedIndex = 0, Margin = new Thickness(4) };
        var module = new ComboBox { ItemsSource = new[] { "全部模块" }.Concat(RuntimeLogEntry.ModuleNames), SelectedIndex = 0, Margin = new Thickness(4) };
        var view = CollectionViewSource.GetDefaultView(_runtimeLogs);
        view.Filter = item => item is RuntimeLogEntry entry &&
            (level.SelectedIndex == 0 || entry.Level <= level.SelectedIndex) &&
            (module.SelectedIndex == 0 || entry.Module == module.SelectedIndex - 1);
        level.SelectionChanged += (_, _) => view.Refresh();
        module.SelectionChanged += (_, _) => view.Refresh();
        foreach (var item in new UIElement[] { read, stop, clear, export, _runtimeLogAuto, level, module }) controls.Children.Add(item);
        DockPanel.SetDock(controls, Dock.Top); root.Children.Add(controls);
        DockPanel.SetDock(_runtimeLogStatus, Dock.Top); root.Children.Add(_runtimeLogStatus);
        var grid = new DataGrid { IsReadOnly = true, AutoGenerateColumns = false, CanUserAddRows = false, ItemsSource = view };
        foreach (var column in new[] { ("序号", "Sequence"), ("设备计时", "DeviceTime"), ("等级", "Severity"), ("模块", "ModuleName"), ("事件", "Event"), ("运行过程", "Detail") })
            grid.Columns.Add(new DataGridTextColumn { Header = column.Item1, Binding = new Binding(column.Item2) });
        root.Children.Add(grid);
        MainTabs.Items.Add(new TabItem { Header = "运行日志", Content = root });
#endif
        var timer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };
        timer.Tick += async (_, _) => {
            if (_bms is null) { _runtimeLogAuto.IsChecked = false; _runtimeLogCts?.Cancel(); return; }
            if (_runtimeLogAuto.IsChecked == true) await CaptureRuntimeLogsAsync();
        };
        timer.Start();
        Closed += (_, _) => { _runtimeLogClosed = true; timer.Stop(); _runtimeLogCts?.Cancel(); };
    }

    // OTA owns the transport only after an in-flight log transaction has released it.
    private async Task StopRuntimeLogsForOtaAsync()
    {
        await StopMosReadsForOtaAsync();
        _runtimeLogAuto.IsChecked = false;
        _runtimeLogCts?.Cancel();
        for (int attempt = 0; _runtimeLogBusy && attempt < 100; ++attempt)
            await Task.Delay(50);
        if (_runtimeLogBusy) throw new IOException("运行日志尚未停止，请稍后重试 OTA");
    }

    private async Task CaptureRuntimeLogsAsync()
    {
        if (_runtimeLogClosed || _runtimeLogBusy || _diagBusy || _eventLogReadInProgress ||
            _otaRunning || _autoReconnectRunning || _shBusy || ShFactoryBusy()) return;
        var client = _bms;
        if (client is null) { _runtimeLogStatus.Text = "请先连接 BMS"; return; }
        _runtimeLogBusy = true;
        _eventLogReadInProgress = true;
        using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(30));
        _runtimeLogCts = cts;
        try {
            _pollTimer.Stop();
            await WaitForCommunicationIdleAsync();
            if (!ReferenceEquals(client, _bms)) throw new IOException("连接已改变");
            if (!ReferenceEquals(client, _runtimeLogClient)) {
                _runtimeLogCursor = null; _runtimeLogClient = client; _runtimeLogs.Clear();
            }
            var batch = await client.ReadRuntimeLogsAsync(_runtimeLogCursor, cts.Token);
            if (!ReferenceEquals(client, _bms) || _runtimeLogClosed) return;
            if (!batch.Supported || !batch.Enabled) {
                _runtimeLogAuto.IsChecked = false;
                _runtimeLogStatus.Text = batch.Supported ? "固件运行日志已关闭（量产/默认配置）" : "该固件不支持运行日志窗口";
                return;
            }
            if (batch.Reset) _runtimeLogs.Clear();
            _runtimeLogCursor = batch.Cursor;
            foreach (var entry in batch.Entries) _runtimeLogs.Add(entry);
            while (_runtimeLogs.Count > 2000) _runtimeLogs.RemoveAt(0);
            _runtimeLogStatus.Text = $"Build={batch.Cursor!.BuildId:X8} · 固件等级={batch.DeviceLevel} 模块=0x{batch.ModuleMask:X3} · 新增={batch.Entries.Count} · 本次缺失={batch.Missed} · RAM累计覆盖={batch.Overwritten} · {(batch.Reset ? "设备会话已重置 · " : "")}保留最近 {_runtimeLogs.Count}/2000 条；32K计时会回绕，deep sleep/reset 不保留 RAM。";
        } catch (OperationCanceledException) {
            _runtimeLogAuto.IsChecked = false; _runtimeLogStatus.Text = "运行日志采集已停止或超时";
        } catch (Exception ex) {
            _runtimeLogAuto.IsChecked = false; _runtimeLogStatus.Text = "运行日志读取失败：" + ex.Message;
        } finally {
            _runtimeLogCts = null; _eventLogReadInProgress = false; _runtimeLogBusy = false;
            if (!_runtimeLogClosed && _bms is not null && !_otaRunning) StartAutomaticRefresh();
        }
    }
}
