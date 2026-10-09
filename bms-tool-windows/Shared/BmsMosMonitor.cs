using System.IO;
using System.Text;
using System.Text.Json;

namespace BmsTool.Windows;

// AFE 缓存、软件请求与命令是不同证据，均不等于 Gate/Vgs 实测。
public sealed record MosHistoryEntry(ushort[] Words)
{
    public ushort[]? FailureWords { get; set; }
    public string FailureDetail => FailureWords is { Length: 16 } f && BmsDiagnostics.U32(f,2) != 0 ?
        $"；最近 AFE 失效 #{BmsDiagnostics.U32(f,2)}：{FailureStage(f[6])}，底层错误 {(short)f[7]}，" +
        $"本次 FLAG2 {((f[8]&256)!=0 ? $"0x{f[8]&255:X2}" : "未成功读取")}，" +
        $"本次模式 {((f[8]&256)==0 ? "未知" : (f[8]&1024)!=0 ? "SLEEP" : (f[8]&512)!=0 ? "IDLE" : "NORMAL")}，" +
        $"VADC/CADC 数据年龄 {Age(f[9])}/{Age(f[10])} ms，采样耗时 {Age(f[11])} ms，" +
        $"CRC 累计 {BmsDiagnostics.U32(f,12)}，重试累计 {BmsDiagnostics.U32(f,14)}（现场 tick {BmsDiagnostics.U32(f,4)}）" : "";
    private static string Age(ushort ticks) => (ticks == 65535 ? ">=" : "") + (ticks/32.0).ToString("F2");
    public static string FailureStage(int stage) => stage switch {
        0=>"无失效记录", 1=>"AFE 唤醒", 2=>"状态寄存器读取", 3=>"单体电压读取",
        4=>"总电压读取", 5=>"电流读取", 6=>"温度读取", 7=>"电流换算",
        8=>"单体电压范围检查", 9=>"VADC 数据超过 400 ms", 10=>"CADC 数据超过 400 ms",
        11=>"负载/充电源恢复证据读取", 12=>"短路锁存恢复", 13=>"硬件保护标志恢复",
        14=>"AFE 初始化", 15=>"AFE 重配置", 16=>"硬件保护配置",
        17=>"MOS 命令写入/校验", 18=>"清标志失败后的隔离", 19=>"AFE 休眠",
        20=>"撤销输出授权时写入", _=>$"未知步骤 {stage}" };
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
    public string State => DriverValid ? Bits(Driver) : CommandValid && Command != 3 ?
        "命令：" + Bits(Command) + "；AFE 未知" : "未知（AFE 缓存无效）";
    public string Detail
    {
        get {
            if (!DriverValid) return (CommandValid && Command != 3 ? "固件已下达关闭命令；AFE 状态回读无效；" :
                "AFE 状态无效，不能认定 MOS 关闭；") + Reasons + FailureDetail;
            if (HasPrevious && Words[29] != 0 && (Words[28] & ~Driver & 3) != 0)
                return "AFE 报告开启→关闭；" + Reasons + FailureDetail;
            if (HasPrevious && Words[29] != 0 && (~Words[28] & Driver & 3) != 0)
                return "AFE 报告关闭→开启；" + Reasons + FailureDetail;
            return "请求/命令/资格/保护状态变化；" + Reasons + FailureDetail;
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
        if (HasPrevious && Words[29] == 0)
            return "采样有效性刚恢复，命令已开启，等待下一次状态回读（当前仍报告关闭）";
        return $"请求和命令仍开启，AFE 报告关闭，原因未确认（FLAG1/2=0x{Words[14]:X4}/0x{Words[15]:X4}）";
    }
    public static string Bits(int bits) => $"充电 {((bits & 1) != 0 ? "开" : "关")} / 放电 {((bits & 2) != 0 ? "开" : "关")}";
}

// 只计算有效命令的边沿；无效 AFE 缓存不应丢掉已经成功写入的关断证据。
public sealed class MosCommandTracker
{
    private ushort? _previous;
    private readonly uint?[] _offTicks = new uint?[2];
    private readonly bool[] _restored = new bool[2];
    public long OffEvents { get; private set; }
    public void ResetContinuity() { _previous = null; Array.Clear(_offTicks); Array.Clear(_restored); }
    public void ResetSession() { ResetContinuity(); OffEvents = 0; }
    public string Observe(MosHistoryEntry entry, bool continuous)
    {
        if (!continuous) ResetContinuity();
        if (!entry.CommandValid) { ResetContinuity(); return "；命令无效，计时中断"; }
        int off = _previous.HasValue ? _previous.Value & ~entry.Command & entry.Requested & 3 : 0;
        if (off != 0) ++OffEvents;
        string text = "";
        for (int channel = 0; channel < 2; ++channel) {
            int bit = 1 << channel;
            string name = channel == 0 ? "充电" : "放电";
            if ((off & bit) != 0) { _offTicks[channel] = entry.Tick32k; _restored[channel] = false; text += $"；{name}请求仍开，固件主动关命令"; }
            if (_offTicks[channel] is not { } tick) continue;
            double ms = unchecked(entry.Tick32k-tick)/32.0;
            if ((entry.Command&bit) != 0 && !_restored[channel]) {
                _restored[channel] = true;
                text += $"；{name}命令恢复，关闭命令持续 {ms:F2} ms";
                if (!entry.DriverValid || (entry.Driver&bit)==0) text += "，等待 AFE 状态回读";
            }
            if (_restored[channel] && entry.DriverValid && (entry.Driver&bit)!=0) {
                text += $"；{name} AFE 报告已恢复，距关命令 {ms:F2} ms（物理时长未测）";
                _offTicks[channel] = null;
            }
        }
        _previous = entry.Command;
        return text;
    }
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
    private bool? _mosFailureSupported;
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
        ushort[]? failureHeader = null;
        if (_mosFailureSupported != false) {
            try {
                var failure = await ReadRegistersAsync(0x3700, 32, ct);
                _mosFailureSupported = failure[0] == 0x4146 && failure[1] == 1;
                if (_mosFailureSupported == true) {
                    if (failure[3] != 8 || failure[4] != 16 || failure[5] > 8 || failure[15] != 32)
                        throw new InvalidDataException("AFE 失效扩展布局无效");
                    if (failure[2] == 1 && BmsDiagnostics.U32(failure,8) == result.BootTick &&
                        BmsDiagnostics.U32(failure,10) == result.BuildId) {
                        failureHeader = failure[..16];
                        if (result.Current is { } current && BmsDiagnostics.U32(failure,16) == current.Sequence)
                            current.FailureWords = failure[16..32];
                    }
                }
            }
            catch (BmsModbusException ex) when (ex.Function == 3 && ex.Code is 1 or 2) { _mosFailureSupported = false; }
        }
        uint wanted = afterSequence.HasValue && !reset ? unchecked(latest - afterSequence.Value) : header[5];
        uint count = Math.Min(wanted, header[5]);
        result.Missing = wanted - count;
        for (uint i = count; i > 0; --i) {
            uint seq = unchecked(latest - i + 1);
            ushort address = (ushort)(0x3430 + unchecked(seq - 1) % 8 * 32);
            var entry = new MosHistoryEntry(await ReadRegistersAsync(address, 32, ct));
            if (entry.Sequence == seq) {
                if (failureHeader is not null) {
                    var failure = await ReadRegistersAsync((ushort)(0x3720 + unchecked(seq-1)%8*16),16,ct);
                    if (BmsDiagnostics.U32(failure,0) == seq) entry.FailureWords = failure;
                }
                result.Entries.Add(entry);
            }
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
