using System.Collections.ObjectModel;
using System.Windows;
using Bms.Ota.Core.Firmware;
using Bms.Ota.Core.Telink;
using Bms.Ota.Windows.Ble;
using Microsoft.Win32;
using Windows.Devices.Bluetooth.Advertisement;

namespace Bms.Ota.Windows;

public partial class MainWindow : Window
{
    private readonly ObservableCollection<DiscoveredDevice> _devices = new();
    private readonly Dictionary<ulong, DiscoveredDevice> _deviceMap = new();
    private BluetoothLEAdvertisementWatcher? _watcher;
    private WindowsBleTransport? _transport;
    private FirmwareImage? _firmware;
    private CancellationTokenSource? _otaCts;

    public MainWindow()
    {
        InitializeComponent();
        DeviceList.ItemsSource = _devices;
        Closed += async (_, _) =>
        {
            _watcher?.Stop();
            _otaCts?.Cancel();
            if (_transport is not null) await _transport.DisposeAsync();
        };
        AppendLog("Ready. Telink OTA GATT UUIDs are built in and discovered automatically.");
        AppendLog($"OTA service={WindowsBleTransport.TelinkOtaServiceUuid}");
        AppendLog($"OTA characteristic={WindowsBleTransport.TelinkOtaCharacteristicUuid}");
    }

    private void ScanButton_Click(object sender, RoutedEventArgs e)
    {
        _watcher?.Stop();
        _devices.Clear();
        _deviceMap.Clear();
        _watcher = WindowsBleTransport.CreateWatcher(device => Dispatcher.Invoke(() => AddOrUpdateDevice(device)));
        _watcher.Start();
        AppendLog("BLE scan started.");
    }

    private void StopScanButton_Click(object sender, RoutedEventArgs e)
    {
        _watcher?.Stop();
        AppendLog("BLE scan stopped.");
    }

    private async void ConnectButton_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            if (DeviceList.SelectedItem is not DiscoveredDevice selected)
                throw new InvalidOperationException("请选择 BLE 设备。");

            _watcher?.Stop();
            ConnectButton.IsEnabled = false;
            AppendLog($"Connecting {selected} ...");

            if (_transport is not null) await _transport.DisposeAsync();
            _transport = new WindowsBleTransport();
            await _transport.ConnectAsync(selected.Address);
            AppendLog($"Connected. {_transport.DiscoveryDescription}");
        }
        catch (Exception ex)
        {
            AppendLog("CONNECT ERROR: " + ex.Message);
            MessageBox.Show(ex.Message, "Connect failed", MessageBoxButton.OK, MessageBoxImage.Error);
        }
        finally
        {
            ConnectButton.IsEnabled = true;
        }
    }

    private void BrowseButton_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            var dialog = new OpenFileDialog { Filter = "Telink firmware (*.bin)|*.bin|All files (*.*)|*.*" };
            if (dialog.ShowDialog() != true) return;
            int max = ParseNonNegative(MaxImageBytesBox.Text, "Max image bytes");
            _firmware = FirmwareImage.Load(dialog.FileName, max);
            FirmwarePathText.Text = dialog.FileName;
            FirmwareInfoText.Text = $"Image={_firmware.ImageSize:N0} bytes, Packets={_firmware.PacketCount:N0}, header size @0x18 validated";
            AppendLog($"Firmware loaded: {_firmware.ImageSize} bytes, {_firmware.PacketCount} packets.");
        }
        catch (Exception ex)
        {
            _firmware = null;
            FirmwareInfoText.Text = "Firmware validation failed";
            AppendLog("BIN ERROR: " + ex.Message);
            MessageBox.Show(ex.Message, "Invalid firmware", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    private async void StartButton_Click(object sender, RoutedEventArgs e)
    {
        if (_firmware is null)
        {
            MessageBox.Show("请先选择并校验 BIN。", "OTA", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }
        if (_transport is null || !_transport.IsConnected)
        {
            MessageBox.Show("请先连接 OTA GATT 特征。", "OTA", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        try
        {
            int delay = ParseRange(PacketDelayBox.Text, "Packet delay", 0, 1000);
            _otaCts = new CancellationTokenSource();
            StartButton.IsEnabled = false;
            CancelButton.IsEnabled = true;
            OtaProgressBar.Value = 0;

            var client = new TelinkOtaClient(_transport);
            client.Log += message => Dispatcher.Invoke(() => AppendLog(message));
            client.StateChanged += state => Dispatcher.Invoke(() => ProgressText.Text = state.ToString());
            client.Progress += p => Dispatcher.Invoke(() =>
            {
                OtaProgressBar.Value = p.Percent;
                ProgressText.Text = $"{p.Percent:F1}%  {p.CompletedPackets:N0}/{p.TotalPackets:N0} packets  {p.SentImageBytes:N0}/{_firmware.ImageSize:N0} bytes";
            });

            AppendLog("=== OTA START ===");
            await client.UpgradeAsync(_firmware, delay, _otaCts.Token);
            AppendLog("=== OTA DATA COMPLETE ===");
            MessageBox.Show(
                "OTA 数据发送完成。设备应进行镜像校验/切换并重启。\n\n当前固件版本读取接口尚未确认，因此本版不会伪造“升级后版本验证成功”。建议重连并读取 BMS 固件版本后再作为最终成功判据。",
                "OTA transfer complete", MessageBoxButton.OK, MessageBoxImage.Information);
        }
        catch (OperationCanceledException)
        {
            AppendLog("OTA cancelled by user.");
        }
        catch (Exception ex)
        {
            AppendLog("OTA ERROR: " + ex.Message);
            MessageBox.Show(ex.Message, "OTA failed", MessageBoxButton.OK, MessageBoxImage.Error);
        }
        finally
        {
            _otaCts?.Dispose();
            _otaCts = null;
            StartButton.IsEnabled = true;
            CancelButton.IsEnabled = false;
        }
    }

    private void CancelButton_Click(object sender, RoutedEventArgs e) => _otaCts?.Cancel();

    private void AddOrUpdateDevice(DiscoveredDevice device)
    {
        if (_deviceMap.TryGetValue(device.Address, out var old))
        {
            int index = _devices.IndexOf(old);
            _deviceMap[device.Address] = device;
            if (index >= 0) _devices[index] = device;
        }
        else
        {
            _deviceMap[device.Address] = device;
            _devices.Add(device);
        }
    }

    private static int ParseNonNegative(string value, string field)
    {
        if (!int.TryParse(value.Trim(), out int result) || result < 0)
            throw new InvalidOperationException($"{field} 必须是 >= 0 的整数。");
        return result;
    }

    private static int ParseRange(string value, string field, int min, int max)
    {
        if (!int.TryParse(value.Trim(), out int result) || result < min || result > max)
            throw new InvalidOperationException($"{field} 必须是 {min}..{max} 的整数。");
        return result;
    }

    private void AppendLog(string message)
    {
        LogBox.AppendText($"{DateTime.Now:HH:mm:ss.fff}  {message}{Environment.NewLine}");
        LogBox.ScrollToEnd();
    }
}
