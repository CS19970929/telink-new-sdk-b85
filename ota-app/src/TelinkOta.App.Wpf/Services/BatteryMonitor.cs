using TelinkOta.App.Wpf.Ble;
using TelinkOta.Core.Bms;
using TelinkOta.Core.Ota;

namespace TelinkOta.App.Wpf.Services;

/// <summary>
/// 电池状态监控：独立连接设备，经 SPP 周期轮询 Modbus 寄存器并发布快照。
///  - 每秒：0xD120 稳定窗口（Magic 校验）；
///  - 每 2s：0xD000 完整窗口 + SystemStatus；
///  - 每 4s：0xD100 故障记录；
///  - 连接时一次性：产品信息（序列号/硬件/软件版本）、MAC、蓝牙名、保护参数。
/// </summary>
public sealed class BatteryMonitor : IAsyncDisposable
{
    private WindowsBleTransport? _transport;
    private ModbusSppClient? _client;
    private CancellationTokenSource? _cts;
    private Task? _loop;
    private readonly LogCallback _log;

    public bool IsRunning { get; private set; }
    private volatile bool _staticInfoReady;
    private int _staticRetryCount;

    public event Action<BatterySnapshot>? SnapshotUpdated;
    public event Action<bool>? ConnectionChanged;

    public BatteryMonitor(LogCallback? log = null)
    {
        _log = log ?? ((_, _) => { });
    }

    private ulong _address;
    private volatile bool _linkLost;

    public async Task<bool> ConnectAsync(ulong address, CancellationToken ct)
    {
        if (IsRunning)
            return true;

        _address = address;
        _linkLost = false;
        _transport = new WindowsBleTransport(address);
        _log(LogLevel.Info, $"[BMS] 连接 {address:X12} ...");
        _transport.ConnectionLost += OnLinkLost;
        if (!await _transport.ConnectAsync(TimeSpan.FromSeconds(15), ct))
        {
            _log(LogLevel.Error, "[BMS] 连接失败");
            _transport.ConnectionLost -= OnLinkLost;
            await _transport.DisposeAsync();
            _transport = null;
            return false;
        }
        if (!await _transport.DiscoverSppServiceAsync(TimeSpan.FromSeconds(10), ct))
        {
            _log(LogLevel.Warn, "[BMS] 未发现 SPP 业务服务（无法读取电池信息）");
            _transport.ConnectionLost -= OnLinkLost;
            await _transport.DisposeAsync();
            _transport = null;
            return false;
        }
        if (!await _transport.EnableSppNotificationsAsync(TimeSpan.FromSeconds(10), ct))
        {
            _log(LogLevel.Error, "[BMS] SPP 通知订阅失败");
            _transport.ConnectionLost -= OnLinkLost;
            await _transport.DisposeAsync();
            _transport = null;
            return false;
        }
        _client = new ModbusSppClient(_transport);

        // 一次性静态信息（产品信息/MAC/蓝牙名/保护参数）
        await ReadStaticInfoAsync(ct);
        _staticInfoReady = true;

        _cts = new CancellationTokenSource();
        _loop = Task.Run(() => PollLoopAsync(_cts.Token));
        IsRunning = true;
        ConnectionChanged?.Invoke(true);
        return true;
    }

    private void OnLinkLost()
    {
        _linkLost = true;
        _log(LogLevel.Warn, "[BMS] 检测到链路断开，准备自动重连");
    }

    private async Task<bool> ReconnectAsync(CancellationToken ct)
    {
        _log(LogLevel.Info, "[BMS] 尝试自动重连 ...");
        try
        {
            _client?.Dispose();
            _client = null;
            if (_transport is not null)
            {
                _transport.ConnectionLost -= OnLinkLost;
                await _transport.DisposeAsync();
                _transport = null;
            }

            _transport = new WindowsBleTransport(_address);
            _transport.ConnectionLost += OnLinkLost;
            if (!await _transport.ConnectAsync(TimeSpan.FromSeconds(15), ct))
            {
                _log(LogLevel.Warn, "[BMS] 重连失败（设备不可达）");
                return false;
            }
            if (!await _transport.DiscoverSppServiceAsync(TimeSpan.FromSeconds(10), ct))
            {
                _log(LogLevel.Warn, "[BMS] 重连后 SPP 服务未发现");
                return false;
            }
            if (!await _transport.EnableSppNotificationsAsync(TimeSpan.FromSeconds(10), ct))
            {
                _log(LogLevel.Warn, "[BMS] 重连后 SPP 通知订阅失败");
                return false;
            }
            _client = new ModbusSppClient(_transport);
            _staticInfoReady = false;
            _linkLost = false;
            _log(LogLevel.Info, "[BMS] 重连成功");
            return true;
        }
        catch (Exception ex)
        {
            _log(LogLevel.Error, $"[BMS] 重连异常：{ex.Message}");
            return false;
        }
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

    private async Task ReadStaticInfoAsync(CancellationToken ct)
    {
        var snap = new BatterySnapshot();
        await ReadStaticInfoCoreAsync(snap, ct);

        if (!string.IsNullOrEmpty(snap.SerialNumber) || !string.IsNullOrEmpty(snap.HardwareVersion) ||
            !string.IsNullOrEmpty(snap.SoftwareVersion) || !string.IsNullOrEmpty(snap.Mac) ||
            !string.IsNullOrEmpty(snap.BtName) || snap.ProtectValues.Count > 0)
        {
            snap.IsValid = true;
            SnapshotUpdated?.Invoke(snap);
        }
    }

    /// <summary>读取静态信息（产品信息/MAC/蓝牙名/保护参数）到指定快照，不发布。</summary>
    private async Task ReadStaticInfoCoreAsync(BatterySnapshot snap, CancellationToken ct)
    {
        try
        {
            var sn = await _client!.ReadRegistersAsync(BmsRegisters.ProdSnBase, BmsRegisters.ProdCount, TimeSpan.FromSeconds(2), ct);
            if (sn is not null) snap.SerialNumber = BatterySnapshot.ParseAsciiRegs(sn);
            var hw = await _client.ReadRegistersAsync(BmsRegisters.ProdHwBase, BmsRegisters.ProdCount, TimeSpan.FromSeconds(2), ct);
            if (hw is not null) snap.HardwareVersion = BatterySnapshot.ParseAsciiRegs(hw);
            var sw = await _client.ReadRegistersAsync(BmsRegisters.ProdSwBase, BmsRegisters.ProdCount, TimeSpan.FromSeconds(2), ct);
            if (sw is not null) snap.SoftwareVersion = BatterySnapshot.ParseAsciiRegs(sw);
            var mac = await _client.ReadRegistersAsync(BmsRegisters.MacBase, BmsRegisters.MacCount, TimeSpan.FromSeconds(2), ct);
            if (mac is not null) snap.Mac = BatterySnapshot.ParseMac(mac);
            var name = await _client.ReadRegistersAsync(BmsRegisters.BtNameBase, BmsRegisters.BtNameCount, TimeSpan.FromSeconds(2), ct);
            if (name is not null) snap.BtName = BatterySnapshot.ParseAsciiRegs(name);
            var prot = await _client.ReadRegistersAsync(BmsRegisters.ProtectBase, BmsRegisters.ProtectCount, TimeSpan.FromSeconds(2), ct);
            if (prot is not null) snap.ProtectValues = ParseProtect(prot);
        }
        catch (OperationCanceledException) { throw; }
        catch (Exception ex)
        {
            _log(LogLevel.Debug, $"[BMS] 静态信息读取部分失败：{ex.Message}");
        }
    }

    /// <summary>
    /// 通过固件既有的 0x10/0x0100 接口修改蓝牙名。固件保存的是后缀并自动添加 BT_；
    /// 只有写回包成功且随后 0x03 读回完全一致，才向调用方报告成功。
    /// </summary>
    public async Task<BluetoothNameChangeResult> ChangeBluetoothNameAsync(string input, CancellationToken ct)
    {
        if (!IsRunning || _client is null)
            return BluetoothNameChangeResult.Fail("电池监控尚未连接。请先连接设备。");

        if (!BluetoothNameCodec.TryNormalize(input, out string suffix, out string fullName, out string error))
            return BluetoothNameChangeResult.Fail(error);

        byte[] data = BluetoothNameCodec.EncodeSuffix(suffix);
        _log(LogLevel.Info, $"[BMS] 写入蓝牙名 {fullName} ...");
        bool written = await _client.WriteMultipleRegistersAsync(
            BmsRegisters.BtNameBase, data, TimeSpan.FromSeconds(5), ct);
        if (!written)
            return BluetoothNameChangeResult.Fail("设备未确认蓝牙名写入（0x10 回包缺失或不匹配）。");

        // 固件写入后同步更新运行时名称；短暂等待 Flash KV 保存和响应通知完全排空。
        await Task.Delay(100, ct);
        byte[]? readback = await _client.ReadRegistersAsync(
            BmsRegisters.BtNameBase, BmsRegisters.BtNameCount, TimeSpan.FromSeconds(4), ct);
        string actual = readback is null ? "" : BatterySnapshot.ParseAsciiRegs(readback);
        if (!string.Equals(actual, fullName, StringComparison.Ordinal))
            return BluetoothNameChangeResult.Fail(
                $"设备已确认写入，但读回名称不一致（期望 {fullName}，实际 {(actual.Length == 0 ? "<空>" : actual)}）。");

        var snap = new BatterySnapshot { IsValid = true, BtName = actual };
        SnapshotUpdated?.Invoke(snap);
        return BluetoothNameChangeResult.Ok(actual);
    }

    private static IReadOnlyList<(string Name, ushort[] Values)> ParseProtect(byte[] data)
    {
        var list = new List<(string, ushort[])>();
        if (data is null || data.Length < BmsRegisters.ProtectCount * 2)
            return list;
        foreach (var (name, baseAddr) in ProtectParams.Groups)
        {
            int idx = baseAddr - BmsRegisters.ProtectBase;
            if (idx + 5 > BmsRegisters.ProtectCount)
                break;
            var vals = new ushort[5];
            for (int i = 0; i < 5; i++)
            {
                vals[i] = (ushort)((data[(idx + i) * 2] << 8) | data[(idx + i) * 2 + 1]);
            }
            list.Add((name, vals));
        }
        return list;
    }

    private async Task PollLoopAsync(CancellationToken ct)
    {
        int cycle = 0;
        int consecutiveFailures = 0;
        BatterySnapshot? last = null; // 保留上一轮完整窗口数据，避免奇数轮字段变 "--" 闪烁
        while (!ct.IsCancellationRequested)
        {
            try
            {
                if (consecutiveFailures >= 3)
                {
                    _log(LogLevel.Warn, "[BMS] 连续 3 轮无新数据，执行链路自愈重连");
                    _linkLost = true;
                }

                // 链路断开：自动重连（重连期间本轮跳过）
                if (_linkLost || _client is null)
                {
                    if (_linkLost)
                    {
                        bool ok = await ReconnectAsync(ct);
                        if (!ok)
                        {
                            _log(LogLevel.Warn, "[BMS] 重连失败，10s 后重试");
                            try { await Task.Delay(10000, ct); } catch (OperationCanceledException) { break; }
                            continue;
                        }
                        consecutiveFailures = 0;
                    }
                }

                var snap = new BatterySnapshot();
                bool anyOk = false;

                // 1) 稳定窗口（每秒）
                var sw0 = System.Diagnostics.Stopwatch.StartNew();
                var realtime = await _client!.ReadRegistersAsync(
                    BmsRegisters.RealtimeBase, BmsRegisters.RealtimeCount, TimeSpan.FromSeconds(4), ct);
                sw0.Stop();
                _log(LogLevel.Debug, $"[BMS] 稳定窗口读 {sw0.ElapsedMilliseconds}ms -> {(realtime is null ? "失败" : $"{realtime.Length}B")}");
                if (realtime is not null && BatterySnapshot.ParseRealtime(realtime) is { } rt)
                {
                    Merge(snap, rt, overwrite: true);
                    snap.UsingStableWindow = true;
                    anyOk = true;
                }
                else
                {
                    if (realtime is null)
                    {
                        _log(LogLevel.Debug, "[BMS] 0xD120 稳定窗口读取超时/失败，尝试完整窗口");
                    }
                    else if (realtime.All(b => b == 0))
                    {
                        _log(LogLevel.Warn,
                            "[BMS] 0xD120 窗口全零：设备固件未实现 Modbus 寄存器表（read_reg 空实现），请刷新当前固件。");
                    }
                    else
                    {
                        _log(LogLevel.Debug, $"[BMS] 0xD120 窗口 Magic 不符: {Convert.ToHexString(realtime)}");
                    }
                }

                // 2) 完整窗口 + 状态字（每 2 个周期）
                if (cycle % 2 == 0)
                {
                    var sw1 = System.Diagnostics.Stopwatch.StartNew();
                    var legacy = await _client.ReadRegistersAsync(
                        BmsRegisters.CellsBase, BmsRegisters.CellsCount, TimeSpan.FromSeconds(6), ct);
                    sw1.Stop();
                    _log(LogLevel.Debug, $"[BMS] 完整窗口读 {sw1.ElapsedMilliseconds}ms -> {(legacy is null ? "失败" : $"{legacy.Length}B")}");
                    if (legacy is not null && BatterySnapshot.ParseLegacyWindow(legacy) is { } lw)
                    {
                        // 稳定窗口优先；完整窗口只补充单体、容量、故障等缺失字段。
                        Merge(snap, lw, overwrite: false);
                        anyOk = true;
                    }
                    else if (legacy is null)
                    {
                        _log(LogLevel.Debug, "[BMS] 0xD000 完整窗口读取失败");
                    }

                    var status = await _client.ReadRegistersAsync(
                        BmsRegisters.SystemStatusBase, BmsRegisters.SystemStatusCount, TimeSpan.FromSeconds(4), ct);
                    if (status is not null)
                    {
                        snap.SystemStatus = BatterySnapshot.ParseSystemStatus(status);
                        anyOk = true;
                    }
                }

                // 3) 故障记录（每 4 个周期）
                if (cycle % 4 == 0)
                {
                    var fault = await _client.ReadRegistersAsync(
                        BmsRegisters.FaultBase, BmsRegisters.FaultCount, TimeSpan.FromSeconds(4), ct);
                    if (fault is not null)
                    {
                        snap.FaultRecordsHex = BatterySnapshot.ParseFaultRecords(fault);
                        anyOk = true;
                    }
                }

                // 4) 静态信息（序列号等）未取到时周期重试（每 10 个周期）
                if (!_staticInfoReady || _staticRetryCount++ % 10 == 0)
                {
                    var fresh = new BatterySnapshot();
                    await ReadStaticInfoCoreAsync(fresh, ct);
                    if (!string.IsNullOrEmpty(fresh.SerialNumber) || !string.IsNullOrEmpty(fresh.Mac)
                        || !string.IsNullOrEmpty(fresh.SoftwareVersion) || fresh.ProtectValues.Count > 0)
                    {
                        _staticInfoReady = true;
                        Merge(snap, fresh, overwrite: false);
                    }
                }

                // 5) 用上一轮的完整窗口数据补齐本轮的缺口（Merge 只填 null，不覆盖新值）
                if (last is not null)
                {
                    Merge(snap, last, overwrite: false);
                }

                // 健康判定只看本轮真实读回，绝不能让历史基线伪装成新数据。
                if (anyOk)
                {
                    snap.IsValid = true;
                    last = snap; // 仅保存至少包含一项本轮新数据的快照
                    consecutiveFailures = 0;
                    SnapshotUpdated?.Invoke(snap);
                }
                else
                {
                    consecutiveFailures++;
                    _log(LogLevel.Warn, $"[BMS] 本轮轮询无有效数据（连续失败 {consecutiveFailures}）");
                }
            }
            catch (OperationCanceledException)
            {
                break;
            }
            catch (Exception ex)
            {
                consecutiveFailures++;
                _log(LogLevel.Warn, $"[BMS] 轮询异常：{ex.Message}");
            }

            cycle++;
            try { await Task.Delay(1000, ct); } catch (OperationCanceledException) { break; }
        }
        _log(LogLevel.Info, "[BMS] 轮询已停止");
    }

    /// <summary>把一次窗口解析结果合并进快照（不覆盖已填写的静态字段）。</summary>
    private static void Merge(BatterySnapshot dst, BatterySnapshot src, bool overwrite)
    {
        dst.IsValid |= src.IsValid;
        if (src.PackVoltageV is not null && (overwrite || dst.PackVoltageV is null)) dst.PackVoltageV = src.PackVoltageV;
        if (src.PackCurrentA is not null && (overwrite || dst.PackCurrentA is null)) dst.PackCurrentA = src.PackCurrentA;
        if (src.SocPercent is not null && (overwrite || dst.SocPercent is null)) dst.SocPercent = src.SocPercent;
        if (src.MaxTempC is not null && (overwrite || dst.MaxTempC is null)) dst.MaxTempC = src.MaxTempC;
        if (src.MinTempC is not null && (overwrite || dst.MinTempC is null)) dst.MinTempC = src.MinTempC;
        if (src.MosTempC is not null && (overwrite || dst.MosTempC is null)) dst.MosTempC = src.MosTempC;
        if (src.MaxCellMv is not null && (overwrite || dst.MaxCellMv is null)) dst.MaxCellMv = src.MaxCellMv;
        if (src.MinCellMv is not null && (overwrite || dst.MinCellMv is null)) dst.MinCellMv = src.MinCellMv;
        if (src.CellDeltaMv is not null && (overwrite || dst.CellDeltaMv is null)) dst.CellDeltaMv = src.CellDeltaMv;

        if (src.CellVoltagesMv.Count > 0 && (overwrite || dst.CellVoltagesMv.Count == 0)) dst.CellVoltagesMv = src.CellVoltagesMv;
        if (src.MaxCellPosition is not null && (overwrite || dst.MaxCellPosition is null)) dst.MaxCellPosition = src.MaxCellPosition;
        if (src.MinCellPosition is not null && (overwrite || dst.MinCellPosition is null)) dst.MinCellPosition = src.MinCellPosition;
        if (src.TemperaturesC.Count > 0 && (overwrite || dst.TemperaturesC.Count == 0)) dst.TemperaturesC = src.TemperaturesC;
        if (src.ChargeCurrentA is not null && (overwrite || dst.ChargeCurrentA is null)) dst.ChargeCurrentA = src.ChargeCurrentA;
        if (src.DischargeCurrentA is not null && (overwrite || dst.DischargeCurrentA is null)) dst.DischargeCurrentA = src.DischargeCurrentA;
        if (src.SohPercent is not null && (overwrite || dst.SohPercent is null)) dst.SohPercent = src.SohPercent;
        if (src.CapacityNowAh is not null && (overwrite || dst.CapacityNowAh is null)) dst.CapacityNowAh = src.CapacityNowAh;
        if (src.CapacityFullAh is not null && (overwrite || dst.CapacityFullAh is null)) dst.CapacityFullAh = src.CapacityFullAh;
        if (src.CapacityFactoryAh is not null && (overwrite || dst.CapacityFactoryAh is null)) dst.CapacityFactoryAh = src.CapacityFactoryAh;
        if (src.CycleTimes is not null && (overwrite || dst.CycleTimes is null)) dst.CycleTimes = src.CycleTimes;
        if (src.MdlFaultFirst is not null && (overwrite || dst.MdlFaultFirst is null)) dst.MdlFaultFirst = src.MdlFaultFirst;
        if (src.MdlFaultSecond is not null && (overwrite || dst.MdlFaultSecond is null)) dst.MdlFaultSecond = src.MdlFaultSecond;
        if (src.MdlFaultThird is not null && (overwrite || dst.MdlFaultThird is null)) dst.MdlFaultThird = src.MdlFaultThird;
        if (src.BalanceFlag1 is not null && (overwrite || dst.BalanceFlag1 is null)) dst.BalanceFlag1 = src.BalanceFlag1;
        if (src.BalanceFlag2 is not null && (overwrite || dst.BalanceFlag2 is null)) dst.BalanceFlag2 = src.BalanceFlag2;
        if (src.SystemStatus is not null && (overwrite || dst.SystemStatus is null)) dst.SystemStatus = src.SystemStatus;
        if (src.FaultRecordsHex.Count > 0 && (overwrite || dst.FaultRecordsHex.Count == 0)) dst.FaultRecordsHex = src.FaultRecordsHex;
        if (src.ProtectValues.Count > 0 && (overwrite || dst.ProtectValues.Count == 0)) dst.ProtectValues = src.ProtectValues;

        if (!string.IsNullOrEmpty(src.Mac) && (overwrite || string.IsNullOrEmpty(dst.Mac))) dst.Mac = src.Mac;
        if (!string.IsNullOrEmpty(src.BtName) && (overwrite || string.IsNullOrEmpty(dst.BtName))) dst.BtName = src.BtName;
        if (!string.IsNullOrEmpty(src.SerialNumber) && (overwrite || string.IsNullOrEmpty(dst.SerialNumber))) dst.SerialNumber = src.SerialNumber;
        if (!string.IsNullOrEmpty(src.HardwareVersion) && (overwrite || string.IsNullOrEmpty(dst.HardwareVersion))) dst.HardwareVersion = src.HardwareVersion;
        if (!string.IsNullOrEmpty(src.SoftwareVersion) && (overwrite || string.IsNullOrEmpty(dst.SoftwareVersion))) dst.SoftwareVersion = src.SoftwareVersion;
    }

    public async ValueTask DisposeAsync()
    {
        await StopAsync();
    }
}

public sealed record BluetoothNameChangeResult(bool Success, string Message, string? FullName)
{
    public static BluetoothNameChangeResult Ok(string fullName) =>
        new(true, $"蓝牙名已修改为 {fullName}，并已读回确认。", fullName);

    public static BluetoothNameChangeResult Fail(string message) => new(false, message, null);
}
