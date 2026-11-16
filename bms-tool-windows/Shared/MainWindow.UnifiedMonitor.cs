using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Threading;
using Microsoft.Win32;
using ClosedXML.Excel;

namespace BmsTool.Windows;

public partial class MainWindow
{
    private UnifiedMonitorView? _unifiedMonitor;
    private TabItem? _unifiedRecordTab;
    private readonly MonitorPlot _recordPlot = new() { Height = 300 };
    private readonly ComboBox _recordInterval = new() { Width = 90, ItemsSource = new[] { "1秒", "5秒", "10秒", "30秒", "60秒" }, SelectedIndex = 1 };
    private readonly ComboBox _owcModeBox = new() { Width = 150, ItemsSource = new[] { "自动识别", "A1.3.3", "锂神（含本项目扩展）" }, SelectedIndex = 0 };
    private readonly TextBlock _recordStatus = new() { Text = "未开始", TextWrapping = TextWrapping.Wrap, Margin = new(0, 12, 0, 12) };
    private readonly TextBlock _recordFileStatus = new() { Foreground = Brushes.SlateGray, FontSize = 12, TextWrapping = TextWrapping.Wrap };
    private readonly TextBox _recordNote = new() { MinWidth = 180, MaxLength = 2000, Margin = new(8, 0, 8, 0) };
    private Button? _recordStart, _recordStop, _recordExport;
    private MonitorReading? _monitorReading;
    private readonly MonitorTrend _trend = new();
    private MonitorRecording? _recording;
    private string? _recordDirectory, _lastTrendSequence;
    private string? _monitorError, _recordError, _recordNotice;
    private bool _recordExportBusy, _recordHistoryBusy;
    private bool _trendGap, _recordStopping, _unifiedClosing, _unifiedCloseReady;
    private int _unifiedModeIndex;
    private readonly Dictionary<TabItem, Visibility> _owcHiddenTabs = new();
    private Task? _integratedOwcTask, _recordStopTask, _recordExportTask;
    private CancellationTokenSource? _integratedOwcStop;
    private long _integratedOwcEpoch;
    private DispatcherTimer? _unifiedTimer;
    private bool IsOwcMode => ConnectionModeBox.SelectedIndex == 2;
    private static bool UnifiedTestEdition =>
#if BMS_FACTORY_APP
        true;
#else
        false;
#endif

    private void InitializeUnifiedMonitor()
    {
        var tab = MainTabs.Items.OfType<TabItem>().First(t => Equals(t.Header, "实时监控"));
        // 保留现有命名控件及其业务事件；主页面替换显示，不复制连接/参数实现。
        _unifiedMonitor = new UnifiedMonitorView(UnifiedTestEdition);
        var connectionControls = new FrameworkElement[] { ConnectionModeBox, DeviceList, SerialPortList,
            SerialBaudRateBox, ScanButton, StopScanButton, ConnectButton, CancelConnectionButton };
        foreach (var control in connectionControls)
        {
            if (control.Parent is Panel parent) parent.Children.Remove(control);
            control.Margin = new(0, 3, 7, 3);
            if (control is Button button)
            {
                button.Background = Brushes.White;
                button.BorderBrush = new SolidColorBrush(Color.FromRgb(215, 225, 238));
            }
            _unifiedMonitor.ConnectionHost.Children.Add(control);
        }
        ConnectButton.Background = new SolidColorBrush(Color.FromRgb(37, 99, 235));
        ConnectButton.Foreground = Brushes.White;
        ((ComboBoxItem)ConnectionModeBox.Items[0]).Content = "蓝牙";
        ConnectionModeBox.Items.Add(new ComboBoxItem { Content = "一线通" });
        DeviceList.Width = 230; SerialPortList.Width = 150;
#if BMS_FACTORY_APP
        if (ShowHistoryDevicesBox.Parent is Panel historyParent) historyParent.Children.Remove(ShowHistoryDevicesBox);
        _unifiedMonitor.ConnectionHost.Children.Add(ShowHistoryDevicesBox);
#endif
        _unifiedMonitor.ConnectionHost.Children.Add(_owcModeBox);
        _unifiedMonitor.ConnectionHost.Children.Add(_unifiedMonitor.Disconnect);
        _unifiedMonitor.Disconnect.Click += CustomerDisconnect_Click;
#if !BMS_FACTORY_APP
        _unifiedMonitor.ConnectionHost.Children.Add(_unifiedMonitor.Advanced);
        _unifiedMonitor.Advanced.Click += OpenProtectedFeatures_Click;
#endif
        _unifiedMonitor.StartRecord.Click += UnifiedRecordStart_Click;
        _unifiedMonitor.RecordsButton.Click += (_, _) => MainTabs.SelectedItem = _unifiedRecordTab;
        tab.Header = "实时总览"; tab.Content = _unifiedMonitor;
        MainTabs.TabStripPlacement = Dock.Left;
        MainTabs.Background = new SolidColorBrush(Color.FromRgb(249, 251, 254));
        MainTabs.BorderThickness = new Thickness(0); MainTabs.Padding = new Thickness(0);
        var style = new Style(typeof(TabItem));
        style.Setters.Add(new Setter(Control.PaddingProperty, new Thickness(13, 12, 13, 12)));
        style.Setters.Add(new Setter(Control.MinWidthProperty, 132.0));
        style.Setters.Add(new Setter(Control.FontSizeProperty, 13.0));
        var navTemplate = new ControlTemplate(typeof(TabItem));
        var navBorder = new FrameworkElementFactory(typeof(Border)) { Name = "NavBorder" };
        navBorder.SetValue(Border.CornerRadiusProperty, new CornerRadius(6));
        navBorder.SetValue(Border.PaddingProperty, new Thickness(14, 12, 14, 12));
        navBorder.SetValue(FrameworkElement.MarginProperty, new Thickness(7, 4, 7, 4));
        var label = new FrameworkElementFactory(typeof(ContentPresenter)) { Name = "NavLabel" };
        label.SetValue(System.Windows.Documents.TextElement.ForegroundProperty, new SolidColorBrush(Color.FromRgb(94,116,143)));
        label.SetValue(ContentPresenter.ContentSourceProperty, "Header");
        label.SetValue(FrameworkElement.HorizontalAlignmentProperty, HorizontalAlignment.Left);
        navBorder.AppendChild(label); navTemplate.VisualTree = navBorder;
        var selected = new Trigger { Property = TabItem.IsSelectedProperty, Value = true };
        selected.Setters.Add(new Setter(Border.BackgroundProperty, new SolidColorBrush(Color.FromRgb(233, 241, 255)), "NavBorder"));
        selected.Setters.Add(new Setter(System.Windows.Documents.TextElement.ForegroundProperty, new SolidColorBrush(Color.FromRgb(37,87,179)), "NavLabel"));
        selected.Setters.Add(new Setter(System.Windows.Documents.TextElement.FontWeightProperty, FontWeights.SemiBold, "NavLabel"));
        navTemplate.Triggers.Add(selected);
        style.Setters.Add(new Setter(Control.TemplateProperty, navTemplate));

        MainTabs.ItemContainerStyle = style;
        var tabsTemplate = new ControlTemplate(typeof(TabControl));
        var shell = new FrameworkElementFactory(typeof(DockPanel));
        shell.SetValue(Panel.BackgroundProperty, Brushes.White);
        var nav = new FrameworkElementFactory(typeof(Border));
        nav.SetValue(FrameworkElement.WidthProperty, 150.0);
        nav.SetValue(DockPanel.DockProperty, Dock.Left);
        nav.SetValue(Border.BackgroundProperty, new SolidColorBrush(Color.FromRgb(249,251,254)));
        nav.AppendChild(new FrameworkElementFactory(typeof(ItemsPresenter)));
        shell.AppendChild(nav);
        var selectedHost = new FrameworkElementFactory(typeof(ContentPresenter)) { Name = "PART_SelectedContentHost" };
        selectedHost.SetValue(ContentPresenter.ContentSourceProperty, "SelectedContent");
        shell.AppendChild(selectedHost); tabsTemplate.VisualTree = shell; MainTabs.Template = tabsTemplate;
        if (CustomerChargeMosLamp.Parent is StackPanel charge &&
            charge.Parent is Grid stateGrid && stateGrid.Parent is GroupBox group)
        {
            if (group.Parent is Panel old) old.Children.Remove(group);
            _unifiedMonitor.Operations.Children.Add(group);
        }
        var sleep = UnifiedMonitorView.MakeButton("设备休眠");
        sleep.HorizontalAlignment = HorizontalAlignment.Left;
        sleep.Click += SleepBms_Click; _unifiedMonitor.Operations.Children.Add(sleep);
        BuildUnifiedRecordPage();
        MainTabs.Items.Insert(1, _unifiedRecordTab);
        _unifiedTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(500) };
        _unifiedTimer.Tick += (_, _) => UpdateUnifiedMonitorState();
        _unifiedTimer.Start();
        Closing += UnifiedMonitor_Closing;
        Closed += (_, _) => _unifiedTimer.Stop();
        Loaded += (_, _) => Title = UnifiedTestEdition ? "BMS Assistant · 测试版" : "BMS Assistant";
        UpdateUnifiedMonitorState();
    }

    private void BuildUnifiedRecordPage()
    {
        var body = new StackPanel { Margin = new(24) };
        body.Children.Add(new TextBlock { Text = "趋势与记录", FontSize = 24, FontWeight = FontWeights.SemiBold, Margin = new(0, 0, 0, 20) });
        var controls = new WrapPanel();
        controls.Children.Add(new TextBlock { Text = "记录间隔", VerticalAlignment = VerticalAlignment.Center, Margin = new(0, 0, 8, 0) });
        controls.Children.Add(_recordInterval);
        _recordStart = UnifiedMonitorView.MakeButton("开始记录", true); _recordStart.Click += UnifiedRecordStart_Click;
        _recordStop = UnifiedMonitorView.MakeButton("结束记录"); _recordStop.Click += async (_, _) => await StopUnifiedRecordingAsync();
        _recordExport = UnifiedMonitorView.MakeButton("导出记录"); _recordExport.Click += UnifiedRecordExport_Click;
        controls.Children.Add(_recordStart); controls.Children.Add(_recordStop); controls.Children.Add(_recordExport);
        var history = UnifiedMonitorView.MakeButton("打开历史"); history.Click += UnifiedHistory_Click; controls.Children.Add(history);
        var folder = UnifiedMonitorView.MakeButton("记录目录"); folder.Click += (_, _) => {
            if (_recordDirectory is not null) Process.Start(new ProcessStartInfo(_recordDirectory) { UseShellExecute = true });
        }; controls.Children.Add(folder);
        body.Children.Add(controls); body.Children.Add(_recordStatus); body.Children.Add(_recordFileStatus);
        var chartTools = new WrapPanel { Margin = new(0, 18, 0, 10) };
        var metric = new ComboBox { Width = 100, ItemsSource = new[] { "总压", "电流", "温度", "SOC" }, SelectedIndex = 0 };
        metric.SelectionChanged += (_, _) => { _recordPlot.Metric = metric.SelectedItem?.ToString() ?? "总压"; _recordPlot.InvalidateVisual(); };
        chartTools.Children.Add(metric);
        var range = new ComboBox { Width = 100, Margin = new(10, 0, 0, 0), ItemsSource = new[] { "30分钟", "2小时", "24小时" }, SelectedIndex = 0 };
        range.SelectionChanged += (_, _) => { _recordPlot.Range = range.SelectedIndex switch { 1 => TimeSpan.FromHours(2), 2 => TimeSpan.FromHours(24), _ => TimeSpan.FromMinutes(30) }; _recordPlot.InvalidateVisual(); };
        chartTools.Children.Add(range);
        body.Children.Add(chartTools); body.Children.Add(UnifiedMonitorView.Panel(_recordPlot));
        var notes = new WrapPanel { Margin = new(0, 20, 0, 0) };
        notes.Children.Add(new TextBlock { Text = "事件备注", VerticalAlignment = VerticalAlignment.Center });
        notes.Children.Add(_recordNote);
        var addNote = UnifiedMonitorView.MakeButton("添加标记");
        addNote.Click += (_, _) => {
            try { if (_recording?.IsRunning != true) { _recordStatus.Text = "请先开始记录"; return; }
                _recording.Note(_recordNote.Text); _recordNote.Clear(); _recordStatus.Text = "已添加事件标记"; }
            catch (Exception ex) { HandleUnifiedRecordError(ex); }
        };
        notes.Children.Add(addNote); body.Children.Add(notes);
        _unifiedRecordTab = new TabItem { Header = "趋势与记录", Content = new ScrollViewer {
            Content = body, VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
            Background = new SolidColorBrush(Color.FromRgb(243, 246, 250)) } };
    }

    private bool TryChangeUnifiedMode()
    {
        if (_unifiedMonitor is null) return true;
        if (ConnectionModeBox.SelectedIndex != _unifiedModeIndex &&
            (_integratedOwcTask is not null || _bmsTransport is not null || _manualConnectionInProgress ||
             _autoReconnectRunning || _recording?.IsRunning == true))
        {
            ConnectionModeBox.SelectedIndex = _unifiedModeIndex;
            _unifiedMonitor.Status.Text = "请先结束记录并断开当前连接";
            return false;
        }
        _unifiedModeIndex = ConnectionModeBox.SelectedIndex;
        UpdateUnifiedMonitorState(); return true;
    }

    private void PublishUnifiedBattery(BatterySnapshot sample)
    {
        string endpoint = _connectedSerialPort ?? _connectedAddress?.ToString("X12") ?? "";
        var cells = sample.CellMillivolts.Select((mv, i) => (mv, i))
            .Where(x => x.mv != BmsRegisters.MissingCellVoltageMv && x.mv > 0 && x.mv <= 10000)
            .ToDictionary(x => x.i + 1, x => (double)x.mv);
        var observedUtc = DateTimeOffset.UtcNow;
        PublishUnifiedReading(new MonitorReading {
            ObservedUtc = observedUtc, CellsUtc = sample.CellsReadUtc == default ? null : sample.CellsReadUtc,
            Source = _connectionMode == ConnectionMode.Serial ? "串口" : "蓝牙", Endpoint = endpoint,
            Protocol = _bms?.DeviceModel ?? "BMS", SegmentKey = $"{endpoint}/{_connectionGeneration}",
            Sequence = $"{_connectionGeneration}/{observedUtc.UtcTicks}",
            Soc = sample.SocPercent is >= 0 and <= 100 ? sample.SocPercent : null,
            Voltage = double.IsFinite(sample.PackVoltageV) ? sample.PackVoltageV : null,
            Current = double.IsFinite(sample.CurrentA) ? sample.CurrentA : null,
            Temperature = double.IsFinite(sample.MaxTempC) ? sample.MaxTempC : null,
            MinTemperature = sample.MinTempC, MosTemperature = sample.MosTempC, Cycles = sample.CycleCount,
            FaultCode = (sample.ProtectionLevel1Raw | sample.ProtectionLevel2Raw | sample.ProtectionLevel3Raw) == 0 ? (byte)0 : (byte)255,
            Fault = string.Join("；", new[] { sample.ProtectionLevel1Text, sample.ProtectionLevel2Text, sample.ProtectionLevel3Text }
                .Where(x => !string.IsNullOrWhiteSpace(x) && x != "无")),
            HasCurrent = true, HasFault = true, HasCells = true, Cells = cells
        });
    }

    private void PublishUnifiedReading(MonitorReading? reading)
    {
        if (reading is not null && !reading.IsFresh) reading = null;
        _monitorReading = reading;
        _unifiedMonitor?.SetReading(reading);
        if (reading is null)
        {
            if (!_trendGap) _trend.Add(new("gap", DateTimeOffset.UtcNow, 0, null, "数据中断"));
            _trendGap = true; _lastTrendSequence = null;
        }
        else if (_lastTrendSequence != reading.SegmentKey + reading.Sequence)
        {
            _trend.Add(new("sample", reading.ObservedUtc, reading.SegmentKey.GetHashCode(), reading, null));
            _lastTrendSequence = reading.SegmentKey + reading.Sequence; _trendGap = false;
        }
        if (_unifiedMonitor is not null) { _unifiedMonitor.Plot.Entries = _trend.ToArray(); _unifiedMonitor.Plot.InvalidateVisual(); }
        if (_recording?.IsRunning == true)
        { _recordPlot.Entries = _trend.ToArray(); _recordPlot.InvalidateVisual(); }
    }

    private void UpdateUnifiedMonitorState()
    {
        if (_unifiedMonitor is null) return;
        bool busy = _manualConnectionInProgress || _autoReconnectRunning || _otaRunning || _connectionOperations.IsBusy || ShFactoryBusy();
        bool owc = _integratedOwcTask is not null;
        _owcModeBox.Visibility = IsOwcMode && UnifiedTestEdition ? Visibility.Visible : Visibility.Collapsed;
        _owcModeBox.IsEnabled = !owc && !busy && _recording?.IsRunning != true;
        ConnectionModeBox.IsEnabled = !owc && !busy && _bmsTransport is null && _recording?.IsRunning != true;
        _unifiedMonitor.Disconnect.IsEnabled = owc || _bmsTransport is not null || _connectionDesired || busy;
        _unifiedMonitor.OperationsContainer.Visibility = IsOwcMode ? Visibility.Collapsed : Visibility.Visible;
        CancelConnectionButton.Visibility = _manualConnectionInProgress ? Visibility.Visible : Visibility.Collapsed;
        if (!IsOwcMode && !IsSerialConnectionMode) StopScanButton.Visibility = _bleScanRunning ? Visibility.Visible : Visibility.Collapsed;
#if !BMS_FACTORY_APP
        _unifiedMonitor.Advanced.Visibility = IsOwcMode ? Visibility.Collapsed : Visibility.Visible;
        _unifiedMonitor.Advanced.Content = ProtectedFeaturesButton.Content;
#endif
        _unifiedMonitor.Disconnect.Visibility = _unifiedMonitor.Disconnect.IsEnabled ? Visibility.Visible : Visibility.Collapsed;
        if (IsOwcMode)
        {
            DeviceList.Visibility = SerialBaudRateBox.Visibility = StopScanButton.Visibility = Visibility.Collapsed;
            SerialPortList.Visibility = Visibility.Visible; ScanButton.Content = "刷新";
            ScanButton.IsEnabled = ConnectButton.IsEnabled = !owc && !busy;
            SerialPortList.IsEnabled = !owc && !busy;
        }
        else SerialPortList.IsEnabled = true;
        if (_monitorReading is not null && !_monitorReading.IsFresh) PublishUnifiedReading(null);
        _unifiedMonitor.Status.Text = _monitorReading is not null ? "● 数据在线" :
            owc ? (_monitorError ?? "适配器接收中 · 等待 BMS 数据") :
            busy ? "正在处理连接或设备任务…" : _bmsTransport is not null ? "连接已建立 · 等待有效数据" :
            _bleScanRunning && !IsOwcMode && !IsSerialConnectionMode ? $"正在搜索 · {_devices.Count} 台设备" :
            _monitorError ?? _bleScanProblem ?? "未连接";
        _unifiedMonitor.Status.ToolTip = _monitorError ?? ConnectionText.Text;
        _unifiedMonitor.Status.Foreground = _monitorReading is null ? Brushes.SlateGray : new SolidColorBrush(Color.FromRgb(22, 130, 87));
        foreach (TabItem tab in MainTabs.Items.OfType<TabItem>())
        {
            if (owc && tab != _unifiedRecordTab && !ReferenceEquals(tab.Content, _unifiedMonitor))
            {
                if (!_owcHiddenTabs.ContainsKey(tab)) _owcHiddenTabs[tab] = tab.Visibility;
                tab.Visibility = Visibility.Collapsed;
            }
            else if (!owc && _owcHiddenTabs.Remove(tab, out var visible)) tab.Visibility = visible;
            if (!UnifiedTestEdition && Equals(tab.Header, "BMS 诊断")) tab.Visibility = Visibility.Collapsed;
        }
        if (_recording is not null && !_recordStopping)
        {
            try { _recording.Capture(_monitorReading); }
            catch (Exception ex) { HandleUnifiedRecordError(ex); }
        }
        bool running = _recording?.IsRunning == true;
        string status = _recording is null ? (_recordDirectory is null ? "未开始" : "记录已结束") :
            _recording.Error is Exception failure ? "记录失败：" + failure.Message :
            _recordStopping ? "正在保存记录…" : !_recording.IsRunning ? "记录已停止" :
            $"正在记录 · {_recording.Elapsed:hh\\:mm\\:ss} · 已写入 {_recording.Written:N0} 条";
        _unifiedMonitor.RecordingStatus.Text = _recordError ?? status;
        _unifiedMonitor.RecordingStatus.Foreground = _recordError is null ? Brushes.SlateGray : Brushes.Firebrick;
        _recordFileStatus.Text = _recordDirectory ?? "开始后自动保存到所选目录";
        if (!_recordStopping) _recordStatus.Text = _recordError ?? (_recordHistoryBusy ? "正在读取历史…" : _recordExportBusy ? "正在导出…" : running ? status : _recordNotice ?? status);
        if (_recordStart is not null) _recordStart.IsEnabled = !running && !_recordStopping && !_recordExportBusy && !_recordHistoryBusy;
        if (_recordStop is not null) _recordStop.IsEnabled = running && !_recordStopping;
        if (_recordExport is not null) _recordExport.IsEnabled = !running && !_recordStopping && !_recordExportBusy && !_recordHistoryBusy && _recordDirectory is not null;
        _recordInterval.IsEnabled = !running && !_recordStopping && !_recordExportBusy && !_recordHistoryBusy;
        _unifiedMonitor.StartRecord.IsEnabled = !running && !_recordStopping && !_recordExportBusy && !_recordHistoryBusy;
    }

    private async void UnifiedRecordStart_Click(object sender, RoutedEventArgs e)
    {
        if (_recording?.IsRunning == true || _recordStopping || _recordExportBusy || _recordHistoryBusy) { MainTabs.SelectedItem = _unifiedRecordTab; return; }
        if (_bmsTransport is null && _integratedOwcTask is null) { _recordStatus.Text = "请先连接设备"; MainTabs.SelectedItem = _unifiedRecordTab; return; }
        var dialog = new OpenFolderDialog { Title = "选择记录保存位置" };
        if (dialog.ShowDialog(this) != true) return;
        try
        {
            if (_recording is not null) await StopUnifiedRecordingAsync();
            _recordError = _recordNotice = null;
            _recording = new MonitorRecording(dialog.FolderName, new[] { 1, 5, 10, 30, 60 }[_recordInterval.SelectedIndex]);
            _recordDirectory = _recording.DirectoryPath;
            _recording.Capture(_monitorReading);
            _recordPlot.Entries = _trend.ToArray();
            MainTabs.SelectedItem = _unifiedRecordTab;
            UpdateUnifiedMonitorState();
        }
        catch (Exception ex) { HandleUnifiedRecordError(ex); }
    }

    private void HandleUnifiedRecordError(Exception ex)
    {
        _recordError = "记录已停止：" + ex.Message;
        _recordStatus.Text = _recordError;
        AppendLog(_recordError, "RECORD");
        if (!_recordStopping) _ = StopUnifiedRecordingAsync();
    }

    private Task StopUnifiedRecordingAsync()
    {
        if (_recordStopTask is { IsCompleted: false }) return _recordStopTask;
        if (_recording is null) return Task.CompletedTask;
        _recordStopping = true;
        return _recordStopTask = FinishUnifiedRecordingAsync(_recording);
    }

    private async Task FinishUnifiedRecordingAsync(MonitorRecording recording)
    {
        _recordStatus.Text = "正在保存记录…";
        try { await recording.StopAsync(); }
        catch (Exception ex) { _recordError = "记录保存失败，已有文件保留：" + ex.Message; AppendLog(_recordError, "ERROR"); }
        finally { _recording = null; _recordStopping = false; UpdateUnifiedMonitorState(); }
    }

    private async void UnifiedHistory_Click(object sender, RoutedEventArgs e)
    {
        if (_recording?.IsRunning == true || _recordStopping || _recordExportBusy || _recordHistoryBusy) { _recordStatus.Text = "请先结束当前记录"; return; }
        var dialog = new OpenFolderDialog { Title = "选择一条 BMS 记录目录" };
        if (dialog.ShowDialog(this) != true) return;
        _recordHistoryBusy = true;
        try
        {
            var entries = await Task.Run(() => {
                var history = new MonitorTrend();
                foreach (var entry in MonitorRecording.Read(dialog.FolderName)) history.Add(entry);
                return history.ToArray();
            });
            if (_unifiedClosing || _bleWindowClosed) return;
            if (entries.Length == 0) throw new IOException("目录内没有记录");
            _recordDirectory = dialog.FolderName; _recordPlot.Entries = entries; _recordPlot.InvalidateVisual();
            _recordFileStatus.Text = dialog.FolderName; _recordNotice = _recordStatus.Text = "历史记录已载入 · 最近24小时，较早数据按30秒抽样" + (entries.Any(e => e.Kind == "recovery_gap") ? " · 含异常中断缺口" : "");
        }
        catch (Exception ex) { _recordNotice = _recordStatus.Text = "打开记录失败：" + ex.Message; }
        finally { _recordHistoryBusy = false; UpdateUnifiedMonitorState(); }
    }

    private async void UnifiedRecordExport_Click(object sender, RoutedEventArgs e)
    {
        if (_recordDirectory is null || _recording?.IsRunning == true || _recordStopping || _recordExportBusy) return;
        var dialog = new SaveFileDialog { Filter = "CSV (*.csv)|*.csv|Excel (*.xlsx)|*.xlsx", FileName = $"BMS-{DateTime.Now:yyyyMMdd-HHmmss}", AddExtension = true };
        if (dialog.ShowDialog(this) != true) return;
        _recordExportBusy = true; _recordExport!.IsEnabled = false; _recordStatus.Text = "正在导出…";
        string directory = _recordDirectory;
        try
        {
            _recordExportTask = Task.Run(() => {
                if (!dialog.FileName.EndsWith(".xlsx", StringComparison.OrdinalIgnoreCase))
                    MonitorRecording.ExportCsv(directory, dialog.FileName);
                else
                {
                    ExportUnifiedWorkbook(directory, dialog.FileName);
                }
            });
            await _recordExportTask;
            _recordNotice = _recordStatus.Text = "导出完成：" + dialog.FileName;
        }
        catch (Exception ex) { _recordNotice = _recordStatus.Text = "导出失败：" + ex.Message; }
        finally { _recordExportBusy = false; UpdateUnifiedMonitorState(); }
    }

    internal static void ExportUnifiedWorkbook(string directory, string destination)
    {
        using var workbook = new XLWorkbook();
        var sheet = workbook.AddWorksheet("记录");
        string[] headers = ["UTC", "类型", "段", "来源", "协议", "SOC %", "总压 V", "电流 A", "温度 ℃", "故障", "备注", "电流分辨率 A", "单体 mV（JSON）", "单体更新时间 UTC", "循环次数"];
        for (int col = 0; col < headers.Length; col++) sheet.Cell(1, col + 1).Value = headers[col];
        int row = 2;
        foreach (var entry in MonitorRecording.Read(directory))
        {
            if (row > 100001) throw new IOException("Excel单次最多导出100000条，请选择CSV导出完整记录");
            var r = entry.Reading;
            sheet.Cell(row, 1).Value = entry.Utc.ToString("O"); sheet.Cell(row, 2).Value = entry.Kind;
            sheet.Cell(row, 3).Value = entry.Segment; sheet.Cell(row, 4).Value = r?.Source;
            sheet.Cell(row, 5).Value = r?.Protocol;
            if (r?.Soc is double soc) sheet.Cell(row, 6).Value = soc;
            if (r?.Voltage is double voltage) sheet.Cell(row, 7).Value = voltage;
            if (r?.Current is double current) sheet.Cell(row, 8).Value = current;
            if (r?.Temperature is double temperature) sheet.Cell(row, 9).Value = temperature;
            sheet.Cell(row, 10).Value = r?.Fault; sheet.Cell(row, 11).Value = entry.Note;
                        if (r is not null)
                        {
                            sheet.Cell(row,12).Value = r.CurrentResolutionA;
                            sheet.Cell(row,13).Value = System.Text.Json.JsonSerializer.Serialize(r.Cells);
                            sheet.Cell(row,14).Value = r.CellsUtc?.ToString("O");
                            if (r.Cycles is int cycles) sheet.Cell(row,15).Value = cycles;
                        }
                        ++row;
        }
        sheet.SheetView.FreezeRows(1); workbook.SaveAs(destination);
    }

    private async Task StartIntegratedOwcAsync()
    {
        if (_integratedOwcTask is not null || _manualConnectionInProgress || _otaRunning || _polling ||
            _autoReconnectRunning || _connectionOperations.IsBusy || _bmsTransport is not null || ShFactoryBusy())
            return;
        if (SerialPortList.SelectedItem is not SerialPortEndpoint endpoint) { _monitorError = "请先刷新并选择 Comm Tool 串口"; UpdateUnifiedMonitorState(); return; }
        _pollTimer.Stop(); StopBleDiscovery();
        _connectionDesired = false; _monitorError = null; ClearDeviceDisplays();
        _integratedOwcStop = new CancellationTokenSource();
        var token = _integratedOwcStop.Token; long epoch = ++_integratedOwcEpoch;
        string port = endpoint.PortName;
        var mode = UnifiedTestEdition ? (OwcReceiveMode)_owcModeBox.SelectedIndex : OwcReceiveMode.Auto;
        _integratedOwcTask = Task.Run(async () => {
            while (!token.IsCancellationRequested)
            {
                try
                {
                    using var client = new CommToolOwcClient(port);
                    try
                    {
                        client.Start(mode, token);
                        while (!token.IsCancellationRequested)
                        {
                            var snapshots = client.ReadSnapshots(token);
                            if (!snapshots[0].Enabled) client.SetEnabled(true, token);
                            var reading = MonitorReading.FromOwc(snapshots, port);
                            await Dispatcher.InvokeAsync(() => {
                                if (epoch != _integratedOwcEpoch || token.IsCancellationRequested) return;
                                _monitorError = null; PublishUnifiedReading(reading);
                                if (_unifiedMonitor is not null && UnifiedTestEdition)
                                    _unifiedMonitor.Raw.Text = string.Join(Environment.NewLine, snapshots
                                        .Where(x => x.Value.Raw.Length != 0)
                                        .Select(x => $"v{x.Value.Version} session={x.Value.Session} seq={x.Value.Sequence} age={x.Value.AgeMs}ms good={x.Value.Good} SUM={x.Value.ChecksumErrors} timing={x.Value.TimingErrors} | {Convert.ToHexString(x.Value.Raw)}"));
                            });
                            await Task.Delay(1000, token);
                        }
                    }
                    finally { try { client.SetEnabled(false, CancellationToken.None); } catch (Exception ex) { _sessionLog.WriteException("OWC", "停止接收未确认，等待设备租约释放", ex); } }
                }
                catch (OperationCanceledException) when (token.IsCancellationRequested) { break; }
                catch (Exception ex)
                {
                    await Dispatcher.InvokeAsync(() => {
                        if (epoch != _integratedOwcEpoch) return;
                        _monitorError = "接收中断，正在重试；详情见日志";
                        PublishUnifiedReading(null); AppendLog("一线通：" + ex.Message, "OWC");
                    });
                    try { await Task.Delay(3000, token); } catch (OperationCanceledException) { break; }
                }
            }
        });
        UpdateUnifiedMonitorState();
        await Task.CompletedTask;
    }

    private async Task StopIntegratedOwcAsync()
    {
        if (_integratedOwcTask is null) return;
        ++_integratedOwcEpoch; var task = _integratedOwcTask;
        _integratedOwcStop?.Cancel(); PublishUnifiedReading(null);
        try { await task; } catch (OperationCanceledException) { }
        finally { _integratedOwcTask = null; _integratedOwcStop?.Dispose(); _integratedOwcStop = null; _monitorError = null; UpdateUnifiedMonitorState(); }
    }

    private async void UnifiedMonitor_Closing(object? sender, System.ComponentModel.CancelEventArgs e)
    {
        if (_unifiedCloseReady || (_integratedOwcTask is null && _recording is null && !_recordStopping && !_recordExportBusy)) return;
        e.Cancel = true;
        if (_unifiedClosing) return;
        _unifiedClosing = true;
        await StopIntegratedOwcAsync();
        await StopUnifiedRecordingAsync();
        if (_recordExportTask is not null)
        {
            try { await _recordExportTask; } catch (Exception ex) { AppendLog("关闭时导出失败：" + ex.Message, "ERROR"); }
        }
        _unifiedCloseReady = true; Close();
    }
}
