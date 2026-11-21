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
        AppendLog("客户实时监控页面已启用；连续8秒无有效设备响应时清空设备显示，恢复后重新读取。", "APP");
    }

    private void RefreshCustomerUiFromExistingSnapshot()
    {
        if (IsOwcMode) return;
        if (!CheckDeviceDisplayFreshness()) { UpdateCommunicationStatus(); return; }
        UpdateCustomerSocAndCapacity();
        UpdateCustomerTemperatures();
        UpdateCustomerCells();
        UpdateCustomerSystemLamps();
        CustomerSystemMonitorText.Text = string.IsNullOrWhiteSpace(SystemStateText.Text) ? "—" : SystemStateText.Text;
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
        else
            CustomerSohText.Text = "—";

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

    private static UIElement CreateCellVisual(int index, double? volts, bool isMax, bool isMin, bool balancing)
    {
        var root = new Grid { Width = 160, Height = 48, Margin = new Thickness(4, 4, 4, 5) };
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
            Background = volts.HasValue ? new SolidColorBrush(Color.FromRgb(83, 225, 38)) : Brushes.LightGray
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
            Text = volts is double value ? (value * 1000.0).ToString("F0", CultureInfo.InvariantCulture) : "—",
            ToolTip = "均衡标记表示 AFE 最后回读的已启用通道；断连或 AFE 异常时数据可能过期。",
            FontSize = 15,
            FontWeight = isMax || isMin ? FontWeights.Bold : FontWeights.Normal,
            Foreground = isMax ? Brushes.Red : isMin ? Brushes.Green : Brushes.Black,
            VerticalAlignment = VerticalAlignment.Center,
            Margin = new Thickness(4, 0, 0, 0)
        };
        var reading = new StackPanel { Orientation = Orientation.Horizontal, VerticalAlignment = VerticalAlignment.Center };
        reading.Children.Add(voltage);
        if (balancing) reading.Children.Add(CreateBalanceIcon());
        Grid.SetColumn(reading, 1);
        root.Children.Add(reading);
        return root;
    }

    // 使用矢量双横线，避免符号字体差异；同一图标也用于内部版单体卡片。
    private static Border CreateBalanceIcon()
    {
        var bars = new Grid { Width = 8, Height = 7 };
        bars.Children.Add(new Border { Height = 2, Background = Brushes.White, VerticalAlignment = VerticalAlignment.Top });
        bars.Children.Add(new Border { Height = 2, Background = Brushes.White, VerticalAlignment = VerticalAlignment.Bottom });
        var icon = new Border
        {
            Width = 18, Height = 18, CornerRadius = new CornerRadius(9),
            Margin = new Thickness(6, 0, 0, 0), VerticalAlignment = VerticalAlignment.Center,
            Background = new SolidColorBrush(Color.FromRgb(190, 95, 0)), Child = bars,
            ToolTip = "均衡中：AFE 最后回读的已启用通道；断连或 AFE 异常时数据可能过期。"
        };
        System.Windows.Automation.AutomationProperties.SetName(icon, "均衡中");
        return icon;
    }

    private void UpdateCustomerSystemLamps()
    {
        string mos = MosStateText.Text ?? string.Empty;
        MosControlState? control = _mosControlState;
        bool mosUnknown = _bms is null || _customerLastDataUtc == DateTime.MinValue ||
            DateTime.UtcNow - _customerLastDataUtc > TimeSpan.FromSeconds(5) ||
            (control is not null ? !control.DriverStateValid : _mosAuto.IsChecked == true && _mosDriverInvalid);
        SetLamp(CustomerChargeMosLamp, CustomerChargeMosText, mosUnknown ? null :
            control is { DriverStateValid: true } ? control.ChargeDriverOn : TryGetSwitchState(mos, "充电 MOS"));
        SetLamp(CustomerDischargeMosLamp, CustomerDischargeMosText, mosUnknown ? null :
            control is { DriverStateValid: true } ? control.DischargeDriverOn : TryGetSwitchState(mos, "放电 MOS"));
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
        CustomerAlarm1Text.Text = ExtractValue(text, @"一级：([^\r\n]+)", "—");
        CustomerAlarm2Text.Text = ExtractValue(text, @"二级：([^\r\n]+)", "—");
        CustomerAlarm3Text.Text = ExtractValue(text, @"三级：([^\r\n]+)", "—");
    }

    private void UpdateCustomerIdentityFooter()
    {
        string text = IdentityText.Text ?? string.Empty;
        CustomerHardwareText.Text = "硬件版本：" + ExtractValue(text, @"硬件版本：([^\r\n]+)", "—");
        CustomerSoftwareText.Text = "软件版本：" + ExtractValue(text, @"软件版本：([^\r\n]+)", "—");
        CustomerSerialText.Text = "BMS序列号：" + ExtractValue(text, @"序列号：([^\r\n]+)", "—");
        UpdateCustomerBuildFooter();
    }

    private void UpdateCustomerCommunicationIndicator() => UpdateCommunicationStatus();

    private async void CustomerDisconnect_Click(object sender, RoutedEventArgs e)
    {
        await StopIntegratedOwcAsync();
        PublishUnifiedReading(null);
        try
        {
            _pollTimer.Stop();
            _connectedAddress = null;
            _connectedSerialPort = null;
            _connectedName = string.Empty;
            _pollFailureCount = 0;
            _nextReconnectUtc = DateTime.MaxValue;
            CustomerCommStatusBorder.Visibility = Visibility.Collapsed;
            await DisposeBmsAsync();
            ConnectionText.Text = "已断开";
            AppendLog("用户主动断开 BMS；设备显示已清空。", "CONNECT");
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
