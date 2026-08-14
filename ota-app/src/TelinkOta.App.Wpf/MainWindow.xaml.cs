using System.ComponentModel;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
using Microsoft.Win32;
using TelinkOta.App.Wpf.ViewModels;

namespace TelinkOta.App.Wpf;

public partial class MainWindow : Window
{
    private readonly MainViewModel _vm;
    private bool _shutdownStarted;

    public MainWindow()
    {
        InitializeComponent();
        _vm = new MainViewModel(Dispatcher);
        DataContext = _vm;
        Closing += MainWindow_Closing;
    }

    private async void MainWindow_Closing(object? sender, CancelEventArgs e)
    {
        if (_shutdownStarted)
            return;

        // WPF 的 Closed 事件无法等待 async 清理。先取消本次关闭，待 BLE 句柄完全释放后再关闭窗口，
        // 否则立即重开 App 时 Windows 仍可能把旧 GATT 请求留在适配器队列中。
        e.Cancel = true;
        _shutdownStarted = true;
        IsEnabled = false;
        try
        {
            await _vm.ShutdownAsync();
        }
        finally
        {
            // 即使 ShutdownAsync 在空闲状态同步完成，也必须先退出当前 Closing 回调栈，
            // 否则 WPF 会以“窗口关闭期间再次 Close”拒绝重入。
            await Dispatcher.Yield(DispatcherPriority.ApplicationIdle);
            Closing -= MainWindow_Closing;
            Close();
        }
    }

    private void ScanButton_Click(object sender, RoutedEventArgs e) => _vm.StartScan();

    private void StopScanButton_Click(object sender, RoutedEventArgs e) => _vm.StopScan();

    private void PickFirmwareButton_Click(object sender, RoutedEventArgs e)
    {
        var dlg = new OpenFileDialog
        {
            Filter = "Firmware BIN (*.bin)|*.bin|所有文件 (*.*)|*.*",
            Title = "选择 Telink Firmware BIN",
        };
        if (dlg.ShowDialog(this) == true)
        {
            var error = _vm.ChooseFirmware(dlg.FileName);
            if (error is not null)
                MessageBox.Show(this, error, "固件检查失败", MessageBoxButton.OK, MessageBoxImage.Warning);
        }
    }

    private void StartOtaButton_Click(object sender, RoutedEventArgs e) => _vm.StartOta();

    private void CancelOtaButton_Click(object sender, RoutedEventArgs e) => _vm.CancelOta();

    private void ConnectBatteryButton_Click(object sender, RoutedEventArgs e) => _vm.ConnectBattery();

    private void DisconnectBatteryButton_Click(object sender, RoutedEventArgs e) => _vm.DisconnectBattery();

    private void ChangeBluetoothNameButton_Click(object sender, RoutedEventArgs e) => _vm.ChangeBluetoothName();

    private void LogBox_TextChanged(object sender, TextChangedEventArgs e)
    {
        LogBox.ScrollToEnd();
    }
}
