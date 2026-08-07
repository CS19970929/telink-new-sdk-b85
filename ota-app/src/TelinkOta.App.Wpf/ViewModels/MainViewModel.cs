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
    private CancellationTokenSource? _otaCts;
    private BatteryMonitor? _monitor;
    private bool _restartMonitorAfterOta;

    public ObservableCollection<BleDeviceInfo> Devices { get; } = new();

    private string _filterText = "BT_";
    public string FilterText
    {
        get => _filterText;
        set { _filterText = value; OnPropertyChanged(); OnPropertyChanged(nameof(FilteredDevices)); }
    }

    public IReadOnlyList<BleDeviceInfo> FilteredDevices =>
        string.IsNullOrWhiteSpace(_filterText)
            ? Devices.ToList()
            : Devices.Where(d => d.Name.Contains(_filterText, StringComparison.OrdinalIgnoreCase)
                                 || d.AddressHex.Contains(_filterText)).ToList();

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
        set { _isBusy = value; OnPropertyChanged(); OnPropertyChanged(nameof(CanStart)); OnPropertyChanged(nameof(CanCancel)); OnPropertyChanged(nameof(CanConnectBattery)); }
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

    private OtaFirmware? _firmware;

    public MainViewModel(Dispatcher dispatcher)
    {
        _dispatcher = dispatcher;
    }

    // ================= 扫描 =================

    public async void StartScan()
    {
        if (_scanner.IsScanning) return;
        Status = "扫描中...";
        Devices.Clear();
        _scanner.Start(info =>
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
            });
        });
        await Task.Delay(15000);
        StopScan();
        Status = "扫描完成";
    }

    public void StopScan()
    {
        if (_scanner.IsScanning) _scanner.Stop();
    }

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
            await using var transport = new WindowsBleTransport(device.Address);
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
        private set { _batteryConnected = value; OnPropertyChanged(); OnPropertyChanged(nameof(CanConnectBattery)); }
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

        BatteryConnected = true; // 占用标记，避免重复
        BatteryStateText = $"连接 {device.Name} ...";
        Log(LogLevel.Info, $"电池监控连接：{device.Name} ({device.AddressHex})");

        _monitor = new BatteryMonitor((level, msg) => Log(level, msg));
        _monitor.SnapshotUpdated += snap => Post(() => ApplySnapshot(snap));
        _monitor.ConnectionChanged += connected => Post(() =>
        {
            BatteryStateText = connected ? $"已连接 {device.Name}" : "未连接";
        });

        bool ok = await _monitor.ConnectAsync(device.Address, CancellationToken.None);
        if (!ok)
        {
            BatteryConnected = false;
            BatteryStateText = "连接失败";
            Log(LogLevel.Error, "电池监控连接失败");
            await _monitor.DisposeAsync();
            _monitor = null;
        }
        else
        {
            Log(LogLevel.Info, "电池监控已连接，开始轮询");
        }
    }

    public async void DisconnectBattery()
    {
        if (_monitor is not null)
        {
            Log(LogLevel.Info, "断开电池监控");
            await _monitor.DisposeAsync();
            _monitor = null;
        }
        BatteryConnected = false;
        BatteryStateText = "未连接";
        ClearBatteryPanel();
    }

    private void ApplySnapshot(BatterySnapshot snap)
    {
        PackVoltageText = snap.PackVoltageV is { } v ? $"{v:F2} V" : "--";
        PackCurrentText = snap.PackCurrentA is { } a ? $"{a:F2} A" : "--";
        SocText = snap.SocPercent is { } s ? $"{s} %" : "--";
        SohText = snap.SohPercent is { } soh ? $"{soh} %" : "--";
        TempsText = snap.MaxTempC is { } tMax
            ? $"最高 {tMax:F1}℃ / 最低 {snap.MinTempC:F1}℃ / MOS {snap.MosTempC:F1}℃"
            : "--";
        CellRangeText = snap.MaxCellMv is { } mx
            ? $"最高 {mx} mV / 最低 {snap.MinCellMv} mV / 压差 {snap.CellDeltaMv} mV"
            : "--";
        CellsText = snap.CellVoltagesMv.Count > 0
            ? string.Join("  ", snap.CellVoltagesMv.Select((mv, i) => $"{i + 1}:{mv}mV"))
            : "--";
        CellPosText = snap.MaxCellPosition is { } mxp
            ? $"最高单体 #{mxp} / 最低单体 #{snap.MinCellPosition}"
            : "--";
        TempsListText = snap.TemperaturesC.Count > 0
            ? string.Join("  ", snap.TemperaturesC.Select((t, i) => $"T{i + 1}:{t:F1}℃"))
            : "--";
        CurrentsText = snap.ChargeCurrentA is { } ic
            ? $"充电 {ic:F2} A / 放电 {snap.DischargeCurrentA:F2} A"
            : "--";
        CapacityText = snap.CapacityNowAh is { } cn
            ? $"当前 {cn:F2} / 满充 {snap.CapacityFullAh:F2} / 出厂 {snap.CapacityFactoryAh:F2} Ah"
            : "--";
        CyclesText = snap.CycleTimes is { } cy ? $"{cy}" : "--";
        StatusWordText = snap.SystemStatus is { } st ? $"0x{st:X8}" : "--";
        StatusBitsText = snap.SystemStatus is { } stb
            ? (SystemStatusBits.Decode(stb).Count > 0 ? string.Join(" ", SystemStatusBits.Decode(stb)) : "(无)")
            : "--";
        FaultText = BuildFaultText(snap);
        BalanceText = snap.BalanceFlag1 is { } b1
            ? $"均衡标志1=0x{b1:X4} 标志2=0x{snap.BalanceFlag2:X4}"
            : "--";
        ProtectText = snap.ProtectValues.Count > 0
            ? string.Join(Environment.NewLine,
                snap.ProtectValues.Select(g => $"{g.Name}: " + string.Join(" / ", g.Values.Select(v => v.ToString()))))
            : "--";
        FaultRecordsText = snap.FaultRecordsHex.Count > 0
            ? string.Join(Environment.NewLine, snap.FaultRecordsHex)
            : "--";
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
