using System.Windows.Media;

namespace BmsTool.Windows;

public partial class MainWindow
{
    private BmsClient? _sleepStatusClient;
    private DateTime _sleepStatusReceivedUtc;

    private async Task RefreshSleepStatusAsync(BmsClient client)
    {
        try {
            var status = await client.ReadSleepStatusAsync();
            ApplySleepStatus(client, status);
        }
        catch (Exception ex) {
            if (!ReferenceEquals(client, _bms)) return;
            _sleepStatusClient = client;
            _sleepStatusReceivedUtc = DateTime.UtcNow;
            SleepStatusText.Text = "休眠状态读取失败，当前状态未知：" + ex.Message;
            SleepStatusText.Foreground = Brushes.DarkOrange;
        }
    }

    private void ApplySleepStatus(BmsClient client, BmsSleepStatus? status)
    {
        if (!ReferenceEquals(client, _bms)) return;
        _sleepStatusClient = client;
        _sleepStatusReceivedUtc = DateTime.UtcNow;
        SleepStatusText.Text = status is null ? "当前固件不支持实时休眠原因和倒计时" :
            !status.Fresh ? "休眠状态数据已过期，当前状态未知" : status.Summary;
        SleepStatusText.Foreground = status is null || !status.Fresh ? Brushes.DimGray :
            status.BlockMask != 0 ? Brushes.DarkOrange : Brushes.DarkSlateGray;
    }

    // 在已有界面定时器上检查新鲜度，不新增通信、不推算设备倒计时。
    private void RefreshSleepStatusFreshness()
    {
        if (_bms is null || !ReferenceEquals(_sleepStatusClient, _bms)) {
            if (_bms is null) SleepStatusText.Text = "未连接，无法确认设备是否休眠";
            else SleepStatusText.Text = "连接已改变，等待新的休眠状态";
            SleepStatusText.Foreground = Brushes.DimGray;
        } else if (DateTime.UtcNow - _sleepStatusReceivedUtc > TimeSpan.FromSeconds(5)) {
            SleepStatusText.Text = "休眠状态数据已过期，当前状态未知；等待恢复读取";
            SleepStatusText.Foreground = Brushes.DimGray;
        }
    }
}
