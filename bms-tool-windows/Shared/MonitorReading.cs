using System.Diagnostics;
using System.Globalization;

namespace BmsTool.Windows;

// 设备解析结果；显示与记录共同消费，不从TextBlock反向读取数值。
public sealed record MonitorReading
{
    public DateTimeOffset ObservedUtc { get; init; } = DateTimeOffset.UtcNow;
    public DateTimeOffset? CellsUtc { get; init; }
    [System.Text.Json.Serialization.JsonIgnore]
    public long ReceivedTick { get; init; } = Stopwatch.GetTimestamp();
    public uint SourceAgeMs { get; init; }
    public string Source { get; init; } = "";
    public string Endpoint { get; init; } = "";
    public string Protocol { get; init; } = "";
    public string SegmentKey { get; init; } = "";
    public string Identity { get; init; } = "";
    public string Sequence { get; init; } = "";
    public double? Soc { get; init; }
    public double? Voltage { get; init; }
    public double? Current { get; init; }
    public double? Temperature { get; init; }
    public double? MinTemperature { get; init; }
    public double? MosTemperature { get; init; }
    public int? Cycles { get; init; }
    public string? Fault { get; init; }
    public byte? FaultCode { get; init; }
    public bool HasCurrent { get; init; }
    public bool HasFault { get; init; }
    public bool HasCells { get; init; }
    public double CurrentResolutionA { get; init; } = 0.1;
    public IReadOnlyDictionary<int, double> Cells { get; init; } = new Dictionary<int, double>();
    [System.Text.Json.Serialization.JsonIgnore]
    public bool IsFresh => SourceAgeMs + Stopwatch.GetElapsedTime(ReceivedTick).TotalMilliseconds
        <= (Source == "一线通" ? 5000 : 8000);

    public static MonitorReading? FromOwc(IReadOnlyDictionary<byte, OwcSnapshot> snapshots, string endpoint)
    {
        if (!CommToolOwcProtocol.Online(snapshots) || !snapshots.TryGetValue(0, out var latest)) return null;
        bool lishen = latest.Protocol == OwcWireProtocol.LishenV111;
        byte id = lishen ? (byte)0x80 : (byte)0x3A;
        if (!snapshots.TryGetValue(id, out var live)) return null;
        var fields = CommToolOwcProtocol.VisibleFields(live, snapshots).ToDictionary(x => x.Name, x => x.Value);
        double? Value(string key) => fields.TryGetValue(key, out var value) &&
            double.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out double number) &&
            double.IsFinite(number) ? number : null;
        bool extended = lishen && CommToolOwcProtocol.IsLishenExtended(live.Raw);
        var cells = new Dictionary<int, double>();
        DateTimeOffset? cellUtc = null;
        if (!lishen && snapshots.TryGetValue(0x3B, out var cellPacket))
        {
            foreach (var field in CommToolOwcProtocol.VisibleFields(cellPacket, snapshots))
                if (field.Name.StartsWith("单体 ") && int.TryParse(field.Name[3..], out int index) &&
                    double.TryParse(field.Value, NumberStyles.Float, CultureInfo.InvariantCulture, out double mv) &&
                    mv > 0 && mv <= 10000 && mv != 61001) cells[index] = mv;
            if (cells.Count != 0) cellUtc = DateTimeOffset.UtcNow.AddMilliseconds(-cellPacket.AgeMs);
        }
        string identity = "";
        if (!lishen && snapshots.TryGetValue(0x3C, out var serial))
            identity = CommToolOwcProtocol.VisibleFields(serial, snapshots)
                .FirstOrDefault(f => f.Name == "电池编码")?.Value ?? "";
        return new()
        {
            Source = "一线通", Endpoint = endpoint,
            Protocol = extended ? "锂神扩展（本项目）" : CommToolOwcProtocol.ProtocolName(latest.Protocol),
            SegmentKey = $"{endpoint}/{latest.Session}/{latest.Protocol}/{extended}/{identity}",
            Identity = identity, Sequence = $"{latest.Session}/{live.Sequence}",
            SourceAgeMs = live.AgeMs, CellsUtc = cellUtc,
            Soc = Value("SOC"), Voltage = Value("总电压"), Current = Value("电流"),
            Temperature = Value(lishen ? "电池温度" : "最高温度"),
            MinTemperature = Value("最低温度"), MosTemperature = Value("MOS温度"),
            Cycles = Value("循环次数") is double cycles ? (int)cycles : null,
            HasCurrent = !lishen || extended, HasFault = !lishen || extended, HasCells = !lishen,
            CurrentResolutionA = lishen ? 1 : .1,
            FaultCode = fields.TryGetValue("故障码", out var code) &&
                byte.TryParse(code.Replace("0x", ""), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out byte fault)
                ? fault : null,
            Fault = fields.GetValueOrDefault("故障"), Cells = cells
        };
    }
}
