using System.Collections.ObjectModel;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
using Microsoft.Win32;
using Windows.Devices.Bluetooth.Advertisement;

namespace BmsTool.Windows;

public partial class MainWindow : Window
{
    private readonly ObservableCollection<DiscoveredDevice> _devices = new();
    private readonly Dictionary<ulong, DiscoveredDevice> _deviceMap = new();
    private readonly DispatcherTimer _pollTimer;
    private BluetoothLEAdvertisementWatcher? _watcher;
    private BmsBleTransport? _bmsTransport;
    private BmsClient? _bms;
    private ulong? _connectedAddress;
    private FirmwareImage? _firmware;
    private CancellationTokenSource? _otaCts;
    private bool _polling;
    private bool _otaRunning;

    public MainWindow()
    {
        InitializeComponent();
        DeviceList.ItemsSource = _devices;
        _pollTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(2) };
        _pollTimer.Tick += async (_, _) => await PollTickAsync();
        Closed += async (_, _) => { _watcher?.Stop(); _otaCts?.Cancel(); await DisposeBmsAsync(); };
        AppendLog("Ready. Firmware protocol: NUS 6E400001/2/3 + Modbus RTU slave 0x01 + Telink OTA.");
    }

    private void ScanButton_Click(object sender, RoutedEventArgs e)
    {
        _watcher?.Stop(); _devices.Clear(); _deviceMap.Clear();
        _watcher = BmsBleTransport.CreateWatcher(d => Dispatcher.Invoke(() => UpsertDevice(d)));
        _watcher.Start(); AppendLog("BLE scan started; list filter=BT_* only.");
    }

    private void StopScanButton_Click(object sender, RoutedEventArgs e) { _watcher?.Stop(); AppendLog("BLE scan stopped."); }

    private async void ConnectButton_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            if (DeviceList.SelectedItem is not DiscoveredDevice selected) throw new InvalidOperationException("请先扫描并选择 BT_ 设备。");
            _watcher?.Stop(); ConnectButton.IsEnabled = false; _connectedAddress = selected.Address;
            await ConnectBmsInternalAsync(selected.Address);
            await RefreshIdentityAsync(); await RefreshBatteryAsync();
        }
        catch (Exception ex) { ShowError("连接失败", ex); }
        finally { ConnectButton.IsEnabled = true; }
    }

    private async Task ConnectBmsInternalAsync(ulong address)
    {
        await DisposeBmsAsync(); ConnectionText.Text = $"连接 {address:X12} ..."; AppendLog($"Connecting BMS {address:X12}");
        var transport = new BmsBleTransport(); await transport.ConnectAsync(address);
        var client = new BmsClient(transport); client.Log += OnProtocolLog;
        _bmsTransport = transport; _bms = client; _connectedAddress = address;
        ConnectionText.Text = "已连接 · " + transport.DiscoveryDescription; AppendLog("BMS ready. " + transport.DiscoveryDescription);
    }

    private void OnProtocolLog(string text) => Dispatcher.Invoke(() => AppendLog(text));

    private async Task DisposeBmsAsync()
    {
        if (_bms is not null) { _bms.Log -= OnProtocolLog; await _bms.DisposeAsync(); _bms = null; }
        if (_bmsTransport is not null) { await _bmsTransport.DisposeAsync(); _bmsTransport = null; }
    }

    private async void RefreshBattery_Click(object sender, RoutedEventArgs e) { try { await RefreshBatteryAsync(); } catch (Exception ex) { ShowError("读取电池信息失败", ex); } }
    private async void RefreshIdentity_Click(object sender, RoutedEventArgs e) { try { await RefreshIdentityAsync(); } catch (Exception ex) { ShowError("读取设备信息失败", ex); } }

    private async Task RefreshBatteryAsync()
    {
        var bms = _bms ?? throw new InvalidOperationException("BMS 未连接。");
        BatterySnapshot s = await bms.ReadBatteryAsync();
        PackVoltageText.Text = $"{s.PackVoltageV:F2} V"; CurrentText.Text = $"{s.CurrentA:+0.0;-0.0;0.0} A"; SocText.Text = $"SOC {s.SocPercent}%\nSOH {s.SohPercent}%";
        TempsText.Text = $"最高 {s.MaxTempC:F1} °C\n最低 {s.MinTempC:F1} °C\nMOS {s.MosTempC:F1} °C";
        CellExtremeText.Text = $"Max {s.MaxCellMv} mV  Cell {s.MaxCellPosition}\nMin {s.MinCellMv} mV  Cell {s.MinCellPosition}\nΔ {s.CellDeltaMv} mV";
        CapacityText.Text = $"当前 {s.CapacityNowAh:F2} Ah\n满充 {s.CapacityFullAh:F2} Ah\n工厂 {s.CapacityFactoryAh:F2} Ah\n循环 {s.CycleCount}";
        StatusWordText.Text = $"0x{s.SystemStatus:X8}\nProtocol 0x{s.ProtocolVersion:X4}";
        DataSourceText.Text = s.UsesRealtimeWindow ? "实时窗口 0xD120" : "兼容窗口 0xD000";
        CellsText.Text = string.Join("    ", s.CellMillivolts.Select((mv, i) => $"C{i + 1:00}: {mv / 1000.0:F3} V"));
    }

    private async Task<DeviceIdentity> RefreshIdentityAsync()
    {
        var bms = _bms ?? throw new InvalidOperationException("BMS 未连接。"); DeviceIdentity id = await bms.ReadIdentityAsync();
        IdentityText.Text = $"Bluetooth : {id.BluetoothName}\nMAC       : {id.Mac}\nSN        : {id.Serial}\nHardware  : {id.Hardware}\nSoftware  : {id.Software}";
        BtNameResultText.Text = id.BluetoothName;
        if (id.BluetoothName.StartsWith("BT_", StringComparison.OrdinalIgnoreCase)) NameSuffixBox.Text = id.BluetoothName[3..];
        return id;
    }

    private async void WriteName_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            var bms = _bms ?? throw new InvalidOperationException("BMS 未连接。"); string name = await bms.WriteBluetoothNameSuffixAsync(NameSuffixBox.Text);
            BtNameResultText.Text = $"已写入并回读: {name}"; AppendLog($"Bluetooth name verified: {name}");
            MessageBox.Show($"蓝牙名已写入并回读确认：{name}\n固件已立即刷新广播名；重新扫描可看到新名称。", "蓝牙名", MessageBoxButton.OK, MessageBoxImage.Information);
        }
        catch (Exception ex) { ShowError("修改蓝牙名失败", ex); }
    }

    private async void ManualRead_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            var bms = _bms ?? throw new InvalidOperationException("BMS 未连接。"); ushort address = ParseU16(ManualAddressBox.Text); ushort qty = ParseU16(ManualQuantityBox.Text);
            if (qty is 0 or > 125) throw new ArgumentOutOfRangeException(nameof(qty), "数量必须为 1..125。");
            ushort[] words = await bms.ReadRegistersAsync(address, qty); RegisterOutput.Text = string.Join(Environment.NewLine, words.Select((v, i) => $"0x{address + i:X4} = {v,6}  0x{v:X4}"));
        }
        catch (Exception ex) { ShowError("寄存器读取失败", ex); }
    }

    private async void ProtectPreview_Click(object sender, RoutedEventArgs e)
    {
        try { RegisterOutput.Text = await (_bms ?? throw new InvalidOperationException("BMS 未连接。")).ReadProtectionPreviewAsync(); }
        catch (Exception ex) { ShowError("保护参数读取失败", ex); }
    }

    private void AutoPollCheck_Changed(object sender, RoutedEventArgs e) { if (AutoPollCheck.IsChecked == true) _pollTimer.Start(); else _pollTimer.Stop(); }
    private async Task PollTickAsync()
    {
        if (_polling || _otaRunning || _bms is null) return; _polling = true;
        try { await RefreshBatteryAsync(); } catch (Exception ex) { AppendLog("POLL ERROR: " + ex.Message); } finally { _polling = false; }
    }

    private void BrowseFirmware_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            var dlg = new OpenFileDialog { Filter = "Telink firmware (*.bin)|*.bin|All files (*.*)|*.*" }; if (dlg.ShowDialog() != true) return;
            _firmware = FirmwareImage.Load(dlg.FileName); FirmwareText.Text = $"{_firmware.FileName} · {_firmware.ImageSize:N0} bytes · {_firmware.LegacyPacketCount:N0} legacy packets"; AppendLog("Firmware loaded: " + FirmwareText.Text);
        }
        catch (Exception ex) { _firmware = null; ShowError("固件校验失败", ex); }
    }

    private async void StartOta_Click(object sender, RoutedEventArgs e)
    {
        if (_otaRunning) return;
        try
        {
            var image = _firmware ?? throw new InvalidOperationException("请先选择 BIN。"); ulong address = _connectedAddress ?? throw new InvalidOperationException("请先连接 BMS。");
            _otaRunning = true; _pollTimer.Stop(); StartOtaButton.IsEnabled = false; CancelOtaButton.IsEnabled = true; OtaProgressBar.Value = 0; OtaVerifyText.Text = ""; _otaCts = new CancellationTokenSource();
            string oldVersion = ""; try { if (_bms is not null) oldVersion = (await _bms.ReadIdentityAsync(_otaCts.Token)).Software; } catch (Exception ex) { AppendLog("Pre-OTA version read warning: " + ex.Message); }
            await DisposeBmsAsync(); ConnectionText.Text = "OTA 模式";

            OtaTransferMode requested = GetOtaMode(); bool serverConfirmed;
            try { serverConfirmed = await RunOtaOnceAsync(address, image, requested, _otaCts.Token); }
            catch (Exception ex) when (requested == OtaTransferMode.Auto && IsExtendCompatibilityFailure(ex) && !_otaCts.IsCancellationRequested)
            {
                AppendLog("Extend64 rejected; reconnecting and falling back to Legacy Fast from index 0: " + ex.Message);
                await Task.Delay(700, _otaCts.Token); serverConfirmed = await RunOtaOnceAsync(address, image, OtaTransferMode.LegacyFast, _otaCts.Token);
            }

            OtaVerifyText.Text = serverConfirmed ? "OTA_ACCEPTED：设备返回 OTA_RESULT=SUCCESS，等待重启并验证 APP..." : "TRANSFER_COMPLETE：未收到 OTA_RESULT，继续通过重连验证 APP...";
            DeviceIdentity post = await VerifyAfterOtaAsync(address, _otaCts.Token); string expected = ExpectedVersionBox.Text.Trim();
            if (expected.Length > 0 && !string.Equals(post.Software, expected, StringComparison.OrdinalIgnoreCase)) throw new IOException($"设备已重启，但软件版本校验失败：expected='{expected}', actual='{post.Software}'.");
            string versionNote = expected.Length > 0 ? $"版本匹配 {post.Software}" : (oldVersion.Length > 0 && oldVersion != post.Software ? $"版本 {oldVersion} → {post.Software}" : $"当前版本 {post.Software}（未指定目标版本）");
            OtaVerifyText.Text = $"VERIFIED：设备已重启、SPP/Modbus 正常、实时电池数据可读；{versionNote}." + (serverConfirmed ? " OTA_RESULT=SUCCESS。" : " OTA_RESULT 未确认。 ");
            AppendLog("OTA VERIFIED: " + OtaVerifyText.Text); MessageBox.Show(OtaVerifyText.Text, "OTA 验证完成", MessageBoxButton.OK, MessageBoxImage.Information);
        }
        catch (OperationCanceledException) { AppendLog("OTA cancelled."); OtaVerifyText.Text = "CANCELLED"; }
        catch (Exception ex) { AppendLog("OTA/VERIFY ERROR: " + ex.Message); OtaVerifyText.Text = "FAILED：" + ex.Message; MessageBox.Show(ex.Message, "OTA failed", MessageBoxButton.OK, MessageBoxImage.Error); }
        finally
        {
            _otaCts?.Dispose(); _otaCts = null; _otaRunning = false; StartOtaButton.IsEnabled = true; CancelOtaButton.IsEnabled = false; if (AutoPollCheck.IsChecked == true && _bms is not null) _pollTimer.Start();
        }
    }

    private async Task<bool> RunOtaOnceAsync(ulong address, FirmwareImage image, OtaTransferMode mode, CancellationToken ct)
    {
        await using var transport = new OtaBleTransport(); AppendLog($"Connecting OTA GATT {address:X12}..."); await transport.ConnectAsync(address); AppendLog($"OTA GATT ready; MTU={transport.NegotiatedMtu}, notify={transport.NotificationsEnabled}");
        var client = new TelinkOtaClient(transport); client.Log += m => Dispatcher.Invoke(() => AppendLog(m)); client.Progress += p => Dispatcher.Invoke(() => { OtaProgressBar.Value = p.Percent; string eta = p.Eta is null ? "" : $" ETA {p.Eta.Value.TotalSeconds:F1}s"; OtaProgressText.Text = $"{p.Mode} {p.Percent:F1}% · {p.BytesPerSecond / 1024.0:F1} KB/s{eta}"; });
        return await client.UpgradeAsync(image, mode, ct);
    }

    private async Task<DeviceIdentity> VerifyAfterOtaAsync(ulong address, CancellationToken ct)
    {
        Exception? last = null;
        for (int attempt = 1; attempt <= 12; attempt++)
        {
            ct.ThrowIfCancellationRequested(); try
            {
                await Task.Delay(attempt == 1 ? 1200 : 900, ct); AppendLog($"Post-OTA reconnect attempt {attempt}/12..."); await ConnectBmsInternalAsync(address);
                DeviceIdentity id = await (_bms ?? throw new IOException("BMS client unavailable")).ReadIdentityAsync(ct); _ = await _bms.ReadBatteryAsync(ct); await RefreshIdentityAsync(); await RefreshBatteryAsync(); return id;
            }
            catch (Exception ex) { last = ex; AppendLog($"Post-OTA reconnect {attempt} failed: {ex.Message}"); await DisposeBmsAsync(); }
        }
        throw new IOException("OTA data transfer finished but BMS APP did not return to a readable SPP/Modbus state.", last);
    }

    private OtaTransferMode GetOtaMode() { string tag = (OtaModeBox.SelectedItem as ComboBoxItem)?.Tag?.ToString() ?? "Auto"; return Enum.TryParse(tag, out OtaTransferMode m) ? m : OtaTransferMode.Auto; }
    private static bool IsExtendCompatibilityFailure(Exception ex) { string m=ex.Message; return m.Contains("OTA_PDU_LEN_ERR",StringComparison.OrdinalIgnoreCase)||m.Contains("OTA_MCU_NOT_SUPPORTED",StringComparison.OrdinalIgnoreCase)||m.Contains("OTA_PACKET_INVALID",StringComparison.OrdinalIgnoreCase)||m.Contains("requires MTU",StringComparison.OrdinalIgnoreCase); }
    private void CancelOta_Click(object sender, RoutedEventArgs e) => _otaCts?.Cancel();

    private void UpsertDevice(DiscoveredDevice d)
    {
        if (_deviceMap.TryGetValue(d.Address, out var old)) { int i=_devices.IndexOf(old); _deviceMap[d.Address]=d; if(i>=0)_devices[i]=d; } else { _deviceMap[d.Address]=d; _devices.Add(d); }
    }
    private static ushort ParseU16(string text) { string s=text.Trim(); return s.StartsWith("0x",StringComparison.OrdinalIgnoreCase)?Convert.ToUInt16(s[2..],16):Convert.ToUInt16(s); }
    private void AppendLog(string text) { LogBox.AppendText($"{DateTime.Now:HH:mm:ss.fff}  {text}{Environment.NewLine}"); LogBox.ScrollToEnd(); }
    private void ShowError(string title, Exception ex) { AppendLog($"{title}: {ex.Message}"); MessageBox.Show(ex.Message,title,MessageBoxButton.OK,MessageBoxImage.Error); }
}
