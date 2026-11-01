using System.Buffers.Binary;
using System.IO;
using System.Diagnostics;
using System.Text;

namespace BmsTool.Windows;

public sealed record DeviceIdentity(string Mac, string Serial, string Hardware, string Software, string BluetoothName);

public sealed record FactorySession(ushort Token, ushort TimeoutSeconds, byte ProtocolVersion, byte SeriesCount);

public sealed record FactoryStatus(
    ushort Token,
    ushort InjectionMask,
    ushort CellMaxMv,
    ushort CellMinMv,
    ushort CellDeltaMv,
    ushort PackCv,
    ushort ChargeCurrentTenthA,
    ushort DischargeCurrentTenthA,
    ushort TemperatureMaxRaw,
    ushort TemperatureMinRaw,
    ushort MosTemperatureRaw,
    ushort SocPercent,
    ushort ProtectionLevel1,
    ushort ProtectionLevel2,
    ushort ProtectionLevel3,
    ushort MosState);

public sealed class BatterySnapshot
{
    private static readonly string[] ProtectionNames =
    {
        "单体过压", "单体欠压", "总压过压", "总压欠压",
        "充电过流", "放电过流", "充电高温", "放电高温",
        "充电低温", "放电低温", "单体压差过大", "温差过大",
        "SOC过低", "MOS高温"
    };

    // PC 读取时间范围；协议窗口之间非原子，时间不是设备采样时间。
    public DateTimeOffset ReadStartedUtc { get; init; }
    public DateTimeOffset CellsReadUtc { get; init; }
    public DateTimeOffset StatusReadUtc { get; init; }
    public DateTimeOffset RealtimeReadUtc { get; init; }
    public string CellDataWindow => "D000";
    public double PackVoltageV { get; init; }
    public double CurrentA { get; init; }
    public int SocPercent { get; init; }
    public int SohPercent { get; init; }
    public bool SohValid => SohPercent is >= 0 and <= 100;
    public string SohDisplay => SohValid ? $"{SohPercent}%" : "—";
    public double MaxTempC { get; init; }
    public double MinTempC { get; init; }
    public double MosTempC { get; init; }
    public int MaxCellMv { get; init; }
    public int MinCellMv { get; init; }
    public int CellDeltaMv { get; init; }
    public int MaxCellPosition { get; init; }
    public int MinCellPosition { get; init; }
    public int CycleCount { get; init; }
    public double CapacityNowAh { get; init; }
    public double CapacityFullAh { get; init; }
    public double CapacityFactoryAh { get; init; }
    public uint SystemStatus { get; init; }
    public ushort ProtocolVersion { get; init; }
    public bool UsesRealtimeWindow { get; init; }
    public int ValidCellCount => CellMillivolts.Count(value => value != BmsRegisters.MissingCellVoltageMv);
    public ushort ProtectionLevel1Raw { get; init; }
    public ushort ProtectionLevel2Raw { get; init; }
    public ushort ProtectionLevel3Raw { get; init; }
    public IReadOnlyList<ushort> CellMillivolts { get; init; } = Array.Empty<ushort>();
    // D03D: 第 1～16 串；D03E 低 8 位: 第 17～24 串。状态来自 AFE 最后一次回读。
    public uint BalanceCellMask { get; init; }
    public bool IsCellBalancing(int zeroBasedCellIndex) =>
        (uint)zeroBasedCellIndex < 24u && zeroBasedCellIndex < CellMillivolts.Count &&
        CellMillivolts[zeroBasedCellIndex] != BmsRegisters.MissingCellVoltageMv &&
        (BalanceCellMask & (1u << zeroBasedCellIndex)) != 0;


    public string WorkState => CurrentA > 0.05 ? "放电" : CurrentA < -0.05 ? "充电" : "静置";
    public bool PrechargeMosOn => Bit(1);
    public bool ChargeMosOn => Bit(2);
    public bool DischargeMosOn => Bit(3);
    public bool PrechargeRelayOn => Bit(4);
    public bool ChargeRelayOn => Bit(5);
    public bool DischargeRelayOn => Bit(6);
    public bool MainRelayOn => Bit(7);
    public bool HeatingOn => Bit(8);
    public bool CoolingOn => Bit(9);
    public bool Afe1On => Bit(10);
    public bool Afe2On => Bit(11);
    public bool BalancingOn => Bit(12);
    public bool PreparingSleep => Bit(13);
    public bool ButtonCloseIo => Bit(14);
    public bool HeatCloseIo => Bit(15);
    public bool SystemLimited => Bit(16);
    public bool CbcCloseIo => Bit(17);
    public bool DriverExternalControl => Bit(18);

    public bool HasProtection => (ProtectionLevel1Raw | ProtectionLevel2Raw | ProtectionLevel3Raw) != 0;
    public string ProtectionSummary => HasProtection ? "保护中" : "正常";
    public string ProtectionLevel1Text => DecodeProtection(ProtectionLevel1Raw);
    public string ProtectionLevel2Text => DecodeProtection(ProtectionLevel2Raw);
    public string ProtectionLevel3Text => DecodeProtection(ProtectionLevel3Raw);

    private bool Bit(int bit) => (SystemStatus & (1u << bit)) != 0;

    private static string DecodeProtection(ushort raw)
    {
        if ((raw & 0x3FFF) == 0) return "无";
        var active = new List<string>();
        for (int i = 0; i < ProtectionNames.Length; i++)
        {
            if ((raw & (1u << i)) != 0) active.Add(ProtectionNames[i]);
        }
        return active.Count == 0 ? "无" : string.Join("、", active);
    }
}

// 每个窗口持有一份累计计数；更换 transport 或 BmsClient 后仍保留证据。
public sealed record BmsCommunicationSnapshot(long TxFrames, long TxBytes, long RxFrames,
    long RxBytes, long Timeouts, long CrcErrors, long InvalidResponses, long ExceptionResponses,
    long SendFailures, long Cancellations, double? LastResponseMs, double? LastResponseAgeMs,
    string LastError);

public sealed class BmsCommunicationStatistics
{
    private readonly object _lock = new();
    private long _txFrames, _txBytes, _rxFrames, _rxBytes, _timeouts, _crcErrors;
    private long _invalidResponses, _exceptionResponses, _sendFailures, _cancellations;
    private long? _lastResponseTick;
    private double? _lastResponseMs;
    private string _lastError = string.Empty;

    public BmsCommunicationSnapshot Snapshot()
    {
        lock (_lock) return new(_txFrames, _txBytes, _rxFrames, _rxBytes, _timeouts,
            _crcErrors, _invalidResponses, _exceptionResponses, _sendFailures, _cancellations,
            _lastResponseMs, _lastResponseTick is long tick ? Stopwatch.GetElapsedTime(tick).TotalMilliseconds : null,
            _lastError);
    }
    internal void Sent(int bytes) { lock (_lock) { _txFrames++; _txBytes += bytes; } }
    internal void ReceivedBytes(int bytes) { lock (_lock) _rxBytes += bytes; }
    internal void ReceivedFrame(double elapsedMs, bool exception)
    {
        lock (_lock) {
            _rxFrames++; if (exception) _exceptionResponses++;
            _lastResponseMs = elapsedMs; _lastResponseTick = Stopwatch.GetTimestamp();
        }
    }
    internal void Failed(Exception ex, bool sending)
    {
        lock (_lock) {
            if (ex is OperationCanceledException) _cancellations++;
            else if (sending) _sendFailures++;
            else if (ex is TimeoutException) _timeouts++;
            else if (ex is ModbusCrcException) _crcErrors++;
            else _invalidResponses++;
            _lastError = ex.GetType().Name + ": " + ex.Message;
        }
    }
}

public sealed partial class BmsClient : IAsyncDisposable
{
    // COM3 / D011 独立台架对照：连续请求会丢响应，20 ms 转换间隔通过。
    // 仅限制串口请求，给板端和转换器完成发送到接收的切换；BLE 保持原节奏。
    private static readonly TimeSpan SerialResponseTurnaroundDelay = TimeSpan.FromMilliseconds(20);
    private long _lastReceiveTick;
    private readonly IBmsTransport _transport;
    private readonly SemaphoreSlim _gate = new(1, 1);
    private readonly object _rxLock = new();
    private readonly List<byte> _rx = new();
    private TaskCompletionSource<byte[]>? _pending;
    private byte[]? _pendingRequest;
    public event Action<string>? Log;
    public BmsCommunicationStatistics CommunicationStatistics { get; }

    public BmsClient(IBmsTransport transport, BmsCommunicationStatistics? statistics = null)
    {
        _transport = transport;
        CommunicationStatistics = statistics ?? new BmsCommunicationStatistics();
        _transport.DataReceived += OnData;
    }

    public async Task ProbeAsync(CancellationToken ct = default)
    {
        // Keep COM open: the first requests wake the shared one-wire/UART pin.
        bool serial = _transport.RequiresSerialWakeup;
        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(ct);
        if (serial) deadline.CancelAfter(TimeSpan.FromSeconds(20));
        CancellationToken probeToken = deadline.Token;
        int attempts = serial ? 24 : 3;
        Exception? last = null;
        for (int attempt = 1; attempt <= attempts; attempt++)
        {
            ct.ThrowIfCancellationRequested();
            try
            {
                Log?.Invoke($"[MODBUS] PROBE attempt={attempt}/{attempts} begin");
                byte[] rsp = await TransactAsync(
                    ModbusRtu.ReadHolding(BmsRegisters.Realtime, 2),
                    probeToken,
                    TimeSpan.FromMilliseconds(serial ? 650 : 1800));
                ushort[] words = ModbusRtu.ParseRead(rsp, 2);
                if (words.Length >= 2 && words[0] == BmsRegisters.RealtimeMagic)
                {
                    Log?.Invoke($"[MODBUS] PROBE attempt={attempt}/{attempts} ok realtimeMagic=0x{words[0]:X4}; protocol=0x{words[1]:X4}");
                    return;
                }

                // Older/deployed BMS firmware can expose the same BLE UART and
                // Modbus map while leaving the optional realtime window at zero.
                // A valid D120 response is not enough to identify the device, so
                // verify the stable legacy data window before accepting it.
                Log?.Invoke($"[MODBUS] PROBE attempt={attempt}/{attempts} realtime window unavailable; magic=0x{words.ElementAtOrDefault(0):X4}; trying legacy D000 window");
                ushort[] legacyHead = await ReadRegistersAsync(BmsRegisters.Legacy, 4, probeToken);
                if (legacyHead.All(value => value == 0))
                {
                    ushort[] production = await ReadRegistersAsync(BmsRegisters.Serial, 16, probeToken);
                    if (production.All(value => value == 0))
                        throw new IOException($"BMS Modbus probe failed: D120 magic is 0x{words.ElementAtOrDefault(0):X4}, and both legacy D000 and production C002 windows are empty.");
                    Log?.Invoke($"[MODBUS] PROBE attempt={attempt}/{attempts} ok legacy-compatible; D120 magic=0x{words.ElementAtOrDefault(0):X4}; production data present");
                }
                else
                {
                    Log?.Invoke($"[MODBUS] PROBE attempt={attempt}/{attempts} ok legacy-compatible; D120 magic=0x{words.ElementAtOrDefault(0):X4}; D000={string.Join(" ", legacyHead.Select(value => value.ToString("X4")))}");
                }
                return;
            }
            catch (OperationCanceledException) when (!ct.IsCancellationRequested && serial)
            {
                throw new TimeoutException("串口已打开，但 BMS 在 20 秒内未响应。已等待一线通切换 Modbus，请检查接线、波特率和设备供电。", last);
            }
            catch (OperationCanceledException)
            {
                throw;
            }
            catch (Exception ex)
            {
                last = ex;
                Log?.Invoke($"[MODBUS] PROBE attempt={attempt}/{attempts} failed type={ex.GetType().Name}; hresult=0x{ex.HResult:X8}; message={ex.Message}");
                if (attempt < attempts)
                {
                    ct.ThrowIfCancellationRequested();
                    if (!serial)
                    {
                        Log?.Invoke("[MODBUS] rebuilding BLE/GATT before retry");
                        await _transport.ReconnectAsync(ct);
                    }
                    else if (!_transport.IsConnected)
                        throw new IOException("串口已断开。", ex);
                    await Task.Delay(serial ? 100 : 250, ct);
                }
            }
        }

        throw new IOException(serial
            ? "串口已打开，但等待一线通切换后仍未收到有效 Modbus 响应，请检查接线、波特率和设备供电。"
            : "BLE/GATT 已重建，但 BMS 在 3 次探测后仍未响应 Modbus。", last);
    }

    public bool IsSh3520 { get; private set; }
    public bool IsD015 { get; private set; }

    public async Task<ushort[]> ReadRegistersAsync(ushort start, ushort quantity, CancellationToken ct = default, TimeSpan? responseTimeout = null)
    {
        if (quantity is 0 or > 125) throw new ArgumentOutOfRangeException(nameof(quantity), "Modbus 0x03 quantity must be 1..125.");
        byte[] rsp = await TransactAsync(ModbusRtu.ReadHolding(start, quantity), ct, responseTimeout).ConfigureAwait(false);
        ushort[] words=ModbusRtu.ParseRead(rsp, quantity);
        if(start==Sh3520Parameters.HardwareBase && words.Length>=2) {
            IsSh3520=words[0]==Sh3520Parameters.Magic;
            if(words[0]==1 && words[1]==0x3520) IsD015=true;
        }
        return words;
    }

    public async Task WriteSingleRegisterAsync(ushort register, ushort value, CancellationToken ct = default, TimeSpan? responseTimeout = null)
    {
        if(register>=BmsRegisters.Protect && register<BmsRegisters.Protect+BmsRegisters.ProtectCount) await EnsureLegacyProtectionAsync(ct);
        byte[] rsp = await TransactAsync(ModbusRtu.WriteSingle(register, value), ct, responseTimeout);
        ModbusRtu.ValidateWriteSingleAck(rsp, register, value);
    }

    public async Task<ushort> WriteReadableRegisterAndVerifyAsync(ushort register, ushort value, CancellationToken ct = default)
    {
        await WriteSingleRegisterAsync(register, value, ct);
        ushort readback = (await ReadRegistersAsync(register, 1, ct))[0];
        if (readback != value)
            throw new IOException($"Register verify failed at 0x{register:X4}: wrote {value} (0x{value:X4}), read {readback} (0x{readback:X4}).");
        return readback;
    }

    public async Task<ushort[]> ReadProtectionAllAsync(CancellationToken ct = default)
    {
        await EnsureLegacyProtectionAsync(ct);
        return await ReadRegistersAsync(BmsRegisters.Protect, BmsRegisters.ProtectCount, ct);
    }

    public async Task<FactorySession> FactoryOpenAsync(CancellationToken ct = default)
    {
        byte[] rsp = await TransactAsync(ModbusRtu.FactoryOpen(), ct);
        ModbusRtu.ValidateFactoryResponse(rsp, 0x01);
        if (rsp.Length != 12) throw new IOException("Factory open response length mismatch.");
        return new FactorySession(
            BinaryPrimitives.ReadUInt16BigEndian(rsp.AsSpan(4, 2)),
            BinaryPrimitives.ReadUInt16BigEndian(rsp.AsSpan(6, 2)),
            rsp[8], rsp[9]);
    }

    public async Task FactoryHeartbeatAsync(ushort token, CancellationToken ct = default)
    {
        byte[] rsp = await TransactAsync(ModbusRtu.FactoryCommand(0x02, token), ct);
        ModbusRtu.ValidateFactoryResponse(rsp, 0x02);
        if (rsp.Length != 10 || BinaryPrimitives.ReadUInt16BigEndian(rsp.AsSpan(4, 2)) != token)
            throw new IOException("Factory heartbeat response mismatch.");
    }

    public async Task FactoryInjectAsync(ushort token, byte kind, byte index, ushort value, CancellationToken ct = default)
    {
        byte[] rsp = await TransactAsync(ModbusRtu.FactoryInject(token, kind, index, value), ct);
        ModbusRtu.ValidateFactoryResponse(rsp, 0x03);
        if (rsp.Length != 10 || BinaryPrimitives.ReadUInt16BigEndian(rsp.AsSpan(4, 2)) != token)
            throw new IOException("Factory injection response mismatch.");
    }

    public async Task FactoryClearAsync(ushort token, CancellationToken ct = default)
    {
        byte[] rsp = await TransactAsync(ModbusRtu.FactoryCommand(0x04, token), ct);
        ModbusRtu.ValidateFactoryResponse(rsp, 0x04);
        if (rsp.Length != 8 || BinaryPrimitives.ReadUInt16BigEndian(rsp.AsSpan(4, 2)) != token)
            throw new IOException("Factory clear response mismatch.");
    }

    public async Task FactoryCloseAsync(ushort token, CancellationToken ct = default)
    {
        byte[] rsp = await TransactAsync(ModbusRtu.FactoryCommand(0x05, token), ct);
        ModbusRtu.ValidateFactoryResponse(rsp, 0x05);
        if (rsp.Length != 6) throw new IOException("Factory close response length mismatch.");
    }

    public async Task<FactoryStatus> ReadFactoryStatusAsync(ushort token, CancellationToken ct = default)
    {
        byte[] rsp = await TransactAsync(ModbusRtu.FactoryCommand(0x06, token), ct);
        ModbusRtu.ValidateFactoryResponse(rsp, 0x06);
        if (rsp.Length != 38) throw new IOException("Factory status response length mismatch.");
        ushort U(int offset) => BinaryPrimitives.ReadUInt16BigEndian(rsp.AsSpan(offset, 2));
        if (U(4) != token) throw new IOException("Factory status token mismatch.");
        return new FactoryStatus(U(4), U(6), U(8), U(10), U(12), U(14), U(16), U(18),
            U(20), U(22), U(24), U(26), U(28), U(30), U(32), U(34));
    }

    public Task<DeviceIdentity> ReadIdentityAsync(CancellationToken ct = default) =>
        ReadIdentityAsync(string.Empty, string.Empty, ct);

    public Task<DeviceIdentity> ReadIdentityAsync(string fallbackBluetoothName, CancellationToken ct = default) =>
        ReadIdentityAsync(string.Empty, fallbackBluetoothName, ct);

    public async Task<DeviceIdentity> ReadIdentityAsync(
        string fallbackMac,
        string fallbackBluetoothName,
        CancellationToken ct = default)
    {
        string macText = fallbackMac.Trim();
        try
        {
            ushort[] mac = await ReadRegistersAsync(BmsRegisters.Mac, 3, ct);
            byte[] macBytes = mac.SelectMany(w => new[] { (byte)(w >> 8), (byte)w }).Take(6).ToArray();
            if (macBytes.Any(value => value != 0))
                macText = string.Join(":", macBytes.Select(b => b.ToString("X2")));
        }
        catch (IOException ex)
        {
            // External UART/BLE modules may not expose the optional BMS MAC
            // register. The BLE transport address remains authoritative.
            Log?.Invoke($"[IDENTITY] MAC register 0x{BmsRegisters.Mac:X4} unavailable; using fallback='{macText}'; message={ex.Message}");
        }

        ushort[] sn = await ReadRegistersAsync(BmsRegisters.Serial, 16, ct);
        ushort[] hw = await ReadRegistersAsync(BmsRegisters.Hardware, 16, ct);
        ushort[] sw = await ReadRegistersAsync(BmsRegisters.Software, 16, ct);
        string bluetoothName = fallbackBluetoothName.Trim();
        try
        {
            ushort[] name = await ReadRegistersAsync(BmsRegisters.BtName, BmsRegisters.BtNameReadWords, ct);
            bluetoothName = ModbusRtu.DecodeAscii(name);
        }
        catch (IOException ex)
        {
            // Some external UART/BLE modules own the advertised name and do
            // not implement the optional BMS BT-name register.
            Log?.Invoke($"[IDENTITY] BT name register 0x{BmsRegisters.BtName:X4} unavailable; using fallback name='{bluetoothName}'; message={ex.Message}");
        }

        if (string.IsNullOrWhiteSpace(bluetoothName))
            bluetoothName = "未知";

        if (string.IsNullOrWhiteSpace(macText))
            macText = "未知";

        string serial = ModbusRtu.DecodeAscii(sn);
        string hardware = ModbusRtu.DecodeAscii(hw);
        string software = ModbusRtu.DecodeAscii(sw);
        if (hardware.StartsWith("HS-D015", StringComparison.Ordinal)) {
            var product = await ReadRegistersAsync(0xD130, 2, ct);
            IsD015 = product[0] == 0xD015 && product[1] == 0x0320;
            if (!IsD015) throw new IOException("D015硬件字符串与产品身份不一致，禁止继续写入或升级。");
        }
        return new DeviceIdentity(
            macText,
            string.IsNullOrWhiteSpace(serial) ? "未知" : serial,
            string.IsNullOrWhiteSpace(hardware) ? "未知" : hardware,
            string.IsNullOrWhiteSpace(software) ? "未知" : software,
            bluetoothName);
    }

    /*
     * Canonical application current direction:
     *   positive = discharge, negative = charge.
     *
     * Legacy D000 stores separate positive charge/discharge magnitudes.
     * D120 protocol v1 intentionally kept the historical wire convention
     * positive=charge / negative=discharge. Normalize both at this boundary so
     * every caller above BmsClient sees one direction convention.
     */
    internal static int NormalizeLegacyCurrentTenthA(ushort charge, ushort discharge)
    {
        if (discharge != 0u) return discharge;
        if (charge != 0u) return -(int)charge;
        return 0;
    }

    internal static int NormalizeRealtimeCurrentTenthA(ushort version, ushort wireValue)
    {
        int wireCurrent = unchecked((short)wireValue);
        return version == 1u ? -wireCurrent : wireCurrent;
    }

    public async Task<BatterySnapshot> ReadBatteryAsync(CancellationToken ct = default)
    {
        DateTimeOffset readStartedUtc = DateTimeOffset.UtcNow;
        ushort[] legacy = await ReadRegistersAsync(BmsRegisters.Legacy, 63, ct).ConfigureAwait(false);
        DateTimeOffset cellsReadUtc = DateTimeOffset.UtcNow;
        ushort[] status = await ReadRegistersAsync(BmsRegisters.SystemStatus, 2, ct).ConfigureAwait(false);
        DateTimeOffset statusReadUtc = DateTimeOffset.UtcNow;
        ushort[] realtime = await ReadRegistersAsync(BmsRegisters.Realtime, 11, ct).ConfigureAwait(false);
        DateTimeOffset realtimeReadUtc = DateTimeOffset.UtcNow;
        bool rt = realtime.Length >= 11 && realtime[0] == BmsRegisters.RealtimeMagic;

        ushort chg = legacy[50];
        ushort dsg = legacy[51];
        int current = NormalizeLegacyCurrentTenthA(chg, dsg);
        ushort voltage = legacy[37];
        ushort soc = legacy[52];
        ushort maxTemp = legacy[48];
        ushort minTemp = legacy[49];
        ushort mosTemp = legacy[47];
        ushort maxCell = legacy[32];
        ushort minCell = legacy[33];
        ushort delta = legacy[36];

        if (rt)
        {
            voltage = realtime[2];
            current = NormalizeRealtimeCurrentTenthA(realtime[1], realtime[3]);
            soc = realtime[4];
            maxTemp = realtime[5];
            minTemp = realtime[6];
            mosTemp = realtime[7];
            // 单体数组、极值、串号及压差全部保留同一次 D000 应答。
        }

        return new BatterySnapshot
        {
            ReadStartedUtc = readStartedUtc, CellsReadUtc = cellsReadUtc,
            StatusReadUtc = statusReadUtc, RealtimeReadUtc = realtimeReadUtc,
            PackVoltageV = voltage / 100.0,
            CurrentA = current / 10.0,
            SocPercent = soc,
            SohPercent = legacy[53],
            MaxTempC = maxTemp / 10.0 - 40.0,
            MinTempC = minTemp / 10.0 - 40.0,
            MosTempC = mosTemp / 10.0 - 40.0,
            MaxCellMv = maxCell,
            MinCellMv = minCell,
            CellDeltaMv = delta,
            MaxCellPosition = legacy[34],
            MinCellPosition = legacy[35],
            CycleCount = legacy[57],
            CapacityNowAh = legacy[54] / 100.0,
            CapacityFullAh = legacy[55] / 100.0,
            CapacityFactoryAh = legacy[56] / 100.0,
            SystemStatus = (uint)status[0] | ((uint)status[1] << 16),
            ProtocolVersion = rt ? realtime[1] : (ushort)0,
            UsesRealtimeWindow = rt,
            ProtectionLevel1Raw = legacy[58],
            ProtectionLevel2Raw = legacy[59],
            ProtectionLevel3Raw = legacy[60],
            BalanceCellMask = (uint)legacy[61] | ((uint)(legacy[62] & 0x00FFu) << 16),
            // The first 32 Legacy words are protocol slots, not the configured
            // series count.  61001 marks a slot that is not physically present.
            CellMillivolts = legacy.Take(BmsRegisters.CellVoltageSlotCount).ToArray()
        };
    }

    public async Task<BatterySnapshot> SetSocAndVerifyAsync(ushort soc, CancellationToken ct = default)
    {
        if (soc > 100) throw new ArgumentOutOfRangeException(nameof(soc), "SOC must be 0..100.");
        await WriteSingleRegisterAsync(0x1005, soc, ct);
        await Task.Delay(180, ct);
        BatterySnapshot snapshot = await ReadBatteryAsync(ct);
        if (snapshot.SocPercent != soc)
            throw new IOException($"SOC verification failed: wrote {soc}%, device reports {snapshot.SocPercent}%.");
        return snapshot;
    }

    public async Task<BatterySnapshot> SetCycleCountAndVerifyAsync(ushort cycleCount, CancellationToken ct = default)
    {
        await WriteSingleRegisterAsync(0x2319, cycleCount, ct);
        await Task.Delay(180, ct);
        BatterySnapshot snapshot = await ReadBatteryAsync(ct);
        if (snapshot.CycleCount != cycleCount)
            throw new IOException($"Cycle-count verification failed: wrote {cycleCount}, device reports {snapshot.CycleCount}.");
        return snapshot;
    }

    public async Task<string> ReadBluetoothNameAsync(CancellationToken ct = default) =>
        ModbusRtu.DecodeAscii(await ReadRegistersAsync(BmsRegisters.BtName, BmsRegisters.BtNameReadWords, ct));

    public async Task<string> WriteBluetoothNameSuffixAsync(string suffix, CancellationToken ct = default)
    {
        suffix = suffix.Trim();
        if (suffix.StartsWith("BT_", StringComparison.OrdinalIgnoreCase)) suffix = suffix[3..];
        if (suffix.Length == 0) throw new ArgumentException("Bluetooth name suffix cannot be empty.");
        if (Encoding.ASCII.GetByteCount(suffix) > BmsRegisters.BtNameMaxSuffixBytesPerBleRequest)
            throw new ArgumentException("Current firmware BLE request supports at most 10 suffix bytes.");
        if (suffix.Any(c => !(char.IsAsciiLetterOrDigit(c) || c == '_' || c == '-')))
            throw new ArgumentException("Suffix only supports letters, digits, '_' and '-'.");

        byte[] raw = Encoding.ASCII.GetBytes(suffix);
        if ((raw.Length & 1) != 0) Array.Resize(ref raw, raw.Length + 1);
        byte[] rsp = await TransactAsync(ModbusRtu.WriteMultiple(BmsRegisters.BtName, raw), ct);
        ModbusRtu.ValidateWriteMultipleAck(rsp, BmsRegisters.BtName, checked((ushort)(raw.Length / 2)));
        string readback = await ReadBluetoothNameAsync(ct);
        string expected = "BT_" + suffix;
        if (readback != expected)
            throw new IOException($"Bluetooth name readback mismatch: expected '{expected}', got '{readback}'.");
        return readback;
    }

    public async Task EnsureLegacyProtectionAsync(CancellationToken ct = default)
    {
        ushort[] identity;
        try { identity=await ReadRegistersAsync(Sh3520Parameters.HardwareBase,2,ct); }
        catch(BmsModbusException ex) when(ex.Function==0x03 && (ex.Code==0x01 || ex.Code==0x03)) { return; }
        if(identity[0]==Sh3520Parameters.Magic)
            throw new IOException("当前设备是 SH3673520，请使用 3520 保护参数页。旧 309 参数编码不适用。");
        // A valid non-3520 reply identifies the legacy register layout.
    }

    public async Task Write3520ParametersAsync(ushort address,ushort[] words,CancellationToken ct=default)
    {
        if((address!=Sh3520Parameters.HardwareBase && address!=Sh3520Parameters.SoftwareBase)||words.Length!=24)
            throw new ArgumentException("3520 参数必须整组 24 寄存器写入。");
        Sh3520Parameters.CheckIdentity(await ReadRegistersAsync(Sh3520Parameters.HardwareBase,32,ct));
        byte[] raw=new byte[48];
        for(int i=0;i<24;i++) BinaryPrimitives.WriteUInt16BigEndian(raw.AsSpan(i*2,2),words[i]);
        byte[] request=ModbusRtu.WriteMultiple(address,raw);
        if(_transport is IBmsMtuTransport ble && (ble.NegotiatedMtu??23)<request.Length+3)
            throw new IOException("当前 BLE MTU 无法承载 57 字节原子写入，请使用直连串口或支持 MTU ≥ 60 的透明通道；不拆帧写保护参数。");
        ModbusRtu.ValidateWriteMultipleAck(await TransactAsync(request,ct),address,24);
    }

    private async Task<byte[]> TransactAsync(byte[] request, CancellationToken ct, TimeSpan? responseTimeout = null)
    {
        await _gate.WaitAsync(ct).ConfigureAwait(false);
        var tcs = new TaskCompletionSource<byte[]>(TaskCreationOptions.RunContinuationsAsynchronously);
        bool sending = true;
        try
        {
            if (_transport.RequiresSerialWakeup) {
                long received = Interlocked.Read(ref _lastReceiveTick);
                if (received != 0) {
                    double remainingMs = SerialResponseTurnaroundDelay.TotalMilliseconds - Stopwatch.GetElapsedTime(received).TotalMilliseconds;
                    if (remainingMs > 0) await Task.Delay((int)Math.Ceiling(remainingMs), ct).ConfigureAwait(false);
                }
            }
            lock (_rxLock) { _rx.Clear(); _pendingRequest = request; _pending = tcs; }
            Log?.Invoke("TX " + Convert.ToHexString(request));
            long started = Stopwatch.GetTimestamp();
            await _transport.WriteAsync(request, ct).ConfigureAwait(false);
            CommunicationStatistics.Sent(request.Length);
            sending = false;
            TimeSpan timeout = responseTimeout ?? TimeSpan.FromSeconds(4);
            byte[] rsp;
            try { rsp = await tcs.Task.WaitAsync(timeout, ct).ConfigureAwait(false); }
            catch (TimeoutException ex) {
                byte[] buffered;
                lock (_rxLock) buffered = _rx.ToArray();
                Log?.Invoke($"[MODBUS] TIMEOUT timeoutMs={timeout.TotalMilliseconds:F0}; request={Convert.ToHexString(request)}; buffered={Convert.ToHexString(buffered)}");
                throw new TimeoutException($"Modbus response timed out after {timeout.TotalMilliseconds:F0} ms.", ex);
            }
            ValidateResponseHeader(request, rsp);
            CommunicationStatistics.ReceivedFrame(Stopwatch.GetElapsedTime(started).TotalMilliseconds, (rsp[1] & 0x80) != 0);
            Log?.Invoke("RX " + Convert.ToHexString(rsp));
            return rsp;
        }
        catch (Exception ex) {
            CommunicationStatistics.Failed(ex, sending);
            throw;
        }
        finally {
            // 包括 WriteAsync 失败在内，任何退出路径都释放当前接收所有权。
            lock (_rxLock) {
                if (ReferenceEquals(_pending, tcs)) { _pending = null; _pendingRequest = null; }
                _rx.Clear();
            }
            _gate.Release();
        }
    }

    private static void ValidateResponseHeader(byte[] request, byte[] response)
    {
        ModbusRtu.ValidateFrame(response);
        if (response[0] != request[0] || (response[1] & 0x7F) != request[1])
            throw new IOException("Modbus response address/function mismatch.");
        if ((response[1] & 0x80) != 0) {
            if (response.Length != 5) throw new IOException("Invalid Modbus exception response length.");
            return;
        }
        if (request[1] == 0x03) {
            int bytes = BinaryPrimitives.ReadUInt16BigEndian(request.AsSpan(4, 2)) * 2;
            if (response.Length != bytes + 5 || response[2] != bytes)
                throw new IOException("Modbus read response length mismatch.");
        } else if (request[1] is 0x06 or 0x10) {
            if (response.Length != 8 || !response.AsSpan(0, 6).SequenceEqual(request.AsSpan(0, 6)))
                throw new IOException("Modbus write acknowledgement mismatch.");
        } else if (response.Length < 6 || response[2] != request[2]) {
            throw new IOException("Modbus command response mismatch.");
        }
    }

    private void OnData(ReadOnlyMemory<byte> fragment)
    {
        Interlocked.Exchange(ref _lastReceiveTick, Stopwatch.GetTimestamp());
        CommunicationStatistics.ReceivedBytes(fragment.Length);
        TaskCompletionSource<byte[]>? done = null;
        byte[]? frame = null;
        string message;
        lock (_rxLock)
        {
            if (_pending is null) message = "[MODBUS] unsolicited RX fragment=" + Convert.ToHexString(fragment.Span);
            else {
                if (_rx.Count + fragment.Length > 4096) {
                    _pending.TrySetException(new IOException("Modbus receive buffer overflow."));
                    _rx.Clear(); return;
                }
                _rx.AddRange(fragment.ToArray());
                // Boot may send C while a capability query is in flight. Match only
                // this request; never let an unsolicited prefix become a frame header.
                while (_rx.Count > 0 && (_rx[0] != _pendingRequest![0] ||
                    (_rx.Count > 1 && (_rx[1] & 0x7F) != _pendingRequest[1])))
                    _rx.RemoveAt(0);
                int? expected = ModbusRtu.InferExpectedLength(_rx);
                message = $"[MODBUS] RX_FRAGMENT len={fragment.Length}; accumulated={_rx.Count}; expected={(expected?.ToString() ?? "unknown")}";
                if (expected is not null && _rx.Count >= expected.Value) {
                    frame = _rx.Take(expected.Value).ToArray();
                    done = _pending;
                }
            }
        }
        // 先交付校验后的响应，再处理日志；外部回调不得占用接收锁。
        if (frame is not null && done is not null) {
            try { ModbusRtu.ValidateFrame(frame); done.TrySetResult(frame); }
            catch (Exception ex) { done.TrySetException(ex); }
        }
        Log?.Invoke(message);
    }

    public ValueTask DisposeAsync()
    {
        _transport.DataReceived -= OnData;
        _gate.Dispose();
        return ValueTask.CompletedTask;
    }
}
