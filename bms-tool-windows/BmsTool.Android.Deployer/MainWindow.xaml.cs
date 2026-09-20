using BmsTool.Windows;
using Microsoft.Win32;
using System.IO;
using System.Security.Cryptography;
using System.Windows;

namespace BmsTool.Android.Deployer;

public partial class MainWindow : Window
{
    private AndroidFirmwareSender? _sender;
    private CancellationTokenSource? _sendCts;

    public MainWindow()
    {
        InitializeComponent();
        Loaded += async (_, _) => await InitializeAsync();
    }

    private async Task InitializeAsync()
    {
        try
        {
            string adb = AndroidFirmwareSender.FindAdb();
            _sender = new AndroidFirmwareSender(adb);
            AppendLog($"ADB: {adb}");
            await RefreshDevicesAsync();
        }
        catch (Exception ex)
        {
            SetStatus(ex.Message, true);
            AppendLog("ERROR " + ex.Message);
        }
    }

    private async void RefreshDevices_Click(object sender, RoutedEventArgs e) => await RefreshDevicesAsync();

    private async Task RefreshDevicesAsync()
    {
        if (_sender is null) return;
        try
        {
            SetBusy(true);
            SetStatus("正在查找已授权的 Android 手机…");
            IReadOnlyList<AndroidDeviceInfo> devices = await _sender.ListDevicesAsync(CancellationToken.None);
            DeviceCombo.ItemsSource = devices;
            DeviceCombo.SelectedIndex = devices.Count == 1 ? 0 : -1;
            SetStatus(devices.Count switch
            {
                0 => "未发现已授权手机。请连接 USB，并在手机上允许 USB 调试。",
                1 => "已找到 1 台手机。",
                _ => $"已找到 {devices.Count} 台手机，请明确选择目标手机。"
            }, devices.Count == 0);
            foreach (AndroidDeviceInfo device in devices) AppendLog("DEVICE " + device.DisplayName);
        }
        catch (Exception ex)
        {
            SetStatus("刷新手机失败：" + ex.Message, true);
            AppendLog("ERROR " + ex.Message);
        }
        finally { SetBusy(false); }
    }

    private void BrowseFirmware_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFileDialog
        {
            Title = "选择 Telink 正式 OTA BIN",
            Filter = "正式固件 BIN (*.bin)|*.bin|所有文件 (*.*)|*.*",
            CheckFileExists = true
        };
        if (dialog.ShowDialog(this) == true) FirmwarePathBox.Text = dialog.FileName;
    }

    private void Window_Drop(object sender, DragEventArgs e)
    {
        if (e.Data.GetData(DataFormats.FileDrop) is string[] { Length: > 0 } files)
            FirmwarePathBox.Text = files[0];
    }

    private void FirmwarePathBox_TextChanged(object sender, System.Windows.Controls.TextChangedEventArgs e)
    {
        ValidateFirmware(FirmwarePathBox.Text.Trim());
    }

    private bool ValidateFirmware(string path)
    {
        try
        {
            if (string.IsNullOrWhiteSpace(path) || !File.Exists(path))
            {
                FirmwareInfoText.Text = "选择或拖入 PC 已编译的 825x_ble_sample.bin";
                return false;
            }
            FirmwareImage image = FirmwareImage.LoadStrictTelink(path);
            string sha = Convert.ToHexString(SHA256.HashData(image.Bytes));
            FirmwareInfoText.Text = $"预检通过 · {image.FileName} · {image.ImageSize:N0} bytes · SHA-256 {sha}";
            FirmwareInfoText.Foreground = System.Windows.Media.Brushes.DarkGreen;
            return true;
        }
        catch (Exception ex)
        {
            FirmwareInfoText.Text = "预检不通过：" + ex.Message;
            FirmwareInfoText.Foreground = System.Windows.Media.Brushes.Firebrick;
            return false;
        }
    }

    private async void SendFirmware_Click(object sender, RoutedEventArgs e)
    {
        if (_sender is null) { SetStatus("ADB 尚未就绪。", true); return; }
        if (DeviceCombo.SelectedItem is not AndroidDeviceInfo device)
        {
            SetStatus("请明确选择一台已连接手机。", true);
            return;
        }
        string path = FirmwarePathBox.Text.Trim();
        if (!ValidateFirmware(path)) { SetStatus("固件预检未通过。", true); return; }

        _sendCts?.Cancel();
        _sendCts?.Dispose();
        _sendCts = new CancellationTokenSource(TimeSpan.FromMinutes(2));
        try
        {
            SetBusy(true);
            SendProgress.Value = 0;
            var progress = new Progress<DeploymentProgress>(value =>
            {
                SendProgress.Value = value.Percent;
                SetStatus(value.Message);
                AppendLog(value.Message);
            });
            DeploymentResult result = await _sender.SendAsync(device.Id, path, progress, _sendCts.Token);
            SendProgress.Value = 100;
            SetStatus($"发送完成：{result.RemoteName}。手机已打开固件页，请在 App 内选择 BT_ / BT- 设备并确认 OTA。");
            AppendLog($"DONE bytes={result.Size} sha256={result.Sha256} remote={result.RemoteName}");
        }
        catch (Exception ex)
        {
            SetStatus("发送失败：" + ex.Message, true);
            AppendLog("ERROR " + ex.Message);
        }
        finally { SetBusy(false); }
    }

    private void SetBusy(bool busy)
    {
        SendButton.IsEnabled = !busy;
        DeviceCombo.IsEnabled = !busy;
    }

    private void SetStatus(string text, bool error = false)
    {
        StatusText.Text = text;
        StatusText.Foreground = error ? System.Windows.Media.Brushes.Firebrick : System.Windows.Media.Brushes.DarkGreen;
    }

    private void AppendLog(string text)
    {
        LogBox.AppendText($"{DateTime.Now:HH:mm:ss.fff} {text}{Environment.NewLine}");
        LogBox.ScrollToEnd();
    }
}
