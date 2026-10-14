using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace BmsTool.Windows;

public partial class MainWindow
{
    private readonly TextBlock _mosControlStatus = new()
    {
        Text = "连接后读取独立 MOS 控制能力",
        Margin = new Thickness(8, 4, 12, 4),
        VerticalAlignment = VerticalAlignment.Center,
        TextWrapping = TextWrapping.Wrap
    };
    private readonly TextBlock _chargeMosControlState = new()
    {
        Text = "用户许可：待读取",
        Margin = new Thickness(0, 5, 0, 0),
        HorizontalAlignment = HorizontalAlignment.Center,
        TextAlignment = TextAlignment.Center,
        TextWrapping = TextWrapping.Wrap,
        FontSize = 11,
        Foreground = Brushes.DimGray
    };
    private readonly TextBlock _dischargeMosControlState = new()
    {
        Text = "用户许可：待读取",
        Margin = new Thickness(0, 5, 0, 0),
        HorizontalAlignment = HorizontalAlignment.Center,
        TextAlignment = TextAlignment.Center,
        TextWrapping = TextWrapping.Wrap,
        FontSize = 11,
        Foreground = Brushes.DimGray
    };
    private readonly Button _chargeMosControlButton = new()
    {
        Content = "充电 MOS",
        Margin = new Thickness(2, 5, 2, 0),
        Padding = new Thickness(8, 4, 8, 4),
        HorizontalAlignment = HorizontalAlignment.Center,
        MinWidth = 88,
        IsEnabled = false
    };
    private readonly Button _dischargeMosControlButton = new()
    {
        Content = "放电 MOS",
        Margin = new Thickness(2, 5, 2, 0),
        Padding = new Thickness(8, 4, 8, 4),
        HorizontalAlignment = HorizontalAlignment.Center,
        MinWidth = 88,
        IsEnabled = false
    };
    private MosControlState? _mosControlState;
    private DateTime _nextMosControlReadUtc;
    private bool _mosControlBusy;

    private void AddMosControlPanel()
    {
        if (CustomerChargeMosLamp.Parent is not StackPanel chargePanel ||
            CustomerDischargeMosLamp.Parent is not StackPanel dischargePanel ||
            chargePanel.Parent is not Grid grid || !ReferenceEquals(dischargePanel.Parent, grid))
            return;

        if (grid.RowDefinitions.Count == 0)
        {
            grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            foreach (UIElement child in grid.Children) Grid.SetRow(child, 0);
        }

        /* 主页原系统状态行固定为 132 px；加入状态文字和按钮后会被裁掉。 */
        if (grid.Parent is GroupBox group && group.Parent is Grid host)
        {
            int row = Grid.GetRow(group);
            if (row >= 0 && row < host.RowDefinitions.Count)
                host.RowDefinitions[row].Height = GridLength.Auto;
        }

        _chargeMosControlButton.Click += async (_, _) => await ChangeMosAllowedAsync(true);
        _dischargeMosControlButton.Click += async (_, _) => await ChangeMosAllowedAsync(false);
        _chargeMosControlButton.ToolTip = "设置充电 MOS 用户许可；放电期间固件可能安全临时开启，放电结束后自动恢复关闭。";
        _dischargeMosControlButton.ToolTip = "设置放电 MOS 用户许可；充电期间固件可能安全临时开启，充电结束后自动恢复关闭。";
        chargePanel.Children.Add(_chargeMosControlState);
        chargePanel.Children.Add(_chargeMosControlButton);
        dischargePanel.Children.Add(_dischargeMosControlState);
        dischargePanel.Children.Add(_dischargeMosControlButton);

        _mosControlStatus.Margin = new Thickness(8, 10, 8, 2);
        _mosControlStatus.HorizontalAlignment = HorizontalAlignment.Center;
        _mosControlStatus.TextAlignment = TextAlignment.Center;
        Grid.SetRow(_mosControlStatus, 1);
        Grid.SetColumnSpan(_mosControlStatus, 5);
        grid.Children.Add(_mosControlStatus);
    }

    private async Task RefreshMosControlStateAsync(BmsClient client, CancellationToken ct, bool force = false)
    {
        if (!force && DateTime.UtcNow < _nextMosControlReadUtc) return;
        _nextMosControlReadUtc = DateTime.UtcNow.AddSeconds(5);
        MosControlState? state = await client.TryReadMosControlAsync(ct);
        if (!ReferenceEquals(client, _bms)) return;
        ApplyMosControlState(state);
    }

    private void ApplyMosControlState(MosControlState? state)
    {
        _mosControlState = state;
        if (state is null)
        {
            _mosControlStatus.Text = "当前固件不支持独立充放电 MOS 控制";
            _mosControlStatus.Foreground = Brushes.DimGray;
            _chargeMosControlButton.Content = "充电 MOS 不支持";
            _dischargeMosControlButton.Content = "放电 MOS 不支持";
            _chargeMosControlState.Text = "当前固件不支持";
            _chargeMosControlState.Foreground = Brushes.DimGray;
            _chargeMosControlState.FontWeight = FontWeights.Normal;
            _dischargeMosControlState.Text = "当前固件不支持";
            _dischargeMosControlState.Foreground = Brushes.DimGray;
            _dischargeMosControlState.FontWeight = FontWeights.Normal;
            _chargeMosControlButton.IsEnabled = false;
            _dischargeMosControlButton.IsEnabled = false;
            return;
        }

        bool chargeTemporaryOn = !state.ChargeAllowed && state.DriverStateValid && state.ChargeDriverOn;
        bool dischargeTemporaryOn = !state.DischargeAllowed && state.DriverStateValid && state.DischargeDriverOn;
        string report = state.DriverStateValid
            ? $"AFE 报告：充{(state.ChargeDriverOn ? "开" : "关")} / 放{(state.DischargeDriverOn ? "开" : "关")}"
            : "AFE 报告：暂不可用";
        string gate = state.OutputEnabled && !state.CommunicationInhibited && !state.FaultLatched
            ? "保护门禁允许输出"
            : "保护门禁正在限制输出";
        string temporary = chargeTemporaryOn && dischargeTemporaryOn
            ? "充放电 MOS 均处于安全临时开启"
            : chargeTemporaryOn
                ? "充电 MOS 因放电方向安全临时开启，放电结束后自动关闭"
                : dischargeTemporaryOn
                    ? "放电 MOS 因充电方向安全临时开启，充电结束后自动关闭"
                    : string.Empty;
        _mosControlStatus.Text =
            $"用户许可：充{(state.ChargeAllowed ? "允许" : "禁止")} / 放{(state.DischargeAllowed ? "允许" : "禁止")}；" +
            $"产品请求：充{(state.ChargeRequested ? "开" : "关")} / 放{(state.DischargeRequested ? "开" : "关")}；{report}；{gate}" +
            (temporary.Length == 0 ? string.Empty : $"；{temporary}");
        _mosControlStatus.Foreground = temporary.Length != 0
            ? Brushes.DarkOrange
            : state.ChargeAllowed && state.DischargeAllowed ? Brushes.DarkSlateGray : Brushes.Firebrick;
        SetMosChannelControlState(_chargeMosControlState, state.ChargeAllowed,
            state.ChargeDriverOn, state.DriverStateValid, "放电中安全临时开启");
        SetMosChannelControlState(_dischargeMosControlState, state.DischargeAllowed,
            state.DischargeDriverOn, state.DriverStateValid, "充电中安全临时开启");
        _chargeMosControlButton.Content = state.ChargeAllowed ? "禁止充电 MOS" : "允许充电 MOS";
        _dischargeMosControlButton.Content = state.DischargeAllowed ? "禁止放电 MOS" : "允许放电 MOS";
        _chargeMosControlButton.IsEnabled = !_mosControlBusy && _bms is not null && !_otaRunning;
        _dischargeMosControlButton.IsEnabled = !_mosControlBusy && _bms is not null && !_otaRunning;
        SetLamp(CustomerChargeMosLamp, CustomerChargeMosText,
            state.DriverStateValid ? state.ChargeDriverOn : null);
        SetLamp(CustomerDischargeMosLamp, CustomerDischargeMosText,
            state.DriverStateValid ? state.DischargeDriverOn : null);
    }

    private static void SetMosChannelControlState(TextBlock target, bool allowed,
        bool driverOn, bool driverValid, string temporaryText)
    {
        target.FontWeight = FontWeights.Normal;
        if (!driverValid)
        {
            target.Text = allowed ? "用户许可：允许\n实际状态未知" : "用户许可：禁止\n实际状态未知";
            target.Foreground = Brushes.DimGray;
        }
        else if (!allowed && driverOn)
        {
            target.Text = temporaryText;
            target.Foreground = Brushes.DarkOrange;
            target.FontWeight = FontWeights.SemiBold;
        }
        else if (!allowed)
        {
            target.Text = "用户已禁止\n实际已关闭";
            target.Foreground = Brushes.Firebrick;
        }
        else if (driverOn)
        {
            target.Text = "用户允许\n实际已开启";
            target.Foreground = Brushes.DarkGreen;
        }
        else
        {
            target.Text = "用户允许\n保护/门禁关闭";
            target.Foreground = Brushes.DimGray;
        }
    }

    private async Task ChangeMosAllowedAsync(bool chargeChannel)
    {
        MosControlState? current = _mosControlState;
        BmsClient? client = _bms;
        if (current is null || client is null || _mosControlBusy || _otaRunning) return;
        bool allowed = chargeChannel ? current.ChargeAllowed : current.DischargeAllowed;
        string channel = chargeChannel ? "充电" : "放电";
        string action = allowed ? "禁止" : "允许";
        string oppositeDirection = chargeChannel ? "放电" : "充电";
        string explanation = allowed
            ? $"将{action}{channel} MOS。该许可只保存在 RAM，MCU 重启后恢复允许。" +
              $"若检测到{oppositeDirection}，固件会为保护反向电流通路临时开启该 MOS；" +
              $"{oppositeDirection}结束后自动恢复关闭。"
            : $"将{action}{channel} MOS。保护、采样资格和 AFE 仍可保持 MOS 关闭。";
        if (MessageBox.Show(explanation + "\n\n这不是休眠命令，是否继续？", action + channel + " MOS",
                MessageBoxButton.YesNo, MessageBoxImage.Warning) != MessageBoxResult.Yes)
            return;

        _mosControlBusy = true;
        ApplyMosControlState(current);
        try
        {
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(20));
            MosControlState after = await client.SetMosChannelAllowedAsync(chargeChannel, !allowed, timeout.Token);
            if (!ReferenceEquals(client, _bms)) return;
            _nextMosControlReadUtc = DateTime.MinValue;
            ApplyMosControlState(after);
            AppendLog($"{channel} MOS 用户许可已{action}并读回；实际输出仍受保护控制。", "MOS_CONTROL");
        }
        catch (Exception ex)
        {
            AppendLog($"{action}{channel} MOS 失败：{ex.Message}", "MOS_CONTROL");
            MessageBox.Show(ex.Message, "MOS 控制失败", MessageBoxButton.OK, MessageBoxImage.Error);
        }
        finally
        {
            _mosControlBusy = false;
            ApplyMosControlState(_mosControlState);
        }
    }

    private void ResetMosControlUi()
    {
        _mosControlState = null;
        _nextMosControlReadUtc = DateTime.MinValue;
        _mosControlStatus.Text = "连接后读取独立 MOS 控制能力";
        _mosControlStatus.Foreground = Brushes.DimGray;
        _chargeMosControlButton.Content = "充电 MOS";
        _dischargeMosControlButton.Content = "放电 MOS";
        _chargeMosControlState.Text = "用户许可：待读取";
        _chargeMosControlState.Foreground = Brushes.DimGray;
        _chargeMosControlState.FontWeight = FontWeights.Normal;
        _dischargeMosControlState.Text = "用户许可：待读取";
        _dischargeMosControlState.Foreground = Brushes.DimGray;
        _dischargeMosControlState.FontWeight = FontWeights.Normal;
        _chargeMosControlButton.IsEnabled = false;
        _dischargeMosControlButton.IsEnabled = false;
        SetLamp(CustomerChargeMosLamp, CustomerChargeMosText, null);
        SetLamp(CustomerDischargeMosLamp, CustomerDischargeMosText, null);
    }
}
