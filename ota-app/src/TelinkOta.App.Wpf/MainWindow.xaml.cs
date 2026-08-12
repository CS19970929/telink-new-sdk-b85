using System.Windows;
using System.Windows.Controls;
using Microsoft.Win32;
using TelinkOta.App.Wpf.ViewModels;

namespace TelinkOta.App.Wpf;

public partial class MainWindow : Window
{
    private readonly MainViewModel _vm;

    public MainWindow()
    {
        InitializeComponent();
        _vm = new MainViewModel(Dispatcher);
        DataContext = _vm;
        Closed += (_, _) =>
        {
            _vm.StopScan();
            _vm.CancelOta();
            _vm.DisconnectBattery();
        };
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
