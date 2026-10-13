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
    private readonly Button _chargeMosControlButton = new() { Content = "充电 MOS", Margin = new Thickness(4), Padding = new Thickness(12, 5, 12, 5), IsEnabled = false };
    private readonly Button _dischargeMosControlButton = new() { Content = "放电 MOS", Margin = new Thickness(4), Padding = new Thickness(12, 5, 12, 5), IsEnabled = false };
    private MosControlState? _mosControlState;
    private DateTime _nextMosControlReadUtc;
    private bool _mosControlBusy;

    private void AddMosControlPanel()
    {
        if (CustomerChargeMosLamp.Parent is not StackPanel chargePanel || chargePanel.Parent is not Grid grid)
            return;

        if (grid.RowDefinitions.Count == 0)
        {
            grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            foreach (UIElement child in grid.Children) Grid.SetRow(child, 0);
        }

        var buttons = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right };
        _chargeMosControlButton.Click += async (_, _) => await ChangeMosAllowedAsync(true);
        _dischargeMosControlButton.Click += async (_, _) => await ChangeMosAllowedAsync(false);
        buttons.Children.Add(_chargeMosControlButton);
        buttons.Children.Add(_dischargeMosControlButton);

        var panel = new DockPanel { Margin = new Thickness(4, 8, 4, 0) };
        DockPanel.SetDock(buttons, Dock.Right);
        panel.Children.Add(buttons);
        panel.Children.Add(_mosControlStatus);
        Grid.SetRow(panel, 1);
        Grid.SetColumnSpan(panel, 5);
        grid.Children.Add(panel);
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
            _chargeMosControlButton.IsEnabled = false;
            _dischargeMosControlButton.IsEnabled = false;
            return;
        }

        string report = state.DriverStateValid
            ? $"AFE 报告：充{(state.ChargeDriverOn ? "开" : "关")} / 放{(state.DischargeDriverOn ? "开" : "关")}"
            : "AFE 报告：暂不可用";
        string gate = state.OutputEnabled && !state.CommunicationInhibited && !state.FaultLatched
            ? "保护门禁允许输出"
            : "保护门禁正在限制输出";
        _mosControlStatus.Text =
            $"用户许可：充{(state.ChargeAllowed ? "允许" : "禁止")} / 放{(state.DischargeAllowed ? "允许" : "禁止")}；" +
            $"产品请求：充{(state.ChargeRequested ? "开" : "关")} / 放{(state.DischargeRequested ? "开" : "关")}；{report}；{gate}";
        _mosControlStatus.Foreground = state.ChargeAllowed && state.DischargeAllowed ? Brushes.DarkSlateGray : Brushes.Firebrick;
        _chargeMosControlButton.Content = state.ChargeAllowed ? "禁止充电 MOS" : "允许充电 MOS";
        _dischargeMosControlButton.Content = state.DischargeAllowed ? "禁止放电 MOS" : "允许放电 MOS";
        _chargeMosControlButton.IsEnabled = !_mosControlBusy && _bms is not null && !_otaRunning;
        _dischargeMosControlButton.IsEnabled = !_mosControlBusy && _bms is not null && !_otaRunning;
    }

    private async Task ChangeMosAllowedAsync(bool chargeChannel)
    {
        MosControlState? current = _mosControlState;
        BmsClient? client = _bms;
        if (current is null || client is null || _mosControlBusy || _otaRunning) return;
        bool allowed = chargeChannel ? current.ChargeAllowed : current.DischargeAllowed;
        string channel = chargeChannel ? "充电" : "放电";
        string action = allowed ? "禁止" : "允许";
        string explanation = allowed
            ? $"将{action}{channel} MOS。该许可只保存在 RAM，MCU 重启后恢复允许。"
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
        _chargeMosControlButton.IsEnabled = false;
        _dischargeMosControlButton.IsEnabled = false;
    }
}
