using System.Globalization;
using System.IO;
using System.Text;
namespace BmsTool.Windows;

public sealed record DeviceEventLogRow(int Position, string EventName, string IntervalText, string StateText,
    uint Sequence = 0, DateTimeOffset? UtcTime = null, uint? UptimeSeconds = null, string SourceText = "旧格式",
    string LevelText = "—", ushort? MinCellMv = null, ushort? MaxCellMv = null, short? CurrentDecia = null,
    short? TemperatureDecic = null, ushort? SocPermille = null, byte CellIndex = 0, byte Detail = 0, ushort EventFlags = 0)
{
    public string TimeText => UtcTime?.ToLocalTime().ToString("yyyy-MM-dd HH:mm:ss") ??
        (UptimeSeconds is uint s ? $"未同步 · 启动后{s}秒" : "旧格式，无日期");
    public string VoltageText => MinCellMv is ushort lo && MaxCellMv is ushort hi ? $"{lo}～{hi} mV" : "未取得";
    public string CurrentText => CurrentDecia is short ma ? (ma / 10m).ToString("0.0", CultureInfo.InvariantCulture) + " A" : "未取得";
    public string TemperatureText => TemperatureDecic is short t ? (t / 10m).ToString("0.0", CultureInfo.InvariantCulture) + " ℃" : "未取得";
    public string SocText => SocPermille is ushort soc ? (soc / 10m).ToString("0.0", CultureInfo.InvariantCulture) + "%" : "未采集";
    public string SnapshotText => $"{VoltageText}；{CurrentText}；{TemperatureText}；SOC {SocText}";
    public string DetailText => StateText == "重复汇总" ? (Detail == 255 ? "重复≥255次" : $"重复{Detail}次") :
        EventName == "Flash存储异常" ? $"参数区={((Detail & 1) != 0 ? "异常" : "正常")}，SOC区={((Detail & 2) != 0 ? "异常" : "正常")}" :
        EventName == "BMS启动" ? Detail switch { 1 => "上电/外部复位", 2 => "看门狗复位", 3 => "软件复位", 4 => "深睡充电唤醒", _ => "未细分" } :
        EventName == "进入深度休眠" ? Detail switch { 2 => "持续欠压", 4 => "上位机休眠命令", 6 => "AFE持续失联", _ => $"未识别原因({Detail})" } :
        EventName == "时间设置" && Detail == 1 ? "首次取得真实时间" : CellIndex != 0 ? $"相关单体：第{CellIndex}串" : "—";
}
public sealed record DeviceEventHistory(IReadOnlyList<DeviceEventLogRow> Records, uint LatestSequence,
    uint ClearSequence, ushort Flags, ushort Pending, ushort Dropped, ushort Failures, uint ApproximateErases);

public sealed partial class BmsClient
{
    public const ushort EventHistoryInfoBase = 0x3800;
    public const ushort EventHistoryDataBase = 0x3900;
    public async Task<DeviceEventHistory> ReadEventHistoryAsync(CancellationToken ct = default)
    {
        // 分页读取必须前后核对顺序号和清空边界；只发布完整一致的一份结果。
        for (int attempt = 0; attempt < 2; attempt++)
        {
            ushort[] before = await ReadRegistersAsync(EventHistoryInfoBase, 16, ct).ConfigureAwait(false);
            ValidateEventHistoryInfo(before);
            if ((before[5] & 4) != 0) throw new IOException("设备正在初始化存储，请稍后重新读取日志。");
            var rows = new List<DeviceEventLogRow>(before[4]);
            for (int offset = 0; offset < before[4]; offset += 4)
            {
                int records = Math.Min(4, before[4] - offset);
                ushort[] raw = await ReadRegistersAsync((ushort)(EventHistoryDataBase + offset * 16),
                    (ushort)(records * 16), ct).ConfigureAwait(false);
                for (int i = 0; i < records; i++) rows.Add(DecodeEventHistoryRecord(raw.AsSpan(i * 16, 16), offset + i + 1));
            }
            ushort[] after = await ReadRegistersAsync(EventHistoryInfoBase, 16, ct).ConfigureAwait(false);
            ValidateEventHistoryInfo(after);
            if (before[4] != after[4] || Word32(before, 6) != Word32(after, 6) || Word32(before, 8) != Word32(after, 8)) continue;
            for (int i = 0; i < rows.Count; i++)
                if (rows[i].Sequence == 0 || (i != 0 && unchecked((int)(rows[i - 1].Sequence - rows[i].Sequence)) <= 0))
                    throw new IOException("日志顺序号无效，未发布本次读取结果。");
            return new(rows, Word32(after, 6), Word32(after, 8), after[5], after[10], after[11], after[12], Word32(after, 13));
        }
        throw new IOException("读取期间日志持续变化，请稍后重读；本次未显示混合记录。");
    }
    private static uint Word32(ReadOnlySpan<ushort> w, int i) => (uint)w[i] << 16 | w[i + 1];
    private static void ValidateEventHistoryInfo(ushort[] w)
    {
        if (w.Length != 16 || w[0] != 0x4C47 || w[1] != 1 || w[2] != 16 || w[3] != 100 || w[4] > 100 ||
            (w[5] & ~31) != 0 || w[10] > 16 || w[15] != 0)
            throw new IOException("设备日志接口版本或长度不支持；请使用配套的D015开发版固件。");
    }
    public static DeviceEventLogRow DecodeEventHistoryRecord(ReadOnlySpan<ushort> w, int position)
    {
        if (w.Length != 16 || w[13] != 0 || w[14] != 0 || w[15] != 0) throw new IOException("日志记录长度/保留字段异常。");
        byte id = (byte)(w[6] >> 8), flags = (byte)w[6];
        if (id is 0 or >= 32 || (flags & 128) != 0 || ((flags >> 2) & 3) > 2 || w[11] > 1000 || (w[12] >> 8) > 13)
            throw new IOException("日志事件编号、物理量或状态字段异常。");
        DateTimeOffset? utc = null;
        if ((flags & 64) != 0)
        {
            utc = DateTimeOffset.FromUnixTimeSeconds(Word32(w, 2));
            if (utc.Value.Year is < 2000 or > 2099) throw new IOException("日志日期超出设备RTC范围。");
        }
        string state = (flags & 3) switch { 1 => "发生", 2 => "稳定恢复", 3 => "重复汇总", _ => "普通事件" };
        string source = ((flags >> 2) & 3) switch { 1 => "软件保护", 2 => "AFE硬件保护", _ => "系统" };
        int level = (flags >> 4) & 3;
        return new(position, EventName(id), "—", state, Word32(w, 0), utc, Word32(w, 4), source,
            level == 0 ? "—" : $"{level}级", w[7] == 0xFFFF ? null : w[7], w[8] == 0xFFFF ? null : w[8],
            w[9] == 0x8000 ? null : unchecked((short)w[9]), w[10] == 0x8000 ? null : unchecked((short)w[10]),
            w[11], (byte)(w[12] >> 8), (byte)w[12], flags);
    }
    public static string EventName(byte id) => id switch
    {
        1 => "BMS启动", 2 => "进入深度休眠", 3 => "均衡开启", 4 => "加热开启", 5 => "制冷开启",
        6 or 22 => "单体过压", 7 => "总压过压", 8 or 26 => "充电过流", 9 or 23 => "单体欠压",
        10 => "总压欠压", 11 => "放电过流", 12 or 27 => "充电低温", 13 or 29 => "放电低温",
        14 or 28 => "充电高温", 15 or 30 => "放电高温", 16 => "单体压差保护", 17 => "硬件短路保护",
        18 => "AFE通信异常", 19 => "AFE备用事件", 20 => "Flash存储异常", 21 => "MOS高温",
        24 => "一级硬件放电过流", 25 => "二级硬件放电过流", 31 => "时间设置",
        32 => "硬件充电过流", 33 => "硬件放电过流", _ => $"未知事件({id})"
    };
}
public static class DeviceEventCsv
{
    public static string Export(IEnumerable<DeviceEventLogRow> rows, string identity, DateTimeOffset readUtc)
    {
        static string Escape(string value)
        {
            // Excel打开CSV时，设备SN等外部文字不能成为公式。
            if (value.Length != 0 && ("=+@".Contains(value[0]) || (value[0] == '-' && !decimal.TryParse(value, NumberStyles.Number, CultureInfo.InvariantCulture, out _)))) value = "'" + value;
            return "\"" + value.Replace("\"", "\"\"") + "\"";
        }
        var text = new StringBuilder();
        text.AppendLine(Escape("设备信息") + "," + Escape(identity.Replace("\r", "").Replace("\n", "；")));
        text.AppendLine(Escape("读取UTC") + "," + Escape(readUtc.ToString("O")));
        text.AppendLine("顺序号,事件UTC,显示本地时间,启动后秒数,事件,状态,来源,等级,最低单体mV,最高单体mV,电流A,温度℃,SOC%,相关串号,明细");
        foreach (var r in rows)
        {
            string[] fields = { r.Sequence.ToString(), r.UtcTime?.ToString("O") ?? "", r.TimeText, r.UptimeSeconds?.ToString() ?? "",
                r.EventName, r.StateText, r.SourceText, r.LevelText, r.MinCellMv?.ToString() ?? "", r.MaxCellMv?.ToString() ?? "",
                r.CurrentDecia is short a ? (a / 10m).ToString(CultureInfo.InvariantCulture) : "",
                r.TemperatureDecic is short t ? (t / 10m).ToString(CultureInfo.InvariantCulture) : "",
                r.SocPermille is ushort s ? (s / 10m).ToString(CultureInfo.InvariantCulture) : "", r.CellIndex.ToString(), r.DetailText };
            text.AppendLine(string.Join(",", fields.Select(Escape)));
        }
        return text.ToString();
    }
}
