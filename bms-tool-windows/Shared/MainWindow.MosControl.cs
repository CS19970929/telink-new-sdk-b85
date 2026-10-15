using System.Windows;
using System.Windows.Controls;

namespace BmsTool.Windows;

public partial class MainWindow
{
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

        /* MOS 状态与控制按钮使用内容高度，避免较小窗口下裁剪。 */
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
        chargePanel.Children.Add(_chargeMosControlButton);
        dischargePanel.Children.Add(_dischargeMosControlButton);
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
            _chargeMosControlButton.Content = "充电 MOS";
            _dischargeMosControlButton.Content = "放电 MOS";
            _chargeMosControlButton.IsEnabled = false;
            _dischargeMosControlButton.IsEnabled = false;
            return;
        }

        _chargeMosControlButton.Content = state.ChargeAllowed ? "禁止充电 MOS" : "允许充电 MOS";
        _dischargeMosControlButton.Content = state.DischargeAllowed ? "禁止放电 MOS" : "允许放电 MOS";
        _chargeMosControlButton.IsEnabled = !_mosControlBusy && _bms is not null && !_otaRunning;
        _dischargeMosControlButton.IsEnabled = !_mosControlBusy && _bms is not null && !_otaRunning;
        SetLamp(CustomerChargeMosLamp, CustomerChargeMosText,
            state.DriverStateValid ? state.ChargeDriverOn : null);
        SetLamp(CustomerDischargeMosLamp, CustomerDischargeMosText,
            state.DriverStateValid ? state.DischargeDriverOn : null);
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
        _chargeMosControlButton.Content = "充电 MOS";
        _dischargeMosControlButton.Content = "放电 MOS";
        _chargeMosControlButton.IsEnabled = false;
        _dischargeMosControlButton.IsEnabled = false;
        SetLamp(CustomerChargeMosLamp, CustomerChargeMosText, null);
        SetLamp(CustomerDischargeMosLamp, CustomerDischargeMosText, null);
    }
}
