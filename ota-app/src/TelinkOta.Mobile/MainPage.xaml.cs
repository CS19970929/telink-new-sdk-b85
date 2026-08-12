using System.Collections.ObjectModel;
using TelinkOta.Core.Bms;
using TelinkOta.Core.Ota;
using TelinkOta.Mobile.Ble;
using TelinkOta.Mobile.Services;

namespace TelinkOta.Mobile;

public partial class MainPage : ContentPage
{
    private readonly MobileBleManager _ble = new();
    private readonly ObservableCollection<MobileBleDevice> _devices = new();
    private MobileBleDevice? _selected;
    private MobileBatteryMonitor? _monitor;
    private OtaFirmware? _firmware;
    private CancellationTokenSource? _otaCts;
    private bool _otaRunning;

    public MainPage()
    {
        InitializeComponent();
        DeviceList.ItemsSource = _devices;
        _ble.DeviceUpdated += device => MainThread.BeginInvokeOnMainThread(() => UpsertDevice(device));
        _ble.StatusChanged += text => MainThread.BeginInvokeOnMainThread(() => StatusLabel.Text = text);
    }

    protected override async void OnAppearing()
    {
        base.OnAppearing();
        if (_devices.Count == 0) await StartScanSafeAsync();
    }

    private void ShowMonitorClicked(object sender, EventArgs e)
    {
        MonitorPanel.IsVisible = true; OtaPanel.IsVisible = false;
        MonitorTabButton.BackgroundColor = Color.FromArgb("#1976D2"); MonitorTabButton.TextColor = Colors.White;
        OtaTabButton.BackgroundColor = Color.FromArgb("#E7EEF6"); OtaTabButton.TextColor = Color.FromArgb("#123B62");
    }

    private void ShowOtaClicked(object sender, EventArgs e)
    {
        MonitorPanel.IsVisible = false; OtaPanel.IsVisible = true;
        OtaTabButton.BackgroundColor = Color.FromArgb("#1976D2"); OtaTabButton.TextColor = Colors.White;
        MonitorTabButton.BackgroundColor = Color.FromArgb("#E7EEF6"); MonitorTabButton.TextColor = Color.FromArgb("#123B62");
    }

    private async void ScanClicked(object sender, EventArgs e) => await StartScanSafeAsync();

    private async Task StartScanSafeAsync()
    {
        if (_otaRunning) return;
        try { await _ble.ScanAsync(CancellationToken.None); }
        catch (Exception ex) { await DisplayAlert("扫描失败", ex.Message, "确定"); }
    }

    private void UpsertDevice(MobileBleDevice device)
    {
        int index = _devices.ToList().FindIndex(d => d.Id == device.Id);
        if (index >= 0) _devices[index] = device; else _devices.Add(device);
        var ordered = _devices.OrderByDescending(d => d.Name.StartsWith("BT_", StringComparison.OrdinalIgnoreCase))
            .ThenByDescending(d => d.Rssi).ToList();
        _devices.Clear(); foreach (var item in ordered) _devices.Add(item);
    }

    private void DeviceSelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        _selected = e.CurrentSelection.FirstOrDefault() as MobileBleDevice;
        if (_selected is not null) StatusLabel.Text = $"已选择：{_selected.Display}";
    }

    private async void ConnectClicked(object sender, EventArgs e)
    {
        if (_otaRunning) return;
        if (_monitor?.IsRunning == true)
        {
            await _monitor.StopAsync();
            ConnectButton.Text = "连接"; ConnectionLabel.Text = "未连接";
            return;
        }
        if (_selected is null) { await DisplayAlert("提示", "请先扫描并选择设备。", "确定"); return; }

        ConnectButton.IsEnabled = false; ConnectionLabel.Text = "连接中…";
        await _ble.StopScanAsync();
        MobileBleDevice target = _selected;
        _monitor = new MobileBatteryMonitor(() => _ble.CreateTransport(target), AddLog);
        _monitor.SnapshotUpdated += snapshot => MainThread.BeginInvokeOnMainThread(() => ApplySnapshot(snapshot));
        bool ok = await _monitor.StartAsync(CancellationToken.None);
        ConnectionLabel.Text = ok ? $"已连接 {target.Name}" : "连接失败";
        ConnectButton.Text = ok ? "断开" : "连接";
        ConnectButton.IsEnabled = true;
        if (!ok) await DisplayAlert("连接失败", "未能连接或发现 SPP 服务。请靠近设备后重试。", "确定");
    }

    private void ApplySnapshot(BatterySnapshot s)
    {
        if (s.PackVoltageV is { } v) VoltageLabel.Text = $"{v:F2} V";
        if (s.PackCurrentA is { } a) CurrentLabel.Text = $"{a:F2} A";
        if (s.SocPercent is { } soc) SocLabel.Text = $"{soc}%";
        if (!string.IsNullOrEmpty(s.SerialNumber)) SerialLabel.Text = s.SerialNumber;
        if (!string.IsNullOrEmpty(s.HardwareVersion)) HardwareLabel.Text = s.HardwareVersion;
        if (!string.IsNullOrEmpty(s.SoftwareVersion)) SoftwareLabel.Text = s.SoftwareVersion;
        if (!string.IsNullOrEmpty(s.BtName)) BluetoothNameLabel.Text = s.BtName;
        if (!string.IsNullOrEmpty(s.Mac)) MacLabel.Text = s.Mac;
        if (s.MaxTempC is { } t) TemperatureLabel.Text = $"最高 {t:F1}℃ / 最低 {s.MinTempC:F1}℃ / MOS {s.MosTempC:F1}℃";
        if (s.MaxCellMv is { } max) CellRangeLabel.Text = $"{max}/{s.MinCellMv} mV，压差 {s.CellDeltaMv} mV";
        if (s.CellVoltagesMv.Count > 0) CellsLabel.Text = string.Join("  ", s.CellVoltagesMv.Select((mv, i) => $"{i + 1}:{mv}"));
        if (s.CapacityNowAh is { } now) CapacityLabel.Text = $"当前 {now:F2} / 满充 {s.CapacityFullAh:F2} / 出厂 {s.CapacityFactoryAh:F2} Ah；循环 {s.CycleTimes}";
        if (s.SystemStatus is { } status)
        {
            string bits = string.Join(" ", SystemStatusBits.Decode(status));
            string faults = s.MdlFaultFirst is { } f ? string.Join(" ", FaultBits.Decode(f)) : "无";
            SystemLabel.Text = $"0x{status:X8}  {bits}\n故障：{faults}";
        }
    }

    private async void RenameClicked(object sender, EventArgs e)
    {
        if (_monitor?.IsRunning != true) { await DisplayAlert("提示", "请先连接电池。", "确定"); return; }
        if (!await DisplayAlert("确认修改", $"确定把设备蓝牙名修改为“{BluetoothNameEntry.Text}”吗？", "修改", "取消")) return;
        var result = await _monitor.ChangeNameAsync(BluetoothNameEntry.Text ?? "", CancellationToken.None);
        await DisplayAlert(result.Success ? "修改成功" : "修改失败", result.Message, "确定");
        if (result.Success) BluetoothNameEntry.Text = "";
    }

    private async void PickFirmwareClicked(object sender, EventArgs e)
    {
        try
        {
            var result = await FilePicker.Default.PickAsync(new PickOptions
            {
                PickerTitle = "选择 Telink 固件 BIN",
                FileTypes = new FilePickerFileType(new Dictionary<DevicePlatform, IEnumerable<string>>
                {
                    [DevicePlatform.Android] = new[] { "application/octet-stream", "*/*" },
                    [DevicePlatform.iOS] = new[] { "public.data" },
                }),
            });
            if (result is null) return;
            string cached = Path.Combine(FileSystem.CacheDirectory, "ota_" + Guid.NewGuid().ToString("N") + ".bin");
            await using (var source = await result.OpenReadAsync())
            await using (var target = File.Create(cached)) await source.CopyToAsync(target);

            var parsed = FirmwareParser.Parse(cached);
            if (!parsed.Success) { _firmware = null; FirmwareLabel.Text = parsed.Error; return; }
            _firmware = parsed.Firmware;
            FirmwareLabel.Text = $"{result.FileName}\n{_firmware!.Payload.Length} B，BIN 版本 0x{_firmware.BinVersion:X4}\nSHA-256 {_firmware.Sha256Hex[..16]}…";
            foreach (string warning in parsed.Warnings) AddLog(LogLevel.Warn, warning);
        }
        catch (Exception ex) { await DisplayAlert("固件读取失败", ex.Message, "确定"); }
    }

    private async void StartOtaClicked(object sender, EventArgs e)
    {
        if (_otaRunning || _selected is null || _firmware is null)
        {
            await DisplayAlert("无法开始", "请先选择设备和有效固件。", "确定"); return;
        }
        var battery = _monitor?.Current;
        string power = battery?.PackVoltageV is { } v ? $"\n当前电压 {v:F2} V，SOC {battery.SocPercent}%。" : "";
        if (!await DisplayAlert("确认 OTA", "升级期间请保持稳定供电和近距离连接。任意失败都必须从头重试。" + power, "开始升级", "取消")) return;

        _otaRunning = true; StartOtaButton.IsEnabled = false; CancelOtaButton.IsEnabled = true; ConnectButton.IsEnabled = false;
        DeviceDisplay.Current.KeepScreenOn = true;
        await _ble.StopScanAsync();
        MobileBleDevice target = _selected;
        _otaCts = new CancellationTokenSource();
        bool restartMonitor = _monitor?.IsRunning == true;
        if (restartMonitor) await _monitor!.StopAsync();
        ConnectionLabel.Text = "OTA 独占连接";

        try
        {
            await using var transport = _ble.CreateTransport(target);
            var options = new OtaSessionOptions
            {
                Protocol = ProtocolPicker.SelectedIndex switch { 1 => OtaProtocolChoice.Extend, 2 => OtaProtocolChoice.Legacy, _ => OtaProtocolChoice.Auto },
                PduLength = 16,
                WriteWindow = 1,
                VerifyVersion = true,
                TotalTimeout = TimeSpan.FromSeconds(170),
            };
            var session = new OtaSession(transport, _firmware, options, AddLog);
            session.StateChanged += state => MainThread.BeginInvokeOnMainThread(() => OtaProgressLabel.Text = state.ToString());
            session.ProgressChanged += (index, total) => MainThread.BeginInvokeOnMainThread(() =>
            {
                OtaProgress.Progress = total == 0 ? 0 : (double)index / total;
                OtaProgressLabel.Text = $"{index}/{total} 包";
            });
            OtaSessionResult outcome = await session.RunAsync(_otaCts.Token);
            OtaProgress.Progress = outcome.Outcome == OtaOutcome.Success ? 1 : OtaProgress.Progress;
            await DisplayAlert(outcome.Outcome == OtaOutcome.Success ? "升级成功" : "升级未完成", outcome.Message, "确定");
        }
        catch (Exception ex) { AddLog(LogLevel.Error, ex.ToString()); await DisplayAlert("OTA 异常", ex.Message, "确定"); }
        finally
        {
            _otaCts.Dispose(); _otaCts = null; _otaRunning = false;
            DeviceDisplay.Current.KeepScreenOn = false;
            StartOtaButton.IsEnabled = true; CancelOtaButton.IsEnabled = false; ConnectButton.IsEnabled = true;
            if (restartMonitor && _monitor is not null)
            {
                bool ok = await _monitor.StartAsync(CancellationToken.None);
                ConnectionLabel.Text = ok ? $"已连接 {target.Name}" : "OTA 后监控重连失败";
                ConnectButton.Text = ok ? "断开" : "连接";
            }
            else ConnectionLabel.Text = "未连接";
        }
    }

    private void CancelOtaClicked(object sender, EventArgs e) => _otaCts?.Cancel();

    private void AddLog(LogLevel level, string message) => MainThread.BeginInvokeOnMainThread(() =>
    {
        string line = $"[{DateTime.Now:HH:mm:ss}][{level}] {message}";
        LogEditor.Text = string.IsNullOrEmpty(LogEditor.Text) ? line : LogEditor.Text + Environment.NewLine + line;
        if (LogEditor.Text.Length > 30_000) LogEditor.Text = LogEditor.Text[^20_000..];
    });
}
