using System.IO;

namespace BmsTool.Windows;

/// <summary>只读断线检测快照；完整原始字用于后续诊断，不能代替物理采样线验证。</summary>
public sealed record BmsOpenWireStatus(ushort[] Words)
{
    public const ushort Base = 0x3600, WordCount = 48, Capability = 0x0100;
    public uint Sequence => BmsDiagnostics.U32(Words, 6);
    public uint UpdatedTick => BmsDiagnostics.U32(Words, 8);
    public uint ReadTick => BmsDiagnostics.U32(Words, 44);
    public uint Attempts => BmsDiagnostics.U32(Words, 16);
    public uint Completed => BmsDiagnostics.U32(Words, 18);
    public uint Failed => BmsDiagnostics.U32(Words, 20);
    public uint FaultMask => BmsDiagnostics.U32(Words, 22);
    public uint ResultMask => BmsDiagnostics.U32(Words, 24);
    public uint BuildId => BmsDiagnostics.U32(Words, 42);
    public bool Active => (Words[4] & 1) != 0;
    public bool Fault => (Words[4] & 4) != 0;
    public bool Fresh => unchecked(ReadTick - UpdatedTick) <= 5u * 32000u;
    public string Backend => Words[2] switch { 1 => "DVC1124", 2 => "SH3673510", _ => $"未知({Words[2]})" };
    public string StateText => Words[3] switch {
        0 => "等待检测", 1 => "正在检测（不主动关 MOS）", 2 => "最近检测正常",
        3 => "断线检测故障，保护关断", 4 => "检测失败，等待重试",
        5 => "激励退出待确认", _ => $"未知状态({Words[3]})" };
    public static string ErrorText(ushort error) => error switch {
        0 => "无", 1 => "启动失败", 2 => "AFE 读写失败", 3 => "检测超时",
        4 => "结果不完整或映射不确定", 5 => "退出激励失败", 6 => "AFE 样本失效",
        7 => "电流/加热/安全资格变化，检测中止",
        _ => $"未知错误({error})" };
    public string FaultCells {
        get {
            var cells = new List<int>();
            for (int i = 0; i < Math.Min(24, (int)Words[31]); i++)
                if ((FaultMask & (1u << i)) != 0) cells.Add(i + 1);
            return cells.Count == 0 ? "无已锁存故障" : string.Join(", ", cells.Select(x => "C" + x));
        }
    }
    public string Eligibility {
        get {
            var reasons = new List<string>();
            if ((Words[39] & 1) != 0) reasons.Add("有充放电电流");
            if ((Words[39] & 2) != 0) reasons.Add("加热请求/运行中");
            if ((Words[39] & 4) != 0) reasons.Add("安全门禁未满足");
            if ((Words[39] & 8) != 0) reasons.Add("均衡停止/回读未确认");
            return reasons.Count == 0 ? "等待计划时间和空闲资格" : string.Join("；", reasons);
        }
    }
    public string Summary => $"{Backend} · {(Fresh ? StateText : "固件状态过期，不能判断当前检测阶段")}\n" +
        $"断线保护：{(Fault ? "已锁存" : "未锁存")} · {FaultCells} · 可疑采样：{((Words[4] & 2) != 0 ? "是" : "否")}\n" +
        $"启动 {Attempts} 次 / 完成 {Completed} 次 / 失败 {Failed} 次；本次耗时 {BmsSleepStatus.Duration(BmsDiagnostics.U32(Words, Active ? 34 : 46))}\n" +
        $"重检等待 {BmsSleepStatus.Duration(BmsDiagnostics.U32(Words, 32))}；{Eligibility}\n" +
        $"当前错误：{ErrorText(Words[5])}；保留的最近失败：{ErrorText(Words[38])}\n" +
        $"阶段覆盖 0x{Words[30]:X}；最近结果 0x{ResultMask:X6}；原始 IND=0 0x{BmsDiagnostics.U32(Words,26):X6} / IND=1 0x{BmsDiagnostics.U32(Words,28):X6}";
    public static BmsOpenWireStatus Decode(ushort[] words) {
        if (words.Length != WordCount || words[0] != 0x4F57 || words[1] != 1 || words[31] > 24)
            throw new InvalidDataException("断线检测快照格式或版本不支持");
        return new BmsOpenWireStatus((ushort[])words.Clone());
    }
}

public sealed partial class BmsClient
{
    private bool? _openWireSupported;
    public async Task<BmsOpenWireStatus?> ReadOpenWireStatusAsync(CancellationToken ct = default)
    {
        if (_openWireSupported is null) {
            try {
                var header = await ReadRegistersAsync(BmsDiagnostics.Base, 3, ct);
                _openWireSupported = header[0] == BmsDiagnostics.Magic && header[1] == BmsDiagnostics.Schema &&
                    (header[2] & BmsOpenWireStatus.Capability) != 0;
            } catch (BmsModbusException ex) when (ex.Function == 3 && ex.Code is 1 or 2) {
                _openWireSupported = false;
            }
        }
        return _openWireSupported == true ?
            BmsOpenWireStatus.Decode(await ReadRegistersAsync(BmsOpenWireStatus.Base, BmsOpenWireStatus.WordCount, ct)) : null;
    }
}
