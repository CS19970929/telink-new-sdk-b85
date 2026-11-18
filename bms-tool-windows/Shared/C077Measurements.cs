using System.IO;

namespace BmsTool.Windows;

public sealed class C077Measurements
{
    public const ushort Address = 0x2C00, WordCount = 103;
    private readonly ushort[] _words;

    private C077Measurements(ushort[] words) => _words = words;

    public static C077Measurements Parse(ushort[] words)
    {
        if (words.Length != WordCount || words[0] != 0xC077 || words[1] != 1 ||
            words[11] > 2 || words[16] > 2 || words[17] > 8 ||
            words[21] is < 5 or > 16 || words[22] > 3 || words[23] > 3)
            throw new InvalidDataException("C077 实时测量快照格式或版本无效。");
        int installed = (1 << words[21]) - 1;
        if (((words[3] | words[6] | words[7]) & ~installed) != 0 ||
            (words[4] & words[5]) != 0 || ((words[4] | words[5]) & ~words[20]) != 0 ||
            words[3] != (words[11] == 0 ? 0 : words[5 + words[11]]) ||
            (words[10] & ~(words[6] & words[7])) != 0)
            throw new InvalidDataException("C077 实时测量快照中的通道状态不一致。");
        return new(words);
    }

    internal ReadOnlySpan<ushort> Report => _words.AsSpan(24, 63);
    internal uint SystemStatus => (uint)_words[18] | ((uint)_words[19] << 16);
    public bool CurrentValid => (_words[2] & 1) != 0;
    public bool PackVoltageValid => (_words[2] & 2) != 0;
    public bool ControlInputsReady => (_words[2] & 4) != 0;
    public bool BatteryNtcFault => (_words[2] & 8) != 0;
    public bool MosAdcFault => (_words[2] & 16) != 0;
    public bool NtcSleepTimeMet => (_words[2] & 32) != 0;
    public ushort CellValidMask => _words[3];
    public ushort TemperatureValidMask => _words[4];
    public ushort TemperatureInvalidMask => _words[5];
    public ushort Afe1CellValidMask => _words[6];
    public ushort Afe2CellValidMask => _words[7];
    public ushort Afe1Faults => _words[8];
    public ushort Afe2Faults => _words[9];
    public ushort MismatchMask => _words[10];
    public int VoltageSource => _words[11];
    public ushort Afe1VadcAgeMs => _words[12];
    public ushort Afe2VadcAgeMs => _words[13];
    public ushort CurrentAgeMs => _words[14];
    public ushort MosSampleAgeMs => _words[15];
    public int PowerMode => _words[16];
    public int DeepReason => _words[17];
    public ushort RequiredTemperatureMask => _words[20];
    public int SeriesCount => _words[21];
    public int CompletePacks => _words[22];
    public ushort VadcValidMask => _words[23];
    public bool BatteryTemperatureAvailable => (TemperatureValidMask & 0x1B) != 0;
    public bool MosTemperatureAvailable => (TemperatureValidMask & (1 << 9)) != 0;

    public bool CellValid(int index) => (uint)index < 16u && (CellValidMask & (1 << index)) != 0;

    public IReadOnlyList<double?> TemperaturesC => Enumerable.Range(0, 10)
        .Select(i => (TemperatureValidMask & (1 << i)) != 0 ? (double?)(_words[24 + 38 + i] / 10.0 - 40.0) : null)
        .ToArray();

    public IReadOnlyList<int?> CellDifferencesMv => Enumerable.Range(0, SeriesCount)
        .Select(i => ((Afe1CellValidMask & Afe2CellValidMask) & (1 << i)) != 0
            ? (int?)unchecked((short)_words[87 + i]) : null).ToArray();

    public string TemperatureDetails
    {
        get
        {
            (int Slot, string Label)[] sensors = [(0,"AFE1 T1"), (1,"AFE1 T2"), (3,"AFE2 T1"), (4,"AFE2 T2"), (9,"MOS NTC")];
            var values = TemperaturesC;
            return string.Join(" · ", sensors.Where(s => (RequiredTemperatureMask & (1 << s.Slot)) != 0)
                .Select(s => $"{s.Label} {(values[s.Slot] is double value ? $"{value:F1} ℃" :
                    (TemperatureInvalidMask & (1 << s.Slot)) != 0 ? "— 异常" :
                    SensorAge(s.Slot) != ushort.MaxValue ? "— 数据失效" : "— 等待数据")}"));
        }
    }

    private ushort SensorAge(int slot) => slot switch { < 3 => Afe1VadcAgeMs, < 9 => Afe2VadcAgeMs, _ => MosSampleAgeMs };

    public string VoltageDetails => VoltageSource == 0 ? "电压数据无效" :
        $"AFE{VoltageSource} 电压 · {FormatAge(VoltageSource == 1 ? Afe1VadcAgeMs : Afe2VadcAgeMs)}" +
        (CompletePacks != 3 ? " · 双片电压未齐" : "");

    private static string FormatAge(ushort age) => age == ushort.MaxValue ? "采样时间未知" : $"{age} ms 前采样";

    private static readonly string[] FaultNames =
    [
        "I²C 故障", "CRC 错误", "配置档案不符", "器件故障", "单体采样异常", "VADC 过期",
        "CADC 过期", "NTC 异常", "CADC 启动检查中", "CADC 启动检查失败", "测量不可用",
        "等待首次 VADC", "等待首次 CADC"
    ];

    public string FaultDetails
    {
        get
        {
            var messages = new List<string>();
            var chipFaults = new[] { Afe1Faults, Afe2Faults };
            for (int chip = 0; chip < chipFaults.Length; chip++)
            {
                var faults = Enumerable.Range(0, FaultNames.Length)
                    .Where(bit => (chipFaults[chip] & (1 << bit)) != 0).Select(bit => FaultNames[bit]).ToArray();
                if (faults.Length != 0) messages.Add($"AFE{chip + 1} {string.Join("、", faults)}");
            }
            if (MismatchMask != 0)
            {
                var differences = CellDifferencesMv;
                var cells = Enumerable.Range(0, SeriesCount).Where(i => (MismatchMask & (1 << i)) != 0)
                    .Select(i =>
                    {
                        int value = _words[24 + i], delta = differences[i].GetValueOrDefault();
                        int first = VoltageSource == 2 ? value + delta : value;
                        int second = VoltageSource == 2 ? value : value - delta;
                        return $"第{i + 1}串 {first}/{second} mV";
                    });
                messages.Add("双片电压不一致 " + string.Join("、", cells));
            }
            if (BatteryNtcFault && (TemperatureInvalidMask & 0x1B) == 0) messages.Add("电池 NTC 恢复确认中");
            if (MosAdcFault) messages.Add("MOS NTC 采样故障");
            return string.Join("；", messages);
        }
    }

    public string SleepDetails => DeepReason switch
    {
        1 => "请求深睡", 2 => "欠压深睡", 3 => "低单体电压深睡",
        4 => "AFE 故障深睡", 5 => "存储故障深睡", 6 => "MOS 驱动故障深睡",
        7 => "NTC 故障深睡", 8 => "双片电压不一致深睡",
        _ => PowerMode == 1 ? "RTC 巡检" : NtcSleepTimeMet ? "RUN · NTC 故障达到深睡时长" : "RUN"
    };
}
