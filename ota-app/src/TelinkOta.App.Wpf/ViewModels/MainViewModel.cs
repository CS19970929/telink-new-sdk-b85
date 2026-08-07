using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO;
using System.Runtime.CompilerServices;
using System.Windows;
using System.Windows.Threading;
using TelinkOta.App.Wpf.Ble;
using TelinkOta.Core.Ota;

namespace TelinkOta.App.Wpf.ViewModels;

public sealed class MainViewModel : INotifyPropertyChanged
{
    private readonly Dispatcher _dispatcher;
    private readonly BleScanner _scanner = new();
    private CancellationTokenSource? _otaCts;

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
        set { _selectedDevice = value; OnPropertyChanged(); OnPropertyChanged(nameof(CanStart)); }
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
        set { _isBusy = value; OnPropertyChanged(); OnPropertyChanged(nameof(CanStart)); OnPropertyChanged(nameof(CanCancel)); }
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
    }

    public void CancelOta()
    {
        _otaCts?.Cancel();
        Status = "正在取消...";
    }

    // ================= 日志 =================

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
