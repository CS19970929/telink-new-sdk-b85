using System.Collections.ObjectModel;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
using Microsoft.Win32;
using Windows.Devices.Bluetooth.Advertisement;

namespace BmsTool.Windows;

public partial class MainWindow : Window
{
    private readonly ObservableCollection<DiscoveredDevice> _devices = new();
    private readonly ObservableCollection<ProtectionParameterRow> _protectionRows = new(ProtectionParameterCatalog.Create());
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
    private bool _autoReconnectRunning;
    private int _pollFailureCount;
    private DateTime _nextReconnectUtc = DateTime.MinValue;

    public MainWindow()
    {
        InitializeComponent();
        DeviceList.ItemsSource = _devices;
        ProtectionGrid.ItemsSource = _protectionRows;
        _pollTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };
        _pollTimer.Tick += async (_, _) => await PollTickAsync();
        Closed += async (_, _) =>
        {
            _pollTimer.Stop();
            _watcher?.Stop();
            _otaCts?.Cancel();
            await DisposeBmsAsync();
        };
        AppendLog("Ready. Firmware protocol: NUS 6E400001/2/3 + Modbus RTU slave 0x01 + Telink OTA.");
        AppendLog("Battery data refresh is automatic after connection; interval=1s.");
    }

    private void ScanButton_Click(object sender, RoutedEventArgs e)
    {
        _watcher?.Stop();
        _devices.Clear();
        _deviceMap.Clear();
        _watcher = BmsBleTransport.CreateWatcher(d => Dispatcher.Invoke(() => UpsertDevice(d)));
        _watcher.Start();
        AppendLog("BLE scan started; list filter=BT_* only.");
    }

    private void StopScanButton_Click(object sender, RoutedEventArgs e)
    {
        _watcher?.Stop();
        AppendLog("BLE scan stopped.");
    }

    private async void ConnectButton_Click(object sender, RoutedEventArgs e)
    {
        _pollTimer.Stop();
        try
        {
            if (DeviceList.SelectedItem is not DiscoveredDevice selected)
                throw new InvalidOperationException("请先扫描并选择 BT_ 设备。");

            _watcher?.Stop();
            ConnectButton.IsEnabled = false;
            _connectedAddress = selected.Address;
            _pollFailureCount = 0;
            await ConnectBmsInternalAsync(selected.Address);
            await RefreshIdentityAsync();
            await RefreshBatteryAsync();
            StartAutomaticRefresh();
        }
        catch (Exception ex)
        {
            ShowError("连接失败", ex);
        }
        finally
        {
            ConnectButton.IsEnabled = true;
        }
    }

    private async Task ConnectBmsInternalAsync(ulong address, CancellationToken ct = default)
    {
        await DisposeBmsAsync();
        ConnectionText.Text = $"连接 {address:X12} ...";
        AppendLog($"Connecting BMS {address:X12}");

        var transport = new BmsBleTransport();
        transport.ConnectionProgress += OnConnectionProgress;
        BmsClient? client = null;
        try
        {
            await transport.ConnectAsync(address, ct);
            client = new BmsClient(transport);
            client.Log += OnProtocolLog;

            // Do not call a GATT connection successful until the actual firmware Modbus path responds.
            await client.ProbeAsync(ct);

            _bmsTransport = transport;
            _bms = client;
            _connectedAddress = address;
            transport.ConnectionProgress -= OnConnectionProgress;
            ConnectionText.Text = "已连接 · " + transport.DiscoveryDescription;
            AppendLog("BMS ready; GATT + Modbus probe passed. " + transport.DiscoveryDescription);
        }
        catch
        {
            transport.ConnectionProgress -= OnConnectionProgress;
            if (client is not null)
            {
                client.Log -= OnProtocolLog;
                await client.DisposeAsync();
            }
            await transport.DisposeAsync();
            throw;
        }
    }

    private void OnConnectionProgress(string text) => Dispatcher.BeginInvoke(() =>
    {
        ConnectionText.Text = text;
        AppendLog(text);
    });

    private void OnProtocolLog(string text) => Dispatcher.Invoke(() => AppendLog(text));

    private async Task DisposeBmsAsync()
    {
        if (_bms is not null)
        {
            _bms.Log -= OnProtocolLog;
            await _bms.DisposeAsync();
            _bms = null;
        }
        if (_bmsTransport is not null)
        {
            await _bmsTransport.DisposeAsync();
            _bmsTransport = null;
        }
    }

    private void StartAutomaticRefresh()
    {
        if (_bms is not null && !_otaRunning)
            _pollTimer.Start();
    }

    private async void RefreshBattery_Click(object sender, RoutedEventArgs e)
    {
        try { await RefreshBatteryAsync(); }
        catch (Exception ex) { ShowError("读取电池信息失败", ex); }
    }

    private async void RefreshIdentity_Click(object sender, RoutedEventArgs e)
    {
        try { await RefreshIdentityAsync(); }
        catch (Exception ex) { ShowError("读取设备信息失败", ex); }
    }

    private async Task RefreshBatteryAsync()
    {
        var bms = _bms ?? throw new InvalidOperationException("BMS 未连接。");
        BatterySnapshot s = await bms.ReadBatteryAsync();
        PackVoltageText.Text = $"{s.PackVoltageV:F2} V";
        CurrentText.Text = $"{s.CurrentA:+0.0;-0.0;0.0} A";
        SocText.Text = $"SOC {s.SocPercent}%\nSOH {s.SohPercent}%";
        TempsText.Text = $"最高 {s.MaxTempC:F1} °C\n最低 {s.MinTempC:F1} °C\nMOS {s.MosTempC:F1} °C";
        CellExtremeText.Text = $"Max {s.MaxCellMv} mV  Cell {s.MaxCellPosition}\nMin {s.MinCellMv} mV  Cell {s.MinCellPosition}\nΔ {s.CellDeltaMv} mV";
        CapacityText.Text = $"当前 {s.CapacityNowAh:F2} Ah\n满充 {s.CapacityFullAh:F2} Ah\n工厂 {s.CapacityFactoryAh:F2} Ah\n循环 {s.CycleCount}";
        StatusWordText.Text = $"0x{s.SystemStatus:X8}\nProtocol 0x{s.ProtocolVersion:X4}";
        DataSourceText.Text = s.UsesRealtimeWindow ? "实时窗口 0xD120" : "兼容窗口 0xD000";
        CellsText.Text = string.Join("    ", s.CellMillivolts.Select((mv, i) => $"C{i + 1:00}: {mv / 1000.0:F3} V"));
        AutoRefreshText.Text = $"自动刷新 · 1 秒 · {DateTime.Now:HH:mm:ss}";
    }

    private async Task<DeviceIdentity> RefreshIdentityAsync()
    {
        var bms = _bms ?? throw new InvalidOperationException("BMS 未连接。");
        DeviceIdentity id = await bms.ReadIdentityAsync();
        IdentityText.Text = $"Bluetooth : {id.BluetoothName}\nMAC       : {id.Mac}\nSN        : {id.Serial}\nHardware  : {id.Hardware}\nSoftware  : {id.Software}";
        BtNameResultText.Text = id.BluetoothName;
        if (id.BluetoothName.StartsWith("BT_", StringComparison.OrdinalIgnoreCase))
            NameSuffixBox.Text = id.BluetoothName[3..];
        return id;
    }

    private async Task PollTickAsync()
    {
        if (_polling || _otaRunning || _autoReconnectRunning) return;

        if (_bms is null)
        {
            if (_connectedAddress is not null && DateTime.UtcNow >= _nextReconnectUtc)
                await AutoReconnectAsync();
            return;
        }

        _polling = true;
        try
        {
            await RefreshBatteryAsync();
            _pollFailureCount = 0;
        }
        catch (Exception ex)
        {
            _pollFailureCount++;
            AppendLog($"AUTO REFRESH ERROR {_pollFailureCount}: {ex.Message}");
            if (_pollFailureCount >= 3)
                await AutoReconnectAsync();
        }
        finally
        {
            _polling = false;
        }
    }

    private async Task AutoReconnectAsync()
    {
        if (_autoReconnectRunning || _otaRunning || _connectedAddress is null) return;
        _autoReconnectRunning = true;
        _pollTimer.Stop();
        ulong address = _connectedAddress.Value;
        try
        {
            ConnectionText.Text = "通信异常，自动重连中...";
            AppendLog("Automatic reconnect started after repeated communication failures.");
            await ConnectBmsInternalAsync(address);
            await RefreshIdentityAsync();
            await RefreshBatteryAsync();
            _pollFailureCount = 0;
            _nextReconnectUtc = DateTime.MinValue;
            AppendLog("Automatic reconnect succeeded.");
        }
        catch (Exception ex)
        {
            _nextReconnectUtc = DateTime.UtcNow.AddSeconds(3);
            ConnectionText.Text = "自动重连失败，将继续重试";
            AppendLog("Automatic reconnect failed: " + ex.Message);
        }
        finally
        {
            _autoReconnectRunning = false;
            if (!_otaRunning) _pollTimer.Start();
        }
    }

    private async void ReadProtection_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            _pollTimer.Stop();
            await ReadProtectionAsync();
        }
        catch (Exception ex)
        {
            ShowError("读取保护参数失败", ex);
        }
        finally
        {
            StartAutomaticRefresh();
        }
    }

    private async Task ReadProtectionAsync()
    {
        var bms = _bms ?? throw new InvalidOperationException("BMS 未连接。");
        ProtectionStatusText.Text = "正在读取 65 项...";
        ushort[] values = await bms.ReadProtectionAllAsync();
        if (values.Length != _protectionRows.Count)
            throw new IOException($"保护参数数量错误：expected={_protectionRows.Count}, actual={values.Length}");

        for (int i = 0; i < values.Length; i++)
            _protectionRows[i].LoadFromDevice(values[i]);
        ProtectionStatusText.Text = $"已读取 {_protectionRows.Count} 项 · {DateTime.Now:HH:mm:ss}";
        AppendLog("Protection parameters loaded: 0x2100..0x2140.");
    }

    private async void WriteSelectedProtection_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            CommitProtectionGridEdits();
            if (ProtectionGrid.SelectedItem is not ProtectionParameterRow row)
                throw new InvalidOperationException("请先选择一个保护参数。 ");
            if (!row.TryParseEditedValue(out ushort value))
                throw new FormatException($"{row.AddressText} 修改值无效，请输入 0~65535 或 0x0000~0xFFFF。 ");

            if (MessageBox.Show(
                    $"确认写入保护参数？\n\n{row.AddressText}  {row.Group}/{row.Stage}\n{row.FirmwareField}\n{row.DeviceValue} → {value}\n\n固件会 SaveParam() 并更新 AFE 配置。",
                    "确认写保护参数",
                    MessageBoxButton.YesNo,
                    MessageBoxImage.Warning) != MessageBoxResult.Yes)
                return;

            _pollTimer.Stop();
            var bms = _bms ?? throw new InvalidOperationException("BMS 未连接。");
            ushort readback = await bms.WriteReadableRegisterAndVerifyAsync(row.Address, value);
            row.LoadFromDevice(readback);
            ProtectionStatusText.Text = $"{row.AddressText} 写入并回读通过";
            AppendLog($"Protection write verified: {row.AddressText}={readback}");
        }
        catch (Exception ex)
        {
            ShowError("写保护参数失败", ex);
        }
        finally
        {
            StartAutomaticRefresh();
        }
    }

    private async void WriteChangedProtection_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            CommitProtectionGridEdits();
            var invalid = _protectionRows.Where(r => !r.TryParseEditedValue(out _)).ToList();
            if (invalid.Count > 0)
                throw new FormatException($"存在 {invalid.Count} 个无效修改值；请先读取全部参数并检查输入。第一个：{invalid[0].AddressText}");

            var changed = _protectionRows
                .Select(r => new { Row = r, Parsed = r.TryParseEditedValue(out ushort value) ? value : (ushort)0 })
                .Where(x => x.Parsed != x.Row.DeviceValue)
                .ToList();
            if (changed.Count == 0)
            {
                ProtectionStatusText.Text = "没有待写入的修改项";
                return;
            }

            if (MessageBox.Show(
                    $"确认依次写入 {changed.Count} 个已修改保护参数？\n每项都会等待 Modbus ACK，并立即回读验证。\n固件会持久化参数并触发 AFE 参数更新。",
                    "确认批量写保护参数",
                    MessageBoxButton.YesNo,
                    MessageBoxImage.Warning) != MessageBoxResult.Yes)
                return;

            _pollTimer.Stop();
            var bms = _bms ?? throw new InvalidOperationException("BMS 未连接。");
            for (int i = 0; i < changed.Count; i++)
            {
                var item = changed[i];
                ProtectionStatusText.Text = $"写入 {i + 1}/{changed.Count} · {item.Row.AddressText}";
                ushort readback = await bms.WriteReadableRegisterAndVerifyAsync(item.Row.Address, item.Parsed);
                item.Row.LoadFromDevice(readback);
            }
            ProtectionStatusText.Text = $"{changed.Count} 项写入并回读全部通过";
            AppendLog($"Protection batch write verified: {changed.Count} registers.");
        }
        catch (Exception ex)
        {
            ShowError("批量写保护参数失败", ex);
        }
        finally
        {
            StartAutomaticRefresh();
        }
    }

    private void CommitProtectionGridEdits()
    {
        ProtectionGrid.CommitEdit(DataGridEditingUnit.Cell, true);
        ProtectionGrid.CommitEdit(DataGridEditingUnit.Row, true);
    }

    private async void ReadBmsParameter_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            _pollTimer.Stop();
            var bms = _bms ?? throw new InvalidOperationException("BMS 未连接。");
            ushort address = ParseU16(BmsParamAddressBox.Text);
            ushort value = (await bms.ReadRegistersAsync(address, 1))[0];
            BmsParamValueBox.Text = value.ToString();
            BmsParamResultText.Text = $"读取：{value} / 0x{value:X4}";
        }
        catch (Exception ex)
        {
            ShowError("读取 BMS 参数失败", ex);
        }
        finally
        {
            StartAutomaticRefresh();
        }
    }

    private async void WriteBmsParameter_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            ushort address = ParseU16(BmsParamAddressBox.Text);
            ushort value = ParseU16(BmsParamValueBox.Text);
            if (MessageBox.Show(
                    $"确认写入 BMS 寄存器？\n\nAddress = 0x{address:X4}\nValue = {value} (0x{value:X4})\n\n0x1102/0x1103 等地址可能触发控制行为。",
                    "确认写 BMS 参数",
                    MessageBoxButton.YesNo,
                    MessageBoxImage.Warning) != MessageBoxResult.Yes)
                return;

            _pollTimer.Stop();
            var bms = _bms ?? throw new InvalidOperationException("BMS 未连接。");
            await bms.WriteSingleRegisterAsync(address, value);
            string message = $"ACK 成功：0x{address:X4} <= {value} (0x{value:X4})";
            try
            {
                ushort readback = (await bms.ReadRegistersAsync(address, 1))[0];
                message += readback == value
                    ? $"；回读一致 {readback}"
                    : $"；回读 {readback} (0x{readback:X4})，该地址可能是命令/只写寄存器";
            }
            catch (Exception ex)
            {
                message += "；ACK 已确认，但回读失败：" + ex.Message;
            }
            BmsParamResultText.Text = message;
            AppendLog(message);
        }
        catch (Exception ex)
        {
            ShowError("写 BMS 参数失败", ex);
        }
        finally
        {
            StartAutomaticRefresh();
        }
    }

    private async void WriteName_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            _pollTimer.Stop();
            var bms = _bms ?? throw new InvalidOperationException("BMS 未连接。");
            string name = await bms.WriteBluetoothNameSuffixAsync(NameSuffixBox.Text);
            BtNameResultText.Text = $"已写入并回读: {name}";
            AppendLog($"Bluetooth name verified: {name}");
            MessageBox.Show($"蓝牙名已写入并回读确认：{name}\n固件已立即刷新广播名；重新扫描可看到新名称。", "蓝牙名", MessageBoxButton.OK, MessageBoxImage.Information);
        }
        catch (Exception ex)
        {
            ShowError("修改蓝牙名失败", ex);
        }
        finally
        {
            StartAutomaticRefresh();
        }
    }

    private async void ManualRead_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            _pollTimer.Stop();
            var bms = _bms ?? throw new InvalidOperationException("BMS 未连接。");
            ushort address = ParseU16(ManualAddressBox.Text);
            ushort qty = ParseU16(ManualQuantityBox.Text);
            if (qty is 0 or > 125) throw new ArgumentOutOfRangeException(nameof(qty), "数量必须为 1..125。");
            ushort[] words = await bms.ReadRegistersAsync(address, qty);
            RegisterOutput.Text = string.Join(Environment.NewLine, words.Select((v, i) => $"0x{address + i:X4} = {v,6}  0x{v:X4}"));
        }
        catch (Exception ex)
        {
            ShowError("寄存器读取失败", ex);
        }
        finally
        {
            StartAutomaticRefresh();
        }
    }

    private void BrowseFirmware_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            var dlg = new OpenFileDialog { Filter = "Telink firmware (*.bin)|*.bin|All files (*.*)|*.*" };
            if (dlg.ShowDialog() != true) return;
            _firmware = FirmwareImage.Load(dlg.FileName);
            FirmwareText.Text = $"{_firmware.FileName} · {_firmware.ImageSize:N0} bytes · {_firmware.LegacyPacketCount:N0} legacy packets";
            AppendLog("Firmware loaded: " + FirmwareText.Text);
        }
        catch (Exception ex)
        {
            _firmware = null;
            ShowError("固件校验失败", ex);
        }
    }

    private async void StartOta_Click(object sender, RoutedEventArgs e)
    {
        if (_otaRunning) return;
        try
        {
            var image = _firmware ?? throw new InvalidOperationException("请先选择 BIN。");
            ulong address = _connectedAddress ?? throw new InvalidOperationException("请先连接 BMS。");
            _otaRunning = true;
            _pollTimer.Stop();
            StartOtaButton.IsEnabled = false;
            CancelOtaButton.IsEnabled = true;
            OtaProgressBar.Value = 0;
            OtaVerifyText.Text = "";
            _otaCts = new CancellationTokenSource();

            string oldVersion = "";
            try
            {
                if (_bms is not null) oldVersion = (await _bms.ReadIdentityAsync(_otaCts.Token)).Software;
            }
            catch (Exception ex)
            {
                AppendLog("Pre-OTA version read warning: " + ex.Message);
            }

            await DisposeBmsAsync();
            ConnectionText.Text = "OTA 模式";

            OtaTransferMode requested = GetOtaMode();
            bool serverConfirmed;
            try
            {
                serverConfirmed = await RunOtaOnceAsync(address, image, requested, _otaCts.Token);
            }
            catch (Exception ex) when (requested == OtaTransferMode.Auto && IsExtendCompatibilityFailure(ex) && !_otaCts.IsCancellationRequested)
            {
                AppendLog("Extend64 rejected; reconnecting and falling back to Legacy Fast from index 0: " + ex.Message);
                await Task.Delay(700, _otaCts.Token);
                serverConfirmed = await RunOtaOnceAsync(address, image, OtaTransferMode.LegacyFast, _otaCts.Token);
            }

            OtaVerifyText.Text = serverConfirmed
                ? "OTA_ACCEPTED：设备返回 OTA_RESULT=SUCCESS，等待重启并验证 APP..."
                : "TRANSFER_COMPLETE：未收到 OTA_RESULT，继续通过重连验证 APP...";
            DeviceIdentity post = await VerifyAfterOtaAsync(address, _otaCts.Token);
            string expected = ExpectedVersionBox.Text.Trim();
            if (expected.Length > 0 && !string.Equals(post.Software, expected, StringComparison.OrdinalIgnoreCase))
                throw new IOException($"设备已重启，但软件版本校验失败：expected='{expected}', actual='{post.Software}'.");

            string versionNote = expected.Length > 0
                ? $"版本匹配 {post.Software}"
                : (oldVersion.Length > 0 && oldVersion != post.Software
                    ? $"版本 {oldVersion} → {post.Software}"
                    : $"当前版本 {post.Software}（未指定目标版本）");
            OtaVerifyText.Text = $"VERIFIED：设备已重启、SPP/Modbus 正常、实时电池数据可读；{versionNote}." +
                                 (serverConfirmed ? " OTA_RESULT=SUCCESS。" : " OTA_RESULT 未确认。 ");
            AppendLog("OTA VERIFIED: " + OtaVerifyText.Text);
            MessageBox.Show(OtaVerifyText.Text, "OTA 验证完成", MessageBoxButton.OK, MessageBoxImage.Information);
        }
        catch (OperationCanceledException)
        {
            AppendLog("OTA cancelled.");
            OtaVerifyText.Text = "CANCELLED";
        }
        catch (Exception ex)
        {
            AppendLog("OTA/VERIFY ERROR: " + ex.Message);
            OtaVerifyText.Text = "FAILED：" + ex.Message;
            MessageBox.Show(ex.Message, "OTA failed", MessageBoxButton.OK, MessageBoxImage.Error);
        }
        finally
        {
            _otaCts?.Dispose();
            _otaCts = null;
            _otaRunning = false;
            StartOtaButton.IsEnabled = true;
            CancelOtaButton.IsEnabled = false;
            StartAutomaticRefresh();
        }
    }

    private async Task<bool> RunOtaOnceAsync(ulong address, FirmwareImage image, OtaTransferMode mode, CancellationToken ct)
    {
        await using var transport = new OtaBleTransport();
        AppendLog($"Connecting OTA GATT {address:X12}...");
        await transport.ConnectAsync(address);
        AppendLog($"OTA GATT ready; MTU={transport.NegotiatedMtu}, notify={transport.NotificationsEnabled}");
        var client = new TelinkOtaClient(transport);
        client.Log += m => Dispatcher.Invoke(() => AppendLog(m));
        client.Progress += p => Dispatcher.Invoke(() =>
        {
            OtaProgressBar.Value = p.Percent;
            string eta = p.Eta is null ? "" : $" ETA {p.Eta.Value.TotalSeconds:F1}s";
            OtaProgressText.Text = $"{p.Mode} {p.Percent:F1}% · {p.BytesPerSecond / 1024.0:F1} KB/s{eta}";
        });
        return await client.UpgradeAsync(image, mode, ct);
    }

    private async Task<DeviceIdentity> VerifyAfterOtaAsync(ulong address, CancellationToken ct)
    {
        Exception? last = null;
        for (int attempt = 1; attempt <= 12; attempt++)
        {
            ct.ThrowIfCancellationRequested();
            try
            {
                await Task.Delay(attempt == 1 ? 1200 : 900, ct);
                AppendLog($"Post-OTA reconnect attempt {attempt}/12...");
                await ConnectBmsInternalAsync(address, ct);
                DeviceIdentity id = await (_bms ?? throw new IOException("BMS client unavailable")).ReadIdentityAsync(ct);
                _ = await _bms.ReadBatteryAsync(ct);
                await RefreshIdentityAsync();
                await RefreshBatteryAsync();
                return id;
            }
            catch (Exception ex)
            {
                last = ex;
                AppendLog($"Post-OTA reconnect {attempt} failed: {ex.Message}");
                await DisposeBmsAsync();
            }
        }
        throw new IOException("OTA data transfer finished but BMS APP did not return to a readable SPP/Modbus state.", last);
    }

    private OtaTransferMode GetOtaMode()
    {
        string tag = (OtaModeBox.SelectedItem as ComboBoxItem)?.Tag?.ToString() ?? "Auto";
        return Enum.TryParse(tag, out OtaTransferMode m) ? m : OtaTransferMode.Auto;
    }

    private static bool IsExtendCompatibilityFailure(Exception ex)
    {
        string m = ex.Message;
        return m.Contains("OTA_PDU_LEN_ERR", StringComparison.OrdinalIgnoreCase) ||
               m.Contains("OTA_MCU_NOT_SUPPORTED", StringComparison.OrdinalIgnoreCase) ||
               m.Contains("OTA_PACKET_INVALID", StringComparison.OrdinalIgnoreCase) ||
               m.Contains("requires MTU", StringComparison.OrdinalIgnoreCase);
    }

    private void CancelOta_Click(object sender, RoutedEventArgs e) => _otaCts?.Cancel();

    private void UpsertDevice(DiscoveredDevice d)
    {
        if (_deviceMap.TryGetValue(d.Address, out var old))
        {
            int i = _devices.IndexOf(old);
            _deviceMap[d.Address] = d;
            if (i >= 0) _devices[i] = d;
        }
        else
        {
            _deviceMap[d.Address] = d;
            _devices.Add(d);
        }
    }

    private static ushort ParseU16(string text)
    {
        string s = text.Trim();
        return s.StartsWith("0x", StringComparison.OrdinalIgnoreCase)
            ? Convert.ToUInt16(s[2..], 16)
            : Convert.ToUInt16(s);
    }

    private void AppendLog(string text)
    {
        LogBox.AppendText($"{DateTime.Now:HH:mm:ss.fff}  {text}{Environment.NewLine}");
        LogBox.ScrollToEnd();
    }

    private void ShowError(string title, Exception ex)
    {
        AppendLog($"{title}: {ex.Message}");
        MessageBox.Show(ex.Message, title, MessageBoxButton.OK, MessageBoxImage.Error);
    }
}
