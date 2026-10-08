using System.IO;
using System.Text;
using System.Text.Json;

namespace BmsTool.Windows;

// AFE 缓存、软件请求与命令是不同证据，均不等于 Gate/Vgs 实测。
public sealed record MosHistoryEntry(ushort[] Words)
{
    public uint Sequence => BmsDiagnostics.U32(Words, 0);
    public uint Tick32k => BmsDiagnostics.U32(Words, 2);
    public ushort Requested => Words[4];
    public ushort Command => Words[6];
    public bool CommandValid => Words[7] != 0;
    public ushort Driver => Words[8];
    public bool DriverValid => Words[9] != 0 && Words[19] != 0;
    public uint ChargeReasons => BmsDiagnostics.U32(Words, 10);
    public uint DischargeReasons => BmsDiagnostics.U32(Words, 12);
    public bool HasPrevious => Words[30] != 0;
    public string State => DriverValid ? Bits(Driver) : "未知（AFE 缓存无效）";
    public string Detail
    {
        get {
            if (!DriverValid) return "AFE 状态无效，不能认定 MOS 关闭；" + Reasons;
            if (HasPrevious && Words[29] != 0 && (Words[28] & ~Driver & 3) != 0)
                return "AFE 报告开启→关闭；" + Reasons;
            if (HasPrevious && Words[29] != 0 && (~Words[28] & Driver & 3) != 0)
                return "AFE 报告关闭→开启；" + Reasons;
            return "请求/命令/资格/保护状态变化；" + Reasons;
        }
    }
    public string Reasons => $"充电：{Cause(1, ChargeReasons)}；放电：{Cause(2, DischargeReasons)}" +
        (Words[27] == 0x3510 ? $"；BSTATUS1=0x{Words[31]:X2}，高侧报告 {Bits((Words[31] >> 4) & 3)}" : "") + "；物理 Gate 未测";
    private string Cause(int bit, uint reasons)
    {
        if (reasons != 0) return BmsDiagnostics.Reasons(reasons);
        if (!DriverValid) return "状态未知";
        if ((Driver & bit) != 0) return "AFE 报告开启";
        if ((Requested & bit) == 0) return "软件未请求开启";
        if (Words[27] == 0x3510) {
            var flags = new List<string>();
            ushort f1 = (ushort)(Words[14] & (bit == 1 ? 0x31 : 0x1E));
            ushort f2 = (ushort)(Words[15] & (bit == 1 ? 0x34 : 0xC4));
            foreach (var item in new[] { (1, "过压"), (2, "欠压"), (4, "放电过流1"), (8, "放电过流2"), (16, "短路"), (32, "充电过流") })
                if ((f1 & item.Item1) != 0) flags.Add(item.Item2);
            foreach (var item in new[] { (4, "AFE 看门狗"), (16, "充电低温"), (32, "充电高温"), (64, "放电低温"), (128, "放电高温") })
                if ((f2 & item.Item1) != 0) flags.Add(item.Item2);
            if (flags.Count != 0) return "AFE 相关标志：" + string.Join("、", flags) + "（需核对发生时序）";
        }
        if (!CommandValid) return "控制命令有效性未知";
        if ((Command & bit) == 0) return "命令未开启，具体原因未提供";
        return $"请求和命令仍开启，AFE 报告关闭，原因未确认（FLAG1/2=0x{Words[14]:X4}/0x{Words[15]:X4}）";
    }
    public static string Bits(int bits) => $"充电 {((bits & 1) != 0 ? "开" : "关")} / 放电 {((bits & 2) != 0 ? "开" : "关")}";
}

public sealed class MosHistoryCapture
{
    public ushort[]? Header { get; init; }
    public MosHistoryEntry? Current { get; init; }
    public List<MosHistoryEntry> Entries { get; } = new();
    public ushort[]? LegacyMosWords { get; init; }
    public bool Enabled => Header is { Length: 16 } && Header[2] == 1;
    public uint LatestSequence => Header is null ? 0 : BmsDiagnostics.U32(Header, 6);
    public uint BootTick => Header is null ? 0 : BmsDiagnostics.U32(Header, 10);
    public uint BuildId => Header is null ? 0 : BmsDiagnostics.U32(Header, 12);
    public uint Missing { get; set; }
    public bool Reset { get; init; }
    public string Availability => Enabled ? "固件 MOS 历史已开启" : Header is not null ?
        "固件 MOS 历史未启用（编译配置）" : LegacyMosWords is not null ?
        "旧固件：只有当前诊断，轮询之间的变化可能漏记" : "固件未提供 MOS 诊断，只记录上位机指示";
}

// 自动分卷且每条 Flush；不删除历史，磁盘失败由 UI 显示并停记。
public sealed class BmsMosJournal : IDisposable
{
    private StreamWriter? _writer;
    private int _part;
    private long _bytes;
    public string DirectoryPath { get; }
    public BmsMosJournal(string parent)
    {
        DirectoryPath = Path.Combine(parent, DateTime.Now.ToString("yyyyMMdd_HHmmss_fff") + "_" + Guid.NewGuid().ToString("N")[..8]);
        Directory.CreateDirectory(DirectoryPath);
        File.WriteAllText(Path.Combine(DirectoryPath, "说明.txt"),
            "MOS 自动监控，JSONL 每条含 UTC。sample=界面来源及测量背景，firmware=固件变化，gap=证据缺口。\n" +
            "软件请求、写入命令和 AFE 状态均不等于物理 Gate/Vgs 实测。不同协议窗口非原子采样。\n" +
            "固件 RAM 历史容量 8 条，掉电/深睡/复位清空；未采到的物理短脉冲无法证明。\n" +
            "记录自动分卷，无自动删除；AutoFlush 到操作系统，不保证突然断电持久化。\n", Encoding.UTF8);
    }
    public void Write(string kind, object data)
    {
        string line = JsonSerializer.Serialize(new { schema = 1, utc = DateTimeOffset.UtcNow, kind, data });
        if (_writer is null || _bytes >= 10 * 1024 * 1024) {
            _writer?.Dispose();
            var stream = new FileStream(Path.Combine(DirectoryPath, $"mos_{++_part:D4}.jsonl"), FileMode.CreateNew, FileAccess.Write, FileShare.Read);
            _writer = new StreamWriter(stream, new UTF8Encoding(false)) { AutoFlush = true };
            _bytes = 0;
        }
        _writer.WriteLine(line);
        _bytes += Encoding.UTF8.GetByteCount(line) + Environment.NewLine.Length;
    }
    public void Dispose() { _writer?.Dispose(); _writer = null; }
}

public sealed partial class BmsClient
{
    private bool? _mosHistorySupported, _mosSnapshotSupported;
    public async Task<MosHistoryCapture> ReadMosHistoryAsync(uint? afterSequence = null,
        uint? bootTick = null, uint? buildId = null, CancellationToken ct = default)
    {
        ushort[]? block = null;
        if (_mosHistorySupported != false) {
            try {
                block = await ReadRegistersAsync(0x3400, 48, ct);
                _mosHistorySupported = block[0] == 0x4D48 && block[1] == 1;
            }
            catch (BmsModbusException ex) when (ex.Function == 3 && ex.Code is 1 or 2) { _mosHistorySupported = false; }
        }
        if (_mosHistorySupported != true) {
            if (_mosSnapshotSupported is null) {
                try {
                    var probe = await ReadRegistersAsync(BmsDiagnostics.Base, 3, ct);
                    _mosSnapshotSupported = probe[0] == BmsDiagnostics.Magic && probe[1] == 1 && (probe[2] & 8) != 0;
                }
                catch (BmsModbusException ex) when (ex.Function == 3 && ex.Code is 1 or 2) { _mosSnapshotSupported = false; }
            }
            return new MosHistoryCapture { LegacyMosWords = _mosSnapshotSupported == true ?
                await ReadRegistersAsync(0x2A80, 32, ct) : null };
        }
        var header = block![..16];
        if (header[2] > 1 || header[3] != 8 || header[4] != 32 || header[5] > 8)
            throw new InvalidDataException("MOS 历史布局无效");
        if (header[2] == 0) return new MosHistoryCapture { Header = header,
            LegacyMosWords = await ReadRegistersAsync(0x2A80, 32, ct) };
        uint latest = BmsDiagnostics.U32(header, 6);
        bool reset = (bootTick.HasValue && bootTick != BmsDiagnostics.U32(header, 10)) ||
            (buildId.HasValue && buildId != BmsDiagnostics.U32(header, 12)) ||
            (afterSequence.HasValue && unchecked(latest - afterSequence.Value) >= 0x80000000u);
        var result = new MosHistoryCapture { Header = header, Reset = reset,
            Current = header[2] == 1 && header[5] != 0 ? new MosHistoryEntry(block[16..48]) : null };
        if (!result.Enabled) return result;
        uint wanted = afterSequence.HasValue && !reset ? unchecked(latest - afterSequence.Value) : header[5];
        uint count = Math.Min(wanted, header[5]);
        result.Missing = wanted - count;
        for (uint i = count; i > 0; --i) {
            uint seq = unchecked(latest - i + 1);
            ushort address = (ushort)(0x3430 + unchecked(seq - 1) % 8 * 32);
            var entry = new MosHistoryEntry(await ReadRegistersAsync(address, 32, ct));
            if (entry.Sequence == seq) result.Entries.Add(entry);
            else ++result.Missing; // 并发覆盖：不能把其他序号当成本条。
        }
        if (count != 0) {
            var tail = await ReadRegistersAsync(0x3400, 16, ct);
            if (tail[0] != header[0] || tail[1] != header[1] || tail[2] != header[2] ||
                BmsDiagnostics.U32(tail, 10) != result.BootTick || BmsDiagnostics.U32(tail, 12) != result.BuildId ||
                unchecked(BmsDiagnostics.U32(tail, 6) - latest) >= 0x80000000u)
                throw new InvalidDataException("MOS 历史读取期间设备重启或布局变化，cursor 未推进");
        }
        return result;
    }
}
