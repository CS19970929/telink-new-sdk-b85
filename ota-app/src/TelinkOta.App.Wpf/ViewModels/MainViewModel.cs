using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO;
using System.Runtime.CompilerServices;
using System.Windows;
using System.Windows.Threading;
using TelinkOta.App.Wpf.Ble;
using TelinkOta.App.Wpf.Services;
using TelinkOta.Core.Bms;
using TelinkOta.Core.Ota;

namespace TelinkOta.App.Wpf.ViewModels;

public sealed class MainViewModel : INotifyPropertyChanged
{
    private readonly Dispatcher _dispatcher;
    private readonly BleScanner _scanner = new();
    private CancellationTokenSource? _scanCts;
    private CancellationTokenSource? _otaCts;
    private CancellationTokenSource? _batteryConnectCts;
    private BatteryMonitor? _monitor;
    private bool _restartMonitorAfterOta;

    public ObservableCollection<BleDeviceInfo> Devices { get; } = new();

    // 默认显示全部设备。Telink 实机的广播包可能不带 LocalName，名称通常稍后才由
    // Scan Response/Windows 设备缓存补齐；默认按 BT_ 过滤会造成“发现 N 台但列表为空”。
    private string _filterText = "";
    public string FilterText
    {
        get => _filterText;
        set
        {
            _filterText = value ?? "";
            OnPropertyChanged();
            OnPropertyChanged(nameof(FilteredDevices));
            OnPropertyChanged(nameof(VisibleDeviceCount));
        }
    }

    public IReadOnlyList<BleDeviceInfo> FilteredDevices
    {
        get
        {
            string filter = _filterText.Trim();
            IEnumerable<BleDeviceInfo> matches = string.IsNullOrEmpty(filter)
                ? Devices
                : Devices.Where(d => d.Name.Contains(filter, StringComparison.OrdinalIgnoreCase)
                                     || d.AddressHex.Contains(filter, StringComparison.OrdinalIgnoreCase));
            return matches
                .OrderByDescending(d => d.Name.StartsWith("BT_", StringComparison.OrdinalIgnoreCase))
                .ThenByDescending(d => d.Rssi)
                .ToList();
        }
    }

    public int VisibleDeviceCount => FilteredDevices.Count;

    private BleDeviceInfo? _selectedDevice;
    public BleDeviceInfo? SelectedDevice
    {
        get => _selectedDevice;
        set { _selectedDevice = value; OnPropertyChanged(); OnPropertyChanged(nameof(CanStart)); OnPropertyChanged(nameof(CanConnectBattery)); }
    }

    private string _firmwarePath = "";
    public string FirmwarePath
    {
        get => _firmwarePath;
        set { _firmwarePath = value; OnPropertyChanged(); OnPropertyChanged(nameof(CanStart)); }
    }

    // ---- 设置 ----
    public int[] PduOptions { get; } = { 16, 32, 64, 128, 240 };

    public int ProtocolIndex { get; set; } = 0;          // 0=Auto 1=Extend 2=Legacy
    public int PduLength { get; set; } = 16;
    public int WriteWindow { get; set; } = 6;
    public bool PadToFullPdu { get; set; }
    public bool VersionCompare { get; set; }
    public bool VerifyVersion { get; set; } = true;
    public bool EngineeringMode { get; set; }

    // ---- 运行状态 ----
    private bool _isBusy;
    public bool IsBusy
    {
        get => _isBusy;
        set { _isBusy = value; OnPropertyChanged(); OnPropertyChanged(nameof(CanStart)); OnPropertyChanged(nameof(CanCancel)); OnPropertyChanged(nameof(CanConnectBattery)); OnPropertyChanged(nameof(CanChangeBluetoothName)); OnPropertyChanged(nameof(CanScan)); }
    }

    private string _status = "就绪";
    public string Status
    {
        get => _status;
        set { _status = value; OnPropertyChanged(); }
    }

    private double _progressPercent;
    public double ProgressPercent
    {
        get => _progressPercent;
        set { _progressPercent = value; OnPropertyChanged(); }
    }

    private string _progressText = "";
    public string ProgressText
    {
        get => _progressText;
        set { _progressText = value; OnPropertyChanged(); }
    }

    private string _logText = "";
    public string LogText
    {
        get => _logText;
        private set { _logText = value; OnPropertyChanged(); }
    }

    public bool CanStart => !IsBusy && SelectedDevice is not null && File.Exists(FirmwarePath);
    public bool CanCancel => IsBusy;
    public bool CanScan => !BatteryConnected && !IsBusy;

    private OtaFirmware? _firmware;

    public MainViewModel(Dispatcher dispatcher)
    {
        _dispatcher = dispatcher;
        _scanner.ScanStopped += message => Post(() =>
        {
            Status = message;
            Log(LogLevel.Warn, message + "。请确认 Windows 蓝牙已开启且应用有蓝牙权限。" );
        });
        _scanner.DeviceUnavailable += address => Post(() =>
        {
            // 移除通知进入 UI 队列后设备可能已经重新广播；以扫描器最新状态为准。
            if (_scanner.Devices.Any(d => d.Address == address))
                return;
            var device = Devices.FirstOrDefault(d => d.Address == address);
            if (device is null)
                return;
            Devices.Remove(device);
            if (SelectedDevice?.Address == address)
                SelectedDevice = null;
            OnPropertyChanged(nameof(FilteredDevices));
            OnPropertyChanged(nameof(VisibleDeviceCount));
            Log(LogLevel.Info, $"BLE 设备已停止广播，已从列表移除：{device.Name} ({device.AddressHex})");
        });
    }

    // ================= 扫描 =================

    public async void StartScan()
    {
        if (!CanScan || _scanner.IsScanning) return;
        _scanCts?.Cancel();
        _scanCts?.Dispose();
        _scanCts = new CancellationTokenSource();
        CancellationToken ct = _scanCts.Token;
        Status = "扫描中...";
        Devices.Clear();
        OnPropertyChanged(nameof(FilteredDevices));
        OnPropertyChanged(nameof(VisibleDeviceCount));
        Action<BleDeviceInfo> updateDevice = info =>
        {
            Post(() =>
            {
                var existing = Devices.FirstOrDefault(d => d.Address == info.Address);
                if (existing is not null)
                {
                    int idx = Devices.IndexOf(existing);
                    Devices[idx] = info;
                }
                else
                {
                    Devices.Add(info);
                }
                OnPropertyChanged(nameof(FilteredDevices));
                OnPropertyChanged(nameof(VisibleDeviceCount));
            });
        };
        try
        {
            _scanner.Start(updateDevice);

            // Windows/USB 蓝牙适配器偶尔只给某一轮扫描返回极少广播。12 秒后使用新
            // Watcher 做一次增强重试，保留前半程结果，提升取得 Scan Response 名称的概率。
            await Task.Delay(12000, ct);
            _scanner.Start(updateDevice, clearPrevious: false);
            Log(LogLevel.Info, "BLE 扫描已自动增强重试（保留已发现设备）");

            await Task.Delay(18000, ct);
            if (!ct.IsCancellationRequested)
            {
                _scanner.FinishDiscovery();
                Status = BuildScanSummary("扫描完成");
            }
        }
        catch (OperationCanceledException)
        {
            // 用户停止或启动了新一轮扫描。
        }
        catch (Exception ex)
        {
            _scanner.Stop();
            Status = $"BLE 扫描启动失败：{ex.Message}";
            Log(LogLevel.Error, Status);
        }
    }

    public void StopScan()
    {
        _scanCts?.Cancel();
        _scanner.Stop();
        if (Status == "扫描中...")
            Status = BuildScanSummary("扫描已停止");
    }

    private async Task StopScanBeforeConnectionAsync()
    {
        _scanCts?.Cancel();
        await _scanner.StopAsync(TimeSpan.FromSeconds(2));
        if (Status == "扫描中...")
            Status = BuildScanSummary("扫描已停止");
    }

    private string BuildScanSummary(string prefix) =>
        string.IsNullOrWhiteSpace(FilterText)
            ? $"{prefix}，共发现 {Devices.Count} 台设备"
            : $"{prefix}，共发现 {Devices.Count} 台设备，当前过滤后显示 {VisibleDeviceCount} 台";

    // ================= 固件 =================

    public string? ChooseFirmware(string path)
    {
        var result = FirmwareParser.Parse(path, maxFirmwareSize: OtaConstants.MaxFirmwareSizeBytes,
            autoAppendCrc32: true, requireMark: true);
        if (!result.Success)
        {
            Log(LogLevel.Error, $"固件检查失败：{result.Error}");
            return result.Error;
        }
        _firmware = result.Firmware!;
        FirmwarePath = path;
        foreach (var w in result.Warnings)
            Log(LogLevel.Warn, w);
        Log(LogLevel.Info,
            $"固件解析：Size=0x{_firmware.DeclaredSize:X} 版本=0x{_firmware.BinVersion:X4} Mark=TLNK " +
            $"CRC32尾部={(_firmware.CrcVerified ? "验证通过" : _firmware.CrcWasAppended ? "已自动补齐" : "无")} " +
            $"SHA256={_firmware.Sha256Hex[..16]}...");
        Status = $"固件已加载：{Path.GetFileName(path)}";
        return null;
    }

    // ================= OTA =================

    public async void StartOta()
    {
        if (!CanStart) return;
        if (_firmware is null)
        {
            Status = "请先选择并校验固件";
            return;
        }

        await StopScanBeforeConnectionAsync();

        // OTA 与电池监控互斥：暂停监控（释放设备链路），升级后自动恢复
        if (_monitor is { IsRunning: true })
        {
            _restartMonitorAfterOta = true;
            Log(LogLevel.Info, "OTA 开始，暂停电池监控");
            await _monitor.StopAsync();
        }

        _otaCts = new CancellationTokenSource();
        IsBusy = true;
        ProgressPercent = 0;
        ProgressText = "";
        Status = "OTA 会话开始";

        var options = new OtaSessionOptions
        {
            Protocol = ProtocolIndex switch { 1 => OtaProtocolChoice.Extend, 2 => OtaProtocolChoice.Legacy, _ => OtaProtocolChoice.Auto },
            PduLength = PduLength,
            PadToFullPdu = PadToFullPdu,
            VersionCompare = VersionCompare,
            VerifyVersion = VerifyVersion,
            WriteWindow = Math.Clamp(WriteWindow, 1, 32),
        };

        var device = SelectedDevice!;
        int attempt = 0;
        OtaSessionResult? final = null;

        while (!_otaCts.IsCancellationRequested)
        {
            attempt++;
            await using var transport = new WindowsBleTransport(
                device.Address, device.DeviceId, device.AddressType,
                (level, message) => Log(level, message));
            options.MaxWriteLength = transport.MaxWriteLength;
            Log(LogLevel.Info, $"--- OTA 尝试 #{attempt}：{device.Name} ({device.AddressHex}) PDU={options.PduLength} ---");

            var session = new OtaSession(transport, _firmware, options,
                (level, msg) => Log(level, msg));
            session.StateChanged += s => Post(() =>
            {
                Status = $"状态：{s}";
                Log(LogLevel.Info, $"状态迁移 -> {s}");
            });
            session.ProgressChanged += (idx, total) => Post(() =>
            {
                ProgressPercent = total > 0 ? 100.0 * idx / total : 0;
                ProgressText = $"{idx}/{total} 包（PDU={options.PduLength}）";
            });

            final = await session.RunAsync(_otaCts.Token);

            if (final.Outcome == OtaOutcome.PduTooLarge && options.AutoDowngradePdu && options.PduLength > 16)
            {
                Log(LogLevel.Warn, "写入长度超出设备 MTU，自动降级 PDU=16 从头重试");
                options.PduLength = 16;
                continue;
            }
            break;
        }

        IsBusy = false;
        _otaCts.Dispose();
        _otaCts = null;

        if (final is not null)
        {
            ProgressPercent = final.Outcome == OtaOutcome.Success ? 100 : ProgressPercent;
            ProgressText = $"发送 {final.PacketsSent} 包 / {final.BytesSent} B，耗时 {final.Duration.TotalSeconds:F1}s";
            Status = $"{final.Outcome}：{final.Message}";
            Log(final.Outcome == OtaOutcome.Success ? LogLevel.Info : LogLevel.Error,
                $"=== 最终结果：{final.Outcome} - {final.Message} ===");
        }
        else
        {
            Status = "已取消";
        }

        // 升级结束，恢复电池监控（若之前开启）
        if (_restartMonitorAfterOta && SelectedDevice is not null)
        {
            _restartMonitorAfterOta = false;
            Log(LogLevel.Info, "OTA 结束，恢复电池监控");
            await ConnectBatteryAsync(SelectedDevice);
        }
    }

    public void CancelOta()
    {
        _otaCts?.Cancel();
        Status = "正在取消...";
    }

    // ================= 电池监控 =================

    private bool _batteryConnected;
    public bool BatteryConnected
    {
        get => _batteryConnected;
        private set { _batteryConnected = value; OnPropertyChanged(); OnPropertyChanged(nameof(CanConnectBattery)); OnPropertyChanged(nameof(CanChangeBluetoothName)); OnPropertyChanged(nameof(CanScan)); }
    }

    public bool CanConnectBattery => SelectedDevice is not null && !BatteryConnected && !IsBusy;

    private string _batteryStateText = "未连接";
    public string BatteryStateText { get => _batteryStateText; private set { _batteryStateText = value; OnPropertyChanged(); } }

    private string _packVoltageText = "--";
    public string PackVoltageText { get => _packVoltageText; private set { _packVoltageText = value; OnPropertyChanged(); } }

    private string _packCurrentText = "--";
    public string PackCurrentText { get => _packCurrentText; private set { _packCurrentText = value; OnPropertyChanged(); } }

    private string _socText = "--";
    public string SocText { get => _socText; private set { _socText = value; OnPropertyChanged(); } }

    private string _tempsText = "--";
    public string TempsText { get => _tempsText; private set { _tempsText = value; OnPropertyChanged(); } }

    private string _cellRangeText = "--";
    public string CellRangeText { get => _cellRangeText; private set { _cellRangeText = value; OnPropertyChanged(); } }

    private string _cellsText = "--";
    public string CellsText { get => _cellsText; private set { _cellsText = value; OnPropertyChanged(); } }

    private string _statusWordText = "--";
    public string StatusWordText { get => _statusWordText; private set { _statusWordText = value; OnPropertyChanged(); } }

    private string _serialText = "--";
    public string SerialText { get => _serialText; private set { _serialText = value; OnPropertyChanged(); } }

    private string _hwVersionText = "--";
    public string HwVersionText { get => _hwVersionText; private set { _hwVersionText = value; OnPropertyChanged(); } }

    private string _swVersionText = "--";
    public string SwVersionText { get => _swVersionText; private set { _swVersionText = value; OnPropertyChanged(); } }

    private string _macText = "--";
    public string MacText { get => _macText; private set { _macText = value; OnPropertyChanged(); } }

    private string _btNameText = "--";
    public string BtNameText { get => _btNameText; private set { _btNameText = value; OnPropertyChanged(); } }

    private string _newBluetoothName = "";
    public string NewBluetoothName
    {
        get => _newBluetoothName;
        set { _newBluetoothName = value; OnPropertyChanged(); OnPropertyChanged(nameof(CanChangeBluetoothName)); }
    }

    private bool _isChangingBluetoothName;
    public bool CanChangeBluetoothName => BatteryConnected && !IsBusy && !_isChangingBluetoothName &&
                                          !string.IsNullOrWhiteSpace(NewBluetoothName);

    private string _sohText = "--";
    public string SohText { get => _sohText; private set { _sohText = value; OnPropertyChanged(); } }

    private string _capacityText = "--";
    public string CapacityText { get => _capacityText; private set { _capacityText = value; OnPropertyChanged(); } }

    private string _cyclesText = "--";
    public string CyclesText { get => _cyclesText; private set { _cyclesText = value; OnPropertyChanged(); } }

    private string _tempsListText = "--";
    public string TempsListText { get => _tempsListText; private set { _tempsListText = value; OnPropertyChanged(); } }

    private string _currentsText = "--";
    public string CurrentsText { get => _currentsText; private set { _currentsText = value; OnPropertyChanged(); } }

    private string _cellPosText = "--";
    public string CellPosText { get => _cellPosText; private set { _cellPosText = value; OnPropertyChanged(); } }

    private string _statusBitsText = "--";
    public string StatusBitsText { get => _statusBitsText; private set { _statusBitsText = value; OnPropertyChanged(); } }

    private string _faultText = "--";
    public string FaultText { get => _faultText; private set { _faultText = value; OnPropertyChanged(); } }

    private string _balanceText = "--";
    public string BalanceText { get => _balanceText; private set { _balanceText = value; OnPropertyChanged(); } }

    private string _protectText = "--";
    public string ProtectText { get => _protectText; private set { _protectText = value; OnPropertyChanged(); } }

    private string _faultRecordsText = "--";
    public string FaultRecordsText { get => _faultRecordsText; private set { _faultRecordsText = value; OnPropertyChanged(); } }

    public async void ConnectBattery() => await ConnectBatteryAsync(SelectedDevice);

    private async Task ConnectBatteryAsync(BleDeviceInfo? device)
    {
        if (device is null || BatteryConnected || IsBusy)
            return;

        await StopScanBeforeConnectionAsync();

        BatteryConnected = true; // 占用标记，避免重复
        BatteryStateText = $"连接 {device.Name} ...";
        Log(LogLevel.Info, $"电池监控连接：{device.Name} ({device.AddressHex})");

        var monitor = new BatteryMonitor((level, msg) => Log(level, msg));
        _monitor = monitor;
        monitor.SnapshotUpdated += snap => Post(() => ApplySnapshot(snap));
        monitor.ConnectionChanged += connected => Post(() =>
        {
            BatteryStateText = connected ? $"已连接 {device.Name}" : "未连接";
        });

        var connectCts = new CancellationTokenSource();
        _batteryConnectCts = connectCts;
        bool ok;
        bool wasCanceled = false;
        try
        {
            ok = await monitor.ConnectAsync(device, connectCts.Token);
        }
        catch (OperationCanceledException)
        {
            ok = false;
            wasCanceled = true;
            Log(LogLevel.Info, "电池监控连接已取消");
        }
        finally
        {
            wasCanceled |= connectCts.IsCancellationRequested;
            if (ReferenceEquals(_batteryConnectCts, connectCts))
                _batteryConnectCts = null;
            connectCts.Dispose();
        }

        if (!ok)
        {
            BatteryConnected = false;
            BatteryStateText = "连接失败";
            if (!wasCanceled)
                Log(LogLevel.Error, "电池监控连接失败");
            if (ReferenceEquals(_monitor, monitor))
            {
                _monitor = null;
                await monitor.DisposeAsync();
            }
        }
        else
        {
            Log(LogLevel.Info, "电池监控已连接，开始轮询");
        }
    }

    public async void DisconnectBattery() => await DisconnectBatteryAsync();

    private async Task DisconnectBatteryAsync()
    {
        _batteryConnectCts?.Cancel();
        var monitor = _monitor;
        _monitor = null;
        if (monitor is not null)
        {
            Log(LogLevel.Info, "断开电池监控");
            await monitor.DisposeAsync();
        }
        BatteryConnected = false;
        BatteryStateText = "未连接";
        ClearBatteryPanel();
    }

    public async Task ShutdownAsync()
    {
        _scanCts?.Cancel();
        _otaCts?.Cancel();
        _batteryConnectCts?.Cancel();
        await _scanner.StopAsync(TimeSpan.FromSeconds(2));
        await DisconnectBatteryAsync();
        _scanner.Dispose();
        _scanCts?.Dispose();
        _scanCts = null;
    }

    public async void ChangeBluetoothName()
    {
        if (!CanChangeBluetoothName || _monitor is null)
            return;

        _isChangingBluetoothName = true;
        OnPropertyChanged(nameof(CanChangeBluetoothName));
        try
        {
            var result = await _monitor.ChangeBluetoothNameAsync(NewBluetoothName, CancellationToken.None);
            if (result.Success)
            {
                BtNameText = result.FullName!;
                NewBluetoothName = "";
                Status = result.Message;
                Log(LogLevel.Info, result.Message + " 设备列表名称将在下次扫描时刷新。");
            }
            else
            {
                Status = "蓝牙名修改失败";
                Log(LogLevel.Error, result.Message);
            }
        }
        catch (Exception ex)
        {
            Status = "蓝牙名修改失败";
            Log(LogLevel.Error, $"蓝牙名修改异常：{ex.Message}");
        }
        finally
        {
            _isChangingBluetoothName = false;
            OnPropertyChanged(nameof(CanChangeBluetoothName));
        }
    }

    private void ApplySnapshot(BatterySnapshot snap)
    {
        if (snap.PackVoltageV is { } v) PackVoltageText = $"{v:F2} V";
        if (snap.PackCurrentA is { } a) PackCurrentText = $"{a:F2} A";
        if (snap.SocPercent is { } s) SocText = $"{s} %";
        if (snap.SohPercent is { } soh) SohText = $"{soh} %";
        if (snap.MaxTempC is { } tMax)
            TempsText = $"最高 {tMax:F1}℃ / 最低 {snap.MinTempC:F1}℃ / MOS {snap.MosTempC:F1}℃";
        if (snap.MaxCellMv is { } mx)
            CellRangeText = $"最高 {mx} mV / 最低 {snap.MinCellMv} mV / 压差 {snap.CellDeltaMv} mV";
        if (snap.CellVoltagesMv.Count > 0)
            CellsText = string.Join("  ", snap.CellVoltagesMv.Select((mv, i) => $"{i + 1}:{mv}mV"));
        if (snap.MaxCellPosition is { } mxp)
            CellPosText = $"最高单体 #{mxp} / 最低单体 #{snap.MinCellPosition}";
        if (snap.TemperaturesC.Count > 0)
            TempsListText = string.Join("  ", snap.TemperaturesC.Select((t, i) => $"T{i + 1}:{t:F1}℃"));
        if (snap.ChargeCurrentA is { } ic)
            CurrentsText = $"充电 {ic:F2} A / 放电 {snap.DischargeCurrentA:F2} A";
        if (snap.CapacityNowAh is { } cn)
            CapacityText = $"当前 {cn:F2} / 满充 {snap.CapacityFullAh:F2} / 出厂 {snap.CapacityFactoryAh:F2} Ah";
        if (snap.CycleTimes is { } cy) CyclesText = $"{cy}";
        if (snap.SystemStatus is { } st)
        {
            StatusWordText = $"0x{st:X8}";
            var decoded = SystemStatusBits.Decode(st);
            StatusBitsText = decoded.Count > 0 ? string.Join(" ", decoded) : "(无)";
        }
        if (snap.MdlFaultFirst is not null) FaultText = BuildFaultText(snap);
        if (snap.BalanceFlag1 is { } b1)
            BalanceText = $"均衡标志1=0x{b1:X4} 标志2=0x{snap.BalanceFlag2:X4}";
        if (snap.ProtectValues.Count > 0)
            ProtectText = string.Join(Environment.NewLine,
                snap.ProtectValues.Select(g => $"{g.Name}: " + string.Join(" / ", g.Values.Select(v => v.ToString()))));
        if (snap.FaultRecordsHex.Count > 0)
            FaultRecordsText = string.Join(Environment.NewLine, snap.FaultRecordsHex);
        if (!string.IsNullOrEmpty(snap.Mac)) MacText = snap.Mac;
        if (!string.IsNullOrEmpty(snap.BtName)) BtNameText = snap.BtName;
        if (!string.IsNullOrEmpty(snap.SerialNumber)) SerialText = snap.SerialNumber;
        if (!string.IsNullOrEmpty(snap.HardwareVersion)) HwVersionText = snap.HardwareVersion;
        if (!string.IsNullOrEmpty(snap.SoftwareVersion)) SwVersionText = snap.SoftwareVersion;
    }

    private static string BuildFaultText(BatterySnapshot snap)
    {
        if (snap.MdlFaultFirst is not { } f1)
            return "--";
        string D(ushort f) => FaultBits.Decode(f).Count > 0 ? string.Join(" ", FaultBits.Decode(f)) : "-";
        return $"组1[{D(f1)}]  组2[{D(snap.MdlFaultSecond ?? 0)}]  组3[{D(snap.MdlFaultThird ?? 0)}]";
    }

    private void ClearBatteryPanel()
    {
        PackVoltageText = "--"; PackCurrentText = "--"; SocText = "--"; TempsText = "--";
        CellRangeText = "--"; CellsText = "--"; StatusWordText = "--";
        SerialText = "--"; HwVersionText = "--"; SwVersionText = "--";
        MacText = "--"; BtNameText = "--"; SohText = "--"; CapacityText = "--"; CyclesText = "--";
        NewBluetoothName = "";
        TempsListText = "--"; CurrentsText = "--"; CellPosText = "--"; StatusBitsText = "--";
        FaultText = "--"; BalanceText = "--"; ProtectText = "--"; FaultRecordsText = "--";
    }

    // ================= 日志 =================

    private static readonly string LogFilePath =
        Path.Combine(AppContext.BaseDirectory, "TelinkOta.log");

    private void Log(LogLevel level, string message)
    {
        string tag = level switch
        {
            LogLevel.Debug => "DBG",
            LogLevel.Info => "INF",
            LogLevel.Warn => "WRN",
            _ => "ERR",
        };
        string line = $"[{DateTime.Now:HH:mm:ss.fff}][{tag}] {message}";
        try
        {
            File.AppendAllText(LogFilePath, line + Environment.NewLine);
        }
        catch { /* 文件日志失败不影响运行 */ }
        Post(() =>
        {
            LogText += line + Environment.NewLine;
            if (LogText.Length > 200_000)
                LogText = LogText[^100_000..];
        });
    }

    private void Post(Action action)
    {
        if (_dispatcher.CheckAccess()) action();
        else _dispatcher.BeginInvoke(action);
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    private void OnPropertyChanged([CallerMemberName] string? name = null) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
}
