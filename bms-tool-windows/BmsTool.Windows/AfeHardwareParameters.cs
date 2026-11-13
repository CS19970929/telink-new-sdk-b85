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
    public const ushort ModelSh3673520 = 0x3520;
    public static bool IsSh(ushort model) => model is ModelSh3673510 or ModelSh3673520;

    public const ushort CapCov = 1 << 0;
    public const ushort CapCuv = 1 << 1;
    public const ushort CapOcd1 = 1 << 2;
    public const ushort CapOcd2 = 1 << 3;
    public const ushort CapOcc1 = 1 << 4;
    public const ushort CapOcc2 = 1 << 5;
    public const ushort CapSc = 1 << 6;
    public const ushort CapTemp = 1 << 7;
    public const ushort CapTs1 = 1 << 8;
    public const ushort CapTs2 = 1 << 9;
    public const ushort CapTs3 = 1 << 10;
    public const ushort CapTs4 = 1 << 11;
    public const ushort TempChannelSelect = 1 << 12;
    public const ushort TempChannelMask = 0x0f00;

    public static ushort TemperatureChannels(ushort enableMask) =>
        (enableMask & CapTemp) == 0 ? (ushort)0 :
        (enableMask & TempChannelSelect) == 0 ? (ushort)(CapTs1 | CapTs2) :
        (ushort)(enableMask & TempChannelMask);

    public static string BackendName(ushort model) => model switch
    {
        ModelDvc1124 => "DVC1124",
        ModelSh3673520 => "D015 / SH3673520",
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
    public ushort HardwareFeatures { get; init; }
    public ushort[]? FeatureStatus { get; init; }
    public ushort ShuntElementMicroOhm { get; init; }
    public byte ShuntParallelCount { get; init; }
    public bool HasExactShunt => ShuntElementMicroOhm != 0 && ShuntParallelCount is >= 1 and <= 16;
    public string ShuntDisplay => HasExactShunt
        ? $"{ShuntElementMicroOhm}/{ShuntParallelCount}uΩ ({(decimal)ShuntElementMicroOhm / ShuntParallelCount / 1000m:0.#########}mΩ)"
        : BackendModel == AfeHardwareProtocolMap.ModelDvc1124 ? $"{ShuntMicroOhm}uΩ"
        : $"{ShuntMicroOhm}uΩ（旧固件整数模型，精确比例未知）";
    public uint EffectiveShCurrent(ushort requested, uint stepUv, uint maximumCode)
    {
        if (!HasExactShunt) return uint.MaxValue; // 未知比例交由设备校验，不能按667猜测。
        ulong numerator = (ulong)requested * ShuntElementMicroOhm;
        ulong denominator = stepUv * 10UL * ShuntParallelCount;
        ulong steps = Math.Min(maximumCode + 1UL, Math.Max(1UL, (BackendModel == AfeHardwareProtocolMap.ModelSh3673520 ? numerator / denominator : (numerator + denominator - 1) / denominator)));
        return (uint)Math.Min(65535UL, BackendModel == AfeHardwareProtocolMap.ModelSh3673520
            ? steps * denominator / ShuntElementMicroOhm
            : (steps * denominator + ShuntElementMicroOhm - 1UL) / ShuntElementMicroOhm);
    }
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
    public string PreviewValue { get; private set; } = "—";
    internal void UpdatePreview(string value) { PreviewValue=value; OnPropertyChanged(nameof(PreviewValue)); }
    private string _editValue = string.Empty;
    private string _enabledText = "—";

    public ushort EnableMask { get; init; }
    public required int WireIndex { get; init; }
    public required ushort CapabilityMask { get; init; }
    public required string Group { get; init; }
    public required string Name { get; init; }
    public required string Unit { get; init; }
    private string _valueSource = "芯片编码";
    public string ValueSource { get=>_valueSource; internal set { _valueSource=value; OnPropertyChanged(); } }
    internal void MarkNotApplied() { EffectiveValue="未启用"; }
    public bool IsSwitch { get; init; }
    internal bool IsEdited { get; private set; }
    public bool CanEdit { get; init; } = true;
    public bool IsTemperatureChannel => WireIndex == 34 && (CapabilityMask & AfeHardwareProtocolMap.TempChannelMask) != 0;
    public bool SwitchValue { get => EditValue == "1"; set => EditValue = value ? "1" : "0"; }
    private string _hint = string.Empty;
    private string _allowedCurrentRange = "—";
    public required string Hint { get => _hint; init => _hint = value; }
    public string AllowedCurrentRange { get => _allowedCurrentRange; init => _allowedCurrentRange = value; }

    internal void UpdateGuidance(string hint, string range)
    {
        _hint = hint;
        _allowedCurrentRange = range;
        OnPropertyChanged(nameof(Hint));
        OnPropertyChanged(nameof(AllowedCurrentRange));
    }
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
        set { if (_editValue != value) IsEdited=true; _editValue = value; OnPropertyChanged(); OnPropertyChanged(nameof(SwitchValue)); }
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
        IsEdited = false;
        EnabledText = CapabilityMask == 0 ? "—" : ((enableMask & CapabilityMask) != 0 ? "启用" : "关闭");
    }

    public event PropertyChangedEventHandler? PropertyChanged;
    private void OnPropertyChanged([CallerMemberName] string? name = null) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
}

public sealed record AfeShortCircuitChoice(ushort RequestedA10, ushort EffectiveA10, string Label);

public sealed class AfeHardwareParameterModel
{
    // SH CV1.0A 原 PDF p36–38；与固件新写入的最小延时及上界保持一致。
    private static readonly (int Word, ushort Capability, ushort Minimum, ushort Maximum)[] ShDelayLimits = {
        (3,AfeHardwareProtocolMap.CapCov,140,10010),
        (7,AfeHardwareProtocolMap.CapCuv,490,10010),
        (11,AfeHardwareProtocolMap.CapOcd1,140,10010),
        (13,AfeHardwareProtocolMap.CapOcd2,25,400),
        (17,AfeHardwareProtocolMap.CapOcc1,140,10010)
    };
    // 完整 35-word 候选按固件 validate/validate_write 的全部范围检查，
    // 不只检查正在编辑的组。延时上限即使保护关闭也由固件检查。
    private static readonly (int Word, ushort Capability, ushort Minimum, ushort Maximum, bool AlwaysCheckMaximum)[] ShValueLimits = {
        (2,AfeHardwareProtocolMap.CapCov,3000,4500,false),
        (6,AfeHardwareProtocolMap.CapCuv,1000,3500,false),
        (3,AfeHardwareProtocolMap.CapCov,140,10010,true),
        (7,AfeHardwareProtocolMap.CapCuv,490,10010,true),
        (11,AfeHardwareProtocolMap.CapOcd1,140,10010,true),
        (13,AfeHardwareProtocolMap.CapOcd2,25,400,true),
        (17,AfeHardwareProtocolMap.CapOcc1,140,10010,true),
        (23,AfeHardwareProtocolMap.CapSc,0,576,true),
        (25,AfeHardwareProtocolMap.CapTemp,800,1100,false),
        (27,AfeHardwareProtocolMap.CapTemp,200,500,false),
        (29,AfeHardwareProtocolMap.CapTemp,850,1200,false),
        (31,AfeHardwareProtocolMap.CapTemp,0,500,false)
    };
    private static readonly (int Word, ushort Capability, ushort Minimum, ushort Maximum, bool AlwaysCheckMaximum)[] DvcValueLimits = {
        (2,AfeHardwareProtocolMap.CapCov,501,4595,false),
        (6,AfeHardwareProtocolMap.CapCuv,1,4095,false),
        (3,AfeHardwareProtocolMap.CapCov,200,8000,true),
        (7,AfeHardwareProtocolMap.CapCuv,200,8000,true),
        (11,AfeHardwareProtocolMap.CapOcd1,8,2048,true),
        (13,AfeHardwareProtocolMap.CapOcd2,4,1024,true),
        (17,AfeHardwareProtocolMap.CapOcc1,8,2048,true),
        (19,AfeHardwareProtocolMap.CapOcc2,4,1024,true),
        (23,AfeHardwareProtocolMap.CapSc,0,1992,false)
    };
    // SH CV1.0A 原 PDF p37–38。电流范围由芯片压降与设备精确分流比例计算，
    // 不能用显示用的整数 µΩ 或已取整的有效电流反推。
    private static readonly (int Word, ushort Capability, uint StepUv, uint MaximumCode)[] ShCurrentLimits = {
        (10,AfeHardwareProtocolMap.CapOcd1,5000,15),
        (12,AfeHardwareProtocolMap.CapOcd2,10000,15),
        (16,AfeHardwareProtocolMap.CapOcc1,1375,31)
    };
    private static readonly (int Word, ushort Capability, uint MinimumUv, uint MaximumUv)[] DvcCurrentLimits = {
        (10,AfeHardwareProtocolMap.CapOcd1,250,63750),
        (12,AfeHardwareProtocolMap.CapOcd2,4000,256000),
        (16,AfeHardwareProtocolMap.CapOcc1,250,63750),
        (18,AfeHardwareProtocolMap.CapOcc2,4000,256000)
    };
    private ushort[] _requested = new ushort[AfeHardwareProtocolMap.ProfileWordCount];
    private ushort[] _effective = new ushort[AfeHardwareProtocolMap.ProfileWordCount];
    private AfeHardwareDeviceInfo? _deviceInfo;

    public ObservableCollection<AfeParameterRow> Rows { get; } = new();
    public IReadOnlyList<ushort> RequestedRaw => _requested;
    public AfeHardwareDeviceInfo? DeviceInfo => _deviceInfo;

    public void ClearReadback()
    {
        _deviceInfo = null;
        Array.Clear(_requested);
        Array.Clear(_effective);
        Rows.Clear();
    }

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
                       _deviceInfo.Capabilities != snapshot.Info.Capabilities ||
                       _deviceInfo.ShuntMicroOhm != snapshot.Info.ShuntMicroOhm ||
                       _deviceInfo.ShuntElementMicroOhm != snapshot.Info.ShuntElementMicroOhm ||
                       _deviceInfo.ShuntParallelCount != snapshot.Info.ShuntParallelCount;
        _deviceInfo = snapshot.Info;
        _requested = snapshot.Requested.ToArray();
        _effective = snapshot.Effective.ToArray();
        if (rebuild) BuildRows(snapshot.Info.Capabilities);

        foreach (var row in Rows) row.ValueSource = row.IsSwitch ? "已核对使能位" :
            snapshot.Info.BackendModel==AfeHardwareProtocolMap.ModelSh3673510 && row.WireIndex is 4 or 5 or 8 or 9 or 14 or 15 or 20 or 21 or 26 or 28 or 30 or 32 or 33 ? "MCU 恢复参数" :
            snapshot.Info.BackendModel==AfeHardwareProtocolMap.ModelSh3673510 && row.WireIndex is 25 or 27 or 29 or 31 ?
                ((snapshot.Info.HardwareFeatures&1)!=0 ? "NTC 寄存器反算" : "旧固件请求副本") : "芯片编码";
        LoadRows();
        RefreshValueGuidance();
        RefreshPreviews();
    }

    public void ResetEditsToCurrent()
    {
        LoadRows();
    }

    private void LoadRows()
    {
        ushort enableMask = _requested[34];
        foreach (AfeParameterRow row in Rows)
        {
            if (row.IsTemperatureChannel)
                row.Load(AfeHardwareProtocolMap.TemperatureChannels(enableMask),
                    AfeHardwareProtocolMap.TemperatureChannels(_effective[34]),
                    AfeHardwareProtocolMap.TemperatureChannels(enableMask));
            else row.Load(_requested[row.WireIndex], _effective[row.WireIndex], enableMask);
            if (_deviceInfo!.BackendModel==AfeHardwareProtocolMap.ModelSh3673510 &&
                row.WireIndex is 25 or 27 or 29 or 31 && (_effective[34]&AfeHardwareProtocolMap.CapTemp)==0)
                row.MarkNotApplied();
        }
    }

    public AfeHardwareSnapshot CurrentSnapshot => new(_requested.ToArray(), _effective.ToArray(),
        _deviceInfo ?? throw new InvalidOperationException("请先读取完整 AFE 参数。"));

    public bool TryBuildCandidate(out ushort[] raw, out string error)
    {
        raw = _requested.ToArray();
        if (_deviceInfo is null)
        {
            error = "请先读取设备 AFE 硬件参数。";
            return false;
        }

        ushort temperatureChannels = 0;
        foreach (AfeParameterRow row in Rows)
        {
            if (row.IsTemperatureChannel && !row.CanEdit && row.EditValue != row.RequestedValue)
            {
                error = $"{row.Name} 当前只读；旧固件需升级，未接入通道禁止启用。";
                return false;
            }
            var encoded = row.Encode(row.EditValue);
            if (!encoded.Ok)
            {
                error = $"{row.Group} / {row.Name}: {encoded.Error}";
                return false;
            }
            if (row.EnableMask != 0)
                raw[34] = (ushort)((raw[34] & ~row.EnableMask) | (encoded.Wire != 0 ? row.EnableMask : 0));
            else if (row.IsTemperatureChannel)
                temperatureChannels |= (ushort)(encoded.Wire & row.CapabilityMask);
            else if (row.WireIndex == 34) {
                if (row.IsEdited) raw[34] = (ushort)((raw[34] & ~row.CapabilityMask) | (encoded.Wire & row.CapabilityMask));
            } else raw[row.WireIndex] = encoded.Wire;
        }
        if ((_deviceInfo.Capabilities & AfeHardwareProtocolMap.TempChannelSelect) != 0 &&
            ((_requested[34] & AfeHardwareProtocolMap.TempChannelSelect) != 0 ||
             temperatureChannels != AfeHardwareProtocolMap.TemperatureChannels(_requested[34])))
        {
            raw[34] = (ushort)((raw[34] & ~(AfeHardwareProtocolMap.TempChannelMask | AfeHardwareProtocolMap.CapTemp)) |
                AfeHardwareProtocolMap.TempChannelSelect | temperatureChannels |
                (temperatureChannels != 0 ? AfeHardwareProtocolMap.CapTemp : 0));
        }

        if (_deviceInfo.BackendModel == AfeHardwareProtocolMap.ModelSh3673520)
        {
            if (raw[14] is <1 or >20)
            { error="D015过流恢复电流范围为0.1～2.0A，步进0.1A。";return false; }
            foreach(int index in new[]{5,9,15,21})
                if(raw[index] is <200 or >65000)
                { error="D015硬件恢复稳定时间范围为200～65000ms。";return false; }
            raw[20] = raw[14]; // D015共用过流恢复电流，界面只提供一个编辑项。
            if ((raw[34] & 12) is not (0 or 12))
            { error = "3520 的 OCD1/OCD2 共用使能，必须共同开启或关闭。"; return false; }
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
        if (AfeHardwareProtocolMap.IsSh(_deviceInfo.BackendModel))
        {
            foreach (var limit in ShDelayLimits)
            {
                if ((en & limit.Capability) == 0) continue;
                // 历史请求可低于首档，但实际已量化为首档。只有本项未编辑且设备有效值
                // 明确等于首档时才规范化候选；不修改模型，不掩盖用户新输入的非法值。
                if (raw[limit.Word] == _requested[limit.Word] && raw[limit.Word] < limit.Minimum &&
                    _effective[limit.Word] == limit.Minimum)
                    raw[limit.Word] = limit.Minimum;
            }
        }
        var rangeErrors = new List<string>();
        // 3520 保留其既有专属校验，不能套用其他 backend 的参数范围。
        var valueLimits = _deviceInfo.BackendModel switch {
            AfeHardwareProtocolMap.ModelSh3673510 => ShValueLimits,
            AfeHardwareProtocolMap.ModelDvc1124 => DvcValueLimits,
            _ => []
        };
        foreach (var limit in valueLimits)
        {
            bool enabled = (en & limit.Capability) != 0;
            if ((!enabled || raw[limit.Word] >= limit.Minimum) &&
                ((!enabled && !limit.AlwaysCheckMaximum) || raw[limit.Word] <= limit.Maximum)) continue;
            var row = Rows.FirstOrDefault(r => r.WireIndex == limit.Word);
            if (row is null)
            {
                rangeErrors.Add($"保留字段 word[{limit.Word}]={raw[limit.Word]} 超出固件允许上限 {limit.Maximum}。");
                continue;
            }
            string minimum = row.Decode(enabled ? limit.Minimum : (ushort)0);
            rangeErrors.Add($"{row.Group} / {row.Name}：当前 {row.Decode(raw[limit.Word])} {row.Unit}，" +
                $"允许 {minimum}～{row.Decode(limit.Maximum)} {row.Unit}（word[{limit.Word}]）。");
        }
        // 固件对全部温度字段（含恢复值和未启用字段）检查协议编码上限。
        for (int i = 25; i <= 32; i++)
            if (raw[i] > 1450) rangeErrors.Add($"温度 word[{i}] 超出协议范围 −40～105℃。");
        ushort ocdPair = (ushort)(en & (AfeHardwareProtocolMap.CapOcd1 | AfeHardwareProtocolMap.CapOcd2));
        if (_deviceInfo.BackendModel == AfeHardwareProtocolMap.ModelSh3673510 &&
            ocdPair != 0 && ocdPair != (AfeHardwareProtocolMap.CapOcd1 | AfeHardwareProtocolMap.CapOcd2))
            rangeErrors.Add("SH OCD1/OCD2 共用硬件使能，必须同时开启或同时关闭。");
        foreach (var limit in DvcCurrentLimits)
        {
            if ((en & limit.Capability) == 0 ||
                !TryGetCurrentRange(limit.Word, out uint minimumA10, out uint maximumA10)) continue;
            if (raw[limit.Word] < minimumA10 || raw[limit.Word] > maximumA10)
            {
                var row = Rows.Single(r => r.WireIndex == limit.Word);
                rangeErrors.Add($"{row.Group} / {row.Name}：输入 {row.Decode(raw[limit.Word])} A，" +
                    $"允许请求范围 {FormatCurrent(minimumA10)}～{FormatCurrent(maximumA10)} A" +
                    $"（分流器 {_deviceInfo.ShuntDisplay}）；未发送写入。");
            }
        }
        if ((en & AfeHardwareProtocolMap.CapSc) != 0 && CanCalculateShortRange)
        {
            if (!TryGetShortRange(raw[12], out uint minimumA10, out uint maximumA10, out string hint))
            { rangeErrors.Add("短路保护：" + hint + "；未发送写入。"); }
            else if (raw[22] < minimumA10 || raw[22] > maximumA10)
            {
                rangeErrors.Add($"短路电流允许请求范围 {FormatCurrent(minimumA10)}～{FormatCurrent(maximumA10)} A；" +
                    hint + "；未发送写入。");
            }
        }
        if ((en & AfeHardwareProtocolMap.CapCov) != 0 && (raw[2] == 0 || raw[4] >= raw[2]))
        { rangeErrors.Add("单体过压恢复值必须小于保护值。"); }
        if ((en & AfeHardwareProtocolMap.CapCuv) != 0 && (raw[6] == 0 || raw[8] <= raw[6]))
        { rangeErrors.Add("单体欠压恢复值必须大于保护值。"); }
        if ((en & AfeHardwareProtocolMap.CapOcd1) != 0 && (raw[10] == 0 || raw[14] >= (AfeHardwareProtocolMap.IsSh(_deviceInfo.BackendModel) ? _deviceInfo.EffectiveShCurrent(raw[10], 5000u, 15u) : raw[10])))
        { rangeErrors.Add("放电过流恢复值必须小于 OCD1 阈值。"); }
        if ((en & AfeHardwareProtocolMap.CapOcd2) != 0 && (raw[12] == 0 || raw[14] >= (AfeHardwareProtocolMap.IsSh(_deviceInfo.BackendModel) ? _deviceInfo.EffectiveShCurrent(raw[12], 10000u, 15u) : raw[12])))
        { rangeErrors.Add("放电过流恢复值必须小于 OCD2 阈值。"); }
        if ((en & AfeHardwareProtocolMap.CapOcc1) != 0 && (raw[16] == 0 || raw[20] >= (AfeHardwareProtocolMap.IsSh(_deviceInfo.BackendModel) ? _deviceInfo.EffectiveShCurrent(raw[16], 1375u, 31u) : raw[16])))
        { rangeErrors.Add("充电过流恢复值必须小于 OCC1 阈值。"); }
        if ((en & AfeHardwareProtocolMap.CapOcc2) != 0 && (raw[18] == 0 || raw[20] >= raw[18]))
        { rangeErrors.Add("充电过流恢复值必须小于 OCC2 阈值。"); }
        if ((en & AfeHardwareProtocolMap.CapTemp) != 0)
        {
            if (raw[26] >= raw[25] || raw[30] >= raw[29] || raw[28] <= raw[27] || raw[32] <= raw[31])
            { rangeErrors.Add("硬件温度保护恢复阈值与触发阈值的迟滞关系不合法。"); }
        }

        if ((en & AfeHardwareProtocolMap.CapSc)!=0 && _deviceInfo.BackendModel==AfeHardwareProtocolMap.ModelDvc1124) {
            uint senseUv=(uint)raw[22]*_deviceInfo.ShuntMicroOhm/10u;
            if (_deviceInfo.ShuntMicroOhm==0 || senseUv<10000u || senseUv>630000u) {
                rangeErrors.Add("DVC 短路启用需有效电流（分流压降 10~630 mV）；按芯片档位向下量化，请核对有效值。");
            }
        }
        if (rangeErrors.Count != 0)
        {
            error = "完整 AFE 参数中有以下不合规项，整组未发送：\n" + string.Join("\n", rangeErrors) +
                "\n请显式修正这些项目；温度有效显示值不是独立寄存器反算结果，不能据此自动改写温度。";
            return false;
        }
        error = string.Empty;
        return true;
    }

    public bool HasChanges(ushort[] candidate) => !_requested.SequenceEqual(candidate);

    private void RefreshValueGuidance()
    {
        var limits = _deviceInfo!.BackendModel switch {
            AfeHardwareProtocolMap.ModelSh3673510 => ShValueLimits,
            AfeHardwareProtocolMap.ModelDvc1124 => DvcValueLimits,
            _ => []
        };
        foreach (var row in Rows)
        {
            if (row.IsSwitch || row.WireIndex == 22 || row.Unit == "A") continue;
            var limit = limits.FirstOrDefault(l => l.Word == row.WireIndex);
            string range = limit.Capability != 0
                ? $"{row.Decode(limit.Minimum)}～{row.Decode(limit.Maximum)} {row.Unit}"
                : row.Unit == "℃" ? "−40～105 ℃，并满足恢复回差"
                : row.WireIndex is 4 or 8 or 14 or 20 ? "MCU 恢复值，须满足回差"
                : row.WireIndex is 5 or 9 or 15 or 21 or 33 ? "0～65535 ms（MCU 确认）"
                : row.WireIndex == 34 ? "当前芯片能力位" : $"0～65535 {row.Unit}";
            string hint = row.Hint;
            if (row.Unit == "℃") hint = "协议编码为 (℃+40)×10；来源见来源列，NTC反算值为标称值，不代表实测动作精度。";
            if (limit.Capability != 0) hint = $"启用时允许输入 {range}；完整参数会一起校验。" +
                (row.Unit == "℃" ? "温度来源见来源列；旧固件请求副本不能作为自动修正依据。" :
                 "芯片离散档位由固件量化，实际值见“AFE有效值”。");
            row.UpdateGuidance(hint, range);
        }
    }

    public string DescribeLegacyDelayCorrections(ushort[] candidate)
    {
        if ((_deviceInfo is null || !AfeHardwareProtocolMap.IsSh(_deviceInfo.BackendModel))) return string.Empty;
        var changes = new List<string>();
        foreach (var limit in ShDelayLimits)
        {
            if ((candidate[34] & limit.Capability) == 0 || _requested[limit.Word] >= limit.Minimum ||
                candidate[limit.Word] != limit.Minimum || _effective[limit.Word] != limit.Minimum) continue;
            var row = Rows.Single(r => r.WireIndex == limit.Word);
            var edited = row.Encode(row.EditValue);
            if (!edited.Ok || edited.Wire != _requested[limit.Word]) continue;
            changes.Add($"{row.Group} / {row.Name}：{_requested[limit.Word]} → {candidate[limit.Word]} ms");
        }
        return changes.Count == 0 ? string.Empty :
            "本次将同时规范化以下历史请求，与设备已读回的实际延时一致：\n" +
            string.Join("\n", changes) + "\n这些项目的硬件延时档位保持不变。\n\n";
    }

    public string DescribeTemperatureChannelChanges(ushort[] candidate)
    {
        ushort before = AfeHardwareProtocolMap.TemperatureChannels(_requested[34]);
        ushort after = AfeHardwareProtocolMap.TemperatureChannels(candidate[34]);
        if (before == after) return string.Empty;
        var changes = new List<string>();
        for (int i = 0; i < 4; i++) {
            int bit = 1 << (8 + i);
            if (((before ^ after) & bit) != 0)
                changes.Add($"TS{i+1}：{((after & bit) != 0 ? "启用" : "关闭")}硬件温度保护");
        }
        return string.Join("\n", changes) + "\n各通道同时控制充/放电高低温，并共用电池温度阈值。" +
            (after == 0 ? "\n所有外部通道硬件温度保护将关闭；软件保护与温度采样仍独立运行。" : string.Empty) + "\n\n";
    }

    private static AfeParameterRow ProtectionSwitch(ushort cap, string group, string name) => new() {
        WireIndex=34, CapabilityMask=cap, IsSwitch=true, Group=group, Name=name, Unit="0/1",
        AllowedCurrentRange="0=关闭 / 1=启用",
        Hint="关闭会撤销该项 AFE 自主保护，软件保护独立。SH 的 OCD1/OCD2 共用开关，改变开关会同步设置两级。",
        Decode=v=>(v&cap)!=0?"1":"0",
        Encode=text=>text.Trim() switch {
            "0"=>(true,(ushort)0,string.Empty), "1"=>(true,cap,string.Empty),
            _=>(false,(ushort)0,"保护开关只能为 0 或 1。")
        }
    };

    public string DescribeProtectionSwitchChanges(ushort[] candidate)
    {
        var changes = Rows.Where(r=>r.IsSwitch && !r.IsTemperatureChannel &&
            (_requested[34]&r.CapabilityMask)!=(candidate[34]&r.CapabilityMask))
            .Select(r=>$"{r.Name}：{((candidate[34]&r.CapabilityMask)!=0?"启用":"关闭")}（软件保护独立）").ToArray();
        return changes.Length==0 ? string.Empty : string.Join("\n",changes)+"\n\n";
    }

    private void BuildRows(ushort caps)
    {
        Rows.Clear();
        if (_deviceInfo!.BackendModel == AfeHardwareProtocolMap.ModelSh3673520)
        {
            foreach (var item in new (ushort Mask,string Name)[] {
                (1,"过压使能"),(2,"欠压使能"),(12,"放电过流1/2使能"),
                (16,"充电过流使能"),(64,"短路使能"),(128,"TS1/TS2温度保护使能")})
            {
                ushort mask=item.Mask;
                Rows.Add(new AfeParameterRow {
                    WireIndex=34, EnableMask=mask, CapabilityMask=mask, IsSwitch=true,
                    Group="硬件保护使能",Name=item.Name,Unit="0/1",
                    Hint="0=关闭，1=启用；3520 OCD1/OCD2共同控制，未安装TS3/4不开放。Powerdown固定关闭。",
                    Decode=v=>(v&mask)==mask?"1":"0",
                    Encode=text=>text.Trim() switch {
                        "0"=>(true,(ushort)0,string.Empty), "1"=>(true,(ushort)1,string.Empty),
                        _=>(false,(ushort)0,"只能输入0或1。") }
                });
            }
        }
        else
        {
        if ((caps & AfeHardwareProtocolMap.CapCov) != 0) Rows.Add(ProtectionSwitch(AfeHardwareProtocolMap.CapCov, "单体过压", "过压使能"));
        if ((caps & AfeHardwareProtocolMap.CapCuv) != 0) Rows.Add(ProtectionSwitch(AfeHardwareProtocolMap.CapCuv, "单体欠压", "欠压使能"));
        ushort ocdCaps = (ushort)(caps & (AfeHardwareProtocolMap.CapOcd1 | AfeHardwareProtocolMap.CapOcd2));
        if (_deviceInfo!.BackendModel == AfeHardwareProtocolMap.ModelSh3673510) {
            if (ocdCaps != 0) Rows.Add(ProtectionSwitch(ocdCaps, "放电过流", "OCD1/OCD2 联动使能"));
        } else {
            if ((ocdCaps & AfeHardwareProtocolMap.CapOcd1) != 0) Rows.Add(ProtectionSwitch(AfeHardwareProtocolMap.CapOcd1, "放电过流", "OCD1 使能"));
            if ((ocdCaps & AfeHardwareProtocolMap.CapOcd2) != 0) Rows.Add(ProtectionSwitch(AfeHardwareProtocolMap.CapOcd2, "放电过流", "OCD2 使能"));
        }
        if ((caps & AfeHardwareProtocolMap.CapOcc1) != 0) Rows.Add(ProtectionSwitch(AfeHardwareProtocolMap.CapOcc1, "充电过流", "OCC1 使能"));
        if ((caps & AfeHardwareProtocolMap.CapOcc2) != 0) Rows.Add(ProtectionSwitch(AfeHardwareProtocolMap.CapOcc2, "充电过流", "OCC2 使能"));
        }
        if ((caps & AfeHardwareProtocolMap.CapCov) != 0)
        {
            Rows.Add(U16Row(2, AfeHardwareProtocolMap.CapCov, "单体过压", "保护电压", "mV", 0, ushort.MaxValue));
            Rows.Add(U16Row(4, AfeHardwareProtocolMap.CapCov, "单体过压", "恢复电压", "mV", 0, ushort.MaxValue));
            Rows.Add(U16Row(3, AfeHardwareProtocolMap.CapCov, "单体过压", "保护延时", "ms", 0, ushort.MaxValue));
            Rows.Add(U16Row(5, AfeHardwareProtocolMap.CapCov, "单体过压", "恢复确认", "ms", 0, ushort.MaxValue));
        }
        if ((caps & AfeHardwareProtocolMap.CapCuv) != 0)
        {
            Rows.Add(U16Row(6, AfeHardwareProtocolMap.CapCuv, "单体欠压", "保护电压", "mV", 0, ushort.MaxValue));
            Rows.Add(U16Row(8, AfeHardwareProtocolMap.CapCuv, "单体欠压", "恢复电压", "mV", 0, ushort.MaxValue));
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
        if (_deviceInfo!.BackendModel != AfeHardwareProtocolMap.ModelDvc1124 &&
            (caps & (AfeHardwareProtocolMap.CapOcd1 | AfeHardwareProtocolMap.CapOcd2)) != 0)
        {
            ushort cap = (ushort)(AfeHardwareProtocolMap.CapOcd1 | AfeHardwareProtocolMap.CapOcd2);
            Rows.Add(CurrentRow(14, cap, "放电过流", "MCU恢复电流"));
            Rows.Add(U16Row(15, cap, "放电过流", "MCU恢复确认", "ms", 0, ushort.MaxValue));
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
        if (_deviceInfo!.BackendModel != AfeHardwareProtocolMap.ModelDvc1124 &&
            (caps & (AfeHardwareProtocolMap.CapOcc1 | AfeHardwareProtocolMap.CapOcc2)) != 0)
        {
            ushort cap = (ushort)(AfeHardwareProtocolMap.CapOcc1 | AfeHardwareProtocolMap.CapOcc2);
            if (_deviceInfo.BackendModel != AfeHardwareProtocolMap.ModelSh3673520)
                Rows.Add(CurrentRow(20, cap, "充电过流", "MCU恢复电流"));
            Rows.Add(U16Row(21, cap, "充电过流", "MCU恢复确认", "ms", 0, ushort.MaxValue));
        }
        if ((caps & AfeHardwareProtocolMap.CapSc) != 0)
        {
            if (_deviceInfo.BackendModel != AfeHardwareProtocolMap.ModelSh3673520)
            Rows.Add(ProtectionSwitch(AfeHardwareProtocolMap.CapSc, "短路保护", "SCD 使能"));
            Rows.Add(CurrentRow(22, AfeHardwareProtocolMap.CapSc, "短路保护", "短路电流"));
            Rows.Add(U16Row(23, AfeHardwareProtocolMap.CapSc, "短路保护", "短路延时", "us", 0, ushort.MaxValue));
            // sc_recover_ms 为协议保留字段；当前两后端恢复流程均不消费，不提供无效编辑项。
        }
        if ((caps & AfeHardwareProtocolMap.CapTemp) != 0)
        {
            bool selectionSupported = (caps & AfeHardwareProtocolMap.TempChannelSelect) != 0;
            string[] roles = { "电池1", "电池2", "加热", "MOS" };
            for (int i = 0; i < 4; i++) {
                ushort bit = (ushort)(1 << (8 + i));
                bool canEdit = selectionSupported && (caps & bit) != 0;
                Rows.Add(new AfeParameterRow {
                    WireIndex=34, CapabilityMask=bit, IsSwitch=true, CanEdit=canEdit,
                    Group="温度保护", Name=$"TS{i+1} / {roles[i]} 硬件保护", Unit="0/1",
                    AllowedCurrentRange=canEdit ? "0=关闭 / 1=启用" : "只读",
                    Hint=!selectionSupported ? "旧固件只有温度总开关：TS1/TS2 联动；需升级配套固件才能独立修改通道。" :
                        !canEdit ? "固件未声明该温度通道已接入，禁止启用。" :
                        "同时控制该通道的充电高温、充电低温、放电高温、放电低温；所有启用通道共用四个阈值。关闭不停止采样及软件保护。",
                    Decode=v=>(v & bit)!=0 ? "1" : "0",
                    Encode=text=>text.Trim() switch {
                        "0"=>(true,(ushort)0,string.Empty),
                        "1"=>(true,bit,string.Empty),
                        _=>(false,(ushort)0,"温度通道使能只能为 0 或 1。")
                    }
                });
            }
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
        var ocd2 = Rows.FirstOrDefault(r => r.WireIndex == 12);
        if (ocd2 is not null) ocd2.PropertyChanged += (_, e) => {
            if (e.PropertyName == nameof(AfeParameterRow.EditValue)) RefreshShortGuidance();
        };
        foreach (var row in Rows) row.PropertyChanged += (_, e) => {
            if (e.PropertyName == nameof(AfeParameterRow.EditValue)) RefreshPreviews();
        };
        RefreshShortGuidance();
        RefreshPreviews();
    }

    private bool PredictHardwareWord(int word, ushort[] raw, out ushort value)
    {
        value=raw[word];
        if (_deviceInfo?.BackendModel != AfeHardwareProtocolMap.ModelSh3673510) return false;
        ushort[]? choices=word switch {
            3 or 11 or 17 => new ushort[]{140,280,490,980,2030,3010,4970,10010},
            7 => new ushort[]{490,770,980,1470,2030,3010,4970,10010},
            23 => new ushort[]{0,32,64,96,128,192,224,256,288,320,384,448,480,512,544,576},
            _=>null
        };
        if (choices is not null) { value=choices.Where(v=>v>=raw[word]).DefaultIfEmpty(choices.Last()).First(); return true; }
        if (word is 2 or 6) { value=(ushort)Math.Min(5115,((uint)raw[word]+2)/5*5); return true; }
        if (word==13) { value=(ushort)(Math.Min(16,Math.Max(1,((uint)raw[word]+24)/25))*25); return true; }
        if (!_deviceInfo.HasExactShunt) return false;
        if (word is 10 or 12 or 16) {
            uint step=word==10?5000u:word==12?10000u:1375u;
            value=(ushort)_deviceInfo.EffectiveShCurrent(raw[word],step,word==16?31u:15u); return true;
        }
        if (word==22) {
            ulong step=10000UL*10UL*_deviceInfo.ShuntParallelCount, element=_deviceInfo.ShuntElementMicroOhm;
            ulong baseSense=Math.Min(16UL,Math.Max(1UL,((ulong)raw[12]*element+step-1)/step))*step;
            uint multiple=new uint[]{2,3,4,6}.Where(k=>baseSense*k >= (ulong)raw[22]*element).DefaultIfEmpty(6u).First();
            value=(ushort)Math.Min(65535UL,(baseSense*multiple+element-1)/element); return true;
        }
        return false;
    }

    private void RefreshPreviews()
    {
        if (_deviceInfo is null || _requested.Length!=35) return;
        var raw=_requested.ToArray();
        foreach(var row in Rows.Where(r=>!r.IsSwitch)) {
            var encoded=row.Encode(row.EditValue); if(encoded.Ok)raw[row.WireIndex]=encoded.Wire;
        }
        foreach(var row in Rows) {
            var encoded=row.Encode(row.EditValue);
            row.UpdatePreview(!encoded.Ok?"输入无效":row.IsSwitch?(row.SwitchValue?"开启":"关闭"):
                PredictHardwareWord(row.WireIndex,raw,out ushort value)?row.Decode(value)+" "+row.Unit:
                row.WireIndex is 25 or 27 or 29 or 31?"写后核对 NTC 编码":
                _deviceInfo.BackendModel==AfeHardwareProtocolMap.ModelSh3673510?row.EditValue+"（MCU）":"写后读回核对");
        }
    }

    public bool VerifyHardwareReadback(ushort[] candidate, ushort[] effective, out string error)
    {
        ushort mask=(ushort)(_deviceInfo!.Capabilities&0x7f);
        if ((candidate[34]&mask)!=(effective[34]&mask)) { error="硬件保护使能位回读不一致。"; return false; }
        foreach(var row in Rows.Where(r=>!r.IsSwitch && (candidate[34]&r.CapabilityMask)!=0))
            if(PredictHardwareWord(row.WireIndex,candidate,out ushort expected) && expected!=effective[row.WireIndex]) {
                error=$"{row.Group}/{row.Name} 硬件档位回读不一致：期望 {row.Decode(expected)}，读回 {row.Decode(effective[row.WireIndex])} {row.Unit}。";
                return false;
            }
        error=string.Empty;return true;
    }

    public string DescribeHardwareQuantization(ushort[] candidate)
    {
        var changes=new List<string>();
        foreach(var row in Rows.Where(r=>!r.IsSwitch))
            if(PredictHardwareWord(row.WireIndex,candidate,out ushort actual) && actual!=candidate[row.WireIndex])
                changes.Add($"{row.Group}/{row.Name}：{row.Decode(candidate[row.WireIndex])} → {row.Decode(actual)} {row.Unit}");
        return changes.Count==0?string.Empty:"预计硬件选档（写后仍需核对）：\n"+string.Join("\n",changes)+"\n\n";
    }

    private bool CanCalculateShortRange => _deviceInfo is not null &&
        ((AfeHardwareProtocolMap.IsSh(_deviceInfo.BackendModel) && _deviceInfo.HasExactShunt) ||
         (_deviceInfo.BackendModel == AfeHardwareProtocolMap.ModelDvc1124 && _deviceInfo.ShuntMicroOhm != 0));

    // 原厂 SH p37：SC 为量化后 OCD2 感测压降的 2/3/4/6 倍；不从已取整的有效电流反推。
    // DVC RM V1.2 p20：10～630 mV，10 mV 一档。上下界同时受 0.1 A 协议位宽限制。
    private bool TryGetShortRange(ushort ocd2A10, out uint minimumA10, out uint maximumA10, out string hint)
    {
        minimumA10 = maximumA10 = 0;
        hint = "分流器或型号信息不足，范围由设备校验";
        if (!CanCalculateShortRange) return false;
        if (_deviceInfo!.BackendModel == AfeHardwareProtocolMap.ModelDvc1124)
        {
            ulong shunt = _deviceInfo.ShuntMicroOhm;
            ulong lastCode = Math.Min(63UL, ushort.MaxValue * shunt / 100000UL);
            minimumA10 = (uint)((100000UL + shunt - 1UL) / shunt);
            maximumA10 = (uint)(lastCode * 100000UL / shunt);
            hint = "DVC 短路压降为 10～630 mV、10 mV 一档；恢复使用可靠反向电流或关断后的负载移除证据";
            return maximumA10 >= minimumA10 && maximumA10 != 0;
        }
        ulong element = _deviceInfo.ShuntElementMicroOhm;
        ulong firstStep = 10000UL * 10UL * _deviceInfo.ShuntParallelCount;
        ulong requested = ocd2A10 * element;
        if (requested < firstStep || requested > 16UL * firstStep)
        { hint = "请先输入当前分流器可实现的 OCD2 阈值，短路基准依赖 OCD2，即使 OCD 关闭也不能省略"; return false; }
        ulong sense = (_deviceInfo.BackendModel == AfeHardwareProtocolMap.ModelSh3673520 ? requested / firstStep : (requested + firstStep - 1UL) / firstStep) * firstStep;
        uint[] multiples = { 2, 3, 4, 6 };
        var supported = multiples.Where(k => (sense * k + element - 1UL) / element <= ushort.MaxValue).ToArray();
        if (supported.Length == 0)
        { hint = "当前 OCD2 对应的最低短路档位已超过协议可表示电流"; return false; }
        minimumA10 = (uint)((2UL * sense + element - 1UL) / element);
        maximumA10 = (uint)(supported.Last() * sense / element);
        string steps = string.Join("、", supported.Select(k => FormatCurrent((uint)((sense * k + element - 1UL) / element))));
        hint = $"基于当前编辑的 OCD2（量化后 {FormatCurrent((uint)((sense + element - 1UL) / element))} A），" +
            $"短路按 2/3/4/6 倍选档；可用有效档位 {steps} A。D015向下选保护档，3510沿用原量化规则；改 OCD2 后请重新核对短路电流。" +
            "短路恢复使用可靠反向充电，或连续 10 轮负载移除观察后清标志并读回，不使用保留的恢复时间字段";
        return maximumA10 >= minimumA10;
    }

    // 客户窗口只提供可由现有 0.1 A 协议选中的硬件档位，不联动修改 OCD2。
    public IReadOnlyList<AfeShortCircuitChoice> GetShortCircuitChoices()
    {
        var result = new List<AfeShortCircuitChoice>();
        if (!TryGetShortRange(_requested[12], out uint minimum, out uint maximum, out _)) return result;
        if (_deviceInfo!.BackendModel == AfeHardwareProtocolMap.ModelDvc1124)
        {
            ulong shunt = _deviceInfo.ShuntMicroOhm;
            for (uint code = 1; code <= 63; code++)
            {
                ulong requested = (code * 100000UL + shunt - 1UL) / shunt;
                ulong effective = code * 100000UL / shunt;
                if (requested < minimum || requested > maximum || effective > ushort.MaxValue) continue;
                if (requested * shunt / 100000UL != code) continue;
                result.Add(new((ushort)requested, (ushort)effective,
                    $"{FormatCurrent((uint)effective)} A（{code * 10} mV，保存请求 {FormatCurrent((uint)requested)} A）"));
            }
        }
        else
        {
            ulong element = _deviceInfo.ShuntElementMicroOhm;
            ulong step = 10000UL * 10UL * _deviceInfo.ShuntParallelCount;
            ulong numerator = _requested[12] * element;
            ulong sense = (_deviceInfo.BackendModel == AfeHardwareProtocolMap.ModelSh3673520 ? numerator / step : (numerator + step - 1UL) / step) * step;
            uint[] multiples = { 2, 3, 4, 6 };
            foreach (uint multiple in multiples)
            {
                bool d015 = _deviceInfo.BackendModel == AfeHardwareProtocolMap.ModelSh3673520;
                ulong requested = d015 ? (sense * multiple + element - 1UL) / element : sense * multiple / element;
                ulong effective = d015 ? sense * multiple / element : (sense * multiple + element - 1UL) / element;
                if (requested < minimum || requested > maximum || effective > ushort.MaxValue) continue;
                uint selected = d015 ? multiples.Last(k => requested * element >= sense * k) : multiples.First(k => requested * element <= sense * k);
                if (selected != multiple) continue;
                result.Add(new((ushort)requested, (ushort)effective,
                    $"{FormatCurrent((uint)effective)} A（{multiple} × OCD2，保存请求 {FormatCurrent((uint)requested)} A）"));
            }
        }
        return result;
    }

    public bool TryBuildShortCircuitCandidate(ushort currentA10, ushort delayUs, out ushort[] candidate, out string error)
    {
        candidate = Array.Empty<ushort>();
        error = "请先读取当前设备的完整有效配置。";
        if (_deviceInfo is null) return false;
        if ((_deviceInfo.Capabilities & AfeHardwareProtocolMap.CapSc) == 0)
        { error = "当前设备不支持短路参数设置。"; return false; }
        if ((_requested[34] & AfeHardwareProtocolMap.CapSc) == 0)
        { error = "设备短路保护当前关闭；本窗口保持使能状态，请由内部工具先启用。"; return false; }
        if (!CanCalculateShortRange)
        { error = "缺少计算短路档位所需的分流器信息，请先升级配套固件。"; return false; }
        var draft = new AfeHardwareParameterModel();
        draft.Load(new(_requested, _effective, _deviceInfo));
        draft.Rows.Single(r => r.WireIndex == 22).EditValue = FormatCurrent(currentA10);
        draft.Rows.Single(r => r.WireIndex == 23).EditValue = delayUs.ToString(CultureInfo.InvariantCulture);
        if (!draft.TryBuildCandidate(out candidate, out error)) return false;
        for (int i = 0; i < candidate.Length; i++)
            if (i != 22 && i != 23 && candidate[i] != _requested[i])
            { error = "设备有需内部工具处理的历史 AFE 参数；本窗口不会附带修改 OCD2 或其它参数。请先使用内部工具读取并处理。"; return false; }
        return true;
    }

    private void RefreshShortGuidance()
    {
        var row = Rows.FirstOrDefault(r => r.WireIndex == 22);
        if (row is null) return;
        ushort ocd2 = _requested[12];
        var baseRow = Rows.FirstOrDefault(r => r.WireIndex == 12);
        if (_deviceInfo?.BackendModel == AfeHardwareProtocolMap.ModelSh3673510 && baseRow is not null)
        {
            if (!decimal.TryParse(baseRow.EditValue, NumberStyles.Float, CultureInfo.InvariantCulture, out decimal value) ||
                value < 0 || value > 6553.5m || value * 10m != decimal.Truncate(value * 10m))
            { row.UpdateGuidance("请先输入合法的 OCD2 阈值，再计算短路电流范围。", "OCD2 输入无效"); return; }
            ocd2 = (ushort)(value * 10m);
        }
        bool valid = TryGetShortRange(ocd2, out uint minimumA10, out uint maximumA10, out string hint);
        row.UpdateGuidance(hint, valid ? $"{FormatCurrent(minimumA10)}～{FormatCurrent(maximumA10)} A" :
            CanCalculateShortRange ? "OCD2 无可用基准" : "由设备校验");
    }

    private AfeParameterRow U16Row(int index, ushort cap, string group, string name, string unit, int min, int max) => new()
    {
        WireIndex = index,
        CapabilityMask = cap,
        Group = group,
        Name = name,
        Unit = unit,
        Hint = _deviceInfo?.BackendModel == AfeHardwareProtocolMap.ModelSh3673520 && index is 5 or 9 or 15 or 21 or 33
            ? "D015连续恢复稳定时间；过流还需负载/充电器移除证据。反向电流仅临时放行MOS，保留锁存。"
            : index is 15 or 21
            ? "MCU 清除 SH 过流锁存时的恢复确认参数；需物理移除证据及电流低于恢复阈值。按合格 CADC 观察计数，实际等待可能延长；可靠反向电流立即恢复，不等待此参数。"
            : "语义值；芯片离散档位由固件量化，实际值见“AFE有效值”。",
        Decode = v => v.ToString(CultureInfo.InvariantCulture),
        Encode = s =>
        {
            if (!int.TryParse(s.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out int value) || value < min || value > max)
                return (false, (ushort)0, $"请输入 {min}~{max} 的整数。");
            return (true, (ushort)value, string.Empty);
        }
    };

    private bool TryGetCurrentRange(int index, out uint minimumA10, out uint maximumA10)
    {
        minimumA10 = maximumA10 = 0;
        if (_deviceInfo?.BackendModel == AfeHardwareProtocolMap.ModelDvc1124 && _deviceInfo.ShuntMicroOhm != 0)
        {
            foreach (var limit in DvcCurrentLimits)
            {
                if (limit.Word != index) continue;
                ulong shunt = _deviceInfo.ShuntMicroOhm;
                minimumA10 = (uint)((limit.MinimumUv * 10UL + shunt - 1UL) / shunt);
                maximumA10 = (uint)Math.Min(ushort.MaxValue, limit.MaximumUv * 10UL / shunt);
                return true;
            }
            return false;
        }
        if ((_deviceInfo is null || !AfeHardwareProtocolMap.IsSh(_deviceInfo.BackendModel)) || !_deviceInfo.HasExactShunt)
            return false;
        foreach (var limit in ShCurrentLimits)
        {
            if (limit.Word != index) continue;
            ulong firstStep = limit.StepUv * 10UL * _deviceInfo.ShuntParallelCount;
            ulong element = _deviceInfo.ShuntElementMicroOhm;
            // 请求的 0.1 A 整数必须落在固件的闭区间，最小向上、最大向下取整。
            minimumA10 = (uint)((firstStep + element - 1UL) / element);
            maximumA10 = (uint)Math.Min(ushort.MaxValue, firstStep * (limit.MaximumCode + 1UL) / element);
            return true;
        }
        return false;
    }

    private static string FormatCurrent(uint currentA10) =>
        (currentA10 / 10m).ToString("0.0", CultureInfo.InvariantCulture);

    private string CurrentRange(int index) =>
        TryGetCurrentRange(index, out uint minimumA10, out uint maximumA10)
            ? $"{FormatCurrent(minimumA10)}～{FormatCurrent(maximumA10)} A"
            : "由设备校验";

    private string CurrentHint(int index) =>
        TryGetCurrentRange(index, out uint minimumA10, out uint maximumA10)
            ? $"允许请求范围 {FormatCurrent(minimumA10)}～{FormatCurrent(maximumA10)} A（分流器 {_deviceInfo!.ShuntDisplay}）。" +
                (_deviceInfo.BackendModel == AfeHardwareProtocolMap.ModelDvc1124
                    ? "电流最多保留 0.1 A；固件按感测压降向下选档，实际值见“AFE有效值”。"
                    : "电流最多保留 0.1 A；按硬件档位向上量化，实际值见“AFE有效值”。")
            : "电流最多保留 0.1 A；范围由固件校验，实际硬件量化结果见“AFE有效值”。";

    private AfeParameterRow CurrentRow(int index, ushort cap, string group, string name) => new()
    {
        WireIndex = index,
        CapabilityMask = cap,
        Group = group,
        Name = name,
        Unit = "A",
        Hint = _deviceInfo?.BackendModel == AfeHardwareProtocolMap.ModelSh3673520 && index is 14 or 20
            ? "D015充放过流共用恢复电流，0.1～2.0A；不能只凭关MOS后的零电流恢复，反向临时放行保留锁存。"
            : index is 14 or 20
            ? "MCU 清除 SH 过流锁存时的电流上限，不是芯片保护寄存器。还须有负载/充电器移除等物理证据，不能只凭关 MOS 后电流为零恢复；可靠反向电流立即恢复。"
            : CurrentHint(index),
        AllowedCurrentRange = CurrentRange(index),
        Decode = v => (v / 10.0).ToString("0.0", CultureInfo.InvariantCulture),
        Encode = s =>
        {
            if (!decimal.TryParse(s.Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out decimal value) || value < 0 || value > 6553.5m)
                return (false, (ushort)0, "请输入合法电流值(A)。");
            decimal wire = value * 10m;
            if (wire != decimal.Truncate(wire))
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
            if (!decimal.TryParse(s.Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out decimal c) || c < -40 || c > 105)
                return (false, (ushort)0, "请输入 -40~105℃。");
            decimal wire = (c + 40m) * 10m;
            if (wire != decimal.Truncate(wire))
                return (false, (ushort)0, "温度最多保留 0.1℃。 ");
            return (true, checked((ushort)wire), string.Empty);
        }
    };
}

public enum AfeWriteOutcome { NotApplied, RollbackSucceeded, ConfigurationInconsistent, Unconfirmed }
public sealed class AfeWriteException : IOException {
    public AfeWriteOutcome Outcome { get; }
    public string OutcomeText => Outcome switch {
        AfeWriteOutcome.NotApplied=>"参数未应用", AfeWriteOutcome.RollbackSucceeded=>"应用失败，旧配置已恢复",
        AfeWriteOutcome.ConfigurationInconsistent=>"配置不一致，输出禁止", _=>"结果未确认，请重新读取"
    };
    public AfeWriteException(AfeWriteOutcome outcome,string message,Exception? inner=null):base(message,inner) {Outcome=outcome;}
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
        if (effective[0] != 1 || effective[1] != requested[1])
            throw new IOException("AFE 有效配置读取失败（设备返回无效标记或型号不一致）。" +
                "这不表示请求/保存参数已损坏；请核对 AFE 通信与初始化状态，重新读取后再写入。");
        var info = new AfeHardwareDeviceInfo(
            requested[1], meta[0], meta[1] != 0, meta[2], meta[3], meta[4],
            meta[5] != 0, meta[6], meta[7], meta[8]);
        try {
            ushort[] ratio = await _bms.ReadRegistersAsync(0x252C, 2, ct);
            if (ratio[0] != 0 && ratio[1] is >= 1 and <= 16)
                info = info with { ShuntElementMicroOhm = ratio[0], ShuntParallelCount = (byte)ratio[1] };
        } catch (BmsModbusException ex) when (ex.Code == 2) { /* 旧固件不提供追加窗口。 */ }
        try {
            ushort[] features = await _bms.ReadRegistersAsync(0x252E, 1, ct);
            if (features[0]!=ushort.MaxValue) info=info with {HardwareFeatures=features[0]};
            if(features[0]!=ushort.MaxValue && (features[0]&4)!=0) {
                ushort[] status=await _bms.ReadRegistersAsync(0x253A,6,ct);
                if(status[0]==1)info=info with {FeatureStatus=status};
            }
        } catch (BmsModbusException ex) when (ex.Code==2) { /* 可选追加只读能力，兼容旧固件。 */ }
        return new AfeHardwareSnapshot(requested, effective, info);
    }

    public Task<AfeHardwareSnapshot> WriteAllAsync(ushort[] candidate, CancellationToken ct = default) =>
        WriteCandidateAsync(candidate, null, ct);

    public Task<AfeHardwareSnapshot> WriteAllAsync(AfeHardwareSnapshot snapshot, ushort[] candidate, CancellationToken ct=default) =>
        WriteCandidateAsync(candidate,new(snapshot.Requested.ToArray(),snapshot.Effective.ToArray(),snapshot.Info),ct);

    public Task<AfeHardwareSnapshot> WriteShortCircuitAsync(AfeHardwareSnapshot snapshot,
        ushort currentA10, ushort delayUs, CancellationToken ct = default)
    {
        var expected = new AfeHardwareSnapshot(snapshot.Requested.ToArray(), snapshot.Effective.ToArray(), snapshot.Info);
        var model = new AfeHardwareParameterModel();
        model.Load(expected);
        if (!model.TryBuildShortCircuitCandidate(currentA10, delayUs, out var candidate, out string error))
            throw new InvalidOperationException(error);
        return WriteCandidateAsync(candidate, expected, ct, preserveOcd2:true);
    }

    private async Task<AfeHardwareSnapshot> WriteCandidateAsync(ushort[] candidate,
        AfeHardwareSnapshot? expected, CancellationToken ct, bool preserveOcd2=false)
    {
        if (candidate.Length != AfeHardwareProtocolMap.ProfileWordCount)
            throw new ArgumentException("AFE hardware profile write must contain exactly 35 words.", nameof(candidate));

        candidate=candidate.ToArray();
        bool writeStarted=false;
        AfeHardwareAccessSession? session = null;
        try
        {
            session = await _bms.OpenAfeHardwareAccessAsync(ct);
            if (session.BackendModel != candidate[1])
                throw new IOException($"AFE access backend mismatch: session=0x{session.BackendModel:X4}, profile=0x{candidate[1]:X4}.");

            if (expected is not null)
            {
                AfeHardwareSnapshot latest = await ReadAllAsync(ct);
                if (!latest.Info.ProfileValid || !latest.Requested.SequenceEqual(expected.Requested) ||
                    latest.Effective[12] != expected.Effective[12] ||
                    latest.Info.BackendModel != expected.Info.BackendModel || latest.Info.Capabilities != expected.Info.Capabilities ||
                    latest.Info.ShuntMicroOhm != expected.Info.ShuntMicroOhm ||
                    latest.Info.ShuntElementMicroOhm != expected.Info.ShuntElementMicroOhm ||
                    latest.Info.ShuntParallelCount != expected.Info.ShuntParallelCount ||
                    latest.Info.HardwareFeatures != expected.Info.HardwareFeatures)
                    throw new IOException("AFE 参数或分流器信息已改变，本次参数未提交；请重新读取并确认档位。");
            }
            try
            {
                writeStarted=true;
                await _bms.WriteAfeProfileAsync(candidate, session, ct);
            }
            catch (Exception ex) when (!ct.IsCancellationRequested && (ex is IOException || ex is TimeoutException))
            {
                // 0x42 COMMIT 把校验/存储/应用/回滚错误合并为 status=4。
                // 失败后只读元数据，即使 effective 不可读也能报告实际失败阶段。
                bool rejected=ex is BmsAfeHardwareException access && access.Command==6 && access.Status==4 ||
                    ex is BmsModbusException mb && mb.Function==0x90;
                var detail = await ReadFailureStateAsync(candidate[1], rejected, ct);
                throw new AfeWriteException(detail.Outcome,ex.Message+"\n"+detail.Text,ex);
            }
            AfeHardwareSnapshot readback = await ReadAllAsync(ct);
            if (!readback.Requested.SequenceEqual(candidate))
            {
                int mismatch = Enumerable.Range(0, candidate.Length).First(i => candidate[i] != readback.Requested[i]);
                throw new IOException($"AFE硬件参数回读不一致：word[{mismatch}] target={candidate[mismatch]}, actual={readback.Requested[mismatch]}。");
            }
            if (!readback.Info.ProfileValid || readback.Info.ApplyState != 1 || readback.Info.LastError != 0)
                throw new IOException($"AFE apply state={readback.Info.ApplyStateText}, error={readback.Info.ErrorText}.");
            if ((readback.Info.Capabilities & AfeHardwareProtocolMap.TempChannelSelect) != 0 &&
                AfeHardwareProtocolMap.TemperatureChannels(readback.Requested[34]) !=
                AfeHardwareProtocolMap.TemperatureChannels(readback.Effective[34]))
                throw new IOException("温度通道 requested/effective 不一致，请重新读取；不要重复提交。");
            if (preserveOcd2 && expected is not null && readback.Effective[12] != expected.Effective[12])
                throw new IOException("短路保存后 OCD2 有效值与读取基准不一致，请重新读取设备，不要重复提交。");
            var verification=new AfeHardwareParameterModel();verification.Load(readback);
            if (!verification.VerifyHardwareReadback(candidate,readback.Effective,out string verificationError))
                throw new IOException(verificationError);
            return readback;
        }
        catch (AfeWriteException) { throw; }
        catch (Exception ex) when (ex is IOException || ex is TimeoutException || ex is OperationCanceledException) {
            throw new AfeWriteException(writeStarted?AfeWriteOutcome.Unconfirmed:AfeWriteOutcome.NotApplied,
                ex.Message+(writeStarted?"\n提交结果未确认；重新读取前不得重发。":"\n未发送完整参数提交。"),ex);
        }
        finally
        {
            if (session is not null)
                await _bms.TryCloseAfeHardwareAccessAsync(session.Token, CancellationToken.None);
        }
    }

    public static string DescribeFailureDetail(ushort[] d)
    {
        if(d.Length!=11 || d[0]!=1) return string.Empty;
        string[] names={"Schema","AFE型号","过压保护电压","过压延时","过压恢复电压","过压恢复确认",
            "欠压保护电压","欠压延时","欠压恢复电压","欠压恢复确认","OCD1电流","OCD1延时","OCD2电流","OCD2延时",
            "放电过流恢复电流","放电过流恢复确认","OCC1电流","OCC1延时","OCC2电流","OCC2延时","充电过流恢复电流",
            "充电过流恢复确认","短路电流","短路延时","保留短路恢复","充电高温保护","充电高温恢复","充电低温保护",
            "充电低温恢复","放电高温保护","放电高温恢复","放电低温保护","放电低温恢复","温度恢复确认","保护使能组合"};
        string text=$"\n固件事务序号：{d[1]}";
        if(d[2]<names.Length) text+=$"\n校验失败字段：word[{d[2]}] {names[d[2]]}（含范围/回差/依赖检查）。";
        for(int i=3;i<=7;i+=4) {
            string stage=i==3?"应用":"回滚";
            if(d[i+3]==4) {text+=$"\n{stage}持久化失败，未发起硬件应用。";continue;}
            if(d[i+3]==0 || d[i]==ushort.MaxValue)continue;
            string reason=d[i+3]==1?"写入失败":d[i+3]==2?"读回失败":"读回不一致";
            string actual=d[i+2]==ushort.MaxValue?"未读到":$"0x{d[i+2]:X2}";
            text+=$"\n{stage}寄存器 0x{d[i]:X2}：{reason}，期望0x{d[i+1]:X2}，实际{actual}。";
        }
        return text;
    }

    private async Task<(AfeWriteOutcome Outcome,string Text)> ReadFailureStateAsync(ushort backend, bool rejected, CancellationToken ct)
    {
        try
        {
            ushort[] meta = await _bms.ReadRegistersAsync(AfeHardwareProtocolMap.MetadataBase,
                AfeHardwareProtocolMap.MetadataWordCount, ct);
            var info = new AfeHardwareDeviceInfo(backend, meta[0], meta[1] != 0, meta[2], meta[3],
                meta[4], meta[5] != 0, meta[6], meta[7], meta[8]);
            string reason = info.LastError switch {
                1 => "设备报告授权校验失败。",
                2 => "设备报告完整参数校验拒绝；请核对全部启用参数的范围与恢复关系。",
                3 => "设备报告参数存储失败。",
                4 => "设备报告 AFE 应用或核对失败；请检查回滚状态。",
                5 => "设备报告回滚失败，必须保持输出禁止并排查硬件配置。",
                _ => "当前元数据不能确认本次提交结果，请重新读取 requested/effective；不要重复提交。"
            };
            string detail=string.Empty;
            try {
                ushort[] features=await _bms.ReadRegistersAsync(0x252E,1,ct);
                if (features[0]!=ushort.MaxValue && (features[0]&2)!=0) {
                    ushort[] diagnostic=await _bms.ReadRegistersAsync(0x252F,11,ct);
                    if(diagnostic[0]==1) detail=DescribeFailureDetail(diagnostic);
                }
            } catch(Exception ex) when(ex is IOException || ex is TimeoutException) { detail="\n可选诊断明细读取未完成。"; }
            AfeWriteOutcome outcome = !rejected ? AfeWriteOutcome.Unconfirmed :
                info.ApplyState==3 && info.LastError==5 ? AfeWriteOutcome.ConfigurationInconsistent :
                info.ApplyState==2 && info.LastError==4 ? AfeWriteOutcome.RollbackSucceeded :
                info.LastError is 1 or 2 or 3 ? AfeWriteOutcome.NotApplied : AfeWriteOutcome.Unconfirmed;
            return (outcome,$"失败后读回：apply_state={info.ApplyStateText} ({info.ApplyState})，" +
                $"last_error={info.ErrorText} ({info.LastError})。\n" + reason +
                detail+"\n元数据记录最近一次固件事务；通信结果不确定时，仍须重新读取 requested/effective 确认。");
        }
        catch (Exception ex) when (ex is IOException || ex is TimeoutException)
        {
            return (AfeWriteOutcome.Unconfirmed,"失败后状态读回未完成，提交结果未确认；请重新读取设备，不要重复提交。");
        }
    }
}
