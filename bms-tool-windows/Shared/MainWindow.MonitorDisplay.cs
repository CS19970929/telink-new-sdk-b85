using System.Globalization;
using System.Text.RegularExpressions;
using System.Windows;
using System.Windows.Media;

namespace BmsTool.Windows;

public partial class MainWindow
{
    private void ReportConnectionFailure(Exception error)
    {
        _sessionLog.WriteException("CONNECT", "连接失败", error);
        AppendLog($"连接失败 · 模式={ConnectionModeBox.Text} · {error.GetType().Name} · {error.Message}", "CONNECT");
        ConnectionText.Text = DescribeConnectionFailure(ConnectionModeBox.SelectedIndex, _bms is not null, error);
        ConnectionText.ToolTip = $"{error.Message}\n日志文件：{_sessionLog.FilePath}";
    }

    private static string DescribeConnectionFailure(int mode, bool bmsReady, Exception error)
    {
        if (error is InvalidOperationException && !bmsReady) return error.Message;
        if (mode is 1 or 3 && error is UnauthorizedAccessException)
            return "串口无法打开，可能被其他程序占用。请释放串口后重试。";

        return mode switch
        {
            3 when error.GetBaseException() is CommToolCommandException { Status: 6 } =>
                "Comm Tool 已响应，BMS 未回复 CAN 请求。请确认钥匙已合上或唤醒 BMS，并检查 CAN 接线后重试。",
            3 when bmsReady => "CAN 已连通，部分 BMS 数据读取失败。可等待刷新或重新连接。",
            3 => "未完成 Comm Tool 的 CAN 连接。请检查 USB 连接和所选串口后重试。",
            1 when bmsReady => "串口已连通，部分 BMS 数据读取失败。可等待刷新或重新连接。",
            1 when error is TimeoutException => "串口已打开，未收到有效 BMS 数据。请确认供电、接线和波特率后重试。",
            1 => "串口连接失败。请检查端口、接线和波特率后重试。",
            _ when bmsReady => "蓝牙已连通，部分 BMS 数据读取失败。可等待刷新或重新连接。",
            _ => "蓝牙连接未完成。请靠近设备、确认设备在线，再扫描并重试。"
        };
    }

    private static string DescribeBalanceState(bool isC073, BatterySnapshot snapshot)
    {
        if (snapshot.BalanceStatusConfirmed.HasValue) return snapshot.BalanceStateText;
        if (!snapshot.HasSystemStatus) return "状态未知";
        if (isC073 && snapshot.BalanceCellMask == 0) return "通道状态未提供";
        return snapshot.BalancingOn || isC073 ? "进行中" : "未进行";
    }

    private void SetMonitorModeDisplay()
    {
        bool owc = IsOwcMode;
        FeidaoButton.Visibility = IsCanMode ? Visibility.Visible : Visibility.Collapsed;
        Visibility modbusVisibility = owc ? Visibility.Collapsed : Visibility.Visible;
        Visibility owcVisibility = owc ? Visibility.Visible : Visibility.Collapsed;
        MonitorSleepButton.Visibility = MonitorRenameButton.Visibility = MonitorMosGroup.Visibility = modbusVisibility;
        MonitorSecondaryAlarm.Visibility = MonitorTertiaryAlarm.Visibility = ModbusMonitorDetails.Visibility = modbusVisibility;
        MonitorSecondaryAlarmRow.Height = MonitorTertiaryAlarmRow.Height = owc ? new GridLength(0) : new GridLength(1, GridUnitType.Star);
        ConnectionText.Visibility = modbusVisibility;
        ConnectionText.ToolTip = null;
        OwcStatusText.Visibility = OwcMonitorDetails.Visibility = OwcCellStatusText.Visibility = owcVisibility;
        OwcModeBox.Visibility = UnifiedTestEdition ? owcVisibility : Visibility.Collapsed;
#if BMS_FACTORY_APP
        CommunicationCountersText.Visibility = modbusVisibility;
        MonitorOtaButton.Visibility = modbusVisibility;
#else
        ProtectedFeaturesButton.Visibility = modbusVisibility;
#endif
        MonitorPrimaryAlarm.Header = owc ? "一线通故障" : "一级告警";
        if (owc)
        {
            DeviceList.Visibility = SerialBaudRateBox.Visibility = StopScanButton.Visibility = Visibility.Collapsed;
            BleDiscoveryStatusText.Visibility = Visibility.Collapsed;
#if BMS_FACTORY_APP
            ShowHistoryDevicesBox.Visibility = Visibility.Collapsed;
#endif
            SerialPortList.Visibility = Visibility.Visible;
            ScanButton.Content = "刷新";
            ShowOwcReading(null);
        }
        else
        {
            ScanButton.IsEnabled = ConnectButton.IsEnabled = true;
            ConnectionText.Text = string.Empty;
            MonitorTemperatureLabel.Text = "最高温度";
            UpdateCommunicationStatus();
        }
    }

    private void UpdateOwcConnectionDisplay(bool receiving)
    {
        bool hasReading = _monitorReading?.Source == "一线通";
        OwcStatusText.Text = hasReading ? "一线通数据在线" :
            _monitorError ?? (receiving
                ? (_integratedOwcConnected ? "Comm Tool 已连接，等待 BMS 一线通广播" : "正在连接 Comm Tool…")
                : "选择 Comm Tool 串口后连接");
        CustomerCommStatusBorder.Visibility = Visibility.Visible;
        CustomerCommStatusBorder.Background = hasReading ? LampOnBrush : receiving ? Brushes.Orange : Brushes.LightGray;
        CustomerCommStatusBorder.ToolTip = OwcStatusText.Text;
        CommunicationStatusText.Text = hasReading ? "通信：一线通在线" :
            !receiving ? "通信：未连接" : _monitorError is not null ? "通信：正在重试" :
            _integratedOwcConnected ? "通信：等待广播" : "通信：正在连接";
    }

    private void ShowOwcReading(MonitorReading? reading)
    {
        if (reading?.Source != "一线通") reading = null;
        static string Display(double? value, string format, string unit) => value is double n && double.IsFinite(n)
            ? n.ToString(format, CultureInfo.InvariantCulture) + unit : "—";

        double? current = reading?.HasCurrent == true ? reading.Current : null;
        PackVoltageText.Text = Display(reading?.Voltage, "F2", " V");
        CurrentText.Text = Display(current, reading?.CurrentResolutionA == 1 ? "F0" : "F1", " A");
        WorkStateText.Text = current switch { < -0.05 => "充电", > 0.05 => "放电", not null => "静置", _ => "—" };
        double? soc = reading?.Soc is >= 0 and <= 100 ? reading.Soc : null;
        CustomerSocText.Text = "SOC:" + Display(soc, "0.##", "%");
        CustomerSocFill.Height = 1.35 * soc.GetValueOrDefault();
        CustomerSohText.Text = CustomerRemainCapacityText.Text = CustomerFullCapacityText.Text = "—";
        CustomerCycleText.Text = Display(reading?.Cycles, "F0", "");
        MonitorTemperatureLabel.Text = reading?.HasCells == true ? "最高温度" : "电池温度";
        CustomerMaxTempText.Text = Display(reading?.Temperature, "0.#", " ℃");
        CustomerMinTempText.Text = Display(reading?.MinTemperature, "0.#", " ℃");
        CustomerMosTempText.Text = Display(reading?.MosTemperature, "0.#", " ℃");
        LastUpdateText.Text = reading?.ObservedUtc.ToLocalTime().ToString("HH:mm:ss") ?? "未读取";
        CustomerSerialText.Text = "BMS序列号：" + (string.IsNullOrWhiteSpace(reading?.Identity) ? "—" : reading.Identity);
        CustomerAlarm1Text.Text = reading switch
        {
            null => "—",
            { HasFault: false } => "当前协议未提供故障信息",
            { FaultCode: 0 } => "无上报故障",
            _ => reading.Fault ?? "状态未提供"
        };
        OwcProtocolText.Text = reading is null ? "协议：—" : $"协议：{reading.Protocol} · {reading.Endpoint}";
        OwcCellStatusText.Text = reading?.HasCells == false ? "当前协议未提供单体电压" :
            reading?.CellsUtc is DateTimeOffset utc ? $"单体更新时间：{utc.ToLocalTime():HH:mm:ss}" : "等待有效单体数据";
        ShowMonitorCells(reading?.HasCells == true
            ? reading.Cells.Select(cell => (cell.Key, cell.Value / 1000.0, false)).ToArray()
            : Array.Empty<(int, double, bool)>());
#if BMS_FACTORY_APP
        if (reading is null) OwcRawText.Clear();
#endif
    }

    private string _customerLastCellsRaw = string.Empty;
    private BatterySnapshot? _customerC077Snapshot;

    private void UpdateCustomerCells()
    {
        if (_customerC077Snapshot is { Measurements: { } measured } snapshot)
        {
            var currentCells = Enumerable.Range(0, measured.SeriesCount).Where(snapshot.IsCellVoltageValid)
                .Select(i => (i + 1, snapshot.CellMillivolts[i] / 1000.0, snapshot.IsCellBalancing(i))).ToArray();
            var invalid = Enumerable.Range(0, measured.SeriesCount).Where(i => !snapshot.IsCellVoltageValid(i))
                .Select(i => i + 1).ToArray();
            ShowMonitorCells(currentCells, measured.SeriesCount, invalid);
            return;
        }
        string raw = CellsText.Text ?? string.Empty;
        if (raw == _customerLastCellsRaw) return;
        _customerLastCellsRaw = raw;

        var cells = new List<(int Index, double Volts, bool Balancing)>();
        foreach (Match match in Regex.Matches(raw, @"第\s*(\d+)\s*串\s*([0-9]+(?:\.[0-9]+)?)\s*V(?:\s*(\[均衡\]))?", RegexOptions.IgnoreCase))
        {
            if (int.TryParse(match.Groups[1].Value, out int index) &&
                double.TryParse(match.Groups[2].Value, NumberStyles.Float, CultureInfo.InvariantCulture, out double volts))
                cells.Add((index, volts, match.Groups[3].Success));
        }

        ShowMonitorCells(cells, BmsRegisters.CellVoltageSlotCount);
    }

    private void ShowMonitorCells(IReadOnlyList<(int Index, double Volts, bool Balancing)> cells, int? slotCount = null,
        IReadOnlyList<int>? invalidCells = null)
    {
        CustomerCellPanel.Children.Clear();
        if (cells.Count == 0)
        {
            CustomerMaxCellText.Text = CustomerMinCellText.Text = CustomerAvgCellText.Text = "—";
            CustomerDeltaCellText.Text = "—";
            CustomerCellCountText.Text = slotCount is int known ? $"0 / {known}" : "—";
            if (invalidCells is not null)
                foreach (int index in invalidCells)
                    CustomerCellPanel.Children.Add(CreateCellVisual(index, null, false, false, false));
            return;
        }

        var max = cells.MaxBy(cell => cell.Volts);
        var min = cells.MinBy(cell => cell.Volts);
        CustomerMaxCellText.Text = $"{max.Volts * 1000.0:F0}    {max.Index}";
        CustomerMinCellText.Text = $"{min.Volts * 1000.0:F0}    {min.Index}";
        CustomerAvgCellText.Text = (cells.Average(cell => cell.Volts) * 1000.0).ToString("F2", CultureInfo.InvariantCulture);
        CustomerDeltaCellText.Text = ((max.Volts - min.Volts) * 1000.0).ToString("F0", CultureInfo.InvariantCulture);
        CustomerCellCountText.Text = slotCount is int slots ? $"{cells.Count} / {slots}" : $"{cells.Count}";
        var displayCells = cells.Select(c => (c.Index, Volts: (double?)c.Volts, c.Balancing))
            .Concat((invalidCells ?? Array.Empty<int>()).Select(i => (Index: i, Volts: (double?)null, Balancing: false)));
        foreach (var cell in displayCells.OrderBy(cell => cell.Index))
            CustomerCellPanel.Children.Add(CreateCellVisual(cell.Index, cell.Volts, cell.Index == max.Index, cell.Index == min.Index, cell.Balancing));
    }
}
