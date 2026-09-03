using System.Globalization;
using System.Text.RegularExpressions;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Threading;

namespace BmsTool.Windows;

public partial class MainWindow
{
    private DispatcherTimer? _customerUiTimer;
    private string _customerLastCellsRaw = string.Empty;
    private string _customerLastUpdateRaw = string.Empty;
    private DateTime _customerLastDataUtc = DateTime.MinValue;

    private static readonly Brush LampOnBrush = new SolidColorBrush(Color.FromRgb(46, 190, 78));
    private static readonly Brush LampOffBrush = new SolidColorBrush(Color.FromRgb(230, 45, 45));
    private static readonly Brush LampUnknownBrush = new SolidColorBrush(Color.FromRgb(176, 176, 176));

    private void CustomerUi_Loaded(object sender, RoutedEventArgs e)
    {
        if (_customerUiTimer is not null) return;

        _customerUiTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(350) };
        _customerUiTimer.Tick += (_, _) => RefreshCustomerUiFromExistingSnapshot();
        _customerUiTimer.Start();
        RefreshCustomerUiFromExistingSnapshot();
        AppendLog("客户实时监控页面已启用；断连/重连过程仅写诊断日志，主页面保持最后一次有效数据。", "APP");
    }

    private void RefreshCustomerUiFromExistingSnapshot()
    {
        UpdateCustomerSocAndCapacity();
        UpdateCustomerTemperatures();
        UpdateCustomerCells();
        UpdateCustomerSystemLamps();
        UpdateCustomerAlarms();
        UpdateCustomerIdentityFooter();
        UpdateCustomerCommunicationIndicator();
    }

    private void UpdateCustomerSocAndCapacity()
    {
        string socRaw = SocText.Text ?? string.Empty;
        Match soc = Regex.Match(socRaw, @"SOC\s*(\d+)\s*%", RegexOptions.IgnoreCase);
        Match soh = Regex.Match(socRaw, @"SOH\s*(\d+)\s*%", RegexOptions.IgnoreCase);

        if (soc.Success && int.TryParse(soc.Groups[1].Value, out int socPercent))
        {
            socPercent = Math.Clamp(socPercent, 0, 100);
            CustomerSocText.Text = $"SOC:{socPercent}%";
            CustomerSocFill.Height = 1.35 * socPercent;
        }

        if (soh.Success)
            CustomerSohText.Text = soh.Groups[1].Value + " %";

        string capacity = CapacityText.Text ?? string.Empty;
        CustomerRemainCapacityText.Text = ExtractValue(capacity, @"当前容量：([^\r\n]+)", "—");
        CustomerFullCapacityText.Text = ExtractValue(capacity, @"满充容量：([^\r\n]+)", "—");
        CustomerCycleText.Text = ExtractValue(capacity, @"循环次数：([^\r\n]+)", "—");
    }

    private void UpdateCustomerTemperatures()
    {
        string text = TempsText.Text ?? string.Empty;
        CustomerMaxTempText.Text = ExtractValue(text, @"最高温度：([^\r\n]+)", "—");
        CustomerMinTempText.Text = ExtractValue(text, @"最低温度：([^\r\n]+)", "—");
        CustomerMosTempText.Text = ExtractValue(text, @"MOS\s*温度：([^\r\n]+)", "—");
    }

    private void UpdateCustomerCells()
    {
        string raw = CellsText.Text ?? string.Empty;
        if (raw == _customerLastCellsRaw || string.IsNullOrWhiteSpace(raw) || raw == "—") return;
        _customerLastCellsRaw = raw;

        var cells = new List<(int Index, double Volts)>();
        foreach (Match match in Regex.Matches(raw, @"第\s*(\d+)\s*串\s*([0-9]+(?:\.[0-9]+)?)\s*V", RegexOptions.IgnoreCase))
        {
            if (int.TryParse(match.Groups[1].Value, out int index) &&
                double.TryParse(match.Groups[2].Value, NumberStyles.Float, CultureInfo.InvariantCulture, out double volts))
                cells.Add((index, volts));
        }

        if (cells.Count == 0) return;

        var max = cells.MaxBy(c => c.Volts);
        var min = cells.MinBy(c => c.Volts);
        double avgMv = cells.Average(c => c.Volts) * 1000.0;
        double deltaMv = (max.Volts - min.Volts) * 1000.0;

        CustomerMaxCellText.Text = $"{max.Volts * 1000.0:F0}    {max.Index}";
        CustomerMinCellText.Text = $"{min.Volts * 1000.0:F0}    {min.Index}";
        CustomerAvgCellText.Text = avgMv.ToString("F2", CultureInfo.InvariantCulture);
        CustomerDeltaCellText.Text = deltaMv.ToString("F0", CultureInfo.InvariantCulture);

        CustomerCellPanel.Children.Clear();
        foreach (var cell in cells.OrderBy(c => c.Index))
            CustomerCellPanel.Children.Add(CreateCellVisual(cell.Index, cell.Volts, cell.Index == max.Index, cell.Index == min.Index));
    }

    private static UIElement CreateCellVisual(int index, double volts, bool isMax, bool isMin)
    {
        var root = new Grid { Width = 200, Height = 58, Margin = new Thickness(4, 4, 4, 5) };
        root.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(72) });
        root.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });

        var battery = new Grid { Width = 58, Height = 34, HorizontalAlignment = HorizontalAlignment.Left, VerticalAlignment = VerticalAlignment.Center };
        battery.Children.Add(new Border
        {
            Width = 48,
            Height = 32,
            HorizontalAlignment = HorizontalAlignment.Left,
            BorderBrush = Brushes.Black,
            BorderThickness = new Thickness(1),
            CornerRadius = new CornerRadius(5),
            Background = new SolidColorBrush(Color.FromRgb(83, 225, 38))
        });
        battery.Children.Add(new Border
        {
            Width = 5,
            Height = 14,
            HorizontalAlignment = HorizontalAlignment.Right,
            VerticalAlignment = VerticalAlignment.Center,
            Background = Brushes.White,
            BorderBrush = Brushes.Black,
            BorderThickness = new Thickness(1, 1, 1, 1)
        });
        battery.Children.Add(new TextBlock
        {
            Text = index.ToString("00"),
            Foreground = Brushes.Red,
            FontWeight = FontWeights.Bold,
            HorizontalAlignment = HorizontalAlignment.Left,
            VerticalAlignment = VerticalAlignment.Center,
            Margin = new Thickness(14, 0, 0, 0)
        });
        Grid.SetColumn(battery, 0);
        root.Children.Add(battery);

        var voltage = new TextBlock
        {
            Text = (volts * 1000.0).ToString("F0", CultureInfo.InvariantCulture),
            FontSize = 15,
            FontWeight = isMax || isMin ? FontWeights.Bold : FontWeights.Normal,
            Foreground = isMax ? Brushes.Red : isMin ? Brushes.Green : Brushes.Black,
            VerticalAlignment = VerticalAlignment.Center,
            Margin = new Thickness(4, 0, 0, 0)
        };
        Grid.SetColumn(voltage, 1);
        root.Children.Add(voltage);
        return root;
    }

    private void UpdateCustomerSystemLamps()
    {
        string mos = MosStateText.Text ?? string.Empty;
        SetLamp(CustomerChargeMosLamp, CustomerChargeMosText, TryGetSwitchState(mos, "充电 MOS"));
        SetLamp(CustomerDischargeMosLamp, CustomerDischargeMosText, TryGetSwitchState(mos, "放电 MOS"));

        string system = SystemStateText.Text ?? string.Empty;
        SetLamp(CustomerHeatLamp, CustomerHeatText, TryGetSwitchState(system, "加热"));
        SetLamp(CustomerCoolLamp, CustomerCoolText, TryGetSwitchState(system, "制冷"));

        bool? balancing = null;
        if (Regex.IsMatch(system, @"均衡：\s*进行中")) balancing = true;
        else if (Regex.IsMatch(system, @"均衡：\s*未进行")) balancing = false;
        SetLamp(CustomerBalanceLamp, CustomerBalanceText, balancing);

        string afe = ExtractValue(system, @"AFE：([^\r\n]+)", "—");
        string systemState = ExtractValue(system, @"系统：([^\r\n]+)", "—");
        CustomerSystemMonitorText.Text = $"AFE：{afe}    系统：{systemState}";
    }

    private static bool? TryGetSwitchState(string source, string label)
    {
        Match match = Regex.Match(source, Regex.Escape(label) + @"：\s*(开启|关闭)");
        if (!match.Success) return null;
        return match.Groups[1].Value == "开启";
    }

    private static void SetLamp(Border border, TextBlock text, bool? state)
    {
        border.Background = state is null ? LampUnknownBrush : state.Value ? LampOnBrush : LampOffBrush;
        text.Text = state is null ? "--" : state.Value ? "on" : "off";
    }

    private void UpdateCustomerAlarms()
    {
        string text = ProtectionLevelsText.Text ?? string.Empty;
        CustomerAlarm1Text.Text = ExtractValue(text, @"一级：([^\r\n]+)", "无");
        CustomerAlarm2Text.Text = ExtractValue(text, @"二级：([^\r\n]+)", "无");
        CustomerAlarm3Text.Text = ExtractValue(text, @"三级：([^\r\n]+)", "无");
    }

    private void UpdateCustomerIdentityFooter()
    {
        string text = IdentityText.Text ?? string.Empty;
        CustomerHardwareText.Text = "硬件版本：" + ExtractValue(text, @"硬件版本：([^\r\n]+)", "—");
        CustomerSoftwareText.Text = "软件版本：" + ExtractValue(text, @"软件版本：([^\r\n]+)", "—");
        CustomerSerialText.Text = "BMS序列号：" + ExtractValue(text, @"序列号：([^\r\n]+)", "—");
    }

    private void UpdateCustomerCommunicationIndicator()
    {
        string update = LastUpdateText.Text ?? string.Empty;
        if (update != _customerLastUpdateRaw && !string.IsNullOrWhiteSpace(update) && update != "未读取")
        {
            _customerLastUpdateRaw = update;
            _customerLastDataUtc = DateTime.UtcNow;
        }

        bool dataFresh = _customerLastDataUtc != DateTime.MinValue &&
                         DateTime.UtcNow - _customerLastDataUtc < TimeSpan.FromSeconds(3.2);
        bool show = _bms is not null && dataFresh && !_autoReconnectRunning && !_otaRunning;
        CustomerCommStatusBorder.Visibility = show ? Visibility.Visible : Visibility.Collapsed;
    }

    private async void CustomerDisconnect_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            _pollTimer.Stop();
            _connectedAddress = null;
            _connectedName = string.Empty;
            _pollFailureCount = 0;
            _nextReconnectUtc = DateTime.MaxValue;
            CustomerCommStatusBorder.Visibility = Visibility.Collapsed;
            await DisposeBmsAsync();
            AppendLog("用户主动断开 BMS；客户主页面保留最后一次有效数据。", "CONNECT");
        }
        catch (Exception ex)
        {
            _sessionLog.WriteException("CONNECT", "CustomerDisconnect", ex);
            AppendLog($"主动断开异常：{ex.Message}", "ERROR");
        }
    }

    private void OpenNamePage_Click(object sender, RoutedEventArgs e) => MainTabs.SelectedItem = DeviceInfoTab;

    private static string ExtractValue(string source, string pattern, string fallback)
    {
        Match match = Regex.Match(source ?? string.Empty, pattern, RegexOptions.IgnoreCase);
        return match.Success ? match.Groups[1].Value.Trim() : fallback;
    }
}
