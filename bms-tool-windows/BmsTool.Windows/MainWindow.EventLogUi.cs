using System.IO;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Media;
using Microsoft.Win32;
namespace BmsTool.Windows;

public partial class MainWindow
{
    private bool _eventLogReadInProgress;
    private const ushort EventLogRegister = 0xC008;
    private const ushort EventLogCount = 100;
#if BMS_FACTORY_APP
    private DatePicker? _eventFrom, _eventTo;
    private ComboBox? _eventSource, _eventState;
    private TextBox? _eventSearch;
    private DateTimeOffset _eventReadUtc;
    private string _eventReadIdentity = "";
#endif

    private void AddEventLogTab()
    {
        if (MainTabs.Items.OfType<TabItem>().Any(t => Equals(t.Header, "事件日志"))) return;
        var root = new DockPanel { Margin = new Thickness(12) };
        var toolbar = new StackPanel(); DockPanel.SetDock(toolbar, Dock.Top); root.Children.Add(toolbar);
        var actions = new WrapPanel { Margin = new Thickness(0, 0, 0, 8) }; toolbar.Children.Add(actions);
        var read = new Button { Content = "读取事件日志", Padding = new Thickness(12, 5, 12, 5) };
        read.Click += ReadEventLogs_Click; actions.Children.Add(read);
        var export = new Button { Content = "导出 CSV", Padding = new Thickness(12, 5, 12, 5), Margin = new Thickness(8, 0, 8, 0) };
        export.Click += ExportEventLogs_Click; actions.Children.Add(export);
        _eventLogStatus = new TextBlock { Text = "未读取", VerticalAlignment = VerticalAlignment.Center }; actions.Children.Add(_eventLogStatus);
#if BMS_FACTORY_APP
        var filters = new WrapPanel { Margin = new Thickness(0, 0, 0, 8) }; toolbar.Children.Add(filters);
        filters.Children.Add(new TextBlock { Text = "本地日期：", VerticalAlignment = VerticalAlignment.Center });
        _eventFrom = new DatePicker { Width = 130 }; _eventTo = new DatePicker { Width = 130 };
        filters.Children.Add(_eventFrom); filters.Children.Add(new TextBlock { Text = " 至 ", VerticalAlignment = VerticalAlignment.Center }); filters.Children.Add(_eventTo);
        _eventSource = new ComboBox { Width = 125, Margin = new Thickness(8, 0, 0, 0), ItemsSource = new[] { "全部来源", "系统", "软件保护", "AFE硬件保护", "旧格式" }, SelectedIndex = 0 };
        _eventState = new ComboBox { Width = 115, Margin = new Thickness(8, 0, 0, 0), ItemsSource = new[] { "全部状态", "发生", "稳定恢复", "重复汇总", "普通事件", "旧格式" }, SelectedIndex = 0 };
        _eventSearch = new TextBox { Width = 150, Margin = new Thickness(8, 0, 0, 0), ToolTip = "按事件名称搜索" };
        filters.Children.Add(_eventSource); filters.Children.Add(_eventState); filters.Children.Add(_eventSearch);
        var reset = new Button { Content = "清除筛选", Margin = new Thickness(8, 0, 0, 0) }; filters.Children.Add(reset);
        reset.Click += (_, _) => { _eventFrom.SelectedDate = _eventTo.SelectedDate = null; _eventSource.SelectedIndex = _eventState.SelectedIndex = 0; _eventSearch.Text = ""; };
        _eventFrom.SelectedDateChanged += (_, _) => RefreshEventFilter(); _eventTo.SelectedDateChanged += (_, _) => RefreshEventFilter();
        _eventSource.SelectionChanged += (_, _) => RefreshEventFilter(); _eventState.SelectionChanged += (_, _) => RefreshEventFilter(); _eventSearch.TextChanged += (_, _) => RefreshEventFilter();
        toolbar.Children.Add(new TextBlock { Text = "D015保留最近100条重点事件；按顺序号排列。时间未同步时显示启动后秒数；日期筛选仅包含有日期的记录。", Foreground = Brushes.DimGray, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 0, 0, 8) });
#endif
        var grid = new DataGrid { AutoGenerateColumns = false, CanUserAddRows = false, CanUserDeleteRows = false, IsReadOnly = true, ItemsSource = _deviceEventLogs };
#if BMS_FACTORY_APP
        foreach (var col in new[] { ("序号", "Position", 55d), ("时间（本地）", "TimeText", 215d), ("事件", "EventName", 160d), ("状态", "StateText", 85d), ("来源", "SourceText", 110d), ("故障现场", "SnapshotText", 400d), ("明细", "DetailText", 220d) })
            grid.Columns.Add(new DataGridTextColumn { Header = col.Item1, Binding = new Binding(col.Item2), Width = col.Item3 });
        grid.Columns.Add(new DataGridTextColumn { Header = "等级", Binding = new Binding("LevelText"), Width = 60 });
        grid.Columns.Add(new DataGridTextColumn { Header = "顺序号", Binding = new Binding("Sequence"), Width = 90 });
        grid.Columns.Add(new DataGridTextColumn { Header = "原始标志", Binding = new Binding("EventFlags") { StringFormat = "0x{0:X2}" }, Width = 90 });
        CollectionViewSource.GetDefaultView(_deviceEventLogs).Filter = EventMatchesFilter;
#else
        grid.Columns.Add(new DataGridTextColumn { Header = "序号", Binding = new Binding(nameof(DeviceEventLogRow.Position)), Width = 70 });
        grid.Columns.Add(new DataGridTextColumn { Header = "事件", Binding = new Binding(nameof(DeviceEventLogRow.EventName)), Width = new DataGridLength(1, DataGridLengthUnitType.Star) });
#endif
        root.Children.Add(grid);
        MainTabs.Items.Insert(Math.Min(3, MainTabs.Items.Count), new TabItem { Header = "事件日志", Content = root });
    }
#if BMS_FACTORY_APP
    private bool EventMatchesFilter(object value)
    {
        if (value is not DeviceEventLogRow r) return false;
        if (_eventFrom?.SelectedDate is DateTime from && (r.UtcTime is null || r.UtcTime.Value.LocalDateTime.Date < from.Date)) return false;
        if (_eventTo?.SelectedDate is DateTime to && (r.UtcTime is null || r.UtcTime.Value.LocalDateTime.Date > to.Date)) return false;
        if (_eventSource?.SelectedIndex > 0 && r.SourceText != _eventSource.SelectedItem?.ToString()) return false;
        if (_eventState?.SelectedIndex > 0 && r.StateText != _eventState.SelectedItem?.ToString()) return false;
        return string.IsNullOrWhiteSpace(_eventSearch?.Text) || r.EventName.Contains(_eventSearch.Text.Trim(), StringComparison.OrdinalIgnoreCase);
    }
    private void RefreshEventFilter() => CollectionViewSource.GetDefaultView(_deviceEventLogs).Refresh();
#endif
    private async void ReadEventLogs_Click(object sender, RoutedEventArgs e)
    {
        if (_eventLogReadInProgress) return;
        _eventLogReadInProgress = true;
        if (sender is Button button) button.IsEnabled = false;
        try
        {
            _pollTimer.Stop(); await WaitForCommunicationIdleAsync();
            BmsClient bms = _bms ?? throw new InvalidOperationException("请先连接BMS。");
            _eventLogStatus!.Text = "正在读取…";
            IReadOnlyList<DeviceEventLogRow> rows; string status;
            if (bms.IsD015)
            {
                DeviceEventHistory history = await bms.ReadEventHistoryAsync(); rows = history.Records;
                status = $"已保存{rows.Count}/100条，待写{history.Pending}条";
                if ((history.Flags & 1) != 0) status += "；日志Flash写入或校验异常";
                if ((history.Flags & 8) != 0) status += "；等待安全擦页或空间回收";
                if (history.Dropped != 0) status += $"；本次运行入队失败{history.Dropped}次";
#if BMS_FACTORY_APP
                status += $"；写失败{history.Failures}次；擦除估计{history.ApproximateErases}次";
#endif
            }
            else
            {
                // 其他产品的现有接口保持原样，不向其写入D015寄存器。
                ushort[] words = await bms.ReadRegistersAsync(EventLogRegister, EventLogCount);
                rows = words.Select((w, i) => new DeviceEventLogRow(i + 1, DecodeLegacyEventName((byte)(w >> 8)), DecodeEventInterval((byte)w), "旧格式"))
                    .Where((r, i) => (words[i] >> 8) != 0).ToArray();
                status = $"已读取 {rows.Count} 条";
            }
            if (!ReferenceEquals(_bms, bms)) throw new IOException("连接已变化，未发布上一个设备的日志。");
            _deviceEventLogs.Clear(); foreach (var row in rows) _deviceEventLogs.Add(row);
#if BMS_FACTORY_APP
            _eventReadUtc = DateTimeOffset.UtcNow; _eventReadIdentity = IdentityText.Text ?? "";
#endif
            _eventLogStatus.Text = status;
            AppendLog($"EVENT_LOG_READ_OK count={rows.Count}", "LOG");
        }
        catch (Exception ex)
        {
            _deviceEventLogs.Clear();
            if (_eventLogStatus is not null) _eventLogStatus.Text = "读取失败，已清除旧显示";
            ShowError("读取设备事件日志失败", ex);
        }
        finally
        {
            _eventLogReadInProgress = false;
            if (sender is Button readButton) readButton.IsEnabled = true;
            StartAutomaticRefresh();
        }
    }
    private void ExportEventLogs_Click(object sender, RoutedEventArgs e)
    {
#if BMS_FACTORY_APP
        var rows = CollectionViewSource.GetDefaultView(_deviceEventLogs).Cast<DeviceEventLogRow>().ToArray();
#else
        var rows = _deviceEventLogs.ToArray();
#endif
        if (rows.Length == 0) { MessageBox.Show(this, "没有可导出的事件。", "事件日志"); return; }
        var dialog = new SaveFileDialog { Filter = "CSV文件|*.csv", FileName = $"BMS事件日志-{DateTime.Now:yyyyMMdd-HHmmss}.csv" };
        if (dialog.ShowDialog(this) != true) return;
#if BMS_FACTORY_APP
        string csv = DeviceEventCsv.Export(rows, _eventReadIdentity, _eventReadUtc);
#else
        string csv = ExportEventList(rows);
#endif
        try { File.WriteAllText(dialog.FileName, csv, new UTF8Encoding(true)); }
        catch (Exception ex) { ShowError("导出事件日志失败", ex); }
    }
#if !BMS_FACTORY_APP
    private static string ExportEventList(IEnumerable<DeviceEventLogRow> rows)
    {
        var csv = new StringBuilder();
        csv.AppendLine("序号,事件");
        foreach (var row in rows)
            csv.Append(row.Position).Append(",\"").Append(row.EventName.Replace("\"", "\"\"")).AppendLine("\"");
        return csv.ToString();
    }
#endif
    private static string DecodeLegacyEventName(byte id) => id switch { 17 => "CBC错误", 18 => "AFE1错误", 19 => "AFE2错误", 20 => "EEPROM错误", _ => BmsClient.EventName(id) };
    private static string DecodeEventInterval(byte code) => code switch { 0 => "启动记录 / 0", 171 => "≤1分钟", 170 => ">168小时", >= 1 and <= 168 => $"约{code}小时", _ => $"未知间隔编码{code}" };
}
