using System.Globalization;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Media;
using System.Windows.Threading;
using Microsoft.Win32;
using ClosedXML.Excel;

namespace BmsTool.Windows;

public partial class MainWindow
{
#if !BMS_FACTORY_APP
    private readonly ComboBox _recordInterval = new() { Width = 90, ItemsSource = new[] { "1秒", "5秒", "10秒", "30秒", "60秒" }, SelectedIndex = 1 };
    private readonly TextBlock _recordStatus = new() { Text = "未开始", TextWrapping = TextWrapping.Wrap, Margin = new(0, 12, 0, 12) };

    private MonitorRecording? _recording;
    private string? _recordDirectory;
    private string? _recordError, _recordNotice;
    private bool _recordExportBusy;
    private bool _recordStopping;
    private Task? _recordStopTask, _recordExportTask;
#endif
    private MonitorReading? _monitorReading;
    private string? _monitorError;
    private bool _unifiedClosing, _unifiedCloseReady;
    private int _unifiedModeIndex;
    private readonly Dictionary<TabItem, Visibility> _owcHiddenTabs = new();
    private Task? _integratedOwcTask;
    private CancellationTokenSource? _integratedOwcStop;
    private long _integratedOwcEpoch;
    private Version? _integratedOwcVersion;
    private bool _integratedOwcConnected;
    private DispatcherTimer? _unifiedTimer;
    private bool IsCanMode => ConnectionModeBox.SelectedIndex == 3;
    private bool IsOwcMode => ConnectionModeBox.SelectedIndex == 2;
    private static bool UnifiedTestEdition =>
#if BMS_FACTORY_APP
        true;
#else
        false;
#endif

    private bool IsUnifiedRecording =>
#if BMS_FACTORY_APP
        false;
#else
        _recording?.IsRunning == true;
#endif

    private void InitializeUnifiedMonitor()
    {
#if !BMS_FACTORY_APP
        HomeRecordIntervalBox.ItemsSource = _recordInterval.ItemsSource;
        HomeRecordIntervalBox.SetBinding(ComboBox.SelectedIndexProperty, new Binding(nameof(ComboBox.SelectedIndex))
        {
            Source = _recordInterval,
            Mode = BindingMode.TwoWay
        });
        HomeRecordStartButton.Click += UnifiedRecordStart_Click;
        HomeRecordStopButton.Click += async (_, _) => await StopUnifiedRecordingAsync();
        HomeRecordExportButton.Click += UnifiedRecordExport_Click;
        HomeRecordStatusText.SetBinding(TextBlock.TextProperty, new Binding(nameof(TextBlock.Text)) { Source = _recordStatus });
        HomeRecordStatusText.SetBinding(ToolTipProperty, new Binding(nameof(TextBlock.Text)) { Source = _recordStatus });
#endif
        SetMonitorModeDisplay();
        _unifiedTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(500) };
        _unifiedTimer.Tick += (_, _) => UpdateUnifiedMonitorState();
        _unifiedTimer.Start();
        Closing += UnifiedMonitor_Closing;
        Closed += (_, _) => _unifiedTimer.Stop();
        if (UnifiedTestEdition) Title = "BMS Assistant · 测试版";
        UpdateUnifiedMonitorState();
    }

    private bool TryChangeUnifiedMode()
    {
        if (_unifiedTimer is null) return true;
        if (ConnectionModeBox.SelectedIndex != _unifiedModeIndex &&
            (_integratedOwcTask is not null || _bmsTransport is not null || _manualConnectionInProgress ||
             _autoReconnectRunning || IsUnifiedRecording))
        {
            ConnectionModeBox.SelectedIndex = _unifiedModeIndex;
            (IsOwcMode ? OwcStatusText : ConnectionText).Text = "请先结束记录并断开当前连接";
            return false;
        }
        if (_unifiedModeIndex != ConnectionModeBox.SelectedIndex)
        {
            _unifiedModeIndex = ConnectionModeBox.SelectedIndex;
            SetMonitorModeDisplay();
        }
        UpdateUnifiedMonitorState();
        return true;
    }

    private void PublishUnifiedBattery(BatterySnapshot sample)
    {
        string endpoint = _connectedSerialPort ?? _connectedAddress?.ToString("X12") ?? "";
        var cells = sample.CellMillivolts.Select((mv, i) => (mv, i))
            .Where(x => sample.IsCellVoltageValid(x.i) &&
                (sample.Measurements is not null || (x.mv > 0 && x.mv <= 10000)))
            .ToDictionary(x => x.i + 1, x => (double)x.mv);
        var observedUtc = DateTimeOffset.UtcNow;
        PublishUnifiedReading(new MonitorReading {
            ObservedUtc = observedUtc, CellsUtc = sample.CellsReadUtc == default ? null : sample.CellsReadUtc,
            Source = _connectionMode == ConnectionMode.Can ? "CAN" : _connectionMode == ConnectionMode.Serial ? "串口" : "蓝牙", Endpoint = endpoint,
            Protocol = _bms?.DeviceModel ?? "BMS", SegmentKey = $"{endpoint}/{_connectionGeneration}",
            Sequence = $"{_connectionGeneration}/{observedUtc.UtcTicks}",
            Soc = sample.SocPercent is >= 0 and <= 100 ? sample.SocPercent : null,
            Voltage = sample.PackVoltageValid && double.IsFinite(sample.PackVoltageV) ? sample.PackVoltageV : null,
            Current = sample.CurrentValid && double.IsFinite(sample.CurrentA) ? sample.CurrentA : null,
            Temperature = sample.BatteryTemperatureAvailable && double.IsFinite(sample.MaxTempC) ? sample.MaxTempC : null,
            MinTemperature = sample.BatteryTemperatureAvailable ? sample.MinTempC : null,
            MosTemperature = sample.MosTemperatureAvailable ? sample.MosTempC : null, Cycles = sample.CycleCount,
            FaultCode = (sample.ProtectionLevel1Raw | sample.ProtectionLevel2Raw | sample.ProtectionLevel3Raw) == 0 ? (byte)0 : (byte)255,
            Fault = string.Join("；", new[] { sample.ProtectionLevel1Text, sample.ProtectionLevel2Text,
                sample.ProtectionLevel3Text, sample.Measurements?.FaultDetails }
                .Where(x => !string.IsNullOrWhiteSpace(x) && x != "无")),
            HasCurrent = sample.CurrentValid, HasFault = true, HasCells = true, Cells = cells
        });
    }

    private void PublishUnifiedReading(MonitorReading? reading)
    {
        if (reading is not null && !reading.IsFresh) reading = null;
        _monitorReading = reading;
        if (IsOwcMode) ShowOwcReading(reading);
    }

    private void UpdateCustomerBuildFooter()
    {
        string build = ExtractValue(IdentityText.Text ?? "", @"固件 Build ID：([^\r\n]+)", "—");
        var commToolVersion = IsCanMode ? (_bmsTransport as CommToolCanTransport)?.FirmwareVersion :
            IsOwcMode && _integratedOwcTask is not null ? _integratedOwcVersion : null;
        string versionLabel = commToolVersion is null ? "" : $"  ·  Comm Tool 版本：{commToolVersion}";
        CustomerBuildText.Text = $"固件 Build ID：{build}{versionLabel}";
    }

    private void UpdateUnifiedMonitorState()
    {
        if (_unifiedTimer is null) return;
        bool busy = _manualConnectionInProgress || _autoReconnectRunning || _otaRunning || _connectionOperations.IsBusy || ShFactoryBusy();
        bool owc = _integratedOwcTask is not null;
        UpdateCustomerBuildFooter();
        OwcModeBox.IsEnabled = !owc && !busy && !IsUnifiedRecording;
        ConnectionModeBox.IsEnabled = !owc && !busy && _bmsTransport is null && !IsUnifiedRecording;
        DisconnectButton.IsEnabled = owc || _bmsTransport is not null || _connectionDesired || busy;
        CancelConnectionButton.Visibility = _manualConnectionInProgress && !IsOwcMode ? Visibility.Visible : Visibility.Collapsed;
        StopScanButton.IsEnabled = _bleScanRunning;
        if (_monitorReading is not null && !_monitorReading.IsFresh) PublishUnifiedReading(null);
        if (IsOwcMode)
        {
            ScanButton.IsEnabled = ConnectButton.IsEnabled = SerialPortList.IsEnabled = !owc && !busy;
            UpdateOwcConnectionDisplay(owc);
        }
        else SerialPortList.IsEnabled = true;
        foreach (TabItem tab in MainTabs.Items.OfType<TabItem>())
        {
            if (owc && tab != RealtimeTab)
            {
                if (!_owcHiddenTabs.ContainsKey(tab)) _owcHiddenTabs[tab] = tab.Visibility;
                tab.Visibility = Visibility.Collapsed;
            }
            else if (!owc && _owcHiddenTabs.Remove(tab, out var visible)) tab.Visibility = visible;
            if (!UnifiedTestEdition && Equals(tab.Header, "BMS 诊断")) tab.Visibility = Visibility.Collapsed;
        }
#if !BMS_FACTORY_APP
        if (_recording is not null && !_recordStopping)
        {
            try { _recording.Capture(_monitorReading); }
            catch (Exception ex) { HandleUnifiedRecordError(ex); }
        }
        bool running = IsUnifiedRecording;
        string status = _recording is null ? (_recordDirectory is null ? "未开始" : "记录已结束") :
            _recording.Error is Exception failure ? "记录失败：" + failure.Message :
            _recordStopping ? "正在保存记录…" : !_recording.IsRunning ? "记录已停止" :
            $"正在记录 · {_recording.Elapsed:hh\\:mm\\:ss} · 已写入 {_recording.Written:N0} 条";
        HomeRecordStatusText.Foreground = _recordError is null ? Brushes.DimGray : Brushes.Firebrick;
        if (!_recordStopping) _recordStatus.Text = _recordError ?? (_recordExportBusy ? "正在导出…" : running ? status : _recordNotice ?? status);
        bool idle = !running && !_recordStopping && !_recordExportBusy;
        _recordInterval.IsEnabled = HomeRecordIntervalBox.IsEnabled = HomeRecordStartButton.IsEnabled = idle;
        HomeRecordStopButton.IsEnabled = running && !_recordStopping;
        HomeRecordExportButton.IsEnabled = idle;
#endif
    }

#if !BMS_FACTORY_APP
    private async void UnifiedRecordStart_Click(object sender, RoutedEventArgs e)
    {
        if (_recording?.IsRunning == true || _recordStopping || _recordExportBusy) return;
        if (_bmsTransport is null && _integratedOwcTask is null) { _recordStatus.Text = "请先连接设备"; return; }
        var dialog = new OpenFolderDialog { Title = "选择记录保存位置" };
        if (dialog.ShowDialog(this) != true) return;
        try
        {
            if (_recording is not null) await StopUnifiedRecordingAsync();
            _recordError = _recordNotice = null;
            _recording = new MonitorRecording(dialog.FolderName, new[] { 1, 5, 10, 30, 60 }[_recordInterval.SelectedIndex]);
            _recordDirectory = _recording.DirectoryPath;
            _recording.Capture(_monitorReading);

            UpdateUnifiedMonitorState();
        }
        catch (Exception ex) { HandleUnifiedRecordError(ex); }
    }

    private void HandleUnifiedRecordError(Exception ex)
    {
        _recordError = "记录已停止：" + ex.Message;
        _recordStatus.Text = _recordError;
        AppendLog(_recordError, "RECORD");
        if (!_recordStopping) _ = StopUnifiedRecordingAsync();
    }

    private Task StopUnifiedRecordingAsync()
    {
        if (_recordStopTask is { IsCompleted: false }) return _recordStopTask;
        if (_recording is null) return Task.CompletedTask;
        _recordStopping = true;
        return _recordStopTask = FinishUnifiedRecordingAsync(_recording);
    }

    private async Task FinishUnifiedRecordingAsync(MonitorRecording recording)
    {
        _recordStatus.Text = "正在保存记录…";
        try { await recording.StopAsync(); }
        catch (Exception ex) { _recordError = "记录保存失败，已有文件保留：" + ex.Message; AppendLog(_recordError, "ERROR"); }
        finally { _recording = null; _recordStopping = false; UpdateUnifiedMonitorState(); }
    }

    private async void UnifiedRecordExport_Click(object sender, RoutedEventArgs e)
    {
        if (_recording?.IsRunning == true || _recordStopping || _recordExportBusy) return;
        string? directory = _recordDirectory;
        if (directory is null)
        {
            var folder = new OpenFolderDialog { Title = "选择要导出的记录目录" };
            if (folder.ShowDialog(this) != true) return;
            directory = folder.FolderName;
        }
        var dialog = new SaveFileDialog { Filter = "CSV (*.csv)|*.csv|Excel (*.xlsx)|*.xlsx", FileName = $"BMS-{DateTime.Now:yyyyMMdd-HHmmss}", AddExtension = true };
        if (dialog.ShowDialog(this) != true) return;
        _recordExportBusy = true;
        HomeRecordExportButton.IsEnabled = false;
        _recordStatus.Text = "正在导出…";
        try
        {
            _recordExportTask = Task.Run(() => {
                if (!dialog.FileName.EndsWith(".xlsx", StringComparison.OrdinalIgnoreCase))
                    MonitorRecording.ExportCsv(directory, dialog.FileName);
                else
                {
                    ExportUnifiedWorkbook(directory, dialog.FileName);
                }
            });
            await _recordExportTask;
            _recordNotice = _recordStatus.Text = "导出完成：" + dialog.FileName;
        }
        catch (Exception ex) { _recordNotice = _recordStatus.Text = "导出失败：" + ex.Message; }
        finally { _recordExportBusy = false; UpdateUnifiedMonitorState(); }
    }

#endif

    internal static void ExportUnifiedWorkbook(string directory, string destination)
    {
        using var workbook = new XLWorkbook();
        var sheet = workbook.AddWorksheet("记录");
        string[] headers = ["UTC", "类型", "段", "来源", "协议", "SOC %", "总压 V", "电流 A", "温度 ℃", "故障", "备注", "电流分辨率 A", "单体 mV（JSON）", "单体更新时间 UTC", "循环次数"];
        for (int col = 0; col < headers.Length; col++) sheet.Cell(1, col + 1).Value = headers[col];
        int row = 2;
        foreach (var entry in MonitorRecording.Read(directory))
        {
            if (row > 100001) throw new IOException("Excel单次最多导出100000条，请选择CSV导出完整记录");
            var r = entry.Reading;
            sheet.Cell(row, 1).Value = entry.Utc.ToString("O"); sheet.Cell(row, 2).Value = entry.Kind;
            sheet.Cell(row, 3).Value = entry.Segment; sheet.Cell(row, 4).Value = r?.Source;
            sheet.Cell(row, 5).Value = r?.Protocol;
            if (r?.Soc is double soc) sheet.Cell(row, 6).Value = soc;
            if (r?.Voltage is double voltage) sheet.Cell(row, 7).Value = voltage;
            if (r?.Current is double current) sheet.Cell(row, 8).Value = current;
            if (r?.Temperature is double temperature) sheet.Cell(row, 9).Value = temperature;
            sheet.Cell(row, 10).Value = r?.Fault; sheet.Cell(row, 11).Value = entry.Note;
                        if (r is not null)
                        {
                            sheet.Cell(row,12).Value = r.CurrentResolutionA;
                            sheet.Cell(row,13).Value = System.Text.Json.JsonSerializer.Serialize(r.Cells);
                            sheet.Cell(row,14).Value = r.CellsUtc?.ToString("O");
                            if (r.Cycles is int cycles) sheet.Cell(row,15).Value = cycles;
                        }
                        ++row;
        }
        sheet.SheetView.FreezeRows(1); workbook.SaveAs(destination);
    }

    private async Task StartIntegratedOwcAsync()
    {
        if (_integratedOwcTask is not null || _manualConnectionInProgress || _otaRunning || _polling ||
            _autoReconnectRunning || _connectionOperations.IsBusy || _bmsTransport is not null || ShFactoryBusy())
            return;
        if (SerialPortList.SelectedItem is not SerialPortEndpoint endpoint) { _monitorError = "请先刷新并选择 Comm Tool 串口"; UpdateUnifiedMonitorState(); return; }
        _pollTimer.Stop(); StopBleDiscovery();
        _connectionDesired = false; _monitorError = null; ClearDeviceDisplays();
        _integratedOwcStop = new CancellationTokenSource();
        var token = _integratedOwcStop.Token; long epoch = ++_integratedOwcEpoch;
        string port = endpoint.PortName;
        var mode = UnifiedTestEdition ? (OwcReceiveMode)OwcModeBox.SelectedIndex : OwcReceiveMode.Auto;
        _integratedOwcTask = Task.Run(async () => {
            while (!token.IsCancellationRequested)
            {
                try
                {
                    using var client = new CommToolOwcClient(port);
                    try
                    {
                        client.Start(mode, token);
                        var firmwareVersion = client.TryReadFirmwareVersion(token);
                        await Dispatcher.InvokeAsync(() => {
                            if (epoch != _integratedOwcEpoch || token.IsCancellationRequested) return;
                            _integratedOwcVersion = firmwareVersion;
                            _integratedOwcConnected = true;
                            _monitorError = null;
                            UpdateUnifiedMonitorState();
                        });
                        while (!token.IsCancellationRequested)
                        {
                            var snapshots = client.ReadSnapshots(token);
                            if (!snapshots[0].Enabled) client.SetEnabled(true, token);
                            var reading = MonitorReading.FromOwc(snapshots, port);
                            await Dispatcher.InvokeAsync(() => {
                                if (epoch != _integratedOwcEpoch || token.IsCancellationRequested) return;
                                _monitorError = null; PublishUnifiedReading(reading);
#if BMS_FACTORY_APP
                                OwcRawText.Text = string.Join(Environment.NewLine, snapshots
                                    .Where(x => x.Value.Raw.Length != 0)
                                    .Select(x => $"v{x.Value.Version} session={x.Value.Session} seq={x.Value.Sequence} age={x.Value.AgeMs}ms good={x.Value.Good} SUM={x.Value.ChecksumErrors} timing={x.Value.TimingErrors} | {Convert.ToHexString(x.Value.Raw)}"));
#endif
                            });
                            await Task.Delay(1000, token);
                        }
                    }
                    finally { try { client.SetEnabled(false, CancellationToken.None); } catch (Exception ex) { _sessionLog.WriteException("OWC", "停止接收未确认，等待设备租约释放", ex); } }
                }
                catch (OperationCanceledException) when (token.IsCancellationRequested) { break; }
                catch (Exception ex)
                {
                    await Dispatcher.InvokeAsync(() => {
                        if (epoch != _integratedOwcEpoch) return;
                        _monitorError = ex is UnauthorizedAccessException
                            ? $"无法打开 {port}，可能被占用。释放串口后将自动重试。"
                            : _integratedOwcConnected
                                ? "一线通接收中断，正在重试。请检查 Comm Tool 和一线通接线。"
                                : $"Comm Tool 连接未完成，正在重试。请核对 USB 连接和 {port}。";
                        _integratedOwcConnected = false;
                        _integratedOwcVersion = null;
                        PublishUnifiedReading(null); AppendLog("一线通：" + ex.Message, "OWC");
                        UpdateUnifiedMonitorState();
                    });
                    try { await Task.Delay(3000, token); } catch (OperationCanceledException) { break; }
                }
            }
        });
        UpdateUnifiedMonitorState();
        await Task.CompletedTask;
    }

    private async Task StopIntegratedOwcAsync()
    {
        if (_integratedOwcTask is null) return;
        ++_integratedOwcEpoch; var task = _integratedOwcTask;
        _integratedOwcStop?.Cancel();
        _integratedOwcVersion = null;
        _integratedOwcConnected = false;
        PublishUnifiedReading(null);
        UpdateUnifiedMonitorState();
        try { await task; } catch (OperationCanceledException) { }
        finally { _integratedOwcTask = null; _integratedOwcStop?.Dispose(); _integratedOwcStop = null; _monitorError = null; UpdateUnifiedMonitorState(); }
    }

    private async void UnifiedMonitor_Closing(object? sender, System.ComponentModel.CancelEventArgs e)
    {
        if (_unifiedCloseReady) return;
#if BMS_FACTORY_APP
        if (_integratedOwcTask is null) return;
#else
        if (_integratedOwcTask is null && _recording is null && !_recordStopping && !_recordExportBusy) return;
#endif
        e.Cancel = true;
        if (_unifiedClosing) return;
        _unifiedClosing = true;
        await StopIntegratedOwcAsync();
#if !BMS_FACTORY_APP
        await StopUnifiedRecordingAsync();
        if (_recordExportTask is not null)
        {
            try { await _recordExportTask; } catch (Exception ex) { AppendLog("关闭时导出失败：" + ex.Message, "ERROR"); }
        }
#endif
        _unifiedCloseReady = true; Close();
    }
}
