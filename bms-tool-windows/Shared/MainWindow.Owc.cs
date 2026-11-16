using System.Windows;

namespace BmsTool.Windows;

public partial class MainWindow
{
    private void OpenOwcMonitor_Click(object sender, RoutedEventArgs e)
    {
        MainTabs.SelectedIndex = 0;
        if (_bmsTransport is null && _integratedOwcTask is null)
            ConnectionModeBox.SelectedIndex = 2;
        UpdateUnifiedMonitorState();
    }
}
