using System;
using System.Windows;
using System.Windows.Controls;

namespace BmsTool.Windows;

public partial class MainWindow
{
    private const ushort SleepCommandRegister = 0x1102;
    private const ushort SleepCommandValue = 0x000A;

    private async void SleepBms_Click(object sender, RoutedEventArgs e)
    {
        if (_eventLogReadInProgress || _otaRunning || _shBusy || ShFactoryBusy()) return;
        BmsClient? bms = _bms;
        if (bms is null)
        {
            MessageBox.Show(this, "请先连接BMS。", "设备休眠", MessageBoxButton.OK, MessageBoxImage.Information);
            return;
        }
        if (MessageBox.Show(this, "确认让BMS进入深度休眠？充放电将停止，通信将断开。需要按板上唤醒按键后重新连接。",
                "设备休眠", MessageBoxButton.YesNo, MessageBoxImage.Warning, MessageBoxResult.No) != MessageBoxResult.Yes) return;

        _eventLogReadInProgress = true;
        if (sender is Button button) button.IsEnabled = false;
        bool accepted = false;
        try
        {
            _pollTimer.Stop();
            await WaitForCommunicationIdleAsync();
            if (!ReferenceEquals(bms, _bms)) throw new InvalidOperationException("连接已改变，请重新操作。");
            // Firmware queues sleep until its command reply and pending storage writes finish.
            await bms.WriteSingleRegisterAsync(SleepCommandRegister, SleepCommandValue);
            accepted = true;
            AppendLog("BMS_SLEEP_ACCEPTED: 设备已接受休眠请求，停止轮询和自动重连。", "APP");
            _connectedAddress = null;
            _connectedSerialPort = null;
            _connectedName = string.Empty;
            _pollFailureCount = 0;
            _nextReconnectUtc = DateTime.MaxValue;
            CustomerCommStatusBorder.Visibility = Visibility.Collapsed;
            await DisposeBmsAsync();
            ConnectionText.Text = "休眠请求已接受，请按键唤醒后重新连接";
            MessageBox.Show(this, "设备已接受休眠请求，上位机已断开。请按板上唤醒按键后重新连接。",
                "设备休眠", MessageBoxButton.OK, MessageBoxImage.Information);
        }
        catch (Exception ex)
        {
            ShowError(accepted ? "休眠请求已接受，但连接清理异常" : "休眠请求未确认，请核实板子状态；未自动重发", ex);
        }
        finally
        {
            _eventLogReadInProgress = false;
            if (sender is Button sleepButton) sleepButton.IsEnabled = true;
            if (!accepted) StartAutomaticRefresh();
        }
    }
}
