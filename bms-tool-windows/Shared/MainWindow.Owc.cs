using System.Windows;

namespace BmsTool.Windows;

public partial class MainWindow
{
    private void OpenOwcMonitor_Click(object sender, RoutedEventArgs e)
    {
        if (_bmsTransport is not null || _bms is not null || _otaRunning ||
            _polling || _autoReconnectRunning || ShFactoryBusy())
        {
            MessageBox.Show(this, "请先断开现有BMS连接，并等待监控、升级或测试任务结束。", "一线通监控");
            return;
        }
        // 模态窗口使主窗口无法同时发起另一条串口/升级操作。
        _watcher?.Stop();
        new OwcMonitorWindow { Owner = this }.ShowDialog();
    }
}
