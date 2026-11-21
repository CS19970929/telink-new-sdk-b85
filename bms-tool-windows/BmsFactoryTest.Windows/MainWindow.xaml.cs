using System.Collections.ObjectModel;
using System.Diagnostics;
using System.IO;
using System.IO.Ports;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
using Microsoft.Win32;
using Windows.Devices.Bluetooth.Advertisement;
using BmsFactoryTest.Windows;

namespace BmsTool.Windows;

public partial class MainWindow : Window
{
    private bool ShFactoryBusy() => _factoryTestCts is not null || _flashTestCts is not null;

    private enum ConnectionMode { Ble, Serial, Can }

    private readonly ObservableCollection<DiscoveredDevice> _devices = new();
    private readonly ObservableCollection<SerialPortEndpoint> _serialPorts = new();
    private readonly ObservableCollection<ProtectionParameterRow> _protectionRows = new(ProtectionParameterCatalog.Create());
    private readonly ObservableCollection<FactoryTestStepResult> _factorySteps = new();
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
    private bool _autoReconnectRunning;
    private int _pollFailureCount;
    private DateTime _nextReconnectUtc = DateTime.MinValue;
    private CancellationTokenSource? _factoryTestCts;

    public MainWindow()
    {
        InitializeComponent();
        InitializeBleDiscoveryUi();
        AddMosControlPanel();
        InitializeUiLog();
        Loaded += (_, _) => { if (_diagTimer is null) AddDiagnosticTab(); };
        DeviceList.ItemsSource = _devices;
        SerialPortList.ItemsSource = _serialPorts;
        ProtectionGrid.ItemsSource = _protectionRows;
        InitializeProtectionLayout();
        FactoryStepGrid.ItemsSource = _factorySteps;
        FactoryCaseBox.ItemsSource = FactoryTestEngine.AvailableCases.Select(c => c.Name).ToArray();
        FactoryCaseBox.SelectedIndex = 0;
        LogPathText.Text = "日志文件：" + _sessionLog.FilePath;

        _pollTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };
        _pollTimer.Tick += async (_, _) => await PollTickAsync();
        InitializeUnifiedMonitor();

        Closed += async (_, _) =>
        {
            _pollTimer.Stop();
            StopBleDiscovery();
            _otaCts?.Cancel();
            _factoryTestCts?.Cancel();
            _flashTestCts?.Cancel();
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
        if (!TryChangeUnifiedMode()) return;
        if (IsOwcMode) { RefreshSerialPorts(); UpdateUnifiedMonitorState(); return; }
        bool serial = IsSerialConnectionMode || IsCanMode;
        DeviceList.Visibility = serial ? Visibility.Collapsed : Visibility.Visible;
        SerialPortList.Visibility = serial ? Visibility.Visible : Visibility.Collapsed;
        SerialBaudRateBox.Visibility = IsSerialConnectionMode ? Visibility.Visible : Visibility.Collapsed;
        ScanButton.Content = serial ? "刷新" : "搜索";
        StopScanButton.Visibility = serial ? Visibility.Collapsed : Visibility.Visible;
        RefreshBleDeviceList();
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

    private async void FactoryRunButton_Click(object sender, RoutedEventArgs e) =>
        await RunFactoryTestAsync(null);

    private async void FactorySingleRunButton_Click(object sender, RoutedEventArgs e)
    {
        string? selected = FactoryCaseBox.SelectedItem as string;
        if (string.IsNullOrWhiteSpace(selected))
        {
            MessageBox.Show("请先选择一个测试项。", "单项测试", MessageBoxButton.OK, MessageBoxImage.Information);
            return;
        }
        await RunFactoryTestAsync(selected);
    }

    private async void FactoryRefreshButton_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            await RefreshIdentityAsync();
            await RefreshBatteryAsync();
            FactoryConnectionText.Text = "连接状态：已连接；手动刷新完成";
        }
        catch (Exception ex) { ShowError("刷新电池信息失败", ex); }
    }

    private async Task RunFactoryTestAsync(string? onlyCaseName)
    {
        if (_shBusy) return;
        try
        {
            var bms = _bms ?? throw new InvalidOperationException("请先连接 BMS。");
            FactoryRunButton.IsEnabled = false;
            FactorySingleRunButton.IsEnabled = false;
            FactoryRefreshButton.IsEnabled = false;
            FactoryResultText.Text = onlyCaseName is null ? "完整测试进行中..." : $"测试中：{onlyCaseName}";
            FactoryReportText.Text = "正在执行；步骤结果会实时显示，结束时自动清理会话。";
            _factorySteps.Clear();
            FactorySessionText.Text = "会话：准备建立";
            FactoryInjectionText.Text = "注入：无";
            FactoryEffectiveText.Text = "有效输入：等待 STATUS";
            _factoryTestCts = new CancellationTokenSource();
            var engine = new FactoryTestEngine(message =>
            {
                AppendLog(message, "FACTORY");
                if (!Dispatcher.HasShutdownStarted)
                    Dispatcher.BeginInvoke(() =>
                    {
                        FactoryLogText.AppendText($"{DateTime.Now:HH:mm:ss} {message}{Environment.NewLine}");
                        FactoryLogText.ScrollToEnd();
                    });
            }, status => Dispatcher.BeginInvoke(() => ApplyFactoryStatus(status)),
            step => Dispatcher.BeginInvoke(() =>
            {
                _factorySteps.Add(step);
                FactoryStepGrid.ScrollIntoView(step);
            }),
            session => Dispatcher.BeginInvoke(() => FactorySessionText.Text = "会话：" + session),
            count => Dispatcher.BeginInvoke(() => FactoryParameterSummaryText.Text = $"正式保护参数：已读取 {count} 项\n测试过程只读，不写入"));
            FactoryTestReport report = await engine.RunAsync(bms, _connectedName, IdentityText.Text,
                _factoryTestCts.Token, onlyCaseName);
            string root = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), "BmsFactoryReports");
            Directory.CreateDirectory(root);
            string prefix = onlyCaseName is null ? "BMS_Factory" : "BMS_Factory_Single_" + SanitizeFileName(onlyCaseName);
            string path = Path.Combine(root, $"{prefix}_{DateTime.Now:yyyyMMdd_HHmmss}.json");
            await File.WriteAllTextAsync(path, report.ToJson());
            FactoryResultText.Text = report.Passed ? $"PASS（{report.Steps.Count} 项）" : $"FAIL（{report.Steps.Count} 项）";
            FactoryReportText.Text = $"报告：{path}；清理：{report.CleanupCompleted}";
        }
        catch (Exception ex)
        {
            FactoryResultText.Text = "FAIL（异常）";
            AppendLog("出厂测试异常：" + ex.Message, "FACTORY");
            MessageBox.Show(ex.Message, "出厂测试失败", MessageBoxButton.OK, MessageBoxImage.Error);
        }
        finally
        {
            _factoryTestCts?.Dispose();
            _factoryTestCts = null;
            FactoryRunButton.IsEnabled = _bms is not null;
            FactorySingleRunButton.IsEnabled = _bms is not null;
            FactoryRefreshButton.IsEnabled = _bms is not null;
            StartAutomaticRefresh();
        }
    }

    private void ScanButton_Click(object sender, RoutedEventArgs e)
    {
        try { if (IsSerialConnectionMode || IsCanMode || IsOwcMode) RefreshSerialPorts(); else StartBleDiscovery(); }
        catch (Exception ex) { ShowError("启动搜索失败", ex); }
    }

    private void StopScanButton_Click(object sender, RoutedEventArgs e)
    {
        StopBleDiscovery();
    }

    private async void ConnectButton_Click(object sender, RoutedEventArgs e)
    {
        if (IsOwcMode) { await StartIntegratedOwcAsync(); return; }
        if (_integratedOwcTask is not null) await StopIntegratedOwcAsync();
        if (_otaRunning || _manualConnectionInProgress) return;
        _manualConnectionInProgress = true;
        _connectionDesired = true;
        _manualConnectCts = new CancellationTokenSource();
        CancelConnectionButton.IsEnabled = true;
        _pollTimer.Stop();
        try
        {
            ConnectButton.IsEnabled = false;
            ConnectionText.ToolTip = null;
            ConnectionText.Text = "正在连接设备...";
            if (IsSerialConnectionMode || IsCanMode)
            {
                if (SerialPortList.SelectedItem is not SerialPortEndpoint serial)
                    throw new InvalidOperationException("请先刷新并选择串口。");

                _connectionMode = IsCanMode ? ConnectionMode.Can : ConnectionMode.Serial;
                int baudRate = IsCanMode ? 115200 : GetSerialBaudRate();
                _serialBaudRate = baudRate;
                _connectedAddress = null;
                _connectedSerialPort = serial.PortName;
                _connectedName = IsCanMode ? $"CAN · Comm Tool {serial.PortName}" : $"串口 {serial.PortName}";
                _pollFailureCount = 0;
                AppendLog($"用户发起串口连接；port={serial.PortName}; baud={baudRate}; format=8N1", "CONNECT");
                await ConnectSerialInternalAsync(serial.PortName, baudRate, _manualConnectCts.Token);
                _manualConnectCts.Token.ThrowIfCancellationRequested();
                await RefreshIdentityAsync(_manualConnectCts.Token);
                _manualConnectCts.Token.ThrowIfCancellationRequested();
                await RefreshBatteryAsync(_manualConnectCts.Token);
                StartAutomaticRefresh();
                ConnectionText.Text = IsCanMode ? $"已连接：{_connectedName} · 250 kbit/s" : $"已连接：{serial.PortName} · {baudRate} 8N1";
                return;
            }
            if (DeviceList.SelectedItem is not DiscoveredDevice selected)
                throw new InvalidOperationException("请先扫描并选择设备。");

            if (selected.Source == BleDiscoverySource.Advertisement && !selected.HasRecentAdvertisement)
                throw new InvalidOperationException("所选设备的广播已过期，请重新搜索或从历史记录手动验证。");
            if (selected.Source == BleDiscoverySource.History)
                AppendLog("正在手动验证历史设备；系统记录不代表在线。", "CONNECT");
            _bleScanRunning = false;
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

            bool applicationReady = await ConnectBmsInternalAsync(selected.Address, _manualConnectCts.Token, allowIapOnly: true);
            if (applicationReady)
            {
                _manualConnectCts.Token.ThrowIfCancellationRequested();
                await RefreshIdentityAsync(_manualConnectCts.Token);
                _manualConnectCts.Token.ThrowIfCancellationRequested();
                await RefreshBatteryAsync(_manualConnectCts.Token);
                StartAutomaticRefresh();
                ConnectionText.Text = $"已连接：{_connectedName}";
            }
            else
            {
                ConnectionText.Text = "BLE已连接（STM32 IAP模式，仅可升级）";
                AppendLog("BLE GATT 已连接，但普通 BMS Modbus 探测无响应；按 STM32 IAP 模式保留连接，可直接执行 OTA。", "CONNECT");
            }
        }
        catch (OperationCanceledException)
        {
            ConnectionText.Text = "连接已取消";
        }
        catch (Exception ex)
        {
            ReportConnectionFailure(ex);
        }
        finally
        {
            _manualConnectionInProgress = false;
            _manualConnectCts?.Dispose(); _manualConnectCts = null;
            if (_connectionDesired && !_bleWindowClosed) StartAutomaticRefresh();
            ConnectButton.IsEnabled = true;
            CancelConnectionButton.IsEnabled = false;
            RefreshBleDeviceList();
        }
    }

    private async Task<bool> ConnectBmsOnceAsync(ulong address, CancellationToken ct = default, bool allowIapOnly = false)
    {
        await DisposeBmsCoreAsync();
        long generation = _connectionGeneration;
        using var attempt = CancellationTokenSource.CreateLinkedTokenSource(ct, _connectionLifetime.Token);
        ct = attempt.Token;
        AppendLog($"ConnectBmsInternal START address={address:X12}", "CONNECT");

        var transport = new BmsBleTransport();
        Action<string> progress = text => OnConnectionProgress(generation, text);
        transport.ConnectionProgress += progress;
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

            ct.ThrowIfCancellationRequested();
            if (generation != _connectionGeneration) throw new OperationCanceledException("连接已改变");
            _bmsTransport = transport;
            _bms = client;
            _connectedAddress = address;
            transport.ConnectionProgress -= progress;
            AppendLog($"BMS READY total={total.ElapsedMilliseconds}ms; {transport.DiscoveryDescription}", "CONNECT");
            return true;
        }
        catch (Exception ex)
        {
            _sessionLog.WriteException("CONNECT", "ConnectBmsInternal", ex);
            transport.ConnectionProgress -= progress;
            if (ex is not OperationCanceledException && allowIapOnly && !ct.IsCancellationRequested && generation == _connectionGeneration && client is not null && transport.IsConnected)
            {
                client.Log -= OnProtocolLog;
                await client.DisposeAsync();
                if (ct.IsCancellationRequested)
                {
                    await transport.DisposeAsync();
                    ct.ThrowIfCancellationRequested();
                }
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

    private void OnConnectionProgress(long generation, string text)
    {
        bool can = _connectionMode == ConnectionMode.Can;
        AppendLog(text, can ? "CONNECT" : "BLE");
        if (can || text.Contains("ATTEMPT_BEGIN", StringComparison.Ordinal))
        {
            Dispatcher.BeginInvoke(() => {
                if (generation != _connectionGeneration || !_manualConnectionInProgress) return;
                ConnectionText.ToolTip = null;
                ConnectionText.Text = can ? text : "正在连接设备...";
            });
        }
    }

    private void OnProtocolLog(string text) => AppendLog(text, "MODBUS");

    private async Task DisposeBmsCoreAsync()
    {
        InvalidateConnectionState();
        await CloseFaultInjectionAsync();
        await CloseC11Async();
        InvalidateCurrentCalibrationConnection();
        ResetMosControlUi();
        _mosCts?.Cancel();
        NoteMosGap("设备断开或重连");
        var client = _bms;
        var transport = _bmsTransport;
        _bms = null;
        _bmsTransport = null;
        if (client is not null)
        {
            client.Log -= OnProtocolLog;
            await client.DisposeAsync();
        }
        if (transport is not null) await transport.DisposeAsync();
        ClearDeviceDisplays();
    }

    private void StartAutomaticRefresh()
    {
        if (_bms is not null && !_otaRunning && !_parameterBusy && !_debugMonitorPaused)
            _pollTimer.Start();
    }

    private async void RefreshIdentity_Click(object sender, RoutedEventArgs e)
    {
        try { await RefreshIdentityAsync(); }
        catch (Exception ex) { ShowError("读取设备信息失败", ex); }
    }

    private async Task<DeviceIdentity> RefreshIdentityAsync(CancellationToken ct = default)
    {
        long generation = _connectionGeneration;
        var bms = _bms ?? throw new InvalidOperationException("BMS 未连接。");
        string fallbackMac = _connectionMode != ConnectionMode.Ble
            ? string.Empty
            : BmsBleTransport.FormatBluetoothAddress(_connectedAddress ?? 0);
        string fallbackName = _connectionMode != ConnectionMode.Ble
            ? $"串口 {_connectedSerialPort}"
            : _connectedName;
        DeviceIdentity id = await bms.ReadIdentityAsync(fallbackMac, fallbackName, ct);
        var build = await bms.TryReadFirmwareBuildIdentityAsync(ct);
        await RefreshOptionalDeviceViewAsync("设备时间",()=>RefreshDeviceClockAsync(bms, ct));
        if (generation != _connectionGeneration || !ReferenceEquals(bms, _bms)) return id;
        SetMosIdentity(bms, id);
        UpdateParameterProfileUi();
        _identityRefreshRequired = false;
        IdentityText.Text = $"自动识别：{bms.DeviceModel}\n识别依据：{bms.DeviceDetectionEvidence}\n蓝牙名称：{id.BluetoothName}\nMAC：{id.Mac}\n序列号：{id.Serial}\n硬件版本：{id.Hardware}\n软件版本：{id.Software}\n固件 Build ID：{build?.Display ?? "不可用"}";
        FactoryIdentityText.Text = $"蓝牙：{id.BluetoothName}\nMAC：{id.Mac}\n序列号：{id.Serial}";
        FactoryConnectionText.Text = $"连接状态：已连接\n硬件：{id.Hardware}\n软件：{id.Software}";
        BtNameResultText.Text = id.BluetoothName;
        if (id.BluetoothName.StartsWith("BT_", StringComparison.OrdinalIgnoreCase) ||
            id.BluetoothName.StartsWith("BT-", StringComparison.OrdinalIgnoreCase))
            NameSuffixBox.Text = id.BluetoothName[3..];
        return id;
    }

    private async Task RefreshBatteryAsync(CancellationToken ct = default)
    {
        long generation = _connectionGeneration;
        var bms = _bms ?? throw new InvalidOperationException("BMS 未连接。");
        BatterySnapshot snapshot;
        try { snapshot = await bms.ReadBatteryAsync(ct); }
        catch (NotSupportedException ex) {
            if (ReferenceEquals(bms, _bms)) {
                ConnectionText.Text = "已连接：" + bms.DeviceModel;
                LastUpdateText.Text = ex.Message;
            }
            return;
        }
        if (!ReferenceEquals(bms, _bms)) return;
        EnsureCurrentConnection(bms, generation);
        ApplyBatterySnapshot(snapshot);
        if (bms.UsesC060Protocol) return;
        await RefreshOptionalDeviceViewAsync("MOS许可",()=>RefreshMosControlStateAsync(bms, default));
        await RefreshOptionalDeviceViewAsync("MOS诊断",()=>RefreshMosMonitorAsync(bms));
        await RefreshOptionalDeviceViewAsync("休眠状态",()=>RefreshSleepStatusAsync(bms));
        await RefreshOptionalDeviceViewAsync("设备时间",()=>RefreshDeviceClockAsync(bms));
    }

    private void ApplyBatterySnapshot(BatterySnapshot s)
    {
        _customerC077Snapshot = s.Measurements is null ? null : s;
        PublishUnifiedBattery(s);
        UpdateProtectionProductDisplay(_bms?.IsD015 == true);
        RecordMosSnapshot(s);
        PackVoltageText.Text = s.PackVoltageDisplay;
        CurrentText.Text = s.CurrentValid ? $"{s.CurrentDisplay}（{s.WorkState}）" : "—";
        SocText.Text = $"SOC {s.SocPercent}%\nSOH {s.SohDisplay}";
        WorkStateText.Text = s.WorkState;

        MosStateText.Text =
            $"充电 MOS：{OnOff(s.ChargeMosOn)}\n" +
            $"放电 MOS：{OnOff(s.DischargeMosOn)}\n" +
            $"预充 MOS：{OnOff(s.PrechargeMosOn)}";

        if (s.FaultInjection is { DriverValid: false } || s.FaultInjectionStateUnknown)
            MosStateText.Text = "充电 MOS：未知\n放电 MOS：未知\nAFE 状态回读失效或过期";

        SystemStateText.Text =
            $"AFE：{(s.Afe1On ? "工作" : "未工作")}\n" +
            $"均衡：{DescribeBalanceState(_bms?.IsC073 == true, s)}\n" +
            $"加热：{OnOff(s.HeatingOn)}\n" +
            $"制冷：{OnOff(s.CoolingOn)}\n" +
            $"系统：{(s.PreparingSleep ? "准备休眠" : "运行中")}";

        if (!s.HasSystemStatus) {
            MosStateText.Text = "MOS状态：设备未提供，当前未知";
            SystemStateText.Text = "系统状态：设备未提供，当前未知";
        }
        ProtectionLevelsText.Text =
            $"一级：{s.ProtectionLevel1Text}\n" +
            $"二级：{s.ProtectionLevel2Text}\n" +
            $"三级：{s.ProtectionLevel3Text}";

        string maxTemp = s.BatteryTemperatureAvailable ? $"{s.MaxTempC:F1} °C" : "—";
        string minTemp = s.BatteryTemperatureAvailable ? $"{s.MinTempC:F1} °C" : "—";
        string mosTemp = s.MosTemperatureAvailable ? $"{s.MosTempC:F1} °C" : "—";
        TempsText.Text = $"最高温度：{maxTemp}\n最低温度：{minTemp}\nMOS 温度：{mosTemp}";
        if (s.Measurements is { } measured)
        {
            SystemStateText.Text = $"{SystemStateText.Text}\n{measured.SleepDetails} · {measured.VoltageDetails}\n{measured.TemperatureDetails}";
            if (!string.IsNullOrEmpty(measured.FaultDetails))
                SystemStateText.Text += "\n" + measured.FaultDetails;
        }
        TempsText.ToolTip = FactoryCellTempText.ToolTip = null;
        CellExtremeText.Text = $"最高：{s.MaxCellMv} mV（第 {s.MaxCellPosition} 串）\n最低：{s.MinCellMv} mV（第 {s.MinCellPosition} 串）\n压差：{s.CellDeltaMv} mV";
        CapacityText.Text = $"当前容量：{s.CapacityNowAh:F2} Ah\n满充容量：{s.CapacityFullAh:F2} Ah\n工厂容量：{s.CapacityFactoryAh:F2} Ah\n循环次数：{s.CycleCount}";
        CellsText.Text = string.Join("    ", s.CellMillivolts
            .Select((mv, i) => (mv, Index: i + 1))
            .Where(cell => cell.mv != BmsRegisters.MissingCellVoltageMv)
            .Select(cell => s.IsCellVoltageValid(cell.Index - 1)
                ? FormattableString.Invariant($"第{cell.Index}串 {cell.mv / 1000.0:F3} V{(s.IsCellBalancing(cell.Index - 1) ? " [均衡]" : "")}")
                : $"第{cell.Index}串 —"));

        _customerLastDataUtc = DateTime.UtcNow;
        LastUpdateText.Text = $"最后更新：{DateTime.Now:HH:mm:ss}";
        CurrentSocParamText.Text = $"当前 SOC：{s.SocPercent}%";
        CurrentCycleParamText.Text = $"当前循环次数：{s.CycleCount}";

        DebugRawStatusText.Text =
            $"SystemStatus=0x{s.SystemStatus:X8}    Protocol=0x{s.ProtocolVersion:X4}    DataWindow={(s.UsesRealtimeWindow ? "Realtime" : "Legacy")}\n" +
            $"Protect L1=0x{s.ProtectionLevel1Raw:X4}    L2=0x{s.ProtectionLevel2Raw:X4}    L3=0x{s.ProtectionLevel3Raw:X4}";

        FactoryPackText.Text = $"总压：{s.PackVoltageDisplay}";
        FactoryCurrentText.Text = $"电流：{s.CurrentDisplay}（{s.WorkState}）";
        FactorySocText.Text = $"SOC：{s.SocPercent}%";
        FactorySohText.Text = $"SOH：{s.SohDisplay}";
        FactoryCapacityText.Text = $"容量：{s.CapacityNowAh:F2} / {s.CapacityFullAh:F2} Ah\n工厂标称：{s.CapacityFactoryAh:F2} Ah";
        FactoryCycleText.Text = $"循环：{s.CycleCount}";
        FactoryCellTempText.Text = $"单体最高/最低：{s.MaxTempC:F1} / {s.MinTempC:F1} ℃";
        FactoryMosTempText.Text = $"MOS温度：{s.MosTempC:F1} ℃";
        FactoryCellExtremeText.Text = $"最高：{s.MaxCellMv} mV（第{s.MaxCellPosition}串）\n最低：{s.MinCellMv} mV（第{s.MinCellPosition}串）\n压差：{s.CellDeltaMv} mV";
        FactoryUpdateText.Text = $"更新时间：{DateTime.Now:HH:mm:ss}";
        FactoryMosText.Text = $"充电MOS：{OnOff(s.ChargeMosOn)}\n放电MOS：{OnOff(s.DischargeMosOn)}\n预充MOS：{OnOff(s.PrechargeMosOn)}";
        FactorySystemText.Text = $"AFE：{(s.Afe1On ? "工作" : "未工作")}  均衡：{DescribeBalanceState(_bms?.IsC073 == true, s)}\n加热：{OnOff(s.HeatingOn)}  制冷：{OnOff(s.CoolingOn)}\n系统：{(s.PreparingSleep ? "准备休眠" : "运行中")}";
        FactoryRawText.Text = $"System=0x{s.SystemStatus:X8}\nProtocol=0x{s.ProtocolVersion:X4} · {(s.UsesRealtimeWindow ? "Realtime" : "Legacy")}";
        FactoryProtection1Text.Text = $"一级：{s.ProtectionLevel1Text}\n0x{s.ProtectionLevel1Raw:X4}";
        FactoryProtection2Text.Text = $"二级：{s.ProtectionLevel2Text}\n0x{s.ProtectionLevel2Raw:X4}";
        FactoryProtection3Text.Text = $"三级：{s.ProtectionLevel3Text}\n0x{s.ProtectionLevel3Raw:X4}";
        FactoryCellPanel.Children.Clear();
        for (int i = 0; i < s.CellMillivolts.Count; i++)
        {
            int mv = s.CellMillivolts[i];
            if (mv == BmsRegisters.MissingCellVoltageMv)
                continue;
            var reading = new StackPanel { Orientation = Orientation.Horizontal };
            reading.Children.Add(new TextBlock
            {
                Text = $"第 {i + 1,2} 串   {mv,4} mV",
                FontFamily = new System.Windows.Media.FontFamily("Consolas"),
                VerticalAlignment = VerticalAlignment.Center
            });
            if (s.IsCellBalancing(i)) reading.Children.Add(CreateBalanceIcon());
            FactoryCellPanel.Children.Add(new Border
            {
                BorderBrush = new System.Windows.Media.SolidColorBrush(System.Windows.Media.Colors.LightGray),
                BorderThickness = new Thickness(1),
                CornerRadius = new CornerRadius(2),
                Margin = new Thickness(2),
                Padding = new Thickness(5, 3, 5, 3),
                ToolTip = "均衡标记表示 AFE 最后回读的已启用通道；断连或 AFE 异常时数据可能过期。",
                Child = reading
            });
        }
    }

    private async Task ConnectSerialOnceAsync(string portName, int baudRate, CancellationToken ct = default)
    {
        await DisposeBmsCoreAsync();
        long generation = _connectionGeneration;
        using var attempt = CancellationTokenSource.CreateLinkedTokenSource(ct, _connectionLifetime.Token);
        ct = attempt.Token;
        IBmsTransport transport = _connectionMode == ConnectionMode.Can ? new CommToolCanTransport(portName) : new BmsSerialTransport();
        Action<string> progress = text => OnConnectionProgress(generation, text);
        transport.ConnectionProgress += progress;
        BmsClient? client = null;
        try
        {
            if (transport is BmsSerialTransport serial) await serial.ConnectAsync(portName, baudRate, ct);
            else await transport.ReconnectAsync(ct);
            client = new BmsClient(transport, _communicationStatistics);
            client.Log += OnProtocolLog;
            AppendLog($"[CONNECT] STEP_BEGIN stage=ModbusProbe transport={_connectionMode}", "CONNECT");
            await client.ProbeAsync(ct);
            ct.ThrowIfCancellationRequested();
            if (generation != _connectionGeneration) throw new OperationCanceledException("连接已改变");
            _bmsTransport = transport;
            _bms = client;
            _connectedAddress = null;
            _connectedSerialPort = portName;
            AppendLog($"BMS READY transport={_connectionMode}; {transport.DiscoveryDescription}", "CONNECT");
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
            transport.ConnectionProgress -= progress;
        }
    }

    private void ApplyFactoryStatus(FactoryStatus s)
    {
        FactoryInjectionText.Text = $"Mask=0x{s.InjectionMask:X4}\nCell/Pack/I={((s.InjectionMask & 0x0001) != 0 ? "Y" : "-")}/{((s.InjectionMask & 0x0002) != 0 ? "Y" : "-")}/{((s.InjectionMask & 0x0004) != 0 ? "Y" : "-")}\nTemp/MOS/SOC={((s.InjectionMask & 0x0008) != 0 ? "Y" : "-")}/{((s.InjectionMask & 0x0010) != 0 ? "Y" : "-")}/{((s.InjectionMask & 0x0020) != 0 ? "Y" : "-")}";
        FactoryEffectiveText.Text = $"Cell {s.CellMinMv}~{s.CellMaxMv} mV\nΔ {s.CellDeltaMv} mV · Pack {s.PackCv / 10.0:F1} V\nChg {s.ChargeCurrentTenthA / 10.0:F1}A / Dsg {s.DischargeCurrentTenthA / 10.0:F1}A\nTemp {RawTemperature(s.TemperatureMaxRaw):F1}℃ · MOS {RawTemperature(s.MosTemperatureRaw):F1}℃ · SOC {s.SocPercent}%";
        FactoryProtection1Text.Text = $"一级：{DecodeFactoryBits(s.ProtectionLevel1)}\n0x{s.ProtectionLevel1:X4}";
        FactoryProtection2Text.Text = $"二级：{DecodeFactoryBits(s.ProtectionLevel2)}\n0x{s.ProtectionLevel2:X4}";
        FactoryProtection3Text.Text = $"三级：{DecodeFactoryBits(s.ProtectionLevel3)}\n0x{s.ProtectionLevel3:X4}";
    }

    private static double RawTemperature(ushort raw) => raw / 10.0 - 40.0;

    private static string DecodeFactoryBits(ushort raw)
    {
        if (raw == 0) return "无";
        string[] names = { "单体过压", "单体欠压", "总压过压", "总压欠压", "充电过流", "放电过流", "充电高温", "放电高温", "充电低温", "放电低温", "压差", "温差", "SOC低", "MOS高温" };
        var active = names.Where((_, i) => (raw & (1u << i)) != 0).ToArray();
        return active.Length == 0 ? $"未知位 0x{raw:X4}" : string.Join("、", active);
    }

    private static string SanitizeFileName(string value)
    {
        foreach (char c in Path.GetInvalidFileNameChars()) value = value.Replace(c, '_');
        return value;
    }

    private static string OnOff(bool value) => value ? "开启" : "关闭";

    private async Task PollTickAsync()
    {
        if (_polling || _otaRunning || _autoReconnectRunning || _shBusy || _parameterBusy || _manualConnectionInProgress || _connectionOperations.IsBusy || _bleWindowClosed || _debugMonitorPaused) return;

        if (_bms is null)
        {
            bool endpointAvailable = _connectionMode == ConnectionMode.Ble
                ? _connectedAddress is not null
                : !string.IsNullOrWhiteSpace(_connectedSerialPort);
            if (endpointAvailable && DateTime.UtcNow >= _nextReconnectUtc)
                await AutoReconnectAsync();
            return;
        }

        long generation = _connectionGeneration;
        _polling = true;
        try
        {
            if (_identityRefreshRequired) await RefreshIdentityAsync();
            await RefreshBatteryAsync();
            _pollFailureCount = 0;
        }
        catch (Exception) when (generation != _connectionGeneration) { }
        catch (BmsModbusException ex)
        {
            _pollFailureCount = 0;
            AppendLog("设备已应答但当前请求不可用；保留连接：" + ex.Message, "POLL");
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
        if (_autoReconnectRunning || _otaRunning || _manualConnectionInProgress || _connectionOperations.IsBusy || !_connectionDesired || _bleWindowClosed) return;
        if (_connectionMode == ConnectionMode.Ble && _connectedAddress is null) return;
        if (_connectionMode != ConnectionMode.Ble && string.IsNullOrWhiteSpace(_connectedSerialPort)) return;
        _communicationReconnectAttempts++;
        _autoReconnectRunning = true;
        _pollTimer.Stop();
        try
        {
            ConnectionText.ToolTip = null;
            ConnectionText.Text = "通信异常，自动重连中...";
            AppendLog("自动重连开始：连续 3 次数据刷新失败。", "RECONNECT");
            if (_connectionMode != ConnectionMode.Ble)
                await ConnectSerialInternalAsync(_connectedSerialPort!, _serialBaudRate);
            else
                await ConnectBmsInternalAsync(_connectedAddress!.Value);
            if (_manualConnectionInProgress || !_connectionDesired || _bleWindowClosed) return;
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
            if (_manualConnectionInProgress || !_connectionDesired || _bleWindowClosed) return;
            _nextReconnectUtc = DateTime.UtcNow.AddSeconds(3);
            ConnectionText.Text = "自动重连失败，将继续重试";
        }
        finally
        {
            _autoReconnectRunning = false;
            if (!_otaRunning && !_manualConnectionInProgress && _connectionDesired && !_bleWindowClosed && !_debugMonitorPaused) _pollTimer.Start();
        }
    }

    private async void ReadProtection_Click(object sender, RoutedEventArgs e)
    {
        if (_parameterBusy || _diagBusy || _otaRunning || _eventLogReadInProgress || _shBusy || _autoReconnectRunning) return;
        bool ownsTransaction = false;
        long generation = _connectionGeneration;
        try
        {
            _parameterBusy = ownsTransaction = true;
            _pollTimer.Stop();
            await WaitForCommunicationIdleAsync();
            await ReadProtectionAsync();
        }
        catch (Exception) when (generation != _connectionGeneration) { }
        catch (Exception ex) { ShowError("读取保护参数失败", ex); }
        finally { if (ownsTransaction) _parameterBusy = false; if (generation == _connectionGeneration) StartAutomaticRefresh(); }
    }

    private async Task ReadProtectionAsync()
    {
        long generation = _connectionGeneration;
        var bms = _bms ?? throw new InvalidOperationException("BMS 未连接。");
        _protectionSnapshotClient = null;
        ProtectionStatusText.Text = "正在读取...";
        ushort[] values = await bms.ReadProtectionAllAsync();
        EnsureCurrentConnection(bms, generation);
        if (values.Length != _protectionRows.Count)
            throw new IOException($"保护参数数量错误：expected={_protectionRows.Count}, actual={values.Length}");
        for (int i = 0; i < values.Length; i++) _protectionRows[i].LoadFromDevice(values[i]);
        _protectionSnapshotClient = bms;
        _protectionSnapshotGeneration = generation;
        ProtectionStatusText.Text = $"读取完成 · {DateTime.Now:HH:mm:ss}" +
            (bms.UsesC060Protocol ? $" · {bms.ParameterProduct}：Second/Third实际扫描；First用于恢复；Filter单位10ms" : "");
        FactoryParameterSummaryText.Text = $"正式保护参数：已读取 {values.Length} 项\n范围：0x2100..0x{0x2100 + values.Length - 1:X4}（只读）";
        AppendLog($"保护参数读取完成 count={values.Length}; rawRange=0x2100..0x2140", "PARAM");
    }

    private async void WriteSelectedProtection_Click(object sender, RoutedEventArgs e)
    {
        if (_parameterBusy || _diagBusy || _otaRunning || _eventLogReadInProgress || _shBusy || _autoReconnectRunning) return;
        bool ownsTransaction = false;
        long generation = _connectionGeneration;
        try
        {
            var bms = _bms ?? throw new InvalidOperationException("BMS 未连接。");
            EnsureProtectionSnapshot(bms, generation);
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

            _parameterBusy = ownsTransaction = true;
            _pollTimer.Stop();
            await WaitForCommunicationIdleAsync();
            EnsureProtectionSnapshot(bms, generation);
            await bms.WriteProtectionChangesAsync(new Dictionary<ushort,ushort>{{row.Address,value}},
                (address, values) => {
                    if (!ReferenceEquals(bms, _bms)) throw new InvalidOperationException("连接已改变，请重新读取。");
                    for(int i=0;i<5;i++)_protectionRows.Single(r=>r.Address==address+i).LoadFromDevice(values[i]);
                });
            ushort readback = row.DeviceValue;
            ProtectionStatusText.Text = $"{row.CustomerName} 写入并校验成功";
            AppendLog($"PROTECTION_WRITE_OK name='{row.CustomerName}'; address=0x{row.Address:X4}; raw={readback}", "PARAM");
            ShowParameterSaved(ProtectionStatusText.Text);
        }
        catch (Exception) when (generation != _connectionGeneration) { }
        catch (Exception ex) { ShowError("写保护参数失败", ex); }
        finally { if (ownsTransaction) _parameterBusy = false; if (generation == _connectionGeneration) StartAutomaticRefresh(); }
    }

    private async void WriteChangedProtection_Click(object sender, RoutedEventArgs e)
    {
        if (_parameterBusy || _diagBusy || _otaRunning || _eventLogReadInProgress || _shBusy || _autoReconnectRunning) return;
        bool ownsTransaction = false;
        long generation = _connectionGeneration;
        try
        {
            var bms = _bms ?? throw new InvalidOperationException("BMS 未连接。");
            EnsureProtectionSnapshot(bms, generation);
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

            _parameterBusy = ownsTransaction = true;
            _pollTimer.Stop();
            await WaitForCommunicationIdleAsync();
            EnsureProtectionSnapshot(bms, generation);
            await bms.WriteProtectionChangesAsync(
                changed.ToDictionary(x => x.Row.Address, x => x.Parsed),
                (address, values) => {
                    if (!ReferenceEquals(bms, _bms)) throw new InvalidOperationException("连接已改变，请重新读取。");
                    for (int j = 0; j < values.Length; j++)
                        _protectionRows.Single(row => row.Address == address + j).LoadFromDevice(values[j]);
                    ProtectionStatusText.Text = $"{ProtectionBatch.Name(address)} 整组写入并校验成功";
                    AppendLog($"PROTECTION_GROUP_WRITE_OK address=0x{address:X4}; words=5", "PARAM");
                });
            EnsureCurrentConnection(bms, generation);
            ProtectionStatusText.Text = $"{changed.Count} 项全部写入并校验成功";
            ShowParameterSaved(ProtectionStatusText.Text);
        }
        catch (Exception) when (generation != _connectionGeneration) { }
        catch (Exception ex) { ShowError("批量写保护参数失败", ex, "不同保护组依次提交，已完成组可能已保存；请重新读取全部参数确认，不要直接重发。"); }
        finally { if (ownsTransaction) _parameterBusy = false; if (generation == _connectionGeneration) StartAutomaticRefresh(); }
    }

    private void CommitProtectionGridEdits()
    {
        ProtectionGrid.CommitEdit(DataGridEditingUnit.Cell, true);
        ProtectionGrid.CommitEdit(DataGridEditingUnit.Row, true);
    }

    private async void WriteSocParameter_Click(object sender, RoutedEventArgs e)
    {
        long generation = _connectionGeneration;
        try
        {
            var bms = _bms ?? throw new InvalidOperationException("BMS 未连接。");
            ushort soc = ParseU16(SocSetBox.Text);
            if (soc > 100) throw new ArgumentOutOfRangeException(nameof(soc), "SOC 必须在 0~100%。");
            if (MessageBox.Show($"确认把当前 SOC 校准为 {soc}%？", "SOC 校准", MessageBoxButton.YesNo, MessageBoxImage.Warning) != MessageBoxResult.Yes) return;
            _pollTimer.Stop();
            EnsureCurrentConnection(bms, generation);
            BatterySnapshot snapshot = await bms.SetSocAndVerifyAsync(soc);
            EnsureCurrentConnection(bms, generation);
            ApplyBatterySnapshot(snapshot);
            BmsParamResultText.Text = $"SOC 已写入并校验：{soc}%";
            AppendLog($"SOC_SET_VERIFY_OK value={soc}", "PARAM");
            ShowParameterSaved(BmsParamResultText.Text);
        }
        catch (Exception) when (generation != _connectionGeneration) { }
        catch (Exception ex) { ShowError("SOC 校准失败", ex); }
        finally { if (generation == _connectionGeneration) StartAutomaticRefresh(); }
    }

    private async void WriteCycleParameter_Click(object sender, RoutedEventArgs e)
    {
        long generation = _connectionGeneration;
        try
        {
            var bms = _bms ?? throw new InvalidOperationException("BMS 未连接。");
            ushort cycle = ParseU16(CycleSetBox.Text);
            if (MessageBox.Show($"确认把循环次数修改为 {cycle}？", "修改循环次数", MessageBoxButton.YesNo, MessageBoxImage.Warning) != MessageBoxResult.Yes) return;
            _pollTimer.Stop();
            EnsureCurrentConnection(bms, generation);
            BatterySnapshot snapshot = await bms.SetCycleCountAndVerifyAsync(cycle);
            EnsureCurrentConnection(bms, generation);
            ApplyBatterySnapshot(snapshot);
            BmsParamResultText.Text = $"循环次数已写入并校验：{cycle}";
            AppendLog($"CYCLE_SET_VERIFY_OK value={cycle}", "PARAM");
            ShowParameterSaved(BmsParamResultText.Text);
        }
        catch (Exception) when (generation != _connectionGeneration) { }
        catch (Exception ex) { ShowError("修改循环次数失败", ex); }
        finally { if (generation == _connectionGeneration) StartAutomaticRefresh(); }
    }

    private async void WriteName_Click(object sender, RoutedEventArgs e)
    {
        long generation = _connectionGeneration;
        try
        {
            var bms = _bms ?? throw new InvalidOperationException("BMS 未连接。");
            _pollTimer.Stop();
            EnsureCurrentConnection(bms, generation);
            string name = await bms.WriteBluetoothNameSuffixAsync(NameSuffixBox.Text);
            EnsureCurrentConnection(bms, generation);
            BtNameResultText.Text = $"已写入并确认：{name}";
            _connectedName = name;
            ConnectionText.Text = $"已连接：{name}";
            AppendLog($"BLUETOOTH_NAME_VERIFY_OK name='{name}'", "PARAM");
            MessageBox.Show($"蓝牙名称已修改为：{name}", "修改成功", MessageBoxButton.OK, MessageBoxImage.Information);
        }
        catch (Exception) when (generation != _connectionGeneration) { }
        catch (Exception ex) { ShowError("修改蓝牙名失败", ex); }
        finally { if (generation == _connectionGeneration) StartAutomaticRefresh(); }
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
            RegisterOutput.Text = string.Join(Environment.NewLine, words.Select((v, i) => $"0x{address + i:X4} = {v,6}    0x{v:X4}"));
            AppendLog($"DEBUG_READ address=0x{address:X4}; qty={qty}", "DEBUG");
        }
        catch (Exception ex) { ShowError("寄存器读取失败", ex); }
        finally { StartAutomaticRefresh(); }
    }

    private async void DebugWriteRegister_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            ushort address = ParseU16(DebugWriteAddressBox.Text);
            ushort value = ParseU16(DebugWriteValueBox.Text);
            if (MessageBox.Show($"专业调试：确认写寄存器？\n\nAddress = 0x{address:X4}\nValue = {value} (0x{value:X4})", "确认写寄存器", MessageBoxButton.YesNo, MessageBoxImage.Warning) != MessageBoxResult.Yes) return;
            _pollTimer.Stop();
            var bms = _bms ?? throw new InvalidOperationException("BMS 未连接。");
            await bms.WriteSingleRegisterAsync(address, value);
            string result;
            try
            {
                ushort readback = (await bms.ReadRegistersAsync(address, 1))[0];
                result = $"ACK 成功；readback={readback} (0x{readback:X4})";
            }
            catch (Exception readEx)
            {
                result = "ACK 成功；回读失败/该地址可能不可读：" + readEx.Message;
            }
            RegisterOutput.Text = result;
            AppendLog($"DEBUG_WRITE address=0x{address:X4}; value={value}; {result}", "DEBUG");
        }
        catch (Exception ex) { ShowError("寄存器写入失败", ex); }
        finally { StartAutomaticRefresh(); }
    }

    private void OpenLogFolder_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            string path = _sessionLog.FilePath;
            Process.Start(new ProcessStartInfo("explorer.exe", $"/select,\"{path}\"") { UseShellExecute = true });
        }
        catch (Exception ex) { ShowError("打开日志目录失败", ex); }
    }

    private void ClearLogView_Click(object sender, RoutedEventArgs e)
    {
        LogBox.Clear();
        AppendLog("仅清空界面日志；磁盘诊断日志继续保留。", "DEBUG");
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
        QueueUiLog(text, category);
    }

    private void ShowError(string title, Exception ex, string? extra = null)
    {
        _sessionLog.WriteException("ERROR", title, ex);
        AppendLog($"{title}: type={ex.GetType().FullName}; hresult=0x{ex.HResult:X8}; message={ex.Message}", "ERROR");
        string message = extra is null ? ex.Message : extra + "\n\n" + ex.Message;
        MessageBox.Show(message, title, MessageBoxButton.OK, MessageBoxImage.Error);
    }
}
