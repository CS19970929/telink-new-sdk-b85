using System.Collections.ObjectModel;
using System.Globalization;
using System.IO;
using System.Text.RegularExpressions;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Media;
using System.Windows.Threading;
using ClosedXML.Excel;
using Microsoft.Win32;

namespace BmsTool.Windows;

public partial class MainWindow
{
    private readonly AfeHardwareParameterModel _afeHardwareModel = new();
    private readonly ObservableCollection<DeviceEventLogRow> _deviceEventLogs = new();
    private readonly List<LongTermMonitorRecord> _longTermRecords = new();

    private bool _advancedFeaturesInitialized;
    private DataGrid? _afeHardwareGrid;
    private TextBlock? _afeHardwareStatus;
    private TextBlock? _eventLogStatus;
    private TextBlock? _longTermStatus;
    private ComboBox? _longTermIntervalBox;
    private Button? _longTermStartButton;
    private Button? _longTermStopButton;
    private Button? _longTermExportButton;
    private DispatcherTimer? _advancedFeatureTimer;
    private bool _longTermMonitoring;
    private DateTime _longTermStartedAt;
    private DateTime _longTermLastRecordAt = DateTime.MinValue;
    private string _longTermLastUpdateToken = string.Empty;

    static MainWindow()
    {
        EventManager.RegisterClassHandler(
            typeof(MainWindow),
            FrameworkElement.LoadedEvent,
            new RoutedEventHandler(AdvancedFeatures_ClassLoaded),
            true);
    }

    private static void AdvancedFeatures_ClassLoaded(object sender, RoutedEventArgs e)
    {
        if (sender is not MainWindow window) return;
        window.Dispatcher.BeginInvoke(() => window.InitializeAdvancedFeatures(), DispatcherPriority.Loaded);
    }

    private void InitializeAdvancedFeatures()
    {
        if (_advancedFeaturesInitialized) return;
        _advancedFeaturesInitialized = true;

        AddLongTermMonitorToolbarToMainPage();
        AddAfeHardwareTab();
        AddEventLogTab();

        _advancedFeatureTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(500) };
        _advancedFeatureTimer.Tick += (_, _) => AdvancedFeatureTimer_Tick();
        _advancedFeatureTimer.Start();

        AppendLog("高级功能已启用：SH367309 AFE硬件保护参数、100条设备事件日志、长期监控Excel导出。", "APP");
    }

    private void AddLongTermMonitorToolbarToMainPage()
    {
        TabItem? realtimeTab = MainTabs.Items.OfType<TabItem>()
            .FirstOrDefault(t => string.Equals(t.Header?.ToString(), "实时监控", StringComparison.Ordinal));
        if (realtimeTab?.Content is not UIElement existing) return;

        realtimeTab.Content = null;
        var wrapper = new DockPanel();
        var toolbar = BuildLongTermMonitorToolbar();
        DockPanel.SetDock(toolbar, Dock.Top);
        wrapper.Children.Add(toolbar);
        wrapper.Children.Add(existing);
        realtimeTab.Content = wrapper;
    }

    private Border BuildLongTermMonitorToolbar()
    {
        var border = new Border
        {
            Background = Brushes.White,
            BorderBrush = new SolidColorBrush(Color.FromRgb(205, 205, 205)),
            BorderThickness = new Thickness(1),
            Padding = new Thickness(8, 5, 8, 5),
            Margin = new Thickness(6, 2, 6, 4)
        };
        var panel = new StackPanel { Orientation = Orientation.Horizontal };
        panel.Children.Add(new TextBlock
        {
            Text = "长期监控",
            FontWeight = FontWeights.SemiBold,
            VerticalAlignment = VerticalAlignment.Center,
            Margin = new Thickness(0, 0, 12, 0)
        });
        panel.Children.Add(new TextBlock { Text = "记录间隔", VerticalAlignment = VerticalAlignment.Center });

        _longTermIntervalBox = new ComboBox { Width = 82, Height = 28, Margin = new Thickness(6, 0, 10, 0) };
        foreach (var item in new[] { ("1秒", 1), ("5秒", 5), ("10秒", 10), ("30秒", 30), ("60秒", 60) })
            _longTermIntervalBox.Items.Add(new ComboBoxItem { Content = item.Item1, Tag = item.Item2, IsSelected = item.Item2 == 5 });
        panel.Children.Add(_longTermIntervalBox);

        _longTermStartButton = new Button { Content = "开始监控", Width = 88, Height = 28, Margin = new Thickness(0, 0, 6, 0) };
        _longTermStartButton.Click += LongTermStart_Click;
        panel.Children.Add(_longTermStartButton);

        _longTermStopButton = new Button { Content = "停止", Width = 68, Height = 28, Margin = new Thickness(0, 0, 6, 0), IsEnabled = false };
        _longTermStopButton.Click += LongTermStop_Click;
        panel.Children.Add(_longTermStopButton);

        _longTermExportButton = new Button { Content = "导出 Excel", Width = 96, Height = 28, Margin = new Thickness(0, 0, 12, 0) };
        _longTermExportButton.Click += LongTermExport_Click;
        panel.Children.Add(_longTermExportButton);

        _longTermStatus = new TextBlock
        {
            Text = "未开始",
            VerticalAlignment = VerticalAlignment.Center,
            Foreground = new SolidColorBrush(Color.FromRgb(80, 80, 80))
        };
        panel.Children.Add(_longTermStatus);
        border.Child = panel;
        return border;
    }

    private void AddAfeHardwareTab()
    {
        if (MainTabs.Items.OfType<TabItem>().Any(t => string.Equals(t.Header?.ToString(), "AFE硬件保护", StringComparison.Ordinal))) return;

        var tab = new TabItem { Header = "AFE硬件保护" };
        var root = new Grid { Margin = new Thickness(12) };
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        root.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });

        var top = new StackPanel();
        top.Children.Add(new TextBlock
        {
            Text = "SH367309 AFE硬件保护参数。此页与“保护 / BMS 参数”的MCU软件保护参数完全独立。保存会持久化并由固件异步更新AFE EEPROM；EEPROM寿命有限，请勿频繁写入。",
            TextWrapping = TextWrapping.Wrap,
            Foreground = new SolidColorBrush(Color.FromRgb(150, 60, 0)),
            FontWeight = FontWeights.SemiBold
        });
        var buttons = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 10, 0, 8) };
        var read = new Button { Content = "读取AFE硬件参数", Width = 138, Height = 30 };
        read.Click += ReadAfeHardware_Click;
        buttons.Children.Add(read);
        var apply = new Button { Content = "保存 / 应用修改", Width = 132, Height = 30, Margin = new Thickness(8, 0, 0, 0) };
        apply.Click += ApplyAfeHardware_Click;
        buttons.Children.Add(apply);
        var reset = new Button { Content = "撤销本地修改", Width = 118, Height = 30, Margin = new Thickness(8, 0, 0, 0) };
        reset.Click += (_, _) => { _afeHardwareModel.ResetEditsToCurrent(); if (_afeHardwareStatus is not null) _afeHardwareStatus.Text = "已恢复为上次读取值"; };
        buttons.Children.Add(reset);
        _afeHardwareStatus = new TextBlock { Text = "未读取", VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(14, 0, 0, 0) };
        buttons.Children.Add(_afeHardwareStatus);
        top.Children.Add(buttons);
        Grid.SetRow(top, 0);
        root.Children.Add(top);

        _afeHardwareGrid = new DataGrid
        {
            AutoGenerateColumns = false,
            CanUserAddRows = false,
            CanUserDeleteRows = false,
            SelectionMode = DataGridSelectionMode.Single,
            SelectionUnit = DataGridSelectionUnit.FullRow,
            ItemsSource = _afeHardwareModel.Rows,
            Margin = new Thickness(0, 0, 0, 8)
        };
        _afeHardwareGrid.Columns.Add(new DataGridTextColumn { Header = "类别", Binding = new Binding(nameof(AfeParameterRow.Group)), Width = 120, IsReadOnly = true });
        _afeHardwareGrid.Columns.Add(new DataGridTextColumn { Header = "AFE硬件参数", Binding = new Binding(nameof(AfeParameterRow.Name)), Width = 145, IsReadOnly = true });
        _afeHardwareGrid.Columns.Add(new DataGridTextColumn { Header = "当前值", Binding = new Binding(nameof(AfeParameterRow.CurrentValue)), Width = 100, IsReadOnly = true });
        _afeHardwareGrid.Columns.Add(new DataGridTextColumn
        {
            Header = "修改值",
            Binding = new Binding(nameof(AfeParameterRow.EditValue)) { Mode = BindingMode.TwoWay, UpdateSourceTrigger = UpdateSourceTrigger.PropertyChanged },
            Width = 110
        });
        _afeHardwareGrid.Columns.Add(new DataGridTextColumn { Header = "单位", Binding = new Binding(nameof(AfeParameterRow.Unit)), Width = 70, IsReadOnly = true });
        _afeHardwareGrid.Columns.Add(new DataGridTextColumn { Header = "合法范围 / 档位", Binding = new Binding(nameof(AfeParameterRow.Hint)), Width = new DataGridLength(1, DataGridLengthUnitType.Star), IsReadOnly = true });
        Grid.SetRow(_afeHardwareGrid, 1);
        root.Children.Add(_afeHardwareGrid);

        var bottom = new TextBlock
        {
            Text = "说明：充电过流0x2406/0x2408、延时0x2407/0x2409是同一SH367309硬件参数的协议别名，上位机只显示一组并自动保持别名一致。电流离散档位按当前D3PRO采样电阻配置生成。",
            TextWrapping = TextWrapping.Wrap,
            Foreground = Brushes.DimGray
        };
        Grid.SetRow(bottom, 2);
        root.Children.Add(bottom);
        tab.Content = root;

        int insertIndex = Math.Min(2, MainTabs.Items.Count);
        MainTabs.Items.Insert(insertIndex, tab);
    }

    private void AddEventLogTab()
    {
        if (MainTabs.Items.OfType<TabItem>().Any(t => string.Equals(t.Header?.ToString(), "事件日志", StringComparison.Ordinal))) return;

        var tab = new TabItem { Header = "事件日志" };
        var root = new Grid { Margin = new Thickness(12) };
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        root.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });

        var top = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 0, 0, 8) };
        var read = new Button { Content = "读取100条日志", Width = 120, Height = 30 };
        read.Click += ReadEventLogs_Click;
        top.Children.Add(read);
        _eventLogStatus = new TextBlock
        {
            Text = "未读取",
            VerticalAlignment = VerticalAlignment.Center,
            Margin = new Thickness(12, 0, 0, 0)
        };
        top.Children.Add(_eventLogStatus);
        var hint = new TextBlock
        {
            Text = "日志按最新→最旧显示；固件只保存事件类型和事件间隔，不包含绝对日期时间。",
            VerticalAlignment = VerticalAlignment.Center,
            Margin = new Thickness(20, 0, 0, 0),
            Foreground = Brushes.DimGray
        };
        top.Children.Add(hint);
        Grid.SetRow(top, 0);
        root.Children.Add(top);

        var grid = new DataGrid
        {
            AutoGenerateColumns = false,
            CanUserAddRows = false,
            CanUserDeleteRows = false,
            IsReadOnly = true,
            ItemsSource = _deviceEventLogs
        };
        grid.Columns.Add(new DataGridTextColumn { Header = "序号", Binding = new Binding(nameof(DeviceEventLogRow.Position)), Width = 70 });
        grid.Columns.Add(new DataGridTextColumn { Header = "事件", Binding = new Binding(nameof(DeviceEventLogRow.EventName)), Width = 220 });
        grid.Columns.Add(new DataGridTextColumn { Header = "与上一事件间隔", Binding = new Binding(nameof(DeviceEventLogRow.IntervalText)), Width = 180 });
        grid.Columns.Add(new DataGridTextColumn { Header = "状态", Binding = new Binding(nameof(DeviceEventLogRow.StateText)), Width = new DataGridLength(1, DataGridLengthUnitType.Star) });
        Grid.SetRow(grid, 1);
        root.Children.Add(grid);
        tab.Content = root;

        int insertIndex = Math.Min(3, MainTabs.Items.Count);
        MainTabs.Items.Insert(insertIndex, tab);
    }

    private async void ReadAfeHardware_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            _pollTimer.Stop();
            await WaitForCommunicationIdleAsync();
            BmsClient bms = _bms ?? throw new InvalidOperationException("请先连接BMS。");
            BmsBleTransport transport = _bmsTransport ?? throw new InvalidOperationException("BLE通信未就绪。");
            _afeHardwareStatus!.Text = "正在读取...";
            var client = new AfeHardwareClient(bms, transport);
            ushort[] raw = await client.ReadAllAsync();
            _afeHardwareModel.Load(raw);
            _afeHardwareStatus.Text = $"读取完成 · {DateTime.Now:HH:mm:ss}";
            AppendLog("AFE_HW_READ_OK count=24", "AFE");
        }
        catch (Exception ex)
        {
            if (_afeHardwareStatus is not null) _afeHardwareStatus.Text = "读取失败";
            ShowError("读取AFE硬件参数失败", ex);
        }
        finally
        {
            StartAutomaticRefresh();
        }
    }

    private async void ApplyAfeHardware_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            _afeHardwareGrid?.CommitEdit(DataGridEditingUnit.Cell, true);
            _afeHardwareGrid?.CommitEdit(DataGridEditingUnit.Row, true);
            if (!_afeHardwareModel.TryBuildCandidate(out ushort[] candidate, out string validationError))
                throw new InvalidOperationException(validationError);

            IReadOnlyList<AfeWriteGroup> groups = _afeHardwareModel.GetChangedWriteGroups(candidate);
            if (groups.Count == 0)
            {
                _afeHardwareStatus!.Text = "没有待写入的AFE硬件参数修改";
                return;
            }

            string groupNames = string.Join("、", groups.Select(g => g.Name));
            if (MessageBox.Show(
                    $"确认修改SH367309 AFE硬件保护参数？\n\n将写入：{groupNames}\n\nAFE EEPROM有编程寿命限制。上位机只写发生变化的参数组，并在完成后回读全部24项校验。",
                    "确认修改AFE硬件保护参数",
                    MessageBoxButton.YesNo,
                    MessageBoxImage.Warning) != MessageBoxResult.Yes)
                return;

            _pollTimer.Stop();
            await WaitForCommunicationIdleAsync();
            BmsClient bms = _bms ?? throw new InvalidOperationException("请先连接BMS。");
            BmsBleTransport transport = _bmsTransport ?? throw new InvalidOperationException("BLE通信未就绪。");
            var client = new AfeHardwareClient(bms, transport);

            for (int i = 0; i < groups.Count; i++)
            {
                AfeWriteGroup group = groups[i];
                _afeHardwareStatus!.Text = $"正在写入 {i + 1}/{groups.Count}：{group.Name}";
                AppendLog($"AFE_HW_WRITE group='{group.Name}' start=0x{group.StartRegister:X4} qty={group.Values.Length}", "AFE");
                await client.WriteGroupAsync(group);
                await Task.Delay(120);
            }

            // Firmware ACK means validated+persistent+queued for asynchronous SH367309 EEPROM apply.
            await Task.Delay(800);
            ushort[] readback = await client.ReadAllAsync();
            if (!readback.SequenceEqual(candidate))
            {
                int mismatch = Enumerable.Range(0, candidate.Length).First(i => candidate[i] != readback[i]);
                throw new IOException($"AFE参数回读不一致：参数索引 {mismatch}，写入 {candidate[mismatch]}，回读 {readback[mismatch]}。");
            }

            _afeHardwareModel.Load(readback);
            _afeHardwareStatus!.Text = $"保存成功并回读一致 · {DateTime.Now:HH:mm:ss} · AFE硬件同步由固件异步完成";
            AppendLog($"AFE_HW_WRITE_VERIFY_OK groups={groups.Count}", "AFE");
        }
        catch (Exception ex)
        {
            if (_afeHardwareStatus is not null) _afeHardwareStatus.Text = "保存失败";
            ShowError("保存AFE硬件参数失败", ex);
        }
        finally
        {
            StartAutomaticRefresh();
        }
    }

    private async Task WaitForCommunicationIdleAsync()
    {
        if (_autoReconnectRunning) throw new InvalidOperationException("设备正在后台重连，请稍后再操作参数。");
        for (int i = 0; i < 50 && _polling; i++) await Task.Delay(40);
        if (_polling) throw new IOException("实时数据读取仍在进行，无法安全开始参数事务。");
    }

    private async void ReadEventLogs_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            _pollTimer.Stop();
            BmsClient bms = _bms ?? throw new InvalidOperationException("请先连接BMS。");
            _eventLogStatus!.Text = "正在读取100条...";
            ushort[] words = await bms.ReadRegistersAsync(0xC008, 100);
            _deviceEventLogs.Clear();
            int valid = 0;
            for (int i = 0; i < words.Length; i++)
            {
                byte eventId = (byte)(words[i] >> 8);
                byte interval = (byte)words[i];
                bool populated = eventId != 0;
                if (populated) valid++;
                _deviceEventLogs.Add(new DeviceEventLogRow(
                    i + 1,
                    populated ? DecodeEventName(eventId) : "—",
                    populated ? DecodeEventInterval(interval) : "—",
                    populated ? "有效" : "空记录"));
            }
            _eventLogStatus.Text = $"读取完成 · 有效 {valid}/100 · {DateTime.Now:HH:mm:ss}";
            AppendLog($"EVENT_LOG_READ_OK valid={valid}/100", "LOG");
        }
        catch (Exception ex)
        {
            if (_eventLogStatus is not null) _eventLogStatus.Text = "读取失败";
            ShowError("读取设备事件日志失败", ex);
        }
        finally
        {
            StartAutomaticRefresh();
        }
    }

    private static string DecodeEventName(byte id) => id switch
    {
        1 => "BMS启动",
        2 => "进入休眠",
        3 => "均衡开启",
        4 => "加热开启",
        5 => "制冷开启",
        6 => "单体过压",
        7 => "总压过压",
        8 => "充电过流",
        9 => "单体欠压",
        10 => "总压欠压",
        11 => "放电过流",
        12 => "充电低温",
        13 => "放电低温",
        14 => "充电高温",
        15 => "放电高温",
        16 => "单体压差保护",
        17 => "CBC错误",
        18 => "AFE1错误",
        19 => "AFE2错误",
        20 => "EEPROM错误",
        _ => $"未知事件({id})"
    };

    private static string DecodeEventInterval(byte code)
    {
        if (code == 0) return "启动记录 / 0";
        if (code == 171) return "≤1分钟";
        if (code == 170) return ">168小时";
        if (code is >= 1 and <= 168) return $"约 {code} 小时";
        return $"未知间隔编码 {code}";
    }

    private void LongTermStart_Click(object sender, RoutedEventArgs e)
    {
        if (_longTermRecords.Count > 0 && !_longTermMonitoring)
        {
            if (MessageBox.Show("开始新的长期监控会清空当前尚未导出的监控记录。是否继续？", "开始长期监控", MessageBoxButton.YesNo, MessageBoxImage.Question) != MessageBoxResult.Yes)
                return;
        }

        _longTermRecords.Clear();
        _longTermStartedAt = DateTime.Now;
        _longTermLastRecordAt = DateTime.MinValue;
        _longTermLastUpdateToken = string.Empty;
        _longTermMonitoring = true;
        if (_longTermStartButton is not null) _longTermStartButton.IsEnabled = false;
        if (_longTermStopButton is not null) _longTermStopButton.IsEnabled = true;
        UpdateLongTermStatus();
        CaptureLongTermRecord(force: true);
        AppendLog($"LONG_TERM_MONITOR_START interval={GetLongTermIntervalSeconds()}s", "MONITOR");
    }

    private void LongTermStop_Click(object sender, RoutedEventArgs e)
    {
        _longTermMonitoring = false;
        if (_longTermStartButton is not null) _longTermStartButton.IsEnabled = true;
        if (_longTermStopButton is not null) _longTermStopButton.IsEnabled = false;
        UpdateLongTermStatus();
        AppendLog($"LONG_TERM_MONITOR_STOP records={_longTermRecords.Count}", "MONITOR");
    }

    private async void LongTermExport_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            if (_longTermRecords.Count == 0) throw new InvalidOperationException("当前没有长期监控数据可导出。");
            var dlg = new SaveFileDialog
            {
                Filter = "Excel 工作簿 (*.xlsx)|*.xlsx",
                DefaultExt = ".xlsx",
                AddExtension = true,
                FileName = $"BMS_Monitor_{DateTime.Now:yyyyMMdd_HHmmss}.xlsx"
            };
            if (dlg.ShowDialog() != true) return;

            List<LongTermMonitorRecord> snapshot = _longTermRecords.Select(r => r.Clone()).ToList();
            int intervalSeconds = GetLongTermIntervalSeconds();
            if (_longTermStatus is not null) _longTermStatus.Text = $"正在导出 {snapshot.Count:N0} 条记录...";
            if (_longTermExportButton is not null) _longTermExportButton.IsEnabled = false;
            await Task.Run(() => WriteLongTermWorkbook(dlg.FileName, snapshot, intervalSeconds));
            UpdateLongTermStatus();
            AppendLog($"LONG_TERM_MONITOR_EXPORT path='{dlg.FileName}' records={snapshot.Count}", "MONITOR");
            MessageBox.Show($"已导出 {snapshot.Count:N0} 条监控记录：\n{dlg.FileName}", "导出完成", MessageBoxButton.OK, MessageBoxImage.Information);
        }
        catch (Exception ex)
        {
            ShowError("导出长期监控Excel失败", ex);
        }
        finally
        {
            if (_longTermExportButton is not null) _longTermExportButton.IsEnabled = true;
        }
    }

    private void AdvancedFeatureTimer_Tick()
    {
        if (_longTermMonitoring) CaptureLongTermRecord(force: false);
        UpdateLongTermStatus();
    }

    private int GetLongTermIntervalSeconds()
    {
        if (_longTermIntervalBox?.SelectedItem is ComboBoxItem item && item.Tag is int seconds) return seconds;
        return 5;
    }

    private void CaptureLongTermRecord(bool force)
    {
        if (!_longTermMonitoring || _bms is null || _autoReconnectRunning || _otaRunning) return;
        string updateToken = LastUpdateText.Text?.Trim() ?? string.Empty;
        if (string.IsNullOrWhiteSpace(updateToken) || updateToken == "未读取") return;
        if (!force && updateToken == _longTermLastUpdateToken) return;

        DateTime now = DateTime.Now;
        int intervalSeconds = GetLongTermIntervalSeconds();
        if (!force && _longTermLastRecordAt != DateTime.MinValue && now - _longTermLastRecordAt < TimeSpan.FromSeconds(intervalSeconds))
        {
            _longTermLastUpdateToken = updateToken;
            return;
        }

        LongTermMonitorRecord? record = BuildLongTermRecord(now);
        if (record is null) return;
        _longTermRecords.Add(record);
        _longTermLastRecordAt = now;
        _longTermLastUpdateToken = updateToken;

        // Excel row limit is 1,048,576. Reserve header/info space and stop safely before that.
        if (_longTermRecords.Count >= 1_000_000)
        {
            _longTermMonitoring = false;
            if (_longTermStartButton is not null) _longTermStartButton.IsEnabled = true;
            if (_longTermStopButton is not null) _longTermStopButton.IsEnabled = false;
            AppendLog("LONG_TERM_MONITOR_STOP reason=record_limit", "MONITOR");
        }
    }

    private LongTermMonitorRecord? BuildLongTermRecord(DateTime timestamp)
    {
        if (!TryFirstDouble(PackVoltageText.Text, out double packVoltage)) return null;
        if (!TryFirstDouble(CurrentText.Text, out double current)) return null;

        string socRaw = SocText.Text ?? string.Empty;
        int soc = TryRegexInt(socRaw, @"SOC\s*(\d+)\s*%", -1);
        int soh = TryRegexInt(socRaw, @"SOH\s*(\d+)\s*%", -1);
        if (soc < 0) return null;

        string temps = TempsText.Text ?? string.Empty;
        double tempMax = TryRegexDouble(temps, @"最高温度：\s*([-+]?\d+(?:\.\d+)?)", double.NaN);
        double tempMin = TryRegexDouble(temps, @"最低温度：\s*([-+]?\d+(?:\.\d+)?)", double.NaN);
        double tempMos = TryRegexDouble(temps, @"MOS\s*温度：\s*([-+]?\d+(?:\.\d+)?)", double.NaN);

        string capacity = CapacityText.Text ?? string.Empty;
        double remainAh = TryRegexDouble(capacity, @"当前容量：\s*([-+]?\d+(?:\.\d+)?)", double.NaN);
        double fullAh = TryRegexDouble(capacity, @"满充容量：\s*([-+]?\d+(?:\.\d+)?)", double.NaN);
        int cycle = TryRegexInt(capacity, @"循环次数：\s*(\d+)", -1);

        List<double> cellsMv = new();
        foreach (Match m in Regex.Matches(CellsText.Text ?? string.Empty, @"第\s*\d+\s*串\s*([0-9]+(?:\.[0-9]+)?)\s*V", RegexOptions.IgnoreCase))
        {
            if (double.TryParse(m.Groups[1].Value, NumberStyles.Float, CultureInfo.InvariantCulture, out double v)) cellsMv.Add(v * 1000.0);
        }
        double maxCell = cellsMv.Count > 0 ? cellsMv.Max() : double.NaN;
        double minCell = cellsMv.Count > 0 ? cellsMv.Min() : double.NaN;
        double deltaCell = cellsMv.Count > 0 ? maxCell - minCell : double.NaN;

        string mos = MosStateText.Text ?? string.Empty;
        string system = SystemStateText.Text ?? string.Empty;
        string protections = ProtectionLevelsText.Text ?? string.Empty;
        string identity = IdentityText.Text ?? string.Empty;

        return new LongTermMonitorRecord
        {
            Timestamp = timestamp,
            BluetoothName = _connectedName,
            PackVoltageV = packVoltage,
            CurrentA = current,
            Soc = soc,
            Soh = soh,
            WorkState = WorkStateText.Text ?? string.Empty,
            RemainingAh = remainAh,
            FullAh = fullAh,
            CycleCount = cycle,
            MaxTempC = tempMax,
            MinTempC = tempMin,
            MosTempC = tempMos,
            MaxCellMv = maxCell,
            MinCellMv = minCell,
            DeltaCellMv = deltaCell,
            ChargeMos = ParseSwitchText(mos, "充电 MOS"),
            DischargeMos = ParseSwitchText(mos, "放电 MOS"),
            Heating = ParseSwitchText(system, "加热"),
            Cooling = ParseSwitchText(system, "制冷"),
            Balancing = Regex.IsMatch(system, @"均衡：\s*进行中") ? "开启" : Regex.IsMatch(system, @"均衡：\s*未进行") ? "关闭" : "未知",
            AlarmLevel1 = ExtractRegexText(protections, @"一级：([^\r\n]+)", "无"),
            AlarmLevel2 = ExtractRegexText(protections, @"二级：([^\r\n]+)", "无"),
            AlarmLevel3 = ExtractRegexText(protections, @"三级：([^\r\n]+)", "无"),
            HardwareVersion = ExtractRegexText(identity, @"硬件版本：([^\r\n]+)", ""),
            SoftwareVersion = ExtractRegexText(identity, @"软件版本：([^\r\n]+)", ""),
            SerialNumber = ExtractRegexText(identity, @"序列号：([^\r\n]+)", ""),
            CellsMv = cellsMv
        };
    }

    private void UpdateLongTermStatus()
    {
        if (_longTermStatus is null) return;
        if (_longTermMonitoring)
        {
            TimeSpan elapsed = DateTime.Now - _longTermStartedAt;
            _longTermStatus.Text = $"监控中 · {GetLongTermIntervalSeconds()}秒/条 · {_longTermRecords.Count:N0}条 · {elapsed:dd\.hh\:mm\:ss}";
            _longTermStatus.Foreground = new SolidColorBrush(Color.FromRgb(0, 130, 50));
        }
        else if (_longTermRecords.Count > 0)
        {
            _longTermStatus.Text = $"已停止 · {_longTermRecords.Count:N0}条 · 可导出Excel";
            _longTermStatus.Foreground = Brushes.DimGray;
        }
        else
        {
            _longTermStatus.Text = "未开始";
            _longTermStatus.Foreground = Brushes.DimGray;
        }
    }

    private static void WriteLongTermWorkbook(string path, List<LongTermMonitorRecord> records, int intervalSeconds)
    {
        using var workbook = new XLWorkbook();
        var sheet = workbook.Worksheets.Add("电池长期监控");
        int maxCells = records.Count == 0 ? 0 : records.Max(r => r.CellsMv.Count);
        var headers = new List<string>
        {
            "时间", "蓝牙名称", "总压(V)", "电流(A)", "SOC(%)", "SOH(%)", "工作状态",
            "剩余容量(Ah)", "满充容量(Ah)", "循环次数",
            "最高温度(℃)", "最低温度(℃)", "MOS温度(℃)",
            "最高单体(mV)", "最低单体(mV)", "最大压差(mV)",
            "充电MOS", "放电MOS", "加热", "制冷", "均衡",
            "一级告警", "二级告警", "三级保护", "硬件版本", "软件版本", "BMS序列号"
        };
        for (int i = 1; i <= maxCells; i++) headers.Add($"单体{i}(mV)");

        for (int c = 0; c < headers.Count; c++) sheet.Cell(1, c + 1).Value = headers[c];
        sheet.Row(1).Style.Font.Bold = true;
        sheet.Row(1).Style.Fill.BackgroundColor = XLColor.LightGray;

        for (int r = 0; r < records.Count; r++)
        {
            LongTermMonitorRecord x = records[r];
            int row = r + 2;
            object[] values =
            [
                x.Timestamp, x.BluetoothName, x.PackVoltageV, x.CurrentA, x.Soc, x.Soh, x.WorkState,
                x.RemainingAh, x.FullAh, x.CycleCount,
                x.MaxTempC, x.MinTempC, x.MosTempC,
                x.MaxCellMv, x.MinCellMv, x.DeltaCellMv,
                x.ChargeMos, x.DischargeMos, x.Heating, x.Cooling, x.Balancing,
                x.AlarmLevel1, x.AlarmLevel2, x.AlarmLevel3, x.HardwareVersion, x.SoftwareVersion, x.SerialNumber
            ];
            for (int c = 0; c < values.Length; c++) SetExcelValue(sheet.Cell(row, c + 1), values[c]);
            for (int i = 0; i < x.CellsMv.Count; i++) sheet.Cell(row, values.Length + i + 1).Value = x.CellsMv[i];
        }

        sheet.Column(1).Style.DateFormat.Format = "yyyy-mm-dd hh:mm:ss";
        sheet.SheetView.FreezeRows(1);
        if (records.Count > 0) sheet.Range(1, 1, records.Count + 1, headers.Count).SetAutoFilter();
        sheet.Column(1).Width = 20;
        sheet.Column(2).Width = 18;
        for (int c = 3; c <= 21; c++) sheet.Column(c).Width = 13;
        for (int c = 22; c <= 24; c++) sheet.Column(c).Width = 28;
        for (int c = 25; c <= 27; c++) sheet.Column(c).Width = 18;
        for (int c = 28; c <= headers.Count; c++) sheet.Column(c).Width = 12;

        var info = workbook.Worksheets.Add("监控信息");
        info.Cell("A1").Value = "记录条数"; info.Cell("B1").Value = records.Count;
        info.Cell("A2").Value = "记录间隔(秒)"; info.Cell("B2").Value = intervalSeconds;
        if (records.Count > 0)
        {
            info.Cell("A3").Value = "开始时间"; info.Cell("B3").Value = records.First().Timestamp;
            info.Cell("A4").Value = "结束时间"; info.Cell("B4").Value = records.Last().Timestamp;
            info.Cell("B3").Style.DateFormat.Format = "yyyy-mm-dd hh:mm:ss";
            info.Cell("B4").Style.DateFormat.Format = "yyyy-mm-dd hh:mm:ss";
        }
        info.Cell("A6").Value = "说明";
        info.Cell("B6").Value = "仅记录上位机成功读取到的有效电池快照；蓝牙断线/自动重连期间不会写入伪造或清零数据，重连恢复后继续记录。";
        info.Column(1).Width = 18;
        info.Column(2).Width = 80;
        info.Column(2).Style.Alignment.WrapText = true;

        workbook.SaveAs(path);
    }

    private static void SetExcelValue(IXLCell cell, object value)
    {
        switch (value)
        {
            case DateTime dt: cell.Value = dt; break;
            case double d when double.IsNaN(d) || double.IsInfinity(d): cell.Value = string.Empty; break;
            case double d: cell.Value = d; break;
            case int i: cell.Value = i; break;
            default: cell.Value = value?.ToString() ?? string.Empty; break;
        }
    }

    private static bool TryFirstDouble(string? text, out double value)
    {
        Match m = Regex.Match(text ?? string.Empty, @"[-+]?\d+(?:\.\d+)?");
        return m.Success && double.TryParse(m.Value, NumberStyles.Float, CultureInfo.InvariantCulture, out value);
    }

    private static int TryRegexInt(string source, string pattern, int fallback)
    {
        Match m = Regex.Match(source ?? string.Empty, pattern, RegexOptions.IgnoreCase);
        return m.Success && int.TryParse(m.Groups[1].Value, NumberStyles.Integer, CultureInfo.InvariantCulture, out int v) ? v : fallback;
    }

    private static double TryRegexDouble(string source, string pattern, double fallback)
    {
        Match m = Regex.Match(source ?? string.Empty, pattern, RegexOptions.IgnoreCase);
        return m.Success && double.TryParse(m.Groups[1].Value, NumberStyles.Float, CultureInfo.InvariantCulture, out double v) ? v : fallback;
    }

    private static string ExtractRegexText(string source, string pattern, string fallback)
    {
        Match m = Regex.Match(source ?? string.Empty, pattern, RegexOptions.IgnoreCase);
        return m.Success ? m.Groups[1].Value.Trim() : fallback;
    }

    private static string ParseSwitchText(string source, string label)
    {
        Match m = Regex.Match(source ?? string.Empty, Regex.Escape(label) + @"：\s*(开启|关闭)");
        return m.Success ? m.Groups[1].Value : "未知";
    }
}

public sealed record DeviceEventLogRow(int Position, string EventName, string IntervalText, string StateText);

public sealed class LongTermMonitorRecord
{
    public DateTime Timestamp { get; init; }
    public string BluetoothName { get; init; } = string.Empty;
    public double PackVoltageV { get; init; }
    public double CurrentA { get; init; }
    public int Soc { get; init; }
    public int Soh { get; init; }
    public string WorkState { get; init; } = string.Empty;
    public double RemainingAh { get; init; }
    public double FullAh { get; init; }
    public int CycleCount { get; init; }
    public double MaxTempC { get; init; }
    public double MinTempC { get; init; }
    public double MosTempC { get; init; }
    public double MaxCellMv { get; init; }
    public double MinCellMv { get; init; }
    public double DeltaCellMv { get; init; }
    public string ChargeMos { get; init; } = string.Empty;
    public string DischargeMos { get; init; } = string.Empty;
    public string Heating { get; init; } = string.Empty;
    public string Cooling { get; init; } = string.Empty;
    public string Balancing { get; init; } = string.Empty;
    public string AlarmLevel1 { get; init; } = string.Empty;
    public string AlarmLevel2 { get; init; } = string.Empty;
    public string AlarmLevel3 { get; init; } = string.Empty;
    public string HardwareVersion { get; init; } = string.Empty;
    public string SoftwareVersion { get; init; } = string.Empty;
    public string SerialNumber { get; init; } = string.Empty;
    public List<double> CellsMv { get; init; } = new();

    public LongTermMonitorRecord Clone() => new()
    {
        Timestamp = Timestamp,
        BluetoothName = BluetoothName,
        PackVoltageV = PackVoltageV,
        CurrentA = CurrentA,
        Soc = Soc,
        Soh = Soh,
        WorkState = WorkState,
        RemainingAh = RemainingAh,
        FullAh = FullAh,
        CycleCount = CycleCount,
        MaxTempC = MaxTempC,
        MinTempC = MinTempC,
        MosTempC = MosTempC,
        MaxCellMv = MaxCellMv,
        MinCellMv = MinCellMv,
        DeltaCellMv = DeltaCellMv,
        ChargeMos = ChargeMos,
        DischargeMos = DischargeMos,
        Heating = Heating,
        Cooling = Cooling,
        Balancing = Balancing,
        AlarmLevel1 = AlarmLevel1,
        AlarmLevel2 = AlarmLevel2,
        AlarmLevel3 = AlarmLevel3,
        HardwareVersion = HardwareVersion,
        SoftwareVersion = SoftwareVersion,
        SerialNumber = SerialNumber,
        CellsMv = CellsMv.ToList()
    };
}
