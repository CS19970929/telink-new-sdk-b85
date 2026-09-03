using System.Buffers.Binary;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Globalization;
using System.IO;
using System.Runtime.CompilerServices;

namespace BmsTool.Windows;

public sealed class AfeParameterRow : INotifyPropertyChanged
{
    private string _currentValue = "—";
    private string _editValue = string.Empty;

    public required int WireIndex { get; init; }
    public required string Group { get; init; }
    public required string Name { get; init; }
    public required string Unit { get; init; }
    public required string Hint { get; init; }
    public required Func<ushort, string> Decode { get; init; }
    public required Func<string, (bool Ok, ushort Wire, string Error)> Encode { get; init; }

    public string CurrentValue
    {
        get => _currentValue;
        private set { _currentValue = value; OnPropertyChanged(); }
    }

    public string EditValue
    {
        get => _editValue;
        set { _editValue = value; OnPropertyChanged(); }
    }

    public void Load(ushort wire)
    {
        string text = Decode(wire);
        CurrentValue = text;
        EditValue = text;
    }

    public event PropertyChangedEventHandler? PropertyChanged;
    private void OnPropertyChanged([CallerMemberName] string? name = null) => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
}

public sealed class AfeHardwareParameterModel
{
    public const ushort BaseRegister = 0x2400;
    public const ushort RegisterCount = 24;

    private static readonly ushort[] OvUvDelayWire = [10,20,30,40,60,80,100,200,300,400,600,800,1000,2000,3000,4000];
    private static readonly ushort[] OccOcd2DelayWire = [1,2,4,6,8,10,20,40,60,80,100,200,400,800,1000,2000];
    private static readonly ushort[] Ocd1DelayWire = [5,10,20,40,60,80,100,200,400,600,800,1000,1500,2000,3000,4000];
    private static readonly ushort[] OccOcd1CurrentWire = [200,300,400,500,600,700,800,900,1000,1100,1200,1300,1400,1600,1800,2000];
    private static readonly ushort[] Ocd2CurrentWire = [300,400,500,600,700,800,900,1000,1200,1400,1600,1800,2000,3000,4000,5000];
    private static readonly ushort[] ShortCurrentWire = [50,80,110,140,170,200,230,260,290,320,350,400,500,600,800,1000];
    private static readonly ushort[] ShortDelayWire = [0,64,128,192,256,320,384,448,512,576,640,704,768,832,896,960];

    private ushort[] _deviceRaw = new ushort[RegisterCount];

    public ObservableCollection<AfeParameterRow> Rows { get; } = new();
    public IReadOnlyList<ushort> DeviceRaw => _deviceRaw;

    public AfeHardwareParameterModel()
    {
        Rows.Add(VoltageRow(0, "单体过压", "保护电压", "mV", 3600, 4500, 5));
        Rows.Add(VoltageRow(1, "单体过压", "恢复电压", "mV", 3300, 4500, 5));
        Rows.Add(DelayRow(2, "单体过压", "保护延时", OvUvDelayWire));

        Rows.Add(VoltageRow(3, "单体欠压", "保护电压", "mV", 2000, 3100, 20));
        Rows.Add(VoltageRow(4, "单体欠压", "恢复电压", "mV", 2000, 3600, 20));
        Rows.Add(DelayRow(5, "单体欠压", "保护延时", OvUvDelayWire));

        Rows.Add(CurrentRow(6, "充电过流", "硬件阈值", OccOcd1CurrentWire, "20~200A 离散档位（当前 D3PRO）"));
        Rows.Add(DelayRow(7, "充电过流", "硬件延时", OccOcd2DelayWire));

        Rows.Add(CurrentRow(10, "放电过流1", "硬件阈值", OccOcd1CurrentWire, "20~200A 离散档位（当前 D3PRO）"));
        Rows.Add(DelayRow(11, "放电过流1", "硬件延时", Ocd1DelayWire));

        Rows.Add(CurrentRow(12, "放电过流2", "硬件阈值", Ocd2CurrentWire, "30~500A 离散档位（当前 D3PRO）"));
        Rows.Add(DelayRow(13, "放电过流2", "硬件延时", OccOcd2DelayWire));

        Rows.Add(TemperatureRow(14, "充电高温", "保护温度", 45, 70));
        Rows.Add(TemperatureRow(15, "充电高温", "恢复温度", 40, 70));
        Rows.Add(TemperatureRow(16, "充电低温", "保护温度", -20, 10));
        Rows.Add(TemperatureRow(17, "充电低温", "恢复温度", -20, 15));
        Rows.Add(TemperatureRow(18, "放电高温", "保护温度", 45, 80));
        Rows.Add(TemperatureRow(19, "放电高温", "恢复温度", 40, 80));
        Rows.Add(TemperatureRow(20, "放电低温", "保护温度", -40, 10));
        Rows.Add(TemperatureRow(21, "放电低温", "恢复温度", -40, 15));

        Rows.Add(DiscreteDirectRow(22, "短路保护", "短路电流", "A", ShortCurrentWire, "SH367309 短路电流离散档位"));
        Rows.Add(DiscreteDirectRow(23, "短路保护", "短路延时", "us", ShortDelayWire, "0~960us，64us/档"));
    }

    public void Load(ushort[] raw)
    {
        if (raw.Length != RegisterCount) throw new ArgumentException("AFE parameter block must contain 24 registers.");
        _deviceRaw = raw.ToArray();
        foreach (AfeParameterRow row in Rows) row.Load(raw[row.WireIndex]);
    }

    public void ResetEditsToCurrent()
    {
        foreach (AfeParameterRow row in Rows) row.Load(_deviceRaw[row.WireIndex]);
    }

    public bool TryBuildCandidate(out ushort[] raw, out string error)
    {
        raw = _deviceRaw.ToArray();
        foreach (AfeParameterRow row in Rows)
        {
            var encoded = row.Encode(row.EditValue);
            if (!encoded.Ok)
            {
                error = $"{row.Group} / {row.Name}: {encoded.Error}";
                return false;
            }
            raw[row.WireIndex] = encoded.Wire;
        }

        // SH367309 has only one charge-OCP threshold/delay. 0x2408/0x2409 are protocol aliases.
        raw[8] = raw[6];
        raw[9] = raw[7];

        if (raw[1] >= raw[0]) { error = "单体过压恢复电压必须小于保护电压。"; return false; }
        if (raw[4] <= raw[3]) { error = "单体欠压恢复电压必须大于保护电压。"; return false; }
        if (raw[15] >= raw[14]) { error = "充电高温恢复温度必须低于保护温度。"; return false; }
        if (raw[17] <= raw[16]) { error = "充电低温恢复温度必须高于保护温度。"; return false; }
        if (raw[19] >= raw[18]) { error = "放电高温恢复温度必须低于保护温度。"; return false; }
        if (raw[21] <= raw[20]) { error = "放电低温恢复温度必须高于保护温度。"; return false; }

        error = string.Empty;
        return true;
    }

    public IReadOnlyList<AfeWriteGroup> GetChangedWriteGroups(ushort[] candidate)
    {
        if (candidate.Length != RegisterCount) throw new ArgumentException("AFE candidate must contain 24 registers.");
        var definitions = new (int Start, int Count, string Name)[]
        {
            (0,3,"单体过压"), (3,3,"单体欠压"), (6,4,"充电过流"),
            (10,2,"放电过流1"), (12,2,"放电过流2"),
            (14,2,"充电高温"), (16,2,"充电低温"),
            (18,2,"放电高温"), (20,2,"放电低温"), (22,2,"短路保护")
        };
        var groups = new List<AfeWriteGroup>();
        foreach (var d in definitions)
        {
            bool changed = false;
            for (int i = 0; i < d.Count; i++)
            {
                if (_deviceRaw[d.Start + i] != candidate[d.Start + i]) { changed = true; break; }
            }
            if (changed)
                groups.Add(new AfeWriteGroup((ushort)(BaseRegister + d.Start), candidate.Skip(d.Start).Take(d.Count).ToArray(), d.Name));
        }
        return groups;
    }

    private static AfeParameterRow VoltageRow(int index, string group, string name, string unit, int min, int max, int step) => new()
    {
        WireIndex = index, Group = group, Name = name, Unit = unit,
        Hint = $"{min}~{max}{unit}，{step}{unit}/step",
        Decode = v => v.ToString(CultureInfo.InvariantCulture),
        Encode = s => EncodeIntRange(s, min, max, step, 1)
    };

    private static AfeParameterRow DelayRow(int index, string group, string name, ushort[] allowedWire) => new()
    {
        WireIndex = index, Group = group, Name = name, Unit = "ms",
        Hint = "SH367309 离散延时档位",
        Decode = v => (v * 10u).ToString(CultureInfo.InvariantCulture),
        Encode = s =>
        {
            if (!TryParseInteger(s, out int ms)) return Fail("请输入整数毫秒值。");
            if (ms < 0 || (ms % 10) != 0) return Fail("延时必须是 10ms 的整数倍并且属于芯片离散档位。");
            int wire = ms / 10;
            if (!allowedWire.Contains((ushort)wire)) return Fail("该延时不是 SH367309 支持的离散档位。");
            return Ok((ushort)wire);
        }
    };

    private static AfeParameterRow CurrentRow(int index, string group, string name, ushort[] allowedWire, string hint) => new()
    {
        WireIndex = index, Group = group, Name = name, Unit = "A", Hint = hint,
        Decode = v => (v / 10.0).ToString("0.0", CultureInfo.InvariantCulture),
        Encode = s =>
        {
            if (!double.TryParse(s.Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out double a)) return Fail("请输入电流值(A)。");
            int wire = (int)Math.Round(a * 10.0, MidpointRounding.AwayFromZero);
            if (Math.Abs(a * 10.0 - wire) > 0.0001 || wire < 0 || wire > ushort.MaxValue) return Fail("电流必须精确到 0.1A。 ");
            if (!allowedWire.Contains((ushort)wire)) return Fail("该电流不是当前 D3PRO/SH367309 支持的离散档位。");
            return Ok((ushort)wire);
        }
    };

    private static AfeParameterRow TemperatureRow(int index, string group, string name, int minC, int maxC) => new()
    {
        WireIndex = index, Group = group, Name = name, Unit = "℃",
        Hint = $"{minC}~{maxC}℃，1℃/step",
        Decode = v => (v / 10.0 - 40.0).ToString("0", CultureInfo.InvariantCulture),
        Encode = s =>
        {
            if (!TryParseInteger(s, out int c)) return Fail("请输入整数摄氏温度。");
            if (c < minC || c > maxC) return Fail($"允许范围 {minC}~{maxC}℃。");
            return Ok(checked((ushort)((c + 40) * 10)));
        }
    };

    private static AfeParameterRow DiscreteDirectRow(int index, string group, string name, string unit, ushort[] allowed, string hint) => new()
    {
        WireIndex = index, Group = group, Name = name, Unit = unit, Hint = hint,
        Decode = v => v.ToString(CultureInfo.InvariantCulture),
        Encode = s =>
        {
            if (!TryParseInteger(s, out int value) || value < 0 || value > ushort.MaxValue) return Fail("请输入有效整数。");
            if (!allowed.Contains((ushort)value)) return Fail("该值不是 SH367309 支持的离散档位。");
            return Ok((ushort)value);
        }
    };

    private static (bool Ok, ushort Wire, string Error) EncodeIntRange(string s, int min, int max, int step, int wireScale)
    {
        if (!TryParseInteger(s, out int value)) return Fail("请输入整数值。");
        if (value < min || value > max) return Fail($"允许范围 {min}~{max}。");
        if (((value - min) % step) != 0) return Fail($"必须满足 {step} 的步进。");
        return Ok(checked((ushort)(value * wireScale)));
    }

    private static bool TryParseInteger(string s, out int value) => int.TryParse(s.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out value);
    private static (bool Ok, ushort Wire, string Error) Ok(ushort wire) => (true, wire, string.Empty);
    private static (bool Ok, ushort Wire, string Error) Fail(string error) => (false, 0, error);
}

public sealed record AfeWriteGroup(ushort StartRegister, ushort[] Values, string Name);

public sealed class AfeHardwareClient
{
    private readonly BmsClient _bms;
    private readonly BmsBleTransport _transport;

    public AfeHardwareClient(BmsClient bms, BmsBleTransport transport)
    {
        _bms = bms;
        _transport = transport;
    }

    public Task<ushort[]> ReadAllAsync(CancellationToken ct = default) =>
        _bms.ReadRegistersAsync(AfeHardwareParameterModel.BaseRegister, AfeHardwareParameterModel.RegisterCount, ct);

    public async Task WriteGroupAsync(AfeWriteGroup group, CancellationToken ct = default)
    {
        if (group.Values.Length is < 1 or > 5)
            throw new ArgumentOutOfRangeException(nameof(group), "BLE/Modbus atomic write group must contain 1..5 registers at MTU 23.");

        byte[] raw = new byte[group.Values.Length * 2];
        for (int i = 0; i < group.Values.Length; i++)
            BinaryPrimitives.WriteUInt16BigEndian(raw.AsSpan(i * 2, 2), group.Values[i]);

        byte[] request = ModbusRtu.WriteMultiple(group.StartRegister, raw);
        if (request.Length > 20)
            throw new IOException($"AFE write request is {request.Length} bytes; it exceeds the 20-byte payload supported at ATT MTU 23.");

        byte[] response = await RawTransactionAsync(request, ct);
        ModbusRtu.ValidateWriteMultipleAck(response, group.StartRegister, checked((ushort)group.Values.Length));
    }

    private async Task<byte[]> RawTransactionAsync(byte[] request, CancellationToken ct)
    {
        var gate = new object();
        var rx = new List<byte>();
        var tcs = new TaskCompletionSource<byte[]>(TaskCreationOptions.RunContinuationsAsynchronously);

        void OnData(ReadOnlyMemory<byte> fragment)
        {
            lock (gate)
            {
                if (tcs.Task.IsCompleted) return;
                rx.AddRange(fragment.ToArray());
                int? expected = ModbusRtu.InferExpectedLength(rx);
                if (expected is not null && rx.Count >= expected.Value)
                {
                    byte[] frame = rx.Take(expected.Value).ToArray();
                    try
                    {
                        ModbusRtu.ValidateFrame(frame);
                        tcs.TrySetResult(frame);
                    }
                    catch (Exception ex)
                    {
                        tcs.TrySetException(ex);
                    }
                }
            }
        }

        _transport.DataReceived += OnData;
        try
        {
            await _transport.WriteAsync(request, ct);
            return await tcs.Task.WaitAsync(TimeSpan.FromSeconds(4), ct);
        }
        finally
        {
            _transport.DataReceived -= OnData;
        }
    }
}
