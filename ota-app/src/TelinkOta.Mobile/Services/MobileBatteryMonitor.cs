using TelinkOta.Core.Bms;
using TelinkOta.Core.Ota;
using TelinkOta.Mobile.Ble;

namespace TelinkOta.Mobile.Services;

public sealed class MobileBatteryMonitor : IAsyncDisposable
{
    private readonly Func<MobileBleTransport> _transportFactory;
    private readonly LogCallback _log;
    private MobileBleTransport? _transport;
    private ModbusSppClient? _client;
    private CancellationTokenSource? _cts;
    private Task? _loop;
    private Task? _initialStaticRead;
    private volatile bool _linkLost;
    private BatterySnapshot _current = new();

    public MobileBatteryMonitor(Func<MobileBleTransport> transportFactory, LogCallback? log = null)
    {
        _transportFactory = transportFactory;
        _log = log ?? ((_, _) => { });
    }

    public bool IsRunning { get; private set; }
    public BatterySnapshot Current => _current;
    public event Action<BatterySnapshot>? SnapshotUpdated;
    public event Action<bool>? ConnectionChanged;

    public async Task<bool> StartAsync(CancellationToken ct)
    {
        if (IsRunning) return true;
        if (!await OpenAsync(ct)) return false;
        _current = new BatterySnapshot();
        _cts = new CancellationTokenSource();
        IsRunning = true;
        ConnectionChanged?.Invoke(true);
        _loop = Task.Run(() => PollAsync(_cts.Token));
        // BLE 链路和 SPP 已经就绪即可返回连接成功；静态信息在后台读取，避免连接界面被 5 组串行寄存器读取阻塞。
        _initialStaticRead = ReadInitialStaticAsync(_cts.Token);
        return true;
    }

    private async Task ReadInitialStaticAsync(CancellationToken ct)
    {
        try { await ReadStaticAsync(_current, ct); }
        catch (OperationCanceledException) { }
        catch (Exception ex) { _log(LogLevel.Warn, $"初始静态信息读取异常：{ex.Message}"); }
    }

    private async Task<bool> OpenAsync(CancellationToken ct)
    {
        await CloseLinkAsync();
        _transport = _transportFactory();
        _transport.ConnectionLost += OnConnectionLost;
        if (!await _transport.ConnectAsync(TimeSpan.FromSeconds(15), ct) ||
            !await _transport.DiscoverSppServiceAsync(TimeSpan.FromSeconds(10), ct) ||
            !await _transport.EnableSppNotificationsAsync(TimeSpan.FromSeconds(10), ct))
        {
            await CloseLinkAsync();
            return false;
        }
        await _transport.NegotiateMtuAsync(TimeSpan.FromSeconds(5), ct);
        _client = new ModbusSppClient(_transport);
        _linkLost = false;
        return true;
    }

    private void OnConnectionLost()
    {
        _linkLost = true;
        _log(LogLevel.Warn, "电池连接已断开，准备自动重连。");
    }

    private async Task PollAsync(CancellationToken ct)
    {
        int cycle = 0;
        int failures = 0;
        while (!ct.IsCancellationRequested)
        {
            try
            {
                if (_linkLost || failures >= 3)
                {
                    _log(LogLevel.Info, "尝试重新连接电池…");
                    if (!await OpenAsync(ct))
                    {
                        await Task.Delay(TimeSpan.FromSeconds(5), ct);
                        continue;
                    }
                    failures = 0;
                    await ReadStaticAsync(_current, ct);
                }

                bool any = false;
                byte[]? realtime = await _client!.ReadRegistersAsync(
                    BmsRegisters.RealtimeBase, BmsRegisters.RealtimeCount, TimeSpan.FromSeconds(3), ct);
                if (realtime is not null && BatterySnapshot.ParseRealtime(realtime) is { } rt)
                {
                    ApplyRealtime(_current, rt);
                    any = true;
                }

                if ((cycle & 1) == 0)
                {
                    byte[]? full = await _client.ReadRegistersAsync(
                        BmsRegisters.CellsBase, BmsRegisters.CellsCount, TimeSpan.FromSeconds(5), ct);
                    if (full is not null && BatterySnapshot.ParseLegacyWindow(full) is { } legacy)
                    {
                        ApplyFull(_current, legacy);
                        any = true;
                    }
                    byte[]? status = await _client.ReadRegistersAsync(
                        BmsRegisters.SystemStatusBase, BmsRegisters.SystemStatusCount,
                        TimeSpan.FromSeconds(3), ct);
                    if (status is not null)
                    {
                        _current.SystemStatus = BatterySnapshot.ParseSystemStatus(status);
                        any = true;
                    }
                }

                if (cycle % 10 == 0) await ReadStaticAsync(_current, ct);
                if (any)
                {
                    _current.IsValid = true;
                    failures = 0;
                    SnapshotUpdated?.Invoke(_current);
                }
                else failures++;
            }
            catch (OperationCanceledException) { break; }
            catch (Exception ex)
            {
                failures++;
                _log(LogLevel.Warn, $"电池轮询异常：{ex.Message}");
            }

            cycle++;
            try { await Task.Delay(1000, ct); } catch (OperationCanceledException) { break; }
        }
    }

    private async Task ReadStaticAsync(BatterySnapshot snap, CancellationToken ct)
    {
        if (_client is null) return;
        async Task<byte[]?> Read(ushort address, ushort count) =>
            await _client.ReadRegistersAsync(address, count, TimeSpan.FromSeconds(3), ct);

        var sn = await Read(BmsRegisters.ProdSnBase, BmsRegisters.ProdCount);
        if (sn is not null) snap.SerialNumber = BatterySnapshot.ParseAsciiRegs(sn);
        var hw = await Read(BmsRegisters.ProdHwBase, BmsRegisters.ProdCount);
        if (hw is not null) snap.HardwareVersion = BatterySnapshot.ParseAsciiRegs(hw);
        var sw = await Read(BmsRegisters.ProdSwBase, BmsRegisters.ProdCount);
        if (sw is not null) snap.SoftwareVersion = BatterySnapshot.ParseAsciiRegs(sw);
        var mac = await Read(BmsRegisters.MacBase, BmsRegisters.MacCount);
        if (mac is not null) snap.Mac = BatterySnapshot.ParseMac(mac);
        var name = await Read(BmsRegisters.BtNameBase, BmsRegisters.BtNameCount);
        if (name is not null) snap.BtName = BatterySnapshot.ParseAsciiRegs(name);
        SnapshotUpdated?.Invoke(snap);
    }

    public async Task<(bool Success, string Message)> ChangeNameAsync(string input, CancellationToken ct)
    {
        if (_client is null) return (false, "设备尚未连接。");
        if (!BluetoothNameCodec.TryNormalize(input, out string suffix, out string fullName, out string error))
            return (false, error);
        byte[] encoded = BluetoothNameCodec.EncodeSuffix(suffix);
        int requestLength = 9 + encoded.Length; // addr+func+start+qty+byteCount + data + CRC
        if (_transport is null || requestLength > _transport.MaxWriteLength)
        {
            int maxData = Math.Max(0, (_transport?.MaxWriteLength ?? 20) - 9);
            int maxSuffix = maxData & ~1;
            return (false, $"当前设备 MTU 仅允许最多 {maxSuffix} 个后缀字符，请缩短名称。");
        }
        bool ok = await _client.WriteMultipleRegistersAsync(BmsRegisters.BtNameBase,
            encoded, TimeSpan.FromSeconds(5), ct);
        if (!ok) return (false, "设备没有确认 0x10 写入。");
        await Task.Delay(100, ct);
        byte[]? readback = await _client.ReadRegistersAsync(BmsRegisters.BtNameBase,
            BmsRegisters.BtNameCount, TimeSpan.FromSeconds(4), ct);
        string actual = readback is null ? "" : BatterySnapshot.ParseAsciiRegs(readback);
        if (actual != fullName) return (false, $"读回不一致：期望 {fullName}，实际 {actual}");
        _current.BtName = actual;
        SnapshotUpdated?.Invoke(_current);
        return (true, $"蓝牙名已修改为 {actual}");
    }

    public async Task StopAsync()
    {
        try { _cts?.Cancel(); } catch { }
        if (_loop is not null) { try { await _loop; } catch { } }
        if (_initialStaticRead is not null) { try { await _initialStaticRead; } catch { } }
        _initialStaticRead = null;
        _cts?.Dispose(); _cts = null; _loop = null;
        await CloseLinkAsync();
        IsRunning = false;
        ConnectionChanged?.Invoke(false);
    }

    private async Task CloseLinkAsync()
    {
        _client?.Dispose(); _client = null;
        if (_transport is not null)
        {
            _transport.ConnectionLost -= OnConnectionLost;
            await _transport.DisposeAsync();
            _transport = null;
        }
    }

    private static void ApplyRealtime(BatterySnapshot d, BatterySnapshot s)
    {
        d.PackVoltageV = s.PackVoltageV; d.PackCurrentA = s.PackCurrentA; d.SocPercent = s.SocPercent;
        d.MaxTempC = s.MaxTempC; d.MinTempC = s.MinTempC; d.MosTempC = s.MosTempC;
        d.MaxCellMv = s.MaxCellMv; d.MinCellMv = s.MinCellMv; d.CellDeltaMv = s.CellDeltaMv;
        d.UsingStableWindow = true;
    }

    private static void ApplyFull(BatterySnapshot d, BatterySnapshot s)
    {
        d.CellVoltagesMv = s.CellVoltagesMv; d.MaxCellPosition = s.MaxCellPosition;
        d.MinCellPosition = s.MinCellPosition; d.TemperaturesC = s.TemperaturesC;
        d.ChargeCurrentA = s.ChargeCurrentA; d.DischargeCurrentA = s.DischargeCurrentA;
        d.SohPercent = s.SohPercent; d.CapacityNowAh = s.CapacityNowAh;
        d.CapacityFullAh = s.CapacityFullAh; d.CapacityFactoryAh = s.CapacityFactoryAh;
        d.CycleTimes = s.CycleTimes; d.MdlFaultFirst = s.MdlFaultFirst;
        d.MdlFaultSecond = s.MdlFaultSecond; d.MdlFaultThird = s.MdlFaultThird;
        d.BalanceFlag1 = s.BalanceFlag1; d.BalanceFlag2 = s.BalanceFlag2;
    }

    public async ValueTask DisposeAsync() => await StopAsync();
}
