using System.IO;
using System.Windows;
using System.Windows.Threading;

namespace BmsTool.Windows;

public partial class C073DebugWindow : Window
{
    internal const ushort WakeBase = 0x2D40;
    internal const ushort LegacyWakeWords = 36;
    internal const ushort WakeWords = LegacyWakeWords + 8 * 3;
    private readonly MainWindow _main;
    private readonly CancellationTokenSource _lifetime = new();
    private readonly DispatcherTimer _displayTimer = new() { Interval = TimeSpan.FromMilliseconds(500) };
    private long _generation = long.MinValue;
    private bool _reading, _closed;

    internal C073DebugWindow(MainWindow main)
    {
        _main = main;
        InitializeComponent();
        Owner = main;
        RefreshLocalDisplay();
        _displayTimer.Tick += (_, _) => RefreshLocalDisplay();
        _displayTimer.Start();
        Closed += (_, _) =>
        {
            _closed = true;
            _displayTimer.Stop();
            _lifetime.Cancel();
            _lifetime.Dispose();
            if (PauseMonitor.IsChecked == true) _main.SetDebugMonitorPaused(false);
        };
    }

    private void RefreshLocalDisplay()
    {
        var status = _main.C073DebugStatus();
        if (status.Generation != _generation)
        {
            _generation = status.Generation;
            WakeHistory.ItemsSource = null;
            HistoryStatus.Text = "点击读取最近的 RTC 退出原因。";
        }

        ConnectionStatus.Text = status.Connection;
        LiveSample.Text = status.Sample;
        ReadHistoryButton.IsEnabled = !_reading && status.CanRead;
    }

    private void PauseMonitor_Changed(object sender, RoutedEventArgs e)
    {
        _main.SetDebugMonitorPaused(PauseMonitor.IsChecked == true);
        RefreshLocalDisplay();
    }

    private async void ReadHistory_Click(object sender, RoutedEventArgs e)
    {
        if (_reading) return;
        long generation = _generation;
        _reading = true;
        ReadHistoryButton.IsEnabled = false;
        HistoryStatus.Text = "正在读取 RTC 退出原因…";
        try
        {
            var words = await _main.ReadC073WakeHistoryAsync(generation, _lifetime.Token);
            if (_closed || generation != _main.C073DebugStatus().Generation) return;
            var history = DecodeWakeHistory(words);
            WakeHistory.ItemsSource = history;
            HistoryStatus.Text = history.Count == 0 ? "本次启动尚无 RTC 退出记录。" : $"最近 {history.Count} 次 RTC 退出，最新在上。记录保存在设备 RAM 中。";
        }
        catch (OperationCanceledException) { }
        catch (Exception ex)
        {
            if (_closed || generation != _main.C073DebugStatus().Generation) return;
            WakeHistory.ItemsSource = null;
            HistoryStatus.Text = ex.Message;
        }
        finally
        {
            _reading = false;
            if (!_closed) RefreshLocalDisplay();
        }
    }

    internal sealed record WakeRow(string Time, string Elapsed, string Stage, string Source);

    internal static IReadOnlyList<WakeRow> DecodeWakeHistory(ushort[] words)
    {
        if (words.Length is not (LegacyWakeWords or WakeWords) || words[0] != 0x5257 || words[1] != 1 || words[3] != 8 || words[2] > words[3])
            throw new InvalidDataException("设备返回的 RTC 调试记录格式无法识别。");

        var history = new List<WakeRow>(words[2]);
        for (int index = 0; index < words[2]; ++index)
        {
            int offset = 4 + index * 4;
            uint milliseconds = ((uint)words[offset + 2] << 16) | words[offset + 3];
            ushort mask = words[offset];
            ushort stageCode = words[offset + 1];
            // 旧版 Debug 会把单纯的定时巡检和无标志的 STOP 返回写入。
            if (stageCode == 1 && (mask & ~1) == 0) continue;
            string stage = stageCode switch
            {
                1 => "外部唤醒退出 RTC",
                2 => "进入 STOP 前出现唤醒信号",
                3 => "钥匙电平退出 RTC",
                4 => "MOS 状态要求回到 RUN",
                5 => "RTC 巡检要求回到 RUN",
                6 => "RTC 闹钟配置失败",
                7 => "STOP 恢复失败",
                8 => "RTC 巡检发现充电，恢复 RUN",
                9 => "RTC 巡检发现放电，恢复 RUN",
                10 => "运行参数未就绪",
                11 => "巡检发现电压保护",
                12 => "巡检采样无效",
                13 => "单体电压低于休眠门限",
                14 => "SOC 校准需要回到 RUN",
                15 => "巡检发现 AFE 硬件保护",
                16 => "巡检发现 NTC 故障",
                33 => "检测到深睡请求",
                34 => "欠压达到深睡条件",
                35 => "低电压达到深睡条件",
                36 => "AFE 故障达到深睡条件",
                37 => "存储故障达到深睡条件",
                38 => "MOS 故障达到深睡条件",
                39 => "NTC 故障达到深睡条件",
                40 => "钥匙断开，准备深睡",
                var value => $"未识别阶段 {value}"
            };
            string[] names = ["RTC 闹钟", "PB12（CAN 唤醒线）", "BLE 唤醒", "钥匙", "充电器", "RS485 RX"];
            string bits = string.Join(" / ", names.Where((_, bit) => (mask & (1 << bit)) != 0));
            string source = mask == 0 ? "无 ISR 标志" : $"{bits} (0x{mask:X2})".TrimStart();
            string elapsed = "—";
            if (words.Length == WakeWords)
            {
                int durationOffset = LegacyWakeWords + index * 3;
                ulong durationMs = ((ulong)words[durationOffset] << 32) |
                    ((ulong)words[durationOffset + 1] << 16) | words[durationOffset + 2];
                elapsed = durationMs == 0xFFFF_FFFF_FFFFul
                    ? "计时不可用"
                    : TimeSpan.FromMilliseconds(durationMs).ToString(@"d\.hh\:mm\:ss\.fff");
            }
            history.Add(new(TimeSpan.FromMilliseconds(milliseconds).ToString(@"d\.hh\:mm\:ss\.fff"), elapsed, stage, source));
        }
        return history;
    }
}

public partial class MainWindow
{
    private C073DebugWindow? _c073DebugWindow;
    private bool _debugMonitorPaused;
    private CancellationTokenSource _automaticMonitoringStop = new();

    private void ShowC073DebugTools_Click(object sender, RoutedEventArgs e)
    {
        if (_c073DebugWindow is not null)
        {
            if (_c073DebugWindow.WindowState == WindowState.Minimized) _c073DebugWindow.WindowState = WindowState.Normal;
            _c073DebugWindow.Activate();
            return;
        }

        _c073DebugWindow = new(this);
        _c073DebugWindow.Closed += (_, _) => _c073DebugWindow = null;
        _c073DebugWindow.Show();
    }

    internal void SetDebugMonitorPaused(bool paused)
    {
        if (_bleWindowClosed || _debugMonitorPaused == paused) return;
        _debugMonitorPaused = paused;
        if (paused)
        {
            _pollTimer.Stop();
            _automaticMonitoringStop.Cancel();
            AppendLog("RTC 调试：已暂停 BMS 自动轮询，正在结束在途请求。", "DEBUG");
            return;
        }
        _automaticMonitoringStop.Dispose();
        _automaticMonitoringStop = new();
        if (_bms is null && _connectionDesired && !_otaRunning && !_parameterBusy)
            _pollTimer.Start();
        else
            StartAutomaticRefresh();
        AppendLog("RTC 调试：已恢复自动轮询。", "DEBUG");
    }

    internal (long Generation, string Connection, string Sample, bool CanRead) C073DebugStatus()
    {
        bool busy = _otaRunning || _parameterBusy || _shBusy || ShFactoryBusy() || _eventLogReadInProgress ||
                    _manualConnectionInProgress || _autoReconnectRunning || _connectionOperations.IsBusy;
        string connection = _bms is null
            ? "请在主页面建立 C073 的 CAN、串口或蓝牙连接。"
            : _bms.IsC073 ? $"当前连接 · {_connectedName} · {_bms.DeviceModel}" : "当前设备不提供 C073 RTC 调试记录。";
        string sample = _monitorReading is { IsFresh: true } live
            ? $"主页面实测 · SOC {live.Soc?.ToString("0.##") ?? "—"}% · 总压 {live.Voltage?.ToString("0.00") ?? "—"} V · 电流 {live.Current?.ToString("+0.00;-0.00;0.00") ?? "—"} A"
            : "主页面暂无有效测量。";
        bool draining = _debugMonitorPaused && (_polling || _diagBusy || _runtimeLogBusy || _parameterBusy ||
                                               _mosBusy || _deviceClockBusy || _autoReconnectRunning);
        if (_debugMonitorPaused) sample += draining ? " · 等待在途请求结束" : " · BMS 自动轮询已暂停";
        if (_currentCalibrationOwner is not null) sample += " · 电流校准会话仍在运行";
        if (busy) sample += " · 连接或维护任务进行中";
        return (_connectionGeneration, connection, sample, !busy && !draining && _currentCalibrationOwner is null && _bms is { IsC073: true });
    }

    internal async Task<ushort[]> ReadC073WakeHistoryAsync(long generation, CancellationToken token)
    {
        var status = C073DebugStatus();
        var bms = _bms;
        if (generation != status.Generation) throw new OperationCanceledException();
        if (!status.CanRead || bms is null) throw new InvalidOperationException("请在连接空闲时读取 C073 RTC 记录。");

        using var read = CancellationTokenSource.CreateLinkedTokenSource(token, _connectionLifetime.Token);

        async Task<ushort[]> ReadSnapshotAsync()
        {
            try
            {
                return await bms.ReadRegistersAsync(C073DebugWindow.WakeBase, C073DebugWindow.WakeWords, read.Token);
            }
            catch (BmsModbusException)
            {
                // 旧版 Debug 只支持同一窗口的前 36 个寄存器。
                EnsureCurrentConnection(bms, generation);
                return await bms.ReadRegistersAsync(C073DebugWindow.WakeBase, C073DebugWindow.LegacyWakeWords, read.Token);
            }
        }

        try
        {
            ushort[] words;
            try
            {
                words = await ReadSnapshotAsync();
            }
            catch (TimeoutException)
            {
                // 第一帧可能只唤醒 C073，同一会话内重试一次只读请求。
                await Task.Delay(250, read.Token);
                EnsureCurrentConnection(bms, generation);
                words = await ReadSnapshotAsync();
            }
            EnsureCurrentConnection(bms, generation);
            return words;
        }
        catch (BmsModbusException ex)
        {
            throw new NotSupportedException("设备未提供 RTC 调试窗口，请使用支持此功能的 C073_APP_Debug。", ex);
        }
    }
}
