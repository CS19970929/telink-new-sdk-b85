using System.Collections.ObjectModel;
using System.Diagnostics;
using System.IO;
using System.IO.Ports;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
using Windows.Devices.Bluetooth.Advertisement;

namespace BmsTool.Windows;

public partial class MainWindow : Window
{
    private bool ShFactoryBusy() => false;

    private enum ConnectionMode { Ble, Serial }

    private readonly ObservableCollection<DiscoveredDevice> _devices = new();
    private readonly ObservableCollection<SerialPortEndpoint> _serialPorts = new();
    private readonly ObservableCollection<ProtectionParameterRow> _protectionRows = new(ProtectionParameterCatalog.Create());
    private readonly Dictionary<ulong, DiscoveredDevice> _deviceMap = new();
    private readonly DispatcherTimer _pollTimer;
    private readonly SessionLogger _sessionLog = new();

    private BmsBleScanner? _watcher;
    private IBmsTransport? _bmsTransport;
    private BmsClient? _bms;
    private ulong? _connectedAddress;
    private string? _connectedSerialPort;
    private int _serialBaudRate = BmsSerialTransport.DefaultBaudRate;
    private string _connectedName = string.Empty;
    private ConnectionMode _connectionMode = ConnectionMode.Ble;
    private bool _polling;
    // 客户版无 OTA 和 Flash 耐久操作，供共享通信门禁读取。
    private bool _otaRunning => false;
    private bool _shBusy => false;
    private bool _autoReconnectRunning;
    private int _pollFailureCount;
    private DateTime _nextReconnectUtc = DateTime.MinValue;

    public MainWindow()
    {
        InitializeComponent();
        AddMosControlPanel();
        Loaded += (_, _) => { if (_diagTimer is null) AddDiagnosticTab(); };
        DeviceList.ItemsSource = _devices;
        SerialPortList.ItemsSource = _serialPorts;
        ProtectionGrid.ItemsSource = _protectionRows;
        InitializeProtectionLayout();
        _pollTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };
        _pollTimer.Tick += async (_, _) => await PollTickAsync();

        Closed += async (_, _) =>
        {
            _pollTimer.Stop();
            _watcher?.Stop();
            await DisposeBmsAsync();
            _sessionLog.Write("SESSION", "Window closed");
            _sessionLog.Dispose();
        };

        AppendLog("应用启动；连接后电池数据自动按 1 秒周期刷新。", "APP");
        AppendLog("完整诊断日志已启用：" + _sessionLog.FilePath, "APP");
    }

    private bool IsSerialConnectionMode => ConnectionModeBox.SelectedIndex == 1;

    private void ConnectionModeBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (!IsInitialized) return;
        bool serial = IsSerialConnectionMode;
        DeviceList.Visibility = serial ? Visibility.Collapsed : Visibility.Visible;
        SerialPortList.Visibility = serial ? Visibility.Visible : Visibility.Collapsed;
        SerialBaudRateBox.Visibility = serial ? Visibility.Visible : Visibility.Collapsed;
        ScanButton.Content = serial ? "刷新" : "搜索";
        StopScanButton.Visibility = serial ? Visibility.Collapsed : Visibility.Visible;
        if (serial)
            RefreshSerialPorts();
    }

    private void RefreshSerialPorts()
    {
        _serialPorts.Clear();
        foreach (string portName in SerialPort.GetPortNames().OrderBy(name => name, StringComparer.OrdinalIgnoreCase))
            _serialPorts.Add(new SerialPortEndpoint(portName));
        if (_serialPorts.Count > 0 && SerialPortList.SelectedIndex < 0)
            SerialPortList.SelectedIndex = 0;
        ConnectionText.Text = _serialPorts.Count == 0 ? "未发现可用串口" : $"发现 {_serialPorts.Count} 个串口，请选择后连接";
        AppendLog($"串口刷新完成；count={_serialPorts.Count}; baud={_serialBaudRate}; format=8N1", "SCAN");
    }

    private int GetSerialBaudRate()
    {
        string text = SerialBaudRateBox.Text.Trim();
        if (!int.TryParse(text, out int baudRate) || baudRate < 300 || baudRate > 4_000_000)
            throw new InvalidOperationException("波特率必须是 300～4000000 之间的整数。常用值：9600、19200、38400、57600、115200。");
        return baudRate;
    }

    private void ScanButton_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            if (IsSerialConnectionMode)
            {
                RefreshSerialPorts();
                return;
            }
            if (_watcher is not null)
            {
                AppendLog($"停止旧扫描器，status={_watcher.Status}", "SCAN");
                _watcher.Stop();
            }

            _devices.Clear();
            _deviceMap.Clear();
            _watcher = new BmsBleScanner(
                d => Dispatcher.BeginInvoke(() => UpsertDevice(d)),
                msg => AppendLog(msg, "SCAN"));
            _watcher.ScanFailed += message => Dispatcher.BeginInvoke(() =>
            {
                if (_bms is null)
                    ConnectionText.Text = message;
            });
            AppendLog($"开始 BLE 设备枚举与主动扫描；filter=BT_/BT-；status(before)={_watcher.Status}", "SCAN");
            _watcher.Start();
            AppendLog($"扫描器已启动；status(after)={_watcher.Status}", "SCAN");
            ConnectionText.Text = "正在扫描设备...";
        }
        catch (Exception ex)
        {
            ShowError("启动扫描失败", ex);
        }
    }

    private void StopScanButton_Click(object sender, RoutedEventArgs e)
    {
        if (_watcher is null) return;
        AppendLog($"用户停止扫描；status(before)={_watcher.Status}", "SCAN");
        _watcher.Stop();
        AppendLog($"停止命令已发送；status(after)={_watcher.Status}", "SCAN");
        if (_bms is null) ConnectionText.Text = "未连接";
    }

    private async void ConnectButton_Click(object sender, RoutedEventArgs e)
    {
        _pollTimer.Stop();
        try
        {
            ConnectButton.IsEnabled = false;
            ConnectionText.Text = "正在连接设备...";
            if (IsSerialConnectionMode)
            {
                if (SerialPortList.SelectedItem is not SerialPortEndpoint serial)
                    throw new InvalidOperationException("请先刷新并选择串口。");

                _connectionMode = ConnectionMode.Serial;
                int baudRate = GetSerialBaudRate();
                _serialBaudRate = baudRate;
                _connectedAddress = null;
                _connectedSerialPort = serial.PortName;
                _connectedName = $"串口 {serial.PortName}";
                _pollFailureCount = 0;
                AppendLog($"用户发起串口连接；port={serial.PortName}; baud={baudRate}; format=8N1", "CONNECT");
                await ConnectSerialInternalAsync(serial.PortName, baudRate);
                await RefreshIdentityAsync();
                await RefreshBatteryAsync();
                StartAutomaticRefresh();
                ConnectionText.Text = $"已连接：{serial.PortName} · {baudRate} 8N1";
                return;
            }
            if (DeviceList.SelectedItem is not DiscoveredDevice selected)
                throw new InvalidOperationException("请先扫描并选择设备。");

            AppendLog($"用户发起连接；name='{selected.Name}'; address={selected.Address:X12}; rssi={selected.Rssi}dBm", "CONNECT");
            if (_watcher is not null)
            {
                AppendLog($"连接前停止扫描；watcherStatus={_watcher.Status}", "SCAN");
                _watcher.Stop();
            }

            _connectionMode = ConnectionMode.Ble;
            _connectedAddress = selected.Address;
            _connectedSerialPort = null;
            _connectedName = selected.Name;
            _pollFailureCount = 0;

            await ConnectBmsInternalAsync(selected.Address);
            await RefreshIdentityAsync();
            await RefreshBatteryAsync();
            StartAutomaticRefresh();
            ConnectionText.Text = $"已连接：{_connectedName}";
        }
        catch (Exception ex)
        {
            ConnectionText.Text = "连接失败";
            ShowError("连接失败", ex, "连接阶段、耗时和 Windows BLE 错误已写入本机日志文件：" + _sessionLog.FilePath);
        }
        finally
        {
            ConnectButton.IsEnabled = true;
        }
    }

    private async Task<bool> ConnectBmsInternalAsync(ulong address, CancellationToken ct = default, bool allowIapOnly = false)
    {
        await DisposeBmsAsync();
        AppendLog($"ConnectBmsInternal START address={address:X12}", "CONNECT");

        var transport = new BmsBleTransport();
        transport.ConnectionProgress += OnConnectionProgress;
        BmsClient? client = null;
        var total = Stopwatch.StartNew();

        try
        {
            await transport.ConnectAsync(address, ct);
            client = new BmsClient(transport, _communicationStatistics);
            client.Log += OnProtocolLog;

            var probeTimer = Stopwatch.StartNew();
            AppendLog("[CONNECT] STEP_BEGIN stage=ModbusProbe", "CONNECT");
            await client.ProbeAsync(ct);
            AppendLog($"[CONNECT] STEP_OK stage=ModbusProbe elapsed={probeTimer.ElapsedMilliseconds}ms", "CONNECT");

            _bmsTransport = transport;
            _bms = client;
            _connectedAddress = address;
            transport.ConnectionProgress -= OnConnectionProgress;
            AppendLog($"BMS READY total={total.ElapsedMilliseconds}ms; {transport.DiscoveryDescription}", "CONNECT");
            return true;
        }
        catch (Exception ex)
        {
            _sessionLog.WriteException("CONNECT", "ConnectBmsInternal", ex);
            transport.ConnectionProgress -= OnConnectionProgress;
            if (allowIapOnly && client is not null && transport.IsConnected)
            {
                client.Log -= OnProtocolLog;
                await client.DisposeAsync();
                _bmsTransport = transport;
                _bms = null;
                _connectedAddress = address;
                AppendLog($"GATT READY without application probe; elapsed={total.ElapsedMilliseconds}ms; {transport.DiscoveryDescription}", "CONNECT");
                return false;
            }
            if (client is not null)
            {
                client.Log -= OnProtocolLog;
                await client.DisposeAsync();
            }
            await transport.DisposeAsync();
            throw;
        }
    }

    private void OnConnectionProgress(string text)
    {
        AppendLog(text, "BLE");
        if (text.Contains("ATTEMPT_BEGIN", StringComparison.Ordinal))
        {
            Dispatcher.BeginInvoke(() => ConnectionText.Text = "正在连接设备...");
        }
    }

    private void OnProtocolLog(string text) => AppendLog(text, "MODBUS");

    private async Task DisposeBmsAsync()
    {
        InvalidateCurrentCalibrationConnection();
        ResetMosControlUi();
        _mosCts?.Cancel();
        NoteMosGap("设备断开或重连");
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
        if (_bms is not null && !_otaRunning && !_parameterBusy)
            _pollTimer.Start();
    }

    private async void RefreshIdentity_Click(object sender, RoutedEventArgs e)
    {
        try { await RefreshIdentityAsync(); }
        catch (Exception ex) { ShowError("读取设备信息失败", ex); }
    }

    private async Task<DeviceIdentity> RefreshIdentityAsync(CancellationToken ct = default)
    {
        var bms = _bms ?? throw new InvalidOperationException("BMS 未连接。");
        string fallbackMac = _connectionMode == ConnectionMode.Serial
            ? string.Empty
            : BmsBleTransport.FormatBluetoothAddress(_connectedAddress ?? 0);
        string fallbackName = _connectionMode == ConnectionMode.Serial
            ? $"串口 {_connectedSerialPort}"
            : _connectedName;
        DeviceIdentity id = await bms.ReadIdentityAsync(fallbackMac, fallbackName, ct);
        if (!ReferenceEquals(bms, _bms)) return id;
        SetMosIdentity(bms, id);
        IdentityText.Text = $"蓝牙名称：{id.BluetoothName}\nMAC：{id.Mac}\n序列号：{id.Serial}\n硬件版本：{id.Hardware}\n软件版本：{id.Software}";
        BtNameResultText.Text = id.BluetoothName;
        if (id.BluetoothName.StartsWith("BT_", StringComparison.OrdinalIgnoreCase) ||
            id.BluetoothName.StartsWith("BT-", StringComparison.OrdinalIgnoreCase))
            NameSuffixBox.Text = id.BluetoothName[3..];
        return id;
    }

    private async Task RefreshBatteryAsync()
    {
        var bms = _bms ?? throw new InvalidOperationException("BMS 未连接。");
        BatterySnapshot snapshot = await bms.ReadBatteryAsync();
        if (!ReferenceEquals(bms, _bms)) return;
        ApplyBatterySnapshot(snapshot);
        await RefreshMosControlStateAsync(bms, default);
        await RefreshMosMonitorAsync(bms);
        await RefreshSleepStatusAsync(bms);
    }

    private void ApplyBatterySnapshot(BatterySnapshot s)
    {
        RecordMosSnapshot(s);
        PackVoltageText.Text = $"{s.PackVoltageV:F2} V";
        CurrentText.Text = $"{s.CurrentA:+0.0;-0.0;0.0} A（{s.WorkState}）";
        SocText.Text = $"SOC {s.SocPercent}%\nSOH {s.SohDisplay}";
        WorkStateText.Text = s.WorkState;

        MosStateText.Text =
            $"充电 MOS：{OnOff(s.ChargeMosOn)}\n" +
            $"放电 MOS：{OnOff(s.DischargeMosOn)}\n" +
            $"预充 MOS：{OnOff(s.PrechargeMosOn)}";

        SystemStateText.Text =
            $"AFE：{(s.Afe1On ? "工作" : "未工作")}\n" +
            $"均衡：{(s.BalancingOn ? "进行中" : "未进行")}\n" +
            $"加热：{OnOff(s.HeatingOn)}\n" +
            $"制冷：{OnOff(s.CoolingOn)}\n" +
            $"系统：{(s.PreparingSleep ? "准备休眠" : "运行中")}";

        ProtectionSummaryText.Text = s.ProtectionSummary;
        ProtectionLevelsText.Text =
            $"一级：{s.ProtectionLevel1Text}\n" +
            $"二级：{s.ProtectionLevel2Text}\n" +
            $"三级：{s.ProtectionLevel3Text}";

        TempsText.Text = $"最高温度：{s.MaxTempC:F1} °C\n最低温度：{s.MinTempC:F1} °C\nMOS 温度：{s.MosTempC:F1} °C";
        CellExtremeText.Text = $"最高：{s.MaxCellMv} mV（第 {s.MaxCellPosition} 串）\n最低：{s.MinCellMv} mV（第 {s.MinCellPosition} 串）\n压差：{s.CellDeltaMv} mV";
        CapacityText.Text = $"当前容量：{s.CapacityNowAh:F2} Ah\n满充容量：{s.CapacityFullAh:F2} Ah\n工厂容量：{s.CapacityFactoryAh:F2} Ah\n循环次数：{s.CycleCount}";
        CellsText.Text = string.Join("    ", s.CellMillivolts
            .Select((mv, i) => (mv, Index: i + 1))
            .Where(cell => cell.mv != BmsRegisters.MissingCellVoltageMv)
            .Select(cell => FormattableString.Invariant($"第{cell.Index}串 {cell.mv / 1000.0:F3} V{(s.IsCellBalancing(cell.Index - 1) ? " [均衡]" : "")}")));

        _customerLastDataUtc = DateTime.UtcNow;
        LastUpdateText.Text = $"最后更新：{DateTime.Now:HH:mm:ss}";
        CurrentSocParamText.Text = $"当前 SOC：{s.SocPercent}%";
        CurrentCycleParamText.Text = $"当前循环次数：{s.CycleCount}";

    }

    private static string OnOff(bool value) => value ? "开启" : "关闭";

    private async Task PollTickAsync()
    {
        if (_polling || _otaRunning || _autoReconnectRunning || _shBusy || _parameterBusy) return;

        if (_bms is null)
        {
            bool endpointAvailable = _connectionMode == ConnectionMode.Ble
                ? _connectedAddress is not null
                : !string.IsNullOrWhiteSpace(_connectedSerialPort);
            if (endpointAvailable && DateTime.UtcNow >= _nextReconnectUtc)
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
            NoteMosGap("电池刷新失败：" + ex.Message);
            _pollFailureCount++;
            AppendLog($"AUTO_REFRESH_FAIL count={_pollFailureCount}; type={ex.GetType().Name}; hresult=0x{ex.HResult:X8}; message={ex.Message}", "POLL");
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
        if (_autoReconnectRunning || _otaRunning) return;
        if (_connectionMode == ConnectionMode.Ble && _connectedAddress is null) return;
        if (_connectionMode == ConnectionMode.Serial && string.IsNullOrWhiteSpace(_connectedSerialPort)) return;
        _communicationReconnectAttempts++;
        _autoReconnectRunning = true;
        _pollTimer.Stop();
        try
        {
            ConnectionText.Text = "通信异常，自动重连中...";
            AppendLog("自动重连开始：连续 3 次数据刷新失败。", "RECONNECT");
            if (_connectionMode == ConnectionMode.Serial)
                await ConnectSerialInternalAsync(_connectedSerialPort!, _serialBaudRate);
            else
                await ConnectBmsInternalAsync(_connectedAddress!.Value);
            await RefreshIdentityAsync();
            await RefreshBatteryAsync();
            _pollFailureCount = 0;
            _nextReconnectUtc = DateTime.MinValue;
            ConnectionText.Text = string.IsNullOrEmpty(_connectedName) ? "已连接" : $"已连接：{_connectedName}";
            AppendLog("自动重连成功。", "RECONNECT");
        }
        catch (Exception ex)
        {
            _sessionLog.WriteException("RECONNECT", "AutoReconnect", ex);
            _nextReconnectUtc = DateTime.UtcNow.AddSeconds(3);
            ConnectionText.Text = "自动重连失败，将继续重试";
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
        catch (Exception ex) { ShowError("读取保护参数失败", ex); }
        finally { StartAutomaticRefresh(); }
    }

    private async Task ReadProtectionAsync()
    {
        var bms = _bms ?? throw new InvalidOperationException("BMS 未连接。");
        ProtectionStatusText.Text = "正在读取...";
        ushort[] values = await bms.ReadProtectionAllAsync();
        if (values.Length != _protectionRows.Count)
            throw new IOException($"保护参数数量错误：expected={_protectionRows.Count}, actual={values.Length}");
        for (int i = 0; i < values.Length; i++) _protectionRows[i].LoadFromDevice(values[i]);
        ProtectionStatusText.Text = $"读取完成 · {DateTime.Now:HH:mm:ss}";
        AppendLog($"保护参数读取完成 count={values.Length}; rawRange=0x2100..0x2140", "PARAM");
    }

    private async void WriteSelectedProtection_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            CommitProtectionGridEdits();
            if (ProtectionGrid.SelectedItem is not ProtectionParameterRow row)
                throw new InvalidOperationException("请先选择一个保护参数。");
            if (!row.TryParseEditedValue(out ushort value))
                throw new FormatException($"{row.CustomerName} 的修改值无效。");

            if (MessageBox.Show(
                    $"确认修改 {row.CustomerName}？\n\n当前值：{row.DeviceDisplayValue} {row.Unit}\n新值：{row.EditValue} {row.Unit}\n\n写入后会立即保存到设备。",
                    "确认修改保护参数",
                    MessageBoxButton.YesNo,
                    MessageBoxImage.Warning) != MessageBoxResult.Yes)
                return;

            _pollTimer.Stop();
            var bms = _bms ?? throw new InvalidOperationException("BMS 未连接。");
            ushort readback = await bms.WriteReadableRegisterAndVerifyAsync(row.Address, value);
            row.LoadFromDevice(readback);
            ProtectionStatusText.Text = $"{row.CustomerName} 写入并校验成功";
            AppendLog($"PROTECTION_WRITE_OK name='{row.CustomerName}'; address=0x{row.Address:X4}; raw={readback}", "PARAM");
            ShowParameterSaved(ProtectionStatusText.Text);
        }
        catch (Exception ex) { ShowError("写保护参数失败", ex); }
        finally { StartAutomaticRefresh(); }
    }

    private async void WriteChangedProtection_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            CommitProtectionGridEdits();
            var invalid = _protectionRows.Where(r => !r.TryParseEditedValue(out _)).ToList();
            if (invalid.Count > 0) throw new FormatException($"存在无效修改值：{invalid[0].CustomerName}");

            var changed = _protectionRows
                .Select(r => new { Row = r, Parsed = r.TryParseEditedValue(out ushort v) ? v : (ushort)0 })
                .Where(x => x.Parsed != x.Row.DeviceValue)
                .ToList();

            if (changed.Count == 0)
            {
                ProtectionStatusText.Text = "没有待写入的修改项";
                return;
            }

            if (MessageBox.Show(
                    $"确认写入 {changed.Count} 个已修改保护参数？\n\n同一保护项目的5个参数整组写入并回读校验；不同项目依次提交。",
                    "确认批量修改",
                    MessageBoxButton.YesNo,
                    MessageBoxImage.Warning) != MessageBoxResult.Yes)
                return;

            _pollTimer.Stop();
            var bms = _bms ?? throw new InvalidOperationException("BMS 未连接。");
            await bms.WriteProtectionChangesAsync(
                changed.ToDictionary(x => x.Row.Address, x => x.Parsed),
                (address, values) => {
                    for (int j = 0; j < values.Length; j++)
                        _protectionRows.Single(row => row.Address == address + j).LoadFromDevice(values[j]);
                    ProtectionStatusText.Text = $"{ProtectionBatch.Name(address)} 整组写入并校验成功";
                    AppendLog($"PROTECTION_GROUP_WRITE_OK address=0x{address:X4}; words=5", "PARAM");
                });
            ProtectionStatusText.Text = $"{changed.Count} 项全部写入并校验成功";
            ShowParameterSaved(ProtectionStatusText.Text);
        }
        catch (Exception ex) { ShowError("批量写保护参数失败", ex, "不同保护组依次提交，已完成组可能已保存；请重新读取全部参数确认，不要直接重发。"); }
        finally { StartAutomaticRefresh(); }
    }

    private void CommitProtectionGridEdits()
    {
        ProtectionGrid.CommitEdit(DataGridEditingUnit.Cell, true);
        ProtectionGrid.CommitEdit(DataGridEditingUnit.Row, true);
    }

    private async void WriteSocParameter_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            ushort soc = ParseU16(SocSetBox.Text);
            if (soc > 100) throw new ArgumentOutOfRangeException(nameof(soc), "SOC 必须在 0~100%。");
            if (MessageBox.Show($"确认把当前 SOC 校准为 {soc}%？", "SOC 校准", MessageBoxButton.YesNo, MessageBoxImage.Warning) != MessageBoxResult.Yes) return;
            _pollTimer.Stop();
            var bms = _bms ?? throw new InvalidOperationException("BMS 未连接。");
            BatterySnapshot snapshot = await bms.SetSocAndVerifyAsync(soc);
            ApplyBatterySnapshot(snapshot);
            BmsParamResultText.Text = $"SOC 已写入并校验：{soc}%";
            AppendLog($"SOC_SET_VERIFY_OK value={soc}", "PARAM");
            ShowParameterSaved(BmsParamResultText.Text);
        }
        catch (Exception ex) { ShowError("SOC 校准失败", ex); }
        finally { StartAutomaticRefresh(); }
    }

    private async void WriteCycleParameter_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            ushort cycle = ParseU16(CycleSetBox.Text);
            if (MessageBox.Show($"确认把循环次数修改为 {cycle}？", "修改循环次数", MessageBoxButton.YesNo, MessageBoxImage.Warning) != MessageBoxResult.Yes) return;
            _pollTimer.Stop();
            var bms = _bms ?? throw new InvalidOperationException("BMS 未连接。");
            BatterySnapshot snapshot = await bms.SetCycleCountAndVerifyAsync(cycle);
            ApplyBatterySnapshot(snapshot);
            BmsParamResultText.Text = $"循环次数已写入并校验：{cycle}";
            AppendLog($"CYCLE_SET_VERIFY_OK value={cycle}", "PARAM");
            ShowParameterSaved(BmsParamResultText.Text);
        }
        catch (Exception ex) { ShowError("修改循环次数失败", ex); }
        finally { StartAutomaticRefresh(); }
    }

    private async void WriteName_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            _pollTimer.Stop();
            var bms = _bms ?? throw new InvalidOperationException("BMS 未连接。");
            string name = await bms.WriteBluetoothNameSuffixAsync(NameSuffixBox.Text);
            BtNameResultText.Text = $"已写入并确认：{name}";
            _connectedName = name;
            ConnectionText.Text = $"已连接：{name}";
            AppendLog($"BLUETOOTH_NAME_VERIFY_OK name='{name}'", "PARAM");
            MessageBox.Show($"蓝牙名称已修改为：{name}", "修改成功", MessageBoxButton.OK, MessageBoxImage.Information);
        }
        catch (Exception ex) { ShowError("修改蓝牙名失败", ex); }
        finally { StartAutomaticRefresh(); }
    }

    private void UpsertDevice(DiscoveredDevice device)
    {
        if (_deviceMap.TryGetValue(device.Address, out var old))
        {
            int index = _devices.IndexOf(old);
            bool wasSelected = DeviceList.SelectedItem is DiscoveredDevice selected &&
                               selected.Address == device.Address;
            _deviceMap[device.Address] = device;
            if (index >= 0) _devices[index] = device;
            if (wasSelected) DeviceList.SelectedItem = device;
        }
        else
        {
            _deviceMap[device.Address] = device;
            _devices.Add(device);
            AppendLog($"DISCOVER name='{device.Name}'; address={device.Address:X12}; rssi={device.Rssi}dBm", "SCAN");
        }
    }

    private static ushort ParseU16(string text)
    {
        string value = text.Trim();
        return value.StartsWith("0x", StringComparison.OrdinalIgnoreCase)
            ? Convert.ToUInt16(value[2..], 16)
            : Convert.ToUInt16(value);
    }

    private void AppendLog(string text, string category = "APP")
    {
        _sessionLog.Write(category, text);
    }

    private async Task ConnectSerialInternalAsync(string portName, int baudRate, CancellationToken ct = default)
    {
        await DisposeBmsAsync();
        var transport = new BmsSerialTransport();
        transport.ConnectionProgress += OnConnectionProgress;
        BmsClient? client = null;
        try
        {
            await transport.ConnectAsync(portName, baudRate, ct);
            client = new BmsClient(transport, _communicationStatistics);
            client.Log += OnProtocolLog;
            AppendLog("[CONNECT] STEP_BEGIN stage=ModbusProbe transport=Serial", "CONNECT");
            await client.ProbeAsync(ct);
            _bmsTransport = transport;
            _bms = client;
            _connectedAddress = null;
            _connectedSerialPort = portName;
            AppendLog($"BMS READY transport=Serial; {transport.DiscoveryDescription}", "CONNECT");
        }
        catch
        {
            if (client is not null)
            {
                client.Log -= OnProtocolLog;
                await client.DisposeAsync();
            }
            await transport.DisposeAsync();
            throw;
        }
        finally
        {
            transport.ConnectionProgress -= OnConnectionProgress;
        }
    }

    private void ShowError(string title, Exception ex, string? extra = null)
    {
        _sessionLog.WriteException("ERROR", title, ex);
        AppendLog($"{title}: type={ex.GetType().FullName}; hresult=0x{ex.HResult:X8}; message={ex.Message}", "ERROR");
        string message = extra is null ? ex.Message : extra + "\n\n" + ex.Message;
        MessageBox.Show(message, title, MessageBoxButton.OK, MessageBoxImage.Error);
    }
}
