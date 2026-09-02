using System.ComponentModel;
using System.Globalization;
using System.Runtime.CompilerServices;

namespace BmsTool.Windows;

public enum ProtectionValueKind
{
    Millivolt,
    PackVoltageHundredthVolt,
    CurrentTenthAmp,
    TemperatureOffsetTenthC,
    Percent,
    FilterRaw
}

public sealed class ProtectionParameterRow : INotifyPropertyChanged
{
    private ushort _deviceValue;
    private string _editValue = string.Empty;

    public required ushort Address { get; init; }
    public required string Group { get; init; }
    public required string Stage { get; init; }
    public required string FirmwareField { get; init; }
    public required ProtectionValueKind ValueKind { get; init; }

    public string AddressText => $"0x{Address:X4}";
    public string CustomerName => $"{Group} / {Stage}";
    public string Unit => ValueKind switch
    {
        ProtectionValueKind.Millivolt => "mV",
        ProtectionValueKind.PackVoltageHundredthVolt => "V",
        ProtectionValueKind.CurrentTenthAmp => "A",
        ProtectionValueKind.TemperatureOffsetTenthC => "°C",
        ProtectionValueKind.Percent => "%",
        _ => "滤波值"
    };

    public string Hint => ValueKind == ProtectionValueKind.FilterRaw ? "滤波参数" : Unit;

    public ushort DeviceValue
    {
        get => _deviceValue;
        private set
        {
            if (_deviceValue == value) return;
            _deviceValue = value;
            OnPropertyChanged();
            OnPropertyChanged(nameof(DeviceValueHex));
            OnPropertyChanged(nameof(DeviceDisplayValue));
            OnPropertyChanged(nameof(IsDirty));
        }
    }

    public string DeviceValueHex => $"0x{DeviceValue:X4}";
    public string DeviceDisplayValue => FormatEngineeringValue(DeviceValue);

    public string EditValue
    {
        get => _editValue;
        set
        {
            if (_editValue == value) return;
            _editValue = value;
            OnPropertyChanged();
            OnPropertyChanged(nameof(IsDirty));
        }
    }

    public bool IsDirty => TryParseEditedValue(out ushort value) && value != DeviceValue;

    public void LoadFromDevice(ushort value)
    {
        DeviceValue = value;
        _editValue = FormatEngineeringValue(value);
        OnPropertyChanged(nameof(EditValue));
        OnPropertyChanged(nameof(IsDirty));
    }

    public bool TryParseEditedValue(out ushort rawValue)
    {
        rawValue = 0;
        string text = (EditValue ?? string.Empty).Trim();
        if (text.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
            return ushort.TryParse(text[2..], NumberStyles.HexNumber, CultureInfo.InvariantCulture, out rawValue);

        if (!double.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out double engineering) &&
            !double.TryParse(text, NumberStyles.Float, CultureInfo.CurrentCulture, out engineering))
            return false;

        double raw = ValueKind switch
        {
            ProtectionValueKind.PackVoltageHundredthVolt => engineering * 100.0,
            ProtectionValueKind.CurrentTenthAmp => engineering * 10.0,
            ProtectionValueKind.TemperatureOffsetTenthC => (engineering + 40.0) * 10.0,
            _ => engineering
        };

        if (raw < 0 || raw > ushort.MaxValue) return false;
        rawValue = checked((ushort)Math.Round(raw, MidpointRounding.AwayFromZero));
        return true;
    }

    private string FormatEngineeringValue(ushort raw)
    {
        return ValueKind switch
        {
            ProtectionValueKind.PackVoltageHundredthVolt => (raw / 100.0).ToString("F2", CultureInfo.InvariantCulture),
            ProtectionValueKind.CurrentTenthAmp => (raw / 10.0).ToString("F1", CultureInfo.InvariantCulture),
            ProtectionValueKind.TemperatureOffsetTenthC => (raw / 10.0 - 40.0).ToString("F1", CultureInfo.InvariantCulture),
            _ => raw.ToString(CultureInfo.InvariantCulture)
        };
    }

    public event PropertyChangedEventHandler? PropertyChanged;
    private void OnPropertyChanged([CallerMemberName] string? name = null) => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
}

public static class ProtectionParameterCatalog
{
    private static readonly string[] Groups =
    {
        "单体过压", "单体欠压", "总压过压", "总压欠压", "充电过流", "放电过流",
        "充电高温", "充电低温", "放电高温", "放电低温", "MOS高温", "压差过大", "SOC低保护"
    };

    private static readonly string[] StageNames = { "一级", "二级", "三级", "恢复", "滤波" };

    private static readonly string[][] FirmwareFields =
    {
        new[] { "u16VcellOvp_First", "u16VcellOvp_Second", "u16VcellOvp_Third", "u16VcellOvp_Rcv", "u16VcellOvp_Filter" },
        new[] { "u16VcellUvp_First", "u16VcellUvp_Second", "u16VcellUvp_Third", "u16VcellUvp_Rcv", "u16VcellUvp_Filter" },
        new[] { "u16VbusOvp_First", "u16VbusOvp_Second", "u16VbusOvp_Third", "u16VbusOvp_Rcv", "u16VbusOvp_Filter" },
        new[] { "u16VbusUvp_First", "u16VbusUvp_Second", "u16VbusUvp_Third", "u16VbusUvp_Rcv", "u16VbusUvp_Filter" },
        new[] { "u16IchgOcp_First", "u16IchgOcp_Second", "u16IchgOcp_Third", "u16IchgOcp_Rcv", "u16IchgOcp_Filter" },
        new[] { "u16IdsgOcp_First", "u16IdsgOcp_Second", "u16IdsgOcp_Third", "u16IdsgOcp_Rcv", "u16IdsgOcp_Filter" },
        new[] { "u16TChgOTp_First", "u16TChgOTp_Second", "u16TChgOTp_Third", "u16TChgOTp_Rcv", "u16TChgOTp_Filter" },
        new[] { "u16TchgUTp_First", "u16TchgUTp_Second", "u16TchgUTp_Third", "u16TchgUTp_Rcv", "u16TchgUTp_Filter" },
        new[] { "u16TdischgOTp_First", "u16TdischgOTp_Second", "u16TdischgOTp_Third", "u16TdischgOTp_Rcv", "u16TdischgOTp_Filter" },
        new[] { "u16TdischgUTp_First", "u16TdischgUTp_Second", "u16TdischgUTp_Third", "u16TdischgUTp_Rcv", "u16TdischgUTp_Filter" },
        new[] { "u16TmosOTp_First", "u16TmosOTp_Second", "u16TmosOTp_Third", "u16TmosOTp_Rcv", "u16TmosOTp_Filter" },
        new[] { "u16VdeltaOvp_First", "u16VdeltaOvp_Second", "u16VdeltaOvp_Third", "u16VdeltaOvp_Rcv", "u16VdeltaOvp_Filter" },
        new[] { "u16SocUp_First", "u16SocUp_Second", "u16SocUp_Third", "u16SocUp_Rcv", "u16SocUp_Filter" },
    };

    public static IReadOnlyList<ProtectionParameterRow> Create()
    {
        var rows = new List<ProtectionParameterRow>(65);
        ushort address = BmsRegisters.Protect;
        for (int group = 0; group < Groups.Length; group++)
        {
            for (int stage = 0; stage < StageNames.Length; stage++, address++)
            {
                rows.Add(new ProtectionParameterRow
                {
                    Address = address,
                    Group = Groups[group],
                    Stage = StageNames[stage],
                    FirmwareField = FirmwareFields[group][stage],
                    ValueKind = GetKind(group, stage)
                });
            }
        }
        return rows;
    }

    private static ProtectionValueKind GetKind(int group, int stage)
    {
        if (stage == 4) return ProtectionValueKind.FilterRaw;
        return group switch
        {
            0 or 1 or 11 => ProtectionValueKind.Millivolt,
            2 or 3 => ProtectionValueKind.PackVoltageHundredthVolt,
            4 or 5 => ProtectionValueKind.CurrentTenthAmp,
            6 or 7 or 8 or 9 or 10 => ProtectionValueKind.TemperatureOffsetTenthC,
            12 => ProtectionValueKind.Percent,
            _ => ProtectionValueKind.FilterRaw
        };
    }
}
