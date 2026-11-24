using BmsTool.Windows;
using Microsoft.Win32;
using System.IO;
using System.Security.Cryptography;
using System.Windows;
using System.Windows.Controls;

namespace BmsTool.Android.Deployer;

public partial class MainWindow : Window
{
    private AndroidFirmwareSender? _sender;
    private CancellationTokenSource? _sendCts;
    private bool _busy;

    public MainWindow()
    {
        InitializeComponent();
        string[] resources = typeof(MainWindow).Assembly.GetManifestResourceNames();
        if (resources.Contains("BmsTool.Customer.apk") && resources.Contains("BmsTool.Internal.apk"))
        {
            InstallPanel.Visibility = Visibility.Visible;
            Title = "BMS Tool 安卓安装与固件发送";
        }
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
        if (_sender is null || _busy) return;
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
        if (_busy) return;
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
            DeploymentResult result = await _sender.SendAsync(device.Id, path, false, progress, _sendCts.Token);
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

    private async void InstallApk_Click(object sender, RoutedEventArgs e)
    {
        if (_busy) return;
        if (_sender is null) { SetStatus("请先配置 ADB，然后重新打开工具。", true); return; }
        if (DeviceCombo.SelectedItem is not AndroidDeviceInfo device)
        {
            SetStatus("请选择已授权的 Android 手机。", true);
            return;
        }
        if (sender is not Button { Tag: string edition } || edition is not ("Customer" or "Internal")) return;
        string label = edition == "Customer" ? "客户版" : "测试版";
        if (MessageBox.Show(this, $"将向 {device.DisplayName} 安装 BMS Tool {label}，是否继续？",
            "确认安装", MessageBoxButton.YesNo, MessageBoxImage.Question) != MessageBoxResult.Yes) return;

        string apk = Path.Combine(Path.GetTempPath(), $"BmsTool-{edition}-{Guid.NewGuid():N}.apk");
        try
        {
            SetBusy(true);
            SetStatus($"正在安装 {label}…");
            using Stream package = typeof(MainWindow).Assembly.GetManifestResourceStream($"BmsTool.{edition}.apk")
                ?? throw new IOException("EXE 未包含所选安装包。");
            await using (var file = new FileStream(apk, FileMode.CreateNew, FileAccess.Write, FileShare.None, 65536, useAsync: true))
                await package.CopyToAsync(file);
            using var timeout = new CancellationTokenSource(TimeSpan.FromMinutes(3));
            await _sender.InstallApkAsync(device.Id, apk, timeout.Token);
            SetStatus($"{label}已安装 · {device.DisplayName}");
            AppendLog($"INSTALL_OK edition={edition} device={device.Id}");
        }
        catch (Exception ex)
        {
            SetStatus("安装失败：" + ex.Message, true);
            AppendLog("INSTALL_FAIL " + ex.Message);
        }
        finally
        {
            try { File.Delete(apk); }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
            {
                AppendLog("清理临时 APK 失败：" + ex.Message);
            }
            SetBusy(false);
        }
    }

    private void SetBusy(bool busy)
    {
        _busy = busy;
        InstallPanel.IsEnabled = !busy;
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
