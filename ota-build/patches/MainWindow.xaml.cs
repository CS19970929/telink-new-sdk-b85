using System.Collections.ObjectModel;
using System.Windows;
using System.Windows.Controls;
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
    private ulong? _connectedAddress;

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
        AppendLog("Ready. Fast OTA: default packet delay is 0 ms; progress UI is throttled.");
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
            _connectedAddress = selected.Address;
            await ConnectTransportAsync(selected.Address);
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

    private async Task ConnectTransportAsync(ulong address)
    {
        AppendLog($"Connecting {address:X12} ...");
        if (_transport is not null) await _transport.DisposeAsync();
        _transport = new WindowsBleTransport();
        await _transport.ConnectAsync(address);
        AppendLog($"Connected. {_transport.DiscoveryDescription}");
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
            FirmwareInfoText.Text = $"Image={_firmware.ImageSize:N0} bytes, Legacy packets={_firmware.PacketCount:N0}, header size @0x18 validated";
            AppendLog($"Firmware loaded: {_firmware.ImageSize} bytes, legacy packets={_firmware.PacketCount}.");
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
        if (_transport is null || !_transport.IsConnected || _connectedAddress is null)
        {
            MessageBox.Show("请先连接 OTA GATT 特征。", "OTA", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        try
        {
            OtaTransferMode requestedMode = GetSelectedMode();
            _otaCts = new CancellationTokenSource();
            StartButton.IsEnabled = false;
            CancelButton.IsEnabled = true;
            OtaProgressBar.Value = 0;

            AppendLog("=== OTA START ===");
            try
            {
                await RunUpgradeOnceAsync(requestedMode, _otaCts.Token);
            }
            catch (Exception ex) when (
                requestedMode == OtaTransferMode.Auto &&
                IsExtendCompatibilityFailure(ex) &&
                !_otaCts.IsCancellationRequested)
            {
                AppendLog($"Extend64 rejected ({ex.Message}). Auto fallback to Legacy Fast from index 0.");
                ProgressText.Text = "Extend64 rejected; reconnecting for Legacy Fast...";
                await ReconnectForFallbackAsync(_connectedAddress.Value, _otaCts.Token);
                OtaProgressBar.Value = 0;
                await RunUpgradeOnceAsync(OtaTransferMode.LegacyFast, _otaCts.Token);
            }

            AppendLog("=== OTA TRANSFER COMPLETE ===");
            MessageBox.Show(
                "OTA 发送流程完成。若收到 OTA_RESULT=SUCCESS，日志中会明确显示。设备随后应校验/切换镜像并重启。\n\n最终产品判据仍建议增加重连后的固件版本读取。",
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

    private async Task RunUpgradeOnceAsync(OtaTransferMode mode, CancellationToken ct)
    {
        var transport = _transport ?? throw new InvalidOperationException("BLE transport unavailable.");
        var firmware = _firmware ?? throw new InvalidOperationException("Firmware unavailable.");
        var client = new TelinkOtaClient(transport);
        client.Log += message => Dispatcher.Invoke(() => AppendLog(message));
        client.StateChanged += state => Dispatcher.Invoke(() => ProgressText.Text = state.ToString());
        client.Progress += p => Dispatcher.Invoke(() =>
        {
            OtaProgressBar.Value = p.Percent;
            string eta = p.EstimatedRemaining is { } remain ? $" ETA {remain.TotalSeconds:F1}s" : string.Empty;
            ProgressText.Text = $"{p.Mode}  {p.Percent:F1}%  {p.SentImageBytes:N0}/{p.TotalImageBytes:N0} B  {p.BytesPerSecond / 1024.0:F1} KB/s{eta}";
        });

        await client.UpgradeAsync(firmware, mode, packetDelayMs: 0, cancellationToken: ct);
    }

    private async Task ReconnectForFallbackAsync(ulong address, CancellationToken ct)
    {
        Exception? last = null;
        for (int attempt = 1; attempt <= 5; attempt++)
        {
            ct.ThrowIfCancellationRequested();
            try
            {
                await Task.Delay(attempt == 1 ? 400 : 800, ct);
                await ConnectTransportAsync(address);
                AppendLog($"Fallback reconnect succeeded on attempt {attempt}.");
                return;
            }
            catch (Exception ex)
            {
                last = ex;
                AppendLog($"Fallback reconnect attempt {attempt} failed: {ex.Message}");
            }
        }
        throw new InvalidOperationException("Unable to reconnect for Legacy Fast fallback.", last);
    }

    private OtaTransferMode GetSelectedMode()
    {
        string tag = (TransferModeBox.SelectedItem as ComboBoxItem)?.Tag?.ToString() ?? "Auto";
        return Enum.TryParse<OtaTransferMode>(tag, out var mode) ? mode : OtaTransferMode.Auto;
    }

    private static bool IsExtendCompatibilityFailure(Exception ex)
    {
        string m = ex.Message;
        return m.Contains("OTA_PDU_LEN_ERR", StringComparison.OrdinalIgnoreCase) ||
               m.Contains("OTA_MCU_NOT_SUPPORTED", StringComparison.OrdinalIgnoreCase) ||
               m.Contains("OTA_PACKET_INVALID", StringComparison.OrdinalIgnoreCase) ||
               m.Contains("Extend64 requires", StringComparison.OrdinalIgnoreCase);
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

    private void AppendLog(string message)
    {
        LogBox.AppendText($"{DateTime.Now:HH:mm:ss.fff}  {message}{Environment.NewLine}");
        LogBox.ScrollToEnd();
    }
}
