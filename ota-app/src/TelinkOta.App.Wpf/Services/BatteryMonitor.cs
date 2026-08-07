using TelinkOta.App.Wpf.Ble;
using TelinkOta.Core.Bms;
using TelinkOta.Core.Ota;

namespace TelinkOta.App.Wpf.Services;

/// <summary>
/// 电池状态监控：独立连接设备，经 SPP 周期轮询 Modbus 寄存器并发布快照。
/// 轮询为单飞（一次一请求），周期 1s；0xD120 稳定窗口失效时回退 0xD000 兼容窗口。
/// </summary>
public sealed class BatteryMonitor : IAsyncDisposable
{
    private WindowsBleTransport? _transport;
    private ModbusSppClient? _client;
    private CancellationTokenSource? _cts;
    private Task? _loop;
    private readonly LogCallback _log;

    public bool IsRunning { get; private set; }

    public event Action<BatterySnapshot>? SnapshotUpdated;
    public event Action<bool>? ConnectionChanged;

    public BatteryMonitor(LogCallback? log = null)
    {
        _log = log ?? ((_, _) => { });
    }

    public async Task<bool> ConnectAsync(ulong address, CancellationToken ct)
    {
        if (IsRunning)
            return true;

        _transport = new WindowsBleTransport(address);
        _log(LogLevel.Info, $"[BMS] 连接 {address:X12} ...");
        if (!await _transport.ConnectAsync(TimeSpan.FromSeconds(15), ct))
        {
            _log(LogLevel.Error, "[BMS] 连接失败");
            await _transport.DisposeAsync();
            _transport = null;
            return false;
        }
        if (!await _transport.DiscoverSppServiceAsync(TimeSpan.FromSeconds(10), ct))
        {
            _log(LogLevel.Warn, "[BMS] 未发现 SPP 业务服务（无法读取电池信息）");
            await _transport.DisposeAsync();
            _transport = null;
            return false;
        }
        await _transport.EnableSppNotificationsAsync(TimeSpan.FromSeconds(10), ct);
        _client = new ModbusSppClient(_transport);

        _cts = new CancellationTokenSource();
        _loop = Task.Run(() => PollLoopAsync(_cts.Token));
        IsRunning = true;
        ConnectionChanged?.Invoke(true);

        // 先读一次产品信息并发布（含序列号/硬件/软件版本）
        _ = Task.Run(async () =>
        {
            var snap = new BatterySnapshot();
            await ReadProductInfoAsync(snap, ct);
            if (!string.IsNullOrEmpty(snap.SerialNumber) || !string.IsNullOrEmpty(snap.SoftwareVersion))
            {
                snap.IsValid = true;
                SnapshotUpdated?.Invoke(snap);
            }
        });
        return true;
    }

    public async Task StopAsync()
    {
        if (!IsRunning && _cts is null)
            return;
        try { _cts?.Cancel(); } catch { }
        try { if (_loop is not null) await _loop; } catch { }
        _client?.Dispose();
        _client = null;
        if (_transport is not null)
        {
            await _transport.DisposeAsync();
            _transport = null;
        }
        _cts?.Dispose();
        _cts = null;
        _loop = null;
        IsRunning = false;
        ConnectionChanged?.Invoke(false);
    }

    private async Task PollLoopAsync(CancellationToken ct)
    {
        var rnd = new Random();
        while (!ct.IsCancellationRequested)
        {
            try
            {
                var snap = new BatterySnapshot();
                await ReadProductInfoAsync(snap, ct);

                var realtime = await _client!.ReadRegistersAsync(
                    BmsRegisters.RealtimeBase, BmsRegisters.RealtimeCount, TimeSpan.FromSeconds(2), ct);
                if (realtime is not null && BatterySnapshot.ParseRealtime(realtime) is { } rt)
                {
                    CopyTo(snap, rt);
                }
                else
                {
                    // 兼容窗口回退
                    var cells = await _client.ReadRegistersAsync(
                        BmsRegisters.CellsBase, BmsRegisters.CellsCount, TimeSpan.FromSeconds(2), ct);
                    if (cells is not null && BatterySnapshot.ParseLegacyCells(cells) is { } legacy)
                    {
                        CopyTo(snap, legacy);
                    }
                    var status = await _client.ReadRegistersAsync(
                        BmsRegisters.SystemStatusBase, BmsRegisters.SystemStatusCount, TimeSpan.FromSeconds(2), ct);
                    if (status is not null)
                    {
                        snap.SystemStatus = BatterySnapshot.ParseSystemStatus(status);
                    }
                }

                if (snap.IsValid)
                {
                    SnapshotUpdated?.Invoke(snap);
                }
            }
            catch (OperationCanceledException)
            {
                break;
            }
            catch (Exception ex)
            {
                _log(LogLevel.Warn, $"[BMS] 轮询异常：{ex.Message}");
            }

            try { await Task.Delay(1000, ct); } catch (OperationCanceledException) { break; }
        }
        _log(LogLevel.Info, "[BMS] 轮询已停止");
    }

    private async Task ReadProductInfoAsync(BatterySnapshot snap, CancellationToken ct)
    {
        try
        {
            if (snap.SerialNumber.Length == 0)
            {
                var sn = await _client!.ReadRegistersAsync(BmsRegisters.ProdSnBase, BmsRegisters.ProdCount, TimeSpan.FromSeconds(2), ct);
                if (sn is not null) snap.SerialNumber = BatterySnapshot.ParseAsciiRegs(sn);
            }
            if (snap.HardwareVersion.Length == 0)
            {
                var hw = await _client!.ReadRegistersAsync(BmsRegisters.ProdHwBase, BmsRegisters.ProdCount, TimeSpan.FromSeconds(2), ct);
                if (hw is not null) snap.HardwareVersion = BatterySnapshot.ParseAsciiRegs(hw);
            }
            if (snap.SoftwareVersion.Length == 0)
            {
                var sw = await _client!.ReadRegistersAsync(BmsRegisters.ProdSwBase, BmsRegisters.ProdCount, TimeSpan.FromSeconds(2), ct);
                if (sw is not null) snap.SoftwareVersion = BatterySnapshot.ParseAsciiRegs(sw);
            }
        }
        catch (OperationCanceledException) { throw; }
        catch (Exception ex)
        {
            _log(LogLevel.Debug, $"[BMS] 产品信息读取失败：{ex.Message}");
        }
    }

    private static void CopyTo(BatterySnapshot dst, BatterySnapshot src)
    {
        dst.IsValid = src.IsValid;
        dst.UsingStableWindow = src.UsingStableWindow;
        dst.PackVoltageV = src.PackVoltageV;
        dst.PackCurrentA = src.PackCurrentA;
        dst.SocPercent = src.SocPercent;
        dst.MaxTempC = src.MaxTempC;
        dst.MinTempC = src.MinTempC;
        dst.MosTempC = src.MosTempC;
        dst.MaxCellMv = src.MaxCellMv;
        dst.MinCellMv = src.MinCellMv;
        dst.CellDeltaMv = src.CellDeltaMv;
        dst.CellVoltagesMv = src.CellVoltagesMv;
        dst.SystemStatus = src.SystemStatus;
    }

    public async ValueTask DisposeAsync()
    {
        await StopAsync();
    }
}
