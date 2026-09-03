using System.Buffers.Binary;
using System.IO;
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

    public double PackVoltageV { get; init; }
    public double CurrentA { get; init; }
    public int SocPercent { get; init; }
    public int SohPercent { get; init; }
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
    public ushort ProtectionLevel1Raw { get; init; }
    public ushort ProtectionLevel2Raw { get; init; }
    public ushort ProtectionLevel3Raw { get; init; }
    public IReadOnlyList<ushort> CellMillivolts { get; init; } = Array.Empty<ushort>();

    public string WorkState => CurrentA > 0.05 ? "充电" : CurrentA < -0.05 ? "放电" : "静置";
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

public sealed class BmsClient : IAsyncDisposable
{
    private readonly BmsBleTransport _transport;
    private readonly SemaphoreSlim _gate = new(1, 1);
    private readonly object _rxLock = new();
    private readonly List<byte> _rx = new();
    private TaskCompletionSource<byte[]>? _pending;
    public event Action<string>? Log;

    public BmsClient(BmsBleTransport transport)
    {
        _transport = transport;
        _transport.DataReceived += OnData;
    }

    public async Task ProbeAsync(CancellationToken ct = default)
    {
        Exception? last = null;
        for (int attempt = 1; attempt <= 3; attempt++)
        {
            ct.ThrowIfCancellationRequested();
            try
            {
                Log?.Invoke($"[MODBUS] PROBE attempt={attempt}/3 begin");
                byte[] rsp = await TransactAsync(
                    ModbusRtu.ReadHolding(BmsRegisters.Realtime, 2),
                    ct,
                    TimeSpan.FromMilliseconds(1800));
                ushort[] words = ModbusRtu.ParseRead(rsp, 2);
                if (words.Length < 2 || words[0] != BmsRegisters.RealtimeMagic)
                    throw new IOException($"BMS Modbus probe failed: expected 0x{BmsRegisters.RealtimeMagic:X4} at 0x{BmsRegisters.Realtime:X4}.");

                Log?.Invoke($"[MODBUS] PROBE attempt={attempt}/3 ok magic=0x{words[0]:X4}; protocol=0x{words[1]:X4}");
                return;
            }
            catch (OperationCanceledException)
            {
                throw;
            }
            catch (Exception ex)
            {
                last = ex;
                Log?.Invoke($"[MODBUS] PROBE attempt={attempt}/3 failed type={ex.GetType().Name}; hresult=0x{ex.HResult:X8}; message={ex.Message}");
                if (attempt < 3)
                {
                    Log?.Invoke($"[MODBUS] PROBE attempt={attempt}/3 requesting full BLE/GATT reconnect before retry");
                    await _transport.ReconnectAsync(ct);
                    await Task.Delay(250, ct);
                }
            }
        }

        throw new IOException("BMS GATT was rebuilt between retries, but the application did not answer Modbus probe after 3 attempts.", last);
    }

    public async Task<ushort[]> ReadRegistersAsync(ushort start, ushort quantity, CancellationToken ct = default)
    {
        if (quantity is 0 or > 125) throw new ArgumentOutOfRangeException(nameof(quantity), "Modbus 0x03 quantity must be 1..125.");
        byte[] rsp = await TransactAsync(ModbusRtu.ReadHolding(start, quantity), ct);
        return ModbusRtu.ParseRead(rsp, quantity);
    }

    public async Task WriteSingleRegisterAsync(ushort register, ushort value, CancellationToken ct = default)
    {
        byte[] rsp = await TransactAsync(ModbusRtu.WriteSingle(register, value), ct);
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

    public Task<ushort[]> ReadProtectionAllAsync(CancellationToken ct = default) =>
        ReadRegistersAsync(BmsRegisters.Protect, BmsRegisters.ProtectCount, ct);

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

    public async Task<DeviceIdentity> ReadIdentityAsync(CancellationToken ct = default)
    {
        ushort[] mac = await ReadRegistersAsync(BmsRegisters.Mac, 3, ct);
        ushort[] sn = await ReadRegistersAsync(BmsRegisters.Serial, 16, ct);
        ushort[] hw = await ReadRegistersAsync(BmsRegisters.Hardware, 16, ct);
        ushort[] sw = await ReadRegistersAsync(BmsRegisters.Software, 16, ct);
        ushort[] name = await ReadRegistersAsync(BmsRegisters.BtName, BmsRegisters.BtNameReadWords, ct);
        byte[] macBytes = mac.SelectMany(w => new[] { (byte)(w >> 8), (byte)w }).Take(6).ToArray();
        return new DeviceIdentity(
            string.Join(":", macBytes.Select(b => b.ToString("X2"))),
            ModbusRtu.DecodeAscii(sn),
            ModbusRtu.DecodeAscii(hw),
            ModbusRtu.DecodeAscii(sw),
            ModbusRtu.DecodeAscii(name));
    }

    public async Task<BatterySnapshot> ReadBatteryAsync(CancellationToken ct = default)
    {
        ushort[] legacy = await ReadRegistersAsync(BmsRegisters.Legacy, 63, ct);
        ushort[] status = await ReadRegistersAsync(BmsRegisters.SystemStatus, 2, ct);
        ushort[] realtime = await ReadRegistersAsync(BmsRegisters.Realtime, 11, ct);
        bool rt = realtime.Length >= 11 && realtime[0] == BmsRegisters.RealtimeMagic;

        short chg = unchecked((short)legacy[50]);
        short dsg = unchecked((short)legacy[51]);
        short current = dsg > 0 ? (short)-dsg : chg;
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
            current = unchecked((short)realtime[3]);
            soc = realtime[4];
            maxTemp = realtime[5];
            minTemp = realtime[6];
            mosTemp = realtime[7];
            maxCell = realtime[8];
            minCell = realtime[9];
            delta = realtime[10];
        }

        return new BatterySnapshot
        {
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
            CellMillivolts = legacy.Take(BmsRegisters.SeriesCount).ToArray()
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

    private async Task<byte[]> TransactAsync(byte[] request, CancellationToken ct, TimeSpan? responseTimeout = null)
    {
        await _gate.WaitAsync(ct);
        try
        {
            var tcs = new TaskCompletionSource<byte[]>(TaskCreationOptions.RunContinuationsAsynchronously);
            lock (_rxLock)
            {
                _rx.Clear();
                _pending = tcs;
            }

            Log?.Invoke("TX " + Convert.ToHexString(request));
            await _transport.WriteAsync(request, ct);
            TimeSpan timeout = responseTimeout ?? TimeSpan.FromSeconds(4);
            try
            {
                byte[] rsp = await tcs.Task.WaitAsync(timeout, ct);
                Log?.Invoke("RX " + Convert.ToHexString(rsp));
                return rsp;
            }
            catch (TimeoutException ex)
            {
                Log?.Invoke($"[MODBUS] TIMEOUT timeoutMs={timeout.TotalMilliseconds:F0}; request={Convert.ToHexString(request)}; buffered={Convert.ToHexString(_rx.ToArray())}");
                throw new TimeoutException($"Modbus response timed out after {timeout.TotalMilliseconds:F0} ms.", ex);
            }
            finally
            {
                lock (_rxLock)
                {
                    if (ReferenceEquals(_pending, tcs)) _pending = null;
                    _rx.Clear();
                }
            }
        }
        finally
        {
            _gate.Release();
        }
    }

    private void OnData(ReadOnlyMemory<byte> fragment)
    {
        TaskCompletionSource<byte[]>? done = null;
        byte[]? frame = null;
        lock (_rxLock)
        {
            if (_pending is null)
            {
                Log?.Invoke("[MODBUS] unsolicited RX fragment=" + Convert.ToHexString(fragment.ToArray()));
                return;
            }

            _rx.AddRange(fragment.ToArray());
            int? expected = ModbusRtu.InferExpectedLength(_rx);
            Log?.Invoke($"[MODBUS] RX_FRAGMENT len={fragment.Length}; accumulated={_rx.Count}; expected={(expected?.ToString() ?? "unknown")}");
            if (expected is not null && _rx.Count >= expected.Value)
            {
                frame = _rx.Take(expected.Value).ToArray();
                done = _pending;
            }
        }

        if (frame is not null && done is not null)
        {
            try
            {
                ModbusRtu.ValidateFrame(frame);
                done.TrySetResult(frame);
            }
            catch (Exception ex)
            {
                done.TrySetException(ex);
            }
        }
    }

    public ValueTask DisposeAsync()
    {
        _transport.DataReceived -= OnData;
        _gate.Dispose();
        return ValueTask.CompletedTask;
    }
}
