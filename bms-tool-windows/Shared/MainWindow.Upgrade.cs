using System.IO;
using System.Windows;
using System.Windows.Controls;
using Microsoft.Win32;
namespace BmsTool.Windows;
public partial class MainWindow
{
    private string? _firmwarePath;
    private CancellationTokenSource? _otaCts;
    private bool _otaRunning;
    private void BrowseFirmware_Click(object sender, RoutedEventArgs e)
    {
        if (_otaRunning) return;
        try
        {
            var dlg = new OpenFileDialog { Filter = "BMS 固件 (*.d015;*.bin)|*.d015;*.bin|所有文件 (*.*)|*.*" };
            if (dlg.ShowDialog() != true) return;
            FirmwareInspection inspection = FirmwareImage.Inspect(dlg.FileName);
            if (!inspection.LooksLikeTelink && !inspection.FitsStm32App)
                throw new InvalidDataException("文件既不是可识别的 Telink BIN，也不在 STM32 APP 55 KB 边界内。");
            _firmwarePath = dlg.FileName;
            FirmwareText.Text = inspection.Description;
            AppendLog($"Firmware candidate loaded file='{inspection.FileName}' size={inspection.FileSize}; telink={inspection.LooksLikeTelink}; stm32={inspection.FitsStm32App}", "OTA");
        }
        catch (Exception ex)
        {
            _firmwarePath = null;
            ShowError("固件校验失败", ex);
        }
    }

    private bool _otaRequireD015;
    private async void StartOta_Click(object sender, RoutedEventArgs e)
    {
        if (_otaRunning || _shBusy || _diagBusy || _parameterBusy || _polling || _autoReconnectRunning) return;
        try
        {
            if(_bms?.IsSh3520==true) throw new InvalidOperationException("已识别 3520：当前 OTA 的旧 STM32 App 地址不匹配，请使用该工程的安全 App 烧录脚本。");
            bool d015Recovery=D015IapRecoveryBox.IsChecked==true;
            if(d015Recovery) {
                if(!IsSerialConnectionMode || SerialPortList.SelectedItem is not SerialPortEndpoint serial)
                    throw new InvalidOperationException("D015恢复请先切换串口模式并选择COM端口。");
                if(_bms is not null && !_bms.IsD015)
                    throw new InvalidOperationException("当前连接不是D015，请先断开后选择D015串口。");
                _connectionMode=ConnectionMode.Serial;
                _connectedSerialPort=serial.PortName;
                _serialBaudRate=GetSerialBaudRate();
                _connectedAddress=null;
            }
            _otaRequireD015 = d015Recovery || _bms?.IsD015 == true;
            string firmwarePath = _firmwarePath ?? throw new InvalidOperationException("请先选择固件。");
            ulong address = _connectedAddress ?? 0;
            if (_connectionMode == ConnectionMode.Ble && _connectedAddress is null)
                throw new InvalidOperationException("请先连接 BMS。");
            if (_connectionMode == ConnectionMode.Serial && string.IsNullOrWhiteSpace(_connectedSerialPort))
                throw new InvalidOperationException("请先连接串口。");

            _otaRunning = true;
            ConnectButton.IsEnabled = ConnectionModeBox.IsEnabled = SerialPortList.IsEnabled = DeviceList.IsEnabled = false;
            OtaTargetBox.IsEnabled = OtaModeBox.IsEnabled = ExpectedVersionBox.IsEnabled = false;
            await StopRuntimeLogsForOtaAsync();
            _pollTimer.Stop();
            StartOtaButton.IsEnabled = false;
            CancelOtaButton.IsEnabled = true;
            D015IapRecoveryBox.IsEnabled = false;
            OtaProgressBar.Value = 0;
            OtaVerifyText.Text = string.Empty;
            _otaCts = new CancellationTokenSource();
            OtaTargetKind target = _connectionMode == ConnectionMode.Serial
                ? OtaTargetKind.Stm32SerialIap
                : await OtaTargetDetector.DetectAsync(address, GetOtaTargetKind(), _otaCts.Token);
            FirmwareImage image = FirmwareImage.LoadForTarget(firmwarePath, target);
            AppendLog($"OTA architecture detected={target}; firmware={image.FileName}; bytes={image.ImageSize}", "OTA");

            string oldVersion = string.Empty;
            try
            {
                if (_bms is not null && !d015Recovery) oldVersion = (await _bms.ReadIdentityAsync(_otaCts.Token)).Software;
            }
            catch (Exception ex)
            {
                AppendLog("Pre-OTA version read warning: " + ex.Message, "OTA");
            }

            await DisposeBmsAsync();
            ConnectionText.Text = "正在升级...";
            OtaTransferMode requested = GetOtaMode();
            bool serverConfirmed;
            D015UpgradeReadback? verified;

            try
            {
                (serverConfirmed, verified) = await RunOtaOnceAsync(address, image, target, requested, _otaCts.Token);
            }
            catch (Exception ex) when (requested == OtaTransferMode.Auto && IsExtendCompatibilityFailure(ex) && !_otaCts.IsCancellationRequested)
            {
                AppendLog("Extend64 rejected; fallback to Legacy Fast: " + ex.Message, "OTA");
                await Task.Delay(700, _otaCts.Token);
                (serverConfirmed, verified) = await RunOtaOnceAsync(address, image, target, OtaTransferMode.LegacyFast, _otaCts.Token);
            }

            OtaVerifyText.Text = serverConfirmed ? "设备已接受固件，等待重启并验证..." : "数据发送完成，等待设备重启并验证...";
            DeviceIdentity post = verified?.Identity ?? await VerifyAfterOtaAsync(_connectionMode == ConnectionMode.Serial ? null : address, _otaCts.Token);
            if (verified is not null) ApplyBatterySnapshot(verified.Battery);
            string expected = ExpectedVersionBox.Text.Trim();
            if (expected.Length > 0 && !string.Equals(post.Software, expected, StringComparison.OrdinalIgnoreCase))
                throw new IOException($"设备已重启，但软件版本不匹配：目标 {expected}，实际 {post.Software}。");

            string versionNote = expected.Length > 0
                ? $"版本 {post.Software} 校验通过"
                : oldVersion.Length > 0 && oldVersion != post.Software
                    ? $"版本 {oldVersion} → {post.Software}"
                    : $"当前版本 {post.Software}";

            OtaVerifyText.Text = $"升级成功：设备已重启，BMS 通信和实时数据正常；{versionNote}。";
            ConnectionText.Text = _bms is null ? "升级已验证；点击连接恢复实时监控" : (string.IsNullOrEmpty(_connectedName) ? "已连接" : $"已连接：{_connectedName}");
            AppendLog("OTA VERIFIED: " + OtaVerifyText.Text + $" serverConfirmed={serverConfirmed}", "OTA");
            MessageBox.Show(OtaVerifyText.Text, "OTA 验证完成", MessageBoxButton.OK, MessageBoxImage.Information);
        }
        catch (OperationCanceledException)
        {
            AppendLog("OTA cancelled", "OTA");
            OtaVerifyText.Text = "升级已取消。新 D015 写入中断时留在 Boot；若已发送最终提交，结果待确认，请先重连核对版本。";
        }
        catch (Exception ex)
        {
            _sessionLog.WriteException("OTA", "OTA/Verify", ex);
            OtaVerifyText.Text = "升级未获成功确认：" + ex.Message;
            MessageBox.Show(ex.Message, "OTA failed", MessageBoxButton.OK, MessageBoxImage.Error);
        }
        finally
        {
            _otaCts?.Dispose();
            _otaCts = null;
            _otaRunning = false;
            ConnectButton.IsEnabled = ConnectionModeBox.IsEnabled = SerialPortList.IsEnabled = DeviceList.IsEnabled = true;
            OtaTargetBox.IsEnabled = OtaModeBox.IsEnabled = ExpectedVersionBox.IsEnabled = true;
            StartOtaButton.IsEnabled = true;
            CancelOtaButton.IsEnabled = false;
            D015IapRecoveryBox.IsEnabled = true;
            if (_bms is not null) _pollTimer.Start();
        }
    }

    private async Task<(bool Confirmed, D015UpgradeReadback? Verified)> RunOtaOnceAsync(ulong address, FirmwareImage image, OtaTargetKind target, OtaTransferMode mode, CancellationToken ct)
    {
        if (target == OtaTargetKind.Stm32SerialIap)
        {
            if (_connectionMode == ConnectionMode.Serial)
            {
                await using var serialPortTransport = new BmsSerialTransport();
                AppendLog($"Connecting STM32 serial OTA COM port={_connectedSerialPort}; baud={_serialBaudRate}", "OTA");
                await serialPortTransport.ConnectAsync(_connectedSerialPort!, _serialBaudRate, ct);
                AppendLog($"STM32 serial OTA port ready; {serialPortTransport.DiscoveryDescription}", "OTA");
                var serialPortClient = new Stm32SerialBleOtaClient(serialPortTransport, chunkForBle: false) { RequireD015Image = _otaRequireD015, AllowUnidentifiedLegacy = GetOtaTargetKind() == OtaTargetKind.Stm32SerialIap && !_otaRequireD015 };
                serialPortClient.Log += m => AppendLog(m, "OTA");
                serialPortClient.Progress += p => Dispatcher.BeginInvoke(() =>
                {
                    OtaProgressBar.Value = p.Percent;
                    OtaProgressText.Text = $"串口升级 · {p.Percent:F1}% · 第 {p.PageIndex}/{p.PageCount} 页";
                });
                bool confirmed = await serialPortClient.UpgradeAsync(image, ct);
                return (confirmed, serialPortClient.Verified);
            }

            await using var serialTransport = new BmsBleTransport();
            AppendLog($"Connecting STM32 serial OTA GATT address={address:X12}", "OTA");
            await serialTransport.ConnectAsync(address, ct);
            AppendLog($"STM32 serial OTA GATT ready; MTU={serialTransport.NegotiatedMtu}; {serialTransport.DiscoveryDescription}", "OTA");
            var serialClient = new Stm32SerialBleOtaClient(serialTransport) { RequireD015Image = _otaRequireD015, AllowUnidentifiedLegacy = GetOtaTargetKind() == OtaTargetKind.Stm32SerialIap && !_otaRequireD015 };
            serialClient.Log += m => AppendLog(m, "OTA");
            serialClient.Progress += p => Dispatcher.BeginInvoke(() =>
            {
                OtaProgressBar.Value = p.Percent;
                OtaProgressText.Text = $"串口升级 · {p.Percent:F1}% · 第 {p.PageIndex}/{p.PageCount} 页";
            });
            bool bleConfirmed = await serialClient.UpgradeAsync(image, ct);
            return (bleConfirmed, serialClient.Verified);
        }

        await using var transport = new OtaBleTransport();
        AppendLog($"Connecting OTA GATT address={address:X12}", "OTA");
        await transport.ConnectAsync(address);
        AppendLog($"OTA GATT ready; MTU={transport.NegotiatedMtu}; notify={transport.NotificationsEnabled}", "OTA");

        var client = new TelinkOtaClient(transport);
        client.Log += m => AppendLog(m, "OTA");
        client.Progress += p => Dispatcher.BeginInvoke(() =>
        {
            OtaProgressBar.Value = p.Percent;
            string eta = p.Eta is null ? string.Empty : $" · ETA {p.Eta.Value.TotalSeconds:F1}s";
            OtaProgressText.Text = $"{p.Mode} · {p.Percent:F1}% · {p.BytesPerSecond / 1024.0:F1} KB/s{eta}";
        });
        return (await client.UpgradeAsync(image, mode, ct), null);
    }

    private async Task<DeviceIdentity> VerifyAfterOtaAsync(ulong? address, CancellationToken ct)
    {
        Exception? last = null;
        for (int attempt = 1; attempt <= 12; attempt++)
        {
            ct.ThrowIfCancellationRequested();
            try
            {
                await Task.Delay(attempt == 1 ? 1200 : 900, ct);
                AppendLog($"Post-OTA reconnect {attempt}/12", "OTA");
                if (_connectionMode == ConnectionMode.Serial)
                    await ConnectSerialInternalAsync(_connectedSerialPort!, _serialBaudRate, ct);
                else
                    await ConnectBmsInternalAsync(address!.Value, ct);
                DeviceIdentity id = await (_bms ?? throw new IOException("BMS client unavailable")).ReadIdentityAsync(ct);
                BatterySnapshot snapshot = await _bms.ReadBatteryAsync(ct);
                ApplyBatterySnapshot(snapshot);
                await RefreshIdentityAsync();
                return id;
            }
            catch (Exception ex)
            {
                last = ex;
                AppendLog($"Post-OTA reconnect attempt={attempt} failed: {ex.GetType().Name}; hresult=0x{ex.HResult:X8}; {ex.Message}", "OTA");
                await DisposeBmsAsync();
            }
        }
        throw new IOException("OTA 数据已发送，但设备重启后未恢复到可读取的 BMS 通信状态。", last);
    }

    private OtaTransferMode GetOtaMode()
    {
        string tag = (OtaModeBox.SelectedItem as ComboBoxItem)?.Tag?.ToString() ?? "Auto";
        return Enum.TryParse(tag, out OtaTransferMode mode) ? mode : OtaTransferMode.Auto;
    }

    private OtaTargetKind GetOtaTargetKind()
    {
        string tag = (OtaTargetBox.SelectedItem as ComboBoxItem)?.Tag?.ToString() ?? "Auto";
        return Enum.TryParse(tag, out OtaTargetKind target) ? target : OtaTargetKind.Auto;
    }

    private static bool IsExtendCompatibilityFailure(Exception ex)
    {
        string message = ex.Message;
        return message.Contains("OTA_PDU_LEN_ERR", StringComparison.OrdinalIgnoreCase) ||
               message.Contains("OTA_MCU_NOT_SUPPORTED", StringComparison.OrdinalIgnoreCase) ||
               message.Contains("OTA_PACKET_INVALID", StringComparison.OrdinalIgnoreCase) ||
               message.Contains("requires MTU", StringComparison.OrdinalIgnoreCase);
    }

    private void CancelOta_Click(object sender, RoutedEventArgs e) => _otaCts?.Cancel();

}
