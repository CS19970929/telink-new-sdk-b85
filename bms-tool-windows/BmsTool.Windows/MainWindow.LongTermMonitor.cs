using System.Globalization;
using System.Text.RegularExpressions;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using ClosedXML.Excel;
using Microsoft.Win32;

namespace BmsTool.Windows;

public partial class MainWindow
{
    private void AddLongTermMonitorToolbarToMainPage()
    {
        TabItem? realtimeTab = MainTabs.Items.OfType<TabItem>()
            .FirstOrDefault(t => string.Equals(t.Header?.ToString(), "实时监控", StringComparison.Ordinal));
        if (realtimeTab?.Content is not UIElement existing) return;

        realtimeTab.Content = null;
        var wrapper = new DockPanel();
        Border toolbar = BuildLongTermMonitorToolbar();
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
        foreach ((string text, int seconds) in new[] { ("1秒", 1), ("5秒", 5), ("10秒", 10), ("30秒", 30), ("60秒", 60) })
            _longTermIntervalBox.Items.Add(new ComboBoxItem { Content = text, Tag = seconds, IsSelected = seconds == 5 });
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

        _longTermStatus = new TextBlock { Text = "未开始", VerticalAlignment = VerticalAlignment.Center, Foreground = Brushes.DimGray };
        panel.Children.Add(_longTermStatus);
        border.Child = panel;
        return border;
    }

    private void LongTermStart_Click(object sender, RoutedEventArgs e)
    {
        if (_longTermRecords.Count > 0 && !_longTermMonitoring)
        {
            if (MessageBox.Show(
                    "开始新的长期监控会清空当前尚未导出的监控记录。是否继续？",
                    "开始长期监控",
                    MessageBoxButton.YesNo,
                    MessageBoxImage.Question) != MessageBoxResult.Yes)
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
            if (_longTermRecords.Count == 0)
                throw new InvalidOperationException("当前没有长期监控数据可导出。");

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
        if (_longTermIntervalBox?.SelectedItem is ComboBoxItem item && item.Tag is int seconds)
            return seconds;
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
            return;

        LongTermMonitorRecord? record = BuildLongTermRecord(now);
        if (record is null) return;

        _longTermRecords.Add(record);
        _longTermLastRecordAt = now;
        _longTermLastUpdateToken = updateToken;

        // Keep below Excel's 1,048,576 row limit including header rows.
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

        var cellsMv = new List<double>();
        foreach (Match match in Regex.Matches(CellsText.Text ?? string.Empty, @"第\s*\d+\s*串\s*([0-9]+(?:\.[0-9]+)?)\s*V", RegexOptions.IgnoreCase))
        {
            if (double.TryParse(match.Groups[1].Value, NumberStyles.Float, CultureInfo.InvariantCulture, out double volts))
                cellsMv.Add(volts * 1000.0);
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
            HardwareVersion = ExtractRegexText(identity, @"硬件版本：([^\r\n]+)", string.Empty),
            SoftwareVersion = ExtractRegexText(identity, @"软件版本：([^\r\n]+)", string.Empty),
            SerialNumber = ExtractRegexText(identity, @"序列号：([^\r\n]+)", string.Empty),
            CellsMv = cellsMv
        };
    }

    private void UpdateLongTermStatus()
    {
        if (_longTermStatus is null) return;

        if (_longTermMonitoring)
        {
            TimeSpan elapsed = DateTime.Now - _longTermStartedAt;
            string elapsedText = $"{(int)elapsed.TotalDays:00}.{elapsed.Hours:00}:{elapsed.Minutes:00}:{elapsed.Seconds:00}";
            _longTermStatus.Text = $"监控中 · {GetLongTermIntervalSeconds()}秒/条 · {_longTermRecords.Count:N0}条 · {elapsedText}";
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
        IXLWorksheet sheet = workbook.Worksheets.Add("电池长期监控");
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
            {
                x.Timestamp, x.BluetoothName, x.PackVoltageV, x.CurrentA, x.Soc, x.Soh, x.WorkState,
                x.RemainingAh, x.FullAh, x.CycleCount,
                x.MaxTempC, x.MinTempC, x.MosTempC,
                x.MaxCellMv, x.MinCellMv, x.DeltaCellMv,
                x.ChargeMos, x.DischargeMos, x.Heating, x.Cooling, x.Balancing,
                x.AlarmLevel1, x.AlarmLevel2, x.AlarmLevel3, x.HardwareVersion, x.SoftwareVersion, x.SerialNumber
            };
            for (int c = 0; c < values.Length; c++) SetExcelValue(sheet.Cell(row, c + 1), values[c]);
            for (int i = 0; i < x.CellsMv.Count; i++) sheet.Cell(row, values.Length + i + 1).Value = x.CellsMv[i];
        }

        sheet.Column(1).Style.DateFormat.Format = "yyyy-mm-dd hh:mm:ss";
        sheet.SheetView.FreezeRows(1);
        if (records.Count > 0) sheet.Range(1, 1, records.Count + 1, headers.Count).SetAutoFilter();
        sheet.Column(1).Width = 20;
        sheet.Column(2).Width = 18;
        for (int c = 3; c <= Math.Min(21, headers.Count); c++) sheet.Column(c).Width = 13;
        for (int c = 22; c <= Math.Min(24, headers.Count); c++) sheet.Column(c).Width = 28;
        for (int c = 25; c <= Math.Min(27, headers.Count); c++) sheet.Column(c).Width = 18;
        for (int c = 28; c <= headers.Count; c++) sheet.Column(c).Width = 12;

        IXLWorksheet info = workbook.Worksheets.Add("监控信息");
        info.Cell("A1").Value = "记录条数";
        info.Cell("B1").Value = records.Count;
        info.Cell("A2").Value = "记录间隔(秒)";
        info.Cell("B2").Value = intervalSeconds;
        if (records.Count > 0)
        {
            info.Cell("A3").Value = "开始时间";
            info.Cell("B3").Value = records.First().Timestamp;
            info.Cell("A4").Value = "结束时间";
            info.Cell("B4").Value = records.Last().Timestamp;
            info.Cell("B3").Style.DateFormat.Format = "yyyy-mm-dd hh:mm:ss";
            info.Cell("B4").Style.DateFormat.Format = "yyyy-mm-dd hh:mm:ss";
        }
        info.Cell("A6").Value = "说明";
        info.Cell("B6").Value = "仅记录上位机成功读取到的有效电池快照；蓝牙断线/自动重连期间不写入伪造或清零数据，通信恢复后继续记录。";
        info.Column(1).Width = 18;
        info.Column(2).Width = 80;
        info.Column(2).Style.Alignment.WrapText = true;

        workbook.SaveAs(path);
    }

    private static void SetExcelValue(IXLCell cell, object value)
    {
        switch (value)
        {
            case DateTime dt:
                cell.Value = dt;
                break;
            case double d when double.IsNaN(d) || double.IsInfinity(d):
                cell.Value = string.Empty;
                break;
            case double d:
                cell.Value = d;
                break;
            case int i:
                cell.Value = i;
                break;
            default:
                cell.Value = value?.ToString() ?? string.Empty;
                break;
        }
    }

    private static bool TryFirstDouble(string? text, out double value)
    {
        Match match = Regex.Match(text ?? string.Empty, @"[-+]?\d+(?:\.\d+)?");
        return match.Success && double.TryParse(match.Value, NumberStyles.Float, CultureInfo.InvariantCulture, out value);
    }

    private static int TryRegexInt(string source, string pattern, int fallback)
    {
        Match match = Regex.Match(source ?? string.Empty, pattern, RegexOptions.IgnoreCase);
        return match.Success && int.TryParse(match.Groups[1].Value, NumberStyles.Integer, CultureInfo.InvariantCulture, out int value) ? value : fallback;
    }

    private static double TryRegexDouble(string source, string pattern, double fallback)
    {
        Match match = Regex.Match(source ?? string.Empty, pattern, RegexOptions.IgnoreCase);
        return match.Success && double.TryParse(match.Groups[1].Value, NumberStyles.Float, CultureInfo.InvariantCulture, out double value) ? value : fallback;
    }

    private static string ExtractRegexText(string source, string pattern, string fallback)
    {
        Match match = Regex.Match(source ?? string.Empty, pattern, RegexOptions.IgnoreCase);
        return match.Success ? match.Groups[1].Value.Trim() : fallback;
    }

    private static string ParseSwitchText(string source, string label)
    {
        Match match = Regex.Match(source ?? string.Empty, Regex.Escape(label) + @"：\s*(开启|关闭)");
        return match.Success ? match.Groups[1].Value : "未知";
    }
}

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
