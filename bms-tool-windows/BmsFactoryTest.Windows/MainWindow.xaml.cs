using System.Collections.ObjectModel;
using System.Diagnostics;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
using Microsoft.Win32;
using Windows.Devices.Bluetooth.Advertisement;
using BmsFactoryTest.Windows;

namespace BmsTool.Windows;

public partial class MainWindow : Window
{
    private readonly ObservableCollection<DiscoveredDevice> _devices = new();
    private readonly ObservableCollection<ProtectionParameterRow> _protectionRows = new(ProtectionParameterCatalog.Create());
    private readonly ObservableCollection<FactoryTestStepResult> _factorySteps = new();
    private readonly Dictionary<ulong, DiscoveredDevice> _deviceMap = new();
    private readonly DispatcherTimer _pollTimer;
    private readonly SessionLogger _sessionLog = new();

    private BluetoothLEAdvertisementWatcher? _watcher;
    private BmsBleTransport? _bmsTransport;
    private BmsClient? _bms;
    private ulong? _connectedAddress;
    private string _connectedName = string.Empty;
    private FirmwareImage? _firmware;
    private CancellationTokenSource? _otaCts;
    private bool _polling;
    private bool _otaRunning;
    private bool _autoReconnectRunning;
    private int _pollFailureCount;
    private DateTime _nextReconnectUtc = DateTime.MinValue;
    private CancellationTokenSource? _factoryTestCts;

    public MainWindow()
    {
        InitializeComponent();
        DeviceList.ItemsSource = _devices;
        ProtectionGrid.ItemsSource = _protectionRows;
        FactoryStepGrid.ItemsSource = _factorySteps;
        FactoryCaseBox.ItemsSource = FactoryTestEngine.AvailableCases.Select(c => c.Name).ToArray();
        FactoryCaseBox.SelectedIndex = 0;
        LogPathText.Text = "日志文件：" + _sessionLog.FilePath;

        _pollTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };
        _pollTimer.Tick += async (_, _) => await PollTickAsync();

        Closed += async (_, _) =>
        {
            _pollTimer.Stop();
            _watcher?.Stop();
            _otaCts?.Cancel();
            _factoryTestCts?.Cancel();
            await DisposeBmsAsync();
            _sessionLog.Write("SESSION", "Window closed");
            _sessionLog.Dispose();
        };

        AppendLog("应用启动；连接后电池数据自动按 1 秒周期刷新。", "APP");
        AppendLog("完整诊断日志已启用：" + _sessionLog.FilePath, "APP");
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
        try
        {
            if (_watcher is not null)
            {
                AppendLog($"停止旧扫描器，status={_watcher.Status}", "SCAN");
                _watcher.Stop();
            }

            _devices.Clear();
            _deviceMap.Clear();
            _watcher = BmsBleTransport.CreateWatcher(
                d => Dispatcher.BeginInvoke(() => UpsertDevice(d)),
                msg => AppendLog(msg, "SCAN"));
            AppendLog($"开始 BLE 主动扫描；filter=BT_*；status(before)={_watcher.Status}", "SCAN");
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
            if (DeviceList.SelectedItem is not DiscoveredDevice selected)
                throw new InvalidOperationException("请先扫描并选择设备。");

            AppendLog($"用户发起连接；name='{selected.Name}'; address={selected.Address:X12}; rssi={selected.Rssi}dBm", "CONNECT");
            if (_watcher is not null)
            {
                AppendLog($"连接前停止扫描；watcherStatus={_watcher.Status}", "SCAN");
                _watcher.Stop();
            }

            ConnectButton.IsEnabled = false;
            ConnectionText.Text = "正在连接设备...";
            _connectedAddress = selected.Address;
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
            ShowError("连接失败", ex, "连接阶段、耗时和 Windows BLE 错误已写入“专业调试”页的日志文件。后续请直接把该日志文件发给我。 ");
        }
        finally
        {
            ConnectButton.IsEnabled = true;
        }
    }

    private async Task ConnectBmsInternalAsync(ulong address, CancellationToken ct = default)
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
            client = new BmsClient(transport);
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
        }
        catch (Exception ex)
        {
            _sessionLog.WriteException("CONNECT", "ConnectBmsInternal", ex);
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

    private async void RefreshIdentity_Click(object sender, RoutedEventArgs e)
    {
        try { await RefreshIdentityAsync(); }
        catch (Exception ex) { ShowError("读取设备信息失败", ex); }
    }

    private async Task<DeviceIdentity> RefreshIdentityAsync()
    {
        var bms = _bms ?? throw new InvalidOperationException("BMS 未连接。");
        DeviceIdentity id = await bms.ReadIdentityAsync();
        IdentityText.Text = $"蓝牙名称：{id.BluetoothName}\nMAC：{id.Mac}\n序列号：{id.Serial}\n硬件版本：{id.Hardware}\n软件版本：{id.Software}";
        FactoryIdentityText.Text = $"蓝牙：{id.BluetoothName}\nMAC：{id.Mac}\n序列号：{id.Serial}";
        FactoryConnectionText.Text = $"连接状态：已连接\n硬件：{id.Hardware}\n软件：{id.Software}";
        BtNameResultText.Text = id.BluetoothName;
        if (id.BluetoothName.StartsWith("BT_", StringComparison.OrdinalIgnoreCase))
            NameSuffixBox.Text = id.BluetoothName[3..];
        return id;
    }

    private async Task RefreshBatteryAsync()
    {
        var bms = _bms ?? throw new InvalidOperationException("BMS 未连接。");
        BatterySnapshot snapshot = await bms.ReadBatteryAsync();
        ApplyBatterySnapshot(snapshot);
    }

    private void ApplyBatterySnapshot(BatterySnapshot s)
    {
        PackVoltageText.Text = $"{s.PackVoltageV:F2} V";
        CurrentText.Text = $"{s.CurrentA:+0.0;-0.0;0.0} A";
        SocText.Text = $"SOC {s.SocPercent}%\nSOH {s.SohPercent}%";
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
        CellsText.Text = string.Join("    ", s.CellMillivolts.Select((mv, i) => $"第{i + 1}串 {mv / 1000.0:F3} V"));

        LastUpdateText.Text = $"最后更新：{DateTime.Now:HH:mm:ss}";
        CurrentSocParamText.Text = $"当前 SOC：{s.SocPercent}%";
        CurrentCycleParamText.Text = $"当前循环次数：{s.CycleCount}";

        DebugRawStatusText.Text =
            $"SystemStatus=0x{s.SystemStatus:X8}    Protocol=0x{s.ProtocolVersion:X4}    DataWindow={(s.UsesRealtimeWindow ? "Realtime" : "Legacy")}\n" +
            $"Protect L1=0x{s.ProtectionLevel1Raw:X4}    L2=0x{s.ProtectionLevel2Raw:X4}    L3=0x{s.ProtectionLevel3Raw:X4}";

        FactoryPackText.Text = $"总压：{s.PackVoltageV:F2} V";
        FactoryCurrentText.Text = $"电流：{s.CurrentA:+0.0;-0.0;0.0} A（{s.WorkState}）";
        FactorySocText.Text = $"SOC：{s.SocPercent}%";
        FactorySohText.Text = $"SOH：{s.SohPercent}%";
        FactoryCapacityText.Text = $"容量：{s.CapacityNowAh:F2} / {s.CapacityFullAh:F2} Ah\n工厂标称：{s.CapacityFactoryAh:F2} Ah";
        FactoryCycleText.Text = $"循环：{s.CycleCount}";
        FactoryCellTempText.Text = $"单体最高/最低：{s.MaxTempC:F1} / {s.MinTempC:F1} ℃";
        FactoryMosTempText.Text = $"MOS温度：{s.MosTempC:F1} ℃";
        FactoryCellExtremeText.Text = $"最高：{s.MaxCellMv} mV（第{s.MaxCellPosition}串）\n最低：{s.MinCellMv} mV（第{s.MinCellPosition}串）\n压差：{s.CellDeltaMv} mV";
        FactoryUpdateText.Text = $"更新时间：{DateTime.Now:HH:mm:ss}";
        FactoryMosText.Text = $"充电MOS：{OnOff(s.ChargeMosOn)}\n放电MOS：{OnOff(s.DischargeMosOn)}\n预充MOS：{OnOff(s.PrechargeMosOn)}";
        FactorySystemText.Text = $"AFE：{(s.Afe1On ? "工作" : "未工作")}  均衡：{(s.BalancingOn ? "进行中" : "未进行")}\n加热：{OnOff(s.HeatingOn)}  制冷：{OnOff(s.CoolingOn)}\n系统：{(s.PreparingSleep ? "准备休眠" : "运行中")}";
        FactoryRawText.Text = $"System=0x{s.SystemStatus:X8}\nProtocol=0x{s.ProtocolVersion:X4} · {(s.UsesRealtimeWindow ? "Realtime" : "Legacy")}";
        FactoryProtection1Text.Text = $"一级：{s.ProtectionLevel1Text}\n0x{s.ProtectionLevel1Raw:X4}";
        FactoryProtection2Text.Text = $"二级：{s.ProtectionLevel2Text}\n0x{s.ProtectionLevel2Raw:X4}";
        FactoryProtection3Text.Text = $"三级：{s.ProtectionLevel3Text}\n0x{s.ProtectionLevel3Raw:X4}";
        FactoryCellPanel.Children.Clear();
        for (int i = 0; i < s.CellMillivolts.Count; i++)
        {
            int mv = s.CellMillivolts[i];
            FactoryCellPanel.Children.Add(new Border
            {
                BorderBrush = new System.Windows.Media.SolidColorBrush(System.Windows.Media.Colors.LightGray),
                BorderThickness = new Thickness(1),
                CornerRadius = new CornerRadius(2),
                Margin = new Thickness(2),
                Padding = new Thickness(5, 3, 5, 3),
                Child = new TextBlock { Text = $"第 {i + 1,2} 串   {mv,4} mV", FontFamily = new System.Windows.Media.FontFamily("Consolas") }
            });
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
        if (_autoReconnectRunning || _otaRunning || _connectedAddress is null) return;
        _autoReconnectRunning = true;
        _pollTimer.Stop();
        ulong address = _connectedAddress.Value;
        try
        {
            ConnectionText.Text = "通信异常，自动重连中...";
            AppendLog("自动重连开始：连续 3 次数据刷新失败。", "RECONNECT");
            await ConnectBmsInternalAsync(address);
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
        FactoryParameterSummaryText.Text = $"正式保护参数：已读取 {values.Length} 项\n范围：0x2100..0x{0x2100 + values.Length - 1:X4}（只读）";
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
                    $"确认写入 {changed.Count} 个已修改保护参数？\n\n每项都会写入后立即回读校验。",
                    "确认批量修改",
                    MessageBoxButton.YesNo,
                    MessageBoxImage.Warning) != MessageBoxResult.Yes)
                return;

            _pollTimer.Stop();
            var bms = _bms ?? throw new InvalidOperationException("BMS 未连接。");
            for (int i = 0; i < changed.Count; i++)
            {
                var item = changed[i];
                ProtectionStatusText.Text = $"正在写入 {i + 1}/{changed.Count}：{item.Row.CustomerName}";
                ushort readback = await bms.WriteReadableRegisterAndVerifyAsync(item.Row.Address, item.Parsed);
                item.Row.LoadFromDevice(readback);
                AppendLog($"PROTECTION_WRITE_OK name='{item.Row.CustomerName}'; address=0x{item.Row.Address:X4}; raw={readback}", "PARAM");
            }
            ProtectionStatusText.Text = $"{changed.Count} 项全部写入并校验成功";
        }
        catch (Exception ex) { ShowError("批量写保护参数失败", ex); }
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

    private void BrowseFirmware_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            var dlg = new OpenFileDialog { Filter = "Telink firmware (*.bin)|*.bin|All files (*.*)|*.*" };
            if (dlg.ShowDialog() != true) return;
            _firmware = FirmwareImage.Load(dlg.FileName);
            FirmwareText.Text = $"{_firmware.FileName} · {_firmware.ImageSize:N0} bytes";
            AppendLog($"Firmware loaded file='{_firmware.FileName}' size={_firmware.ImageSize}", "OTA");
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
            OtaVerifyText.Text = string.Empty;
            _otaCts = new CancellationTokenSource();

            string oldVersion = string.Empty;
            try
            {
                if (_bms is not null) oldVersion = (await _bms.ReadIdentityAsync(_otaCts.Token)).Software;
            }
            catch (Exception ex)
            {
                AppendLog("Pre-OTA version read warning: " + ex.Message, "OTA");
            }

            await DisposeBmsAsync();
            ConnectionText.Text = "正在升级...";
            OtaTransferMode requested = GetOtaMode();
            bool serverConfirmed;

            try
            {
                serverConfirmed = await RunOtaOnceAsync(address, image, requested, _otaCts.Token);
            }
            catch (Exception ex) when (requested == OtaTransferMode.Auto && IsExtendCompatibilityFailure(ex) && !_otaCts.IsCancellationRequested)
            {
                AppendLog("Extend64 rejected; fallback to Legacy Fast: " + ex.Message, "OTA");
                await Task.Delay(700, _otaCts.Token);
                serverConfirmed = await RunOtaOnceAsync(address, image, OtaTransferMode.LegacyFast, _otaCts.Token);
            }

            OtaVerifyText.Text = serverConfirmed ? "设备已接受固件，等待重启并验证..." : "数据发送完成，等待设备重启并验证...";
            DeviceIdentity post = await VerifyAfterOtaAsync(address, _otaCts.Token);
            string expected = ExpectedVersionBox.Text.Trim();
            if (expected.Length > 0 && !string.Equals(post.Software, expected, StringComparison.OrdinalIgnoreCase))
                throw new IOException($"设备已重启，但软件版本不匹配：目标 {expected}，实际 {post.Software}。");

            string versionNote = expected.Length > 0
                ? $"版本 {post.Software} 校验通过"
                : oldVersion.Length > 0 && oldVersion != post.Software
                    ? $"版本 {oldVersion} → {post.Software}"
                    : $"当前版本 {post.Software}";

            OtaVerifyText.Text = $"升级成功：设备已重启，BMS 通信和实时数据正常；{versionNote}。";
            ConnectionText.Text = string.IsNullOrEmpty(_connectedName) ? "已连接" : $"已连接：{_connectedName}";
            AppendLog("OTA VERIFIED: " + OtaVerifyText.Text + $" serverConfirmed={serverConfirmed}", "OTA");
            MessageBox.Show(OtaVerifyText.Text, "OTA 验证完成", MessageBoxButton.OK, MessageBoxImage.Information);
        }
        catch (OperationCanceledException)
        {
            AppendLog("OTA cancelled", "OTA");
            OtaVerifyText.Text = "升级已取消";
        }
        catch (Exception ex)
        {
            _sessionLog.WriteException("OTA", "OTA/Verify", ex);
            OtaVerifyText.Text = "升级失败：" + ex.Message;
            MessageBox.Show(ex.Message, "OTA failed", MessageBoxButton.OK, MessageBoxImage.Error);
        }
        finally
        {
            _otaCts?.Dispose();
            _otaCts = null;
            _otaRunning = false;
            StartOtaButton.IsEnabled = true;
            CancelOtaButton.IsEnabled = false;
            if (_bms is not null) _pollTimer.Start();
        }
    }

    private async Task<bool> RunOtaOnceAsync(ulong address, FirmwareImage image, OtaTransferMode mode, CancellationToken ct)
    {
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
                AppendLog($"Post-OTA reconnect {attempt}/12", "OTA");
                await ConnectBmsInternalAsync(address, ct);
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

    private static bool IsExtendCompatibilityFailure(Exception ex)
    {
        string message = ex.Message;
        return message.Contains("OTA_PDU_LEN_ERR", StringComparison.OrdinalIgnoreCase) ||
               message.Contains("OTA_MCU_NOT_SUPPORTED", StringComparison.OrdinalIgnoreCase) ||
               message.Contains("OTA_PACKET_INVALID", StringComparison.OrdinalIgnoreCase) ||
               message.Contains("requires MTU", StringComparison.OrdinalIgnoreCase);
    }

    private void CancelOta_Click(object sender, RoutedEventArgs e) => _otaCts?.Cancel();

    private void UpsertDevice(DiscoveredDevice device)
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
        if (Dispatcher.HasShutdownStarted || Dispatcher.HasShutdownFinished) return;
        if (!Dispatcher.CheckAccess())
        {
            Dispatcher.BeginInvoke(() => AppendLogToUi(text, category));
            return;
        }
        AppendLogToUi(text, category);
    }

    private void AppendLogToUi(string text, string category)
    {
        LogBox.AppendText($"{DateTime.Now:HH:mm:ss.fff} [{category}] {text}{Environment.NewLine}");
        LogBox.ScrollToEnd();
    }

    private void ShowError(string title, Exception ex, string? extra = null)
    {
        _sessionLog.WriteException("ERROR", title, ex);
        AppendLog($"{title}: type={ex.GetType().FullName}; hresult=0x{ex.HResult:X8}; message={ex.Message}", "ERROR");
        string message = extra is null ? ex.Message : extra + "\n\n" + ex.Message;
        MessageBox.Show(message, title, MessageBoxButton.OK, MessageBoxImage.Error);
    }
}
