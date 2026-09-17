using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Globalization;
using System.IO;
using System.Runtime.CompilerServices;

namespace BmsTool.Windows;

public static class AfeHardwareProtocolMap
{
    public const ushort RequestedBase = 0x2500;
    public const ushort ProfileWordCount = 35;
    public const ushort MetadataBase = 0x2523;
    public const ushort MetadataWordCount = 9;
    public const ushort EffectiveBase = 0x2540;
    public const ushort EffectiveWordCount = ProfileWordCount;
    public const ushort InterfaceVersion = 0x0002;

    public const ushort ModelDvc1124 = 0x1124;
    public const ushort ModelSh3673510 = 0x3510;

    public const ushort CapCov = 1 << 0;
    public const ushort CapCuv = 1 << 1;
    public const ushort CapOcd1 = 1 << 2;
    public const ushort CapOcd2 = 1 << 3;
    public const ushort CapOcc1 = 1 << 4;
    public const ushort CapOcc2 = 1 << 5;
    public const ushort CapSc = 1 << 6;
    public const ushort CapTemp = 1 << 7;

    public static string BackendName(ushort model) => model switch
    {
        ModelDvc1124 => "DVC1124",
        ModelSh3673510 => "SH35xx (SH3673510/SH3673520 backend)",
        _ => $"Unknown 0x{model:X4}"
    };
}

public sealed record AfeHardwareDeviceInfo(
    ushort BackendModel,
    ushort Capabilities,
    bool ProfileValid,
    ushort ShuntMicroOhm,
    ushort CellCount,
    ushort WatchdogSeconds,
    bool AccessActive,
    ushort ApplyState,
    ushort LastError,
    ushort InterfaceVersion)
{
    public string BackendName => AfeHardwareProtocolMap.BackendName(BackendModel);
    public string ApplyStateText => ApplyState switch
    {
        0 => "Idle",
        1 => "OK",
        2 => "Rollback OK",
        3 => "CONFIG_INCONSISTENT",
        _ => $"Unknown({ApplyState})"
    };
    public string ErrorText => LastError switch
    {
        0 => "None",
        1 => "Authorization",
        2 => "Validation",
        3 => "Persistence",
        4 => "Apply/verify",
        5 => "Rollback",
        _ => $"Unknown({LastError})"
    };
}

public sealed record AfeHardwareSnapshot(
    ushort[] Requested,
    ushort[] Effective,
    AfeHardwareDeviceInfo Info);

public sealed class AfeParameterRow : INotifyPropertyChanged
{
    private string _requestedValue = "—";
    private string _effectiveValue = "—";
    private string _editValue = string.Empty;
    private string _enabledText = "—";

    public required int WireIndex { get; init; }
    public required ushort CapabilityMask { get; init; }
    public required string Group { get; init; }
    public required string Name { get; init; }
    public required string Unit { get; init; }
    public required string Hint { get; init; }
    public required Func<ushort, string> Decode { get; init; }
    public required Func<string, (bool Ok, ushort Wire, string Error)> Encode { get; init; }

    public string RequestedValue
    {
        get => _requestedValue;
        private set { _requestedValue = value; OnPropertyChanged(); }
    }

    public string EffectiveValue
    {
        get => _effectiveValue;
        private set { _effectiveValue = value; OnPropertyChanged(); }
    }

    public string EditValue
    {
        get => _editValue;
        set { _editValue = value; OnPropertyChanged(); }
    }

    public string EnabledText
    {
        get => _enabledText;
        private set { _enabledText = value; OnPropertyChanged(); }
    }

    public void Load(ushort requested, ushort effective, ushort enableMask)
    {
        RequestedValue = Decode(requested);
        EffectiveValue = Decode(effective);
        EditValue = RequestedValue;
        EnabledText = CapabilityMask == 0 ? "—" : ((enableMask & CapabilityMask) != 0 ? "启用" : "关闭");
    }

    public event PropertyChangedEventHandler? PropertyChanged;
    private void OnPropertyChanged([CallerMemberName] string? name = null) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
}

public sealed class AfeHardwareParameterModel
{
    private ushort[] _requested = new ushort[AfeHardwareProtocolMap.ProfileWordCount];
    private ushort[] _effective = new ushort[AfeHardwareProtocolMap.ProfileWordCount];
    private AfeHardwareDeviceInfo? _deviceInfo;

    public ObservableCollection<AfeParameterRow> Rows { get; } = new();
    public IReadOnlyList<ushort> RequestedRaw => _requested;
    public AfeHardwareDeviceInfo? DeviceInfo => _deviceInfo;

    public void Load(AfeHardwareSnapshot snapshot)
    {
        if (snapshot.Requested.Length != AfeHardwareProtocolMap.ProfileWordCount ||
            snapshot.Effective.Length != AfeHardwareProtocolMap.ProfileWordCount)
            throw new ArgumentException("AFE hardware profile must contain exactly 35 words.");
        if (!snapshot.Info.ProfileValid)
            throw new IOException("Device reports an invalid AFE hardware profile.");
        if (snapshot.Info.InterfaceVersion < AfeHardwareProtocolMap.InterfaceVersion)
            throw new IOException($"AFE hardware interface version {snapshot.Info.InterfaceVersion} is too old; v2 or newer is required.");
        if (snapshot.Requested[0] != 1)
            throw new IOException($"Unsupported AFE hardware profile schema {snapshot.Requested[0]}.");
        if (snapshot.Requested[1] != snapshot.Info.BackendModel)
            throw new IOException("AFE hardware profile backend ID does not match device metadata.");

        bool rebuild = _deviceInfo is null ||
                       _deviceInfo.BackendModel != snapshot.Info.BackendModel ||
                       _deviceInfo.Capabilities != snapshot.Info.Capabilities;
        _deviceInfo = snapshot.Info;
        _requested = snapshot.Requested.ToArray();
        _effective = snapshot.Effective.ToArray();
        if (rebuild) BuildRows(snapshot.Info.Capabilities);

        ushort enableMask = _requested[34];
        foreach (AfeParameterRow row in Rows)
            row.Load(_requested[row.WireIndex], _effective[row.WireIndex], enableMask);
    }

    public void ResetEditsToCurrent()
    {
        ushort enableMask = _requested[34];
        foreach (AfeParameterRow row in Rows)
            row.Load(_requested[row.WireIndex], _effective[row.WireIndex], enableMask);
    }

    public bool TryBuildCandidate(out ushort[] raw, out string error)
    {
        raw = _requested.ToArray();
        if (_deviceInfo is null)
        {
            error = "请先读取设备 AFE 硬件参数。";
            return false;
        }

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

        if (raw[0] != 1 || raw[1] != _deviceInfo.BackendModel)
        {
            error = "Schema/backend identity is immutable.";
            return false;
        }
        if ((raw[34] & ~_deviceInfo.Capabilities) != 0)
        {
            error = "Enable mask contains a capability unsupported by this AFE.";
            return false;
        }

        ushort en = raw[34];
        if ((en & AfeHardwareProtocolMap.CapCov) != 0 && (raw[2] == 0 || raw[4] >= raw[2]))
        { error = "单体过压恢复值必须小于保护值。"; return false; }
        if ((en & AfeHardwareProtocolMap.CapCuv) != 0 && (raw[6] == 0 || raw[8] <= raw[6]))
        { error = "单体欠压恢复值必须大于保护值。"; return false; }
        if ((en & AfeHardwareProtocolMap.CapOcd1) != 0 && (raw[10] == 0 || raw[14] >= raw[10]))
        { error = "放电过流恢复值必须小于 OCD1 阈值。"; return false; }
        if ((en & AfeHardwareProtocolMap.CapOcd2) != 0 && (raw[12] == 0 || raw[14] >= raw[12]))
        { error = "放电过流恢复值必须小于 OCD2 阈值。"; return false; }
        if ((en & AfeHardwareProtocolMap.CapOcc1) != 0 && (raw[16] == 0 || raw[20] >= raw[16]))
        { error = "充电过流恢复值必须小于 OCC1 阈值。"; return false; }
        if ((en & AfeHardwareProtocolMap.CapOcc2) != 0 && (raw[18] == 0 || raw[20] >= raw[18]))
        { error = "充电过流恢复值必须小于 OCC2 阈值。"; return false; }
        if ((en & AfeHardwareProtocolMap.CapTemp) != 0)
        {
            if (raw[26] >= raw[25] || raw[30] >= raw[29] || raw[28] <= raw[27] || raw[32] <= raw[31])
            { error = "硬件温度保护恢复阈值与触发阈值的迟滞关系不合法。"; return false; }
        }

        if ((en & AfeHardwareProtocolMap.CapSc)!=0 && _deviceInfo.BackendModel==AfeHardwareProtocolMap.ModelDvc1124) {
            uint senseUv=(uint)raw[22]*_deviceInfo.ShuntMicroOhm/10u;
            if (_deviceInfo.ShuntMicroOhm==0 || senseUv<10000u || senseUv>630000u || raw[23]<8 || raw[23]>1999) {
                error="DVC 短路启用需有效电流（分流压降 10~630 mV）及 8~1999 us 延时；按芯片档位向下量化，请核对有效值。";return false;
            }
        }
        error = string.Empty;
        return true;
    }

    public bool HasChanges(ushort[] candidate) => !_requested.SequenceEqual(candidate);

    private void BuildRows(ushort caps)
    {
        Rows.Clear();
        if ((caps & AfeHardwareProtocolMap.CapCov) != 0)
        {
            Rows.Add(U16Row(2, AfeHardwareProtocolMap.CapCov, "单体过压", "保护电压", "mV", 1, 6000));
            Rows.Add(U16Row(4, AfeHardwareProtocolMap.CapCov, "单体过压", "恢复电压", "mV", 1, 6000));
            Rows.Add(U16Row(3, AfeHardwareProtocolMap.CapCov, "单体过压", "保护延时", "ms", 0, ushort.MaxValue));
            Rows.Add(U16Row(5, AfeHardwareProtocolMap.CapCov, "单体过压", "恢复确认", "ms", 0, ushort.MaxValue));
        }
        if ((caps & AfeHardwareProtocolMap.CapCuv) != 0)
        {
            Rows.Add(U16Row(6, AfeHardwareProtocolMap.CapCuv, "单体欠压", "保护电压", "mV", 1, 6000));
            Rows.Add(U16Row(8, AfeHardwareProtocolMap.CapCuv, "单体欠压", "恢复电压", "mV", 1, 6000));
            Rows.Add(U16Row(7, AfeHardwareProtocolMap.CapCuv, "单体欠压", "保护延时", "ms", 0, ushort.MaxValue));
            Rows.Add(U16Row(9, AfeHardwareProtocolMap.CapCuv, "单体欠压", "恢复确认", "ms", 0, ushort.MaxValue));
        }
        if ((caps & AfeHardwareProtocolMap.CapOcd1) != 0)
        {
            Rows.Add(CurrentRow(10, AfeHardwareProtocolMap.CapOcd1, "放电过流", "OCD1 阈值"));
            Rows.Add(U16Row(11, AfeHardwareProtocolMap.CapOcd1, "放电过流", "OCD1 延时", "ms", 0, ushort.MaxValue));
        }
        if ((caps & AfeHardwareProtocolMap.CapOcd2) != 0)
        {
            Rows.Add(CurrentRow(12, AfeHardwareProtocolMap.CapOcd2, "放电过流", "OCD2 阈值"));
            Rows.Add(U16Row(13, AfeHardwareProtocolMap.CapOcd2, "放电过流", "OCD2 延时", "ms", 0, ushort.MaxValue));
        }
        if ((caps & (AfeHardwareProtocolMap.CapOcd1 | AfeHardwareProtocolMap.CapOcd2)) != 0)
        {
            ushort cap = (ushort)(AfeHardwareProtocolMap.CapOcd1 | AfeHardwareProtocolMap.CapOcd2);
            Rows.Add(CurrentRow(14, cap, "放电过流", "恢复电流"));
            Rows.Add(U16Row(15, cap, "放电过流", "恢复确认", "ms", 0, ushort.MaxValue));
        }
        if ((caps & AfeHardwareProtocolMap.CapOcc1) != 0)
        {
            Rows.Add(CurrentRow(16, AfeHardwareProtocolMap.CapOcc1, "充电过流", "OCC1 阈值"));
            Rows.Add(U16Row(17, AfeHardwareProtocolMap.CapOcc1, "充电过流", "OCC1 延时", "ms", 0, ushort.MaxValue));
        }
        if ((caps & AfeHardwareProtocolMap.CapOcc2) != 0)
        {
            Rows.Add(CurrentRow(18, AfeHardwareProtocolMap.CapOcc2, "充电过流", "OCC2 阈值"));
            Rows.Add(U16Row(19, AfeHardwareProtocolMap.CapOcc2, "充电过流", "OCC2 延时", "ms", 0, ushort.MaxValue));
        }
        if ((caps & (AfeHardwareProtocolMap.CapOcc1 | AfeHardwareProtocolMap.CapOcc2)) != 0)
        {
            ushort cap = (ushort)(AfeHardwareProtocolMap.CapOcc1 | AfeHardwareProtocolMap.CapOcc2);
            Rows.Add(CurrentRow(20, cap, "充电过流", "恢复电流"));
            Rows.Add(U16Row(21, cap, "充电过流", "恢复确认", "ms", 0, ushort.MaxValue));
        }
        if ((caps & AfeHardwareProtocolMap.CapSc) != 0)
        {
            Rows.Add(new AfeParameterRow {
                WireIndex=34, CapabilityMask=AfeHardwareProtocolMap.CapSc,
                Group="短路保护", Name="SCD 使能", Unit="0/1",
                Hint="0=关闭，1=启用；仅改变短路位，其他保护使能保持不变。",
                Decode=v=>(v&AfeHardwareProtocolMap.CapSc)!=0?"1":"0",
                Encode=text=>text.Trim() switch {
                    "0"=>(true,(ushort)(_requested[34]&~AfeHardwareProtocolMap.CapSc),string.Empty),
                    "1"=>(true,(ushort)(_requested[34]|AfeHardwareProtocolMap.CapSc),string.Empty),
                    _=>(false,(ushort)0,"短路使能只能输入 0 或 1。")
                }
            });
            Rows.Add(CurrentRow(22, AfeHardwareProtocolMap.CapSc, "短路保护", "短路电流"));
            Rows.Add(U16Row(23, AfeHardwareProtocolMap.CapSc, "短路保护", "短路延时", "us", 0, ushort.MaxValue));
            Rows.Add(U16Row(24, AfeHardwareProtocolMap.CapSc, "短路保护", "恢复确认", "ms", 0, ushort.MaxValue));
        }
        if ((caps & AfeHardwareProtocolMap.CapTemp) != 0)
        {
            Rows.Add(TemperatureRow(25, AfeHardwareProtocolMap.CapTemp, "充电温度", "高温保护"));
            Rows.Add(TemperatureRow(26, AfeHardwareProtocolMap.CapTemp, "充电温度", "高温恢复"));
            Rows.Add(TemperatureRow(27, AfeHardwareProtocolMap.CapTemp, "充电温度", "低温保护"));
            Rows.Add(TemperatureRow(28, AfeHardwareProtocolMap.CapTemp, "充电温度", "低温恢复"));
            Rows.Add(TemperatureRow(29, AfeHardwareProtocolMap.CapTemp, "放电温度", "高温保护"));
            Rows.Add(TemperatureRow(30, AfeHardwareProtocolMap.CapTemp, "放电温度", "高温恢复"));
            Rows.Add(TemperatureRow(31, AfeHardwareProtocolMap.CapTemp, "放电温度", "低温保护"));
            Rows.Add(TemperatureRow(32, AfeHardwareProtocolMap.CapTemp, "放电温度", "低温恢复"));
            Rows.Add(U16Row(33, AfeHardwareProtocolMap.CapTemp, "温度保护", "恢复确认", "ms", 0, ushort.MaxValue));
        }
    }

    private static AfeParameterRow U16Row(int index, ushort cap, string group, string name, string unit, int min, int max) => new()
    {
        WireIndex = index,
        CapabilityMask = cap,
        Group = group,
        Name = name,
        Unit = unit,
        Hint = "语义值；芯片离散档位由固件量化，实际值见“AFE有效值”。",
        Decode = v => v.ToString(CultureInfo.InvariantCulture),
        Encode = s =>
        {
            if (!int.TryParse(s.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out int value) || value < min || value > max)
                return (false, (ushort)0, $"请输入 {min}~{max} 的整数。");
            return (true, (ushort)value, string.Empty);
        }
    };

    private static AfeParameterRow CurrentRow(int index, ushort cap, string group, string name) => new()
    {
        WireIndex = index,
        CapabilityMask = cap,
        Group = group,
        Name = name,
        Unit = "A",
        Hint = "0.1A/LSB 语义值；实际硬件量化结果见“AFE有效值”。",
        Decode = v => (v / 10.0).ToString("0.0", CultureInfo.InvariantCulture),
        Encode = s =>
        {
            if (!double.TryParse(s.Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out double value) || value < 0 || value > 6553.5)
                return (false, (ushort)0, "请输入合法电流值(A)。");
            int wire = (int)Math.Round(value * 10.0, MidpointRounding.AwayFromZero);
            if (Math.Abs(value * 10.0 - wire) > 0.0001)
                return (false, (ushort)0, "电流最多保留 0.1A。 ");
            return (true, checked((ushort)wire), string.Empty);
        }
    };

    private static AfeParameterRow TemperatureRow(int index, ushort cap, string group, string name) => new()
    {
        WireIndex = index,
        CapabilityMask = cap,
        Group = group,
        Name = name,
        Unit = "℃",
        Hint = "设备编码为 (℃+40)×10；具体 AFE 可实现范围由固件校验。",
        Decode = v => (v / 10.0 - 40.0).ToString("0.0", CultureInfo.InvariantCulture),
        Encode = s =>
        {
            if (!double.TryParse(s.Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out double c) || c < -40 || c > 105)
                return (false, (ushort)0, "请输入 -40~105℃。");
            double raw = (c + 40.0) * 10.0;
            int wire = (int)Math.Round(raw, MidpointRounding.AwayFromZero);
            if (Math.Abs(raw - wire) > 0.0001)
                return (false, (ushort)0, "温度最多保留 0.1℃。 ");
            return (true, checked((ushort)wire), string.Empty);
        }
    };
}

public sealed class AfeHardwareClient
{
    private readonly BmsClient _bms;

    public AfeHardwareClient(BmsClient bms) => _bms = bms;
    public AfeHardwareClient(BmsClient bms, IBmsTransport _) : this(bms) { }

    public async Task<AfeHardwareSnapshot> ReadAllAsync(CancellationToken ct = default)
    {
        ushort[] requested = await _bms.ReadRegistersAsync(AfeHardwareProtocolMap.RequestedBase, AfeHardwareProtocolMap.ProfileWordCount, ct);
        ushort[] meta = await _bms.ReadRegistersAsync(AfeHardwareProtocolMap.MetadataBase, AfeHardwareProtocolMap.MetadataWordCount, ct);
        ushort[] effective = await _bms.ReadRegistersAsync(AfeHardwareProtocolMap.EffectiveBase, AfeHardwareProtocolMap.EffectiveWordCount, ct);
        var info = new AfeHardwareDeviceInfo(
            requested[1], meta[0], meta[1] != 0, meta[2], meta[3], meta[4],
            meta[5] != 0, meta[6], meta[7], meta[8]);
        return new AfeHardwareSnapshot(requested, effective, info);
    }

    public async Task<AfeHardwareSnapshot> WriteAllAsync(ushort[] candidate, CancellationToken ct = default)
    {
        if (candidate.Length != AfeHardwareProtocolMap.ProfileWordCount)
            throw new ArgumentException("AFE hardware profile write must contain exactly 35 words.", nameof(candidate));

        AfeHardwareAccessSession? session = null;
        try
        {
            session = await _bms.OpenAfeHardwareAccessAsync(ct);
            if (session.BackendModel != candidate[1])
                throw new IOException($"AFE access backend mismatch: session=0x{session.BackendModel:X4}, profile=0x{candidate[1]:X4}.");

            await _bms.WriteAfeProfileAsync(candidate, session, ct);
            AfeHardwareSnapshot readback = await ReadAllAsync(ct);
            if (!readback.Requested.SequenceEqual(candidate))
            {
                int mismatch = Enumerable.Range(0, candidate.Length).First(i => candidate[i] != readback.Requested[i]);
                throw new IOException($"AFE硬件参数回读不一致：word[{mismatch}] target={candidate[mismatch]}, actual={readback.Requested[mismatch]}。");
            }
            if (readback.Info.ApplyState != 1 || readback.Info.LastError != 0)
                throw new IOException($"AFE apply state={readback.Info.ApplyStateText}, error={readback.Info.ErrorText}.");
            return readback;
        }
        finally
        {
            if (session is not null)
                await _bms.TryCloseAfeHardwareAccessAsync(session.Token, CancellationToken.None);
        }
    }
}
