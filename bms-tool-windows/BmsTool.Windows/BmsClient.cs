using System.Text;

namespace BmsTool.Windows;

public sealed record DeviceIdentity(string Mac, string Serial, string Hardware, string Software, string BluetoothName);

public sealed class BatterySnapshot
{
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
    public IReadOnlyList<ushort> CellMillivolts { get; init; } = Array.Empty<ushort>();
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

    public async Task<ushort[]> ReadRegistersAsync(ushort start, ushort quantity, CancellationToken ct = default)
    {
        byte[] rsp = await TransactAsync(ModbusRtu.ReadHolding(start, quantity), ct);
        return ModbusRtu.ParseRead(rsp, quantity);
    }

    public async Task<DeviceIdentity> ReadIdentityAsync(CancellationToken ct = default)
    {
        ushort[] mac = await ReadRegistersAsync(BmsRegisters.Mac, 3, ct);
        ushort[] sn = await ReadRegistersAsync(BmsRegisters.Serial, 16, ct);
        ushort[] hw = await ReadRegistersAsync(BmsRegisters.Hardware, 16, ct);
        ushort[] sw = await ReadRegistersAsync(BmsRegisters.Software, 16, ct);
        ushort[] name = await ReadRegistersAsync(BmsRegisters.BtName, BmsRegisters.BtNameReadWords, ct);
        byte[] macBytes = mac.SelectMany(w => new[] { (byte)(w >> 8), (byte)w }).Take(6).ToArray();
        return new DeviceIdentity(string.Join(":", macBytes.Select(b => b.ToString("X2"))), ModbusRtu.DecodeAscii(sn), ModbusRtu.DecodeAscii(hw), ModbusRtu.DecodeAscii(sw), ModbusRtu.DecodeAscii(name));
    }

    public async Task<BatterySnapshot> ReadBatteryAsync(CancellationToken ct = default)
    {
        ushort[] legacy = await ReadRegistersAsync(BmsRegisters.Legacy, 63, ct);
        ushort[] status = await ReadRegistersAsync(BmsRegisters.SystemStatus, 2, ct);
        ushort[] realtime = await ReadRegistersAsync(BmsRegisters.Realtime, 11, ct);
        bool rt = realtime.Length >= 11 && realtime[0] == BmsRegisters.RealtimeMagic;
        short chg = unchecked((short)legacy[50]); short dsg = unchecked((short)legacy[51]); short current = dsg > 0 ? (short)-dsg : chg;
        ushort voltage = legacy[37], soc = legacy[52], maxTemp = legacy[48], minTemp = legacy[49], mosTemp = legacy[47], maxCell = legacy[32], minCell = legacy[33], delta = legacy[36];
        if (rt) { voltage = realtime[2]; current = unchecked((short)realtime[3]); soc = realtime[4]; maxTemp = realtime[5]; minTemp = realtime[6]; mosTemp = realtime[7]; maxCell = realtime[8]; minCell = realtime[9]; delta = realtime[10]; }
        return new BatterySnapshot
        {
            PackVoltageV = voltage / 100.0, CurrentA = current / 10.0, SocPercent = soc, SohPercent = legacy[53],
            MaxTempC = maxTemp / 10.0 - 40.0, MinTempC = minTemp / 10.0 - 40.0, MosTempC = mosTemp / 10.0 - 40.0,
            MaxCellMv = maxCell, MinCellMv = minCell, CellDeltaMv = delta, MaxCellPosition = legacy[34], MinCellPosition = legacy[35],
            CycleCount = legacy[57], CapacityNowAh = legacy[54] / 100.0, CapacityFullAh = legacy[55] / 100.0, CapacityFactoryAh = legacy[56] / 100.0,
            SystemStatus = (uint)status[0] | ((uint)status[1] << 16), ProtocolVersion = rt ? realtime[1] : (ushort)0, UsesRealtimeWindow = rt,
            CellMillivolts = legacy.Take(BmsRegisters.SeriesCount).ToArray()
        };
    }

    public async Task<string> ReadBluetoothNameAsync(CancellationToken ct = default) => ModbusRtu.DecodeAscii(await ReadRegistersAsync(BmsRegisters.BtName, BmsRegisters.BtNameReadWords, ct));

    public async Task<string> WriteBluetoothNameSuffixAsync(string suffix, CancellationToken ct = default)
    {
        suffix = suffix.Trim(); if (suffix.StartsWith("BT_", StringComparison.OrdinalIgnoreCase)) suffix = suffix[3..];
        if (suffix.Length == 0) throw new ArgumentException("Bluetooth name suffix cannot be empty.");
        if (Encoding.ASCII.GetByteCount(suffix) > BmsRegisters.BtNameMaxSuffixBytesPerBleRequest) throw new ArgumentException("Current firmware BLE request supports at most 10 suffix bytes.");
        if (suffix.Any(c => !(char.IsAsciiLetterOrDigit(c) || c == '_' || c == '-'))) throw new ArgumentException("Suffix only supports letters, digits, '_' and '-'.");
        byte[] raw = Encoding.ASCII.GetBytes(suffix); if ((raw.Length & 1) != 0) Array.Resize(ref raw, raw.Length + 1);
        byte[] rsp = await TransactAsync(ModbusRtu.WriteMultiple(BmsRegisters.BtName, raw), ct);
        ModbusRtu.ValidateWriteMultipleAck(rsp, BmsRegisters.BtName, checked((ushort)(raw.Length / 2)));
        string readback = await ReadBluetoothNameAsync(ct); string expected = "BT_" + suffix;
        if (readback != expected) throw new IOException($"Bluetooth name readback mismatch: expected '{expected}', got '{readback}'.");
        return readback;
    }

    public async Task<string> ReadProtectionPreviewAsync(CancellationToken ct = default)
    {
        ushort[] words = await ReadRegistersAsync(BmsRegisters.Protect, 15, ct);
        return string.Join(Environment.NewLine, words.Select((v, i) => $"0x{BmsRegisters.Protect + i:X4} = {v} (0x{v:X4})"));
    }

    private async Task<byte[]> TransactAsync(byte[] request, CancellationToken ct)
    {
        await _gate.WaitAsync(ct);
        try
        {
            var tcs = new TaskCompletionSource<byte[]>(TaskCreationOptions.RunContinuationsAsynchronously);
            lock (_rxLock) { _rx.Clear(); _pending = tcs; }
            Log?.Invoke("TX " + Convert.ToHexString(request)); await _transport.WriteAsync(request, ct);
            try { byte[] rsp = await tcs.Task.WaitAsync(TimeSpan.FromSeconds(2), ct); Log?.Invoke("RX " + Convert.ToHexString(rsp)); return rsp; }
            finally { lock (_rxLock) { if (ReferenceEquals(_pending, tcs)) _pending = null; _rx.Clear(); } }
        }
        finally { _gate.Release(); }
    }

    private void OnData(ReadOnlyMemory<byte> fragment)
    {
        TaskCompletionSource<byte[]>? done = null; byte[]? frame = null;
        lock (_rxLock)
        {
            if (_pending is null) return; _rx.AddRange(fragment.ToArray()); int? expected = ModbusRtu.InferExpectedLength(_rx);
            if (expected is not null && _rx.Count >= expected.Value) { frame = _rx.Take(expected.Value).ToArray(); done = _pending; }
        }
        if (frame is not null && done is not null) { try { ModbusRtu.ValidateFrame(frame); done.TrySetResult(frame); } catch (Exception ex) { done.TrySetException(ex); } }
    }

    public ValueTask DisposeAsync() { _transport.DataReceived -= OnData; _gate.Dispose(); return ValueTask.CompletedTask; }
}
