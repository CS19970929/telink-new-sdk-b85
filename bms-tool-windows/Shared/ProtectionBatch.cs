namespace BmsTool.Windows;

public sealed record ProtectionGroupWrite(ushort Address, ushort[] Values);

public static class ProtectionBatch
{
    private static readonly string[] Names = { "单体过压", "单体欠压", "总压过压", "总压欠压", "充电过流", "放电过流", "充电高温", "充电低温", "放电高温", "放电低温", "MOS高温", "压差过大", "SOC低保护" };
    public static string Name(ushort address) => Names[(address - 0x2100) / 5];

    public static IReadOnlyList<ProtectionGroupWrite> Plan(ushort[] current, IReadOnlyDictionary<ushort, ushort> changes, bool d015 = false, bool c060 = false, bool c077 = false)
    {
        if (current.Length != 65) throw new ArgumentException("保护参数读取长度必须为65。");
        var candidate = (ushort[])current.Clone();
        foreach (var change in changes) {
            if (change.Key < 0x2100 || change.Key > 0x2140) throw new ArgumentOutOfRangeException(nameof(changes));
            candidate[change.Key - 0x2100] = change.Value;
        }
        if(c077)C077Parameters.ValidateProtection(candidate);
        else if (c060) ValidateC060(candidate);
        // Match the existing 65-word firmware validator. SOC legacy fields are not evaluated.
        for (int group = 0; !c060 && !c077 && group < 12; group++) {
            int n = group * 5;
            ushort first = candidate[n], second = candidate[n+1], third = candidate[n+2], recover = candidate[n+3];
            bool low = group is 1 or 3 or 7 or 9;
            if (d015) {
                ushort previous=0;
                foreach(ushort level in new[]{first,second,third}) {
                    if(level==0)continue;
                    if(previous!=0 && (low?previous<level:previous>level))
                        throw new ArgumentException($"{Names[group]}：已启用级的顺序不合法。");
                    previous=level;
                }
                if(group is >=6 and <=10 && new[]{first,second,third,recover}.Any(v=>v>1650))
                    throw new ArgumentException($"{Names[group]}：温度范围为-40～125℃。");
            } else {
                if (low ? first < second || second < third : first > second || second > third)
                    throw new ArgumentException($"{Names[group]}：三级顺序不合法。");
                if (group is >= 6 and <= 10 && (third > 1450 || recover > 1450))
                    throw new ArgumentException($"{Names[group]}：温度编码超出固件范围。");
            }
            if (third != 0 && (low ? recover <= third : recover >= third))
                throw new ArgumentException($"{Names[group]}：恢复值与三级阈值没有正确滞回。");
        }
        var result = new List<ProtectionGroupWrite>();
        for (int n=0; n<65; n+=5)
            if (!candidate.AsSpan(n,5).SequenceEqual(current.AsSpan(n,5)))
                result.Add(new((ushort)(0x2100+n), candidate.AsSpan(n,5).ToArray()));
        return result;
    }
    public static void ValidateC060(ushort[] words)
    {
        if (words.Length != 65) throw new ArgumentException("C060保护参数必须为65字。");
        // Bounds come from C060 Fault.h. Zero is a threshold, not a per-level disable flag.
        ushort[] minimum = {1000,1000,300,300,10,10,400,0,400,0,400,10,0};
        ushort[] maximum = {5000,5000,20000,20000,50000,50000,2000,800,2000,800,2000,2000,50};
        for (int group = 0; group < 13; group++) {
            int n=group*5;
            for(int field=0;field<4;field++)
                if(words[n+field]<minimum[group] || words[n+field]>maximum[group])
                    throw new ArgumentException($"{Names[group]}：原始编码须在{minimum[group]}～{maximum[group]}内；C060不使用0禁用保护级。");
            if(words[n+4] is <1 or >50000)
                throw new ArgumentException($"{Names[group]}：Filter须为1～50000个10ms扫描计数。");
            bool low=group is 1 or 3 or 7 or 9 or 12;
            if(low ? words[n]<words[n+1] || words[n+1]<words[n+2] : words[n]>words[n+1] || words[n+1]>words[n+2])
                throw new ArgumentException($"{Names[group]}：First、Second、Third顺序不合法。");
            if(low ? words[n+3]<=words[n+2] : words[n+3]>=words[n+2])
                throw new ArgumentException($"{Names[group]}：Recovery须与Third保留正确滞回。");
        }
    }

}

public sealed partial class BmsClient
{
    public async Task WriteProtectionChangesAsync(IReadOnlyDictionary<ushort, ushort> changes,
        Action<ushort, ushort[]> verified, CancellationToken ct = default)
    {
        await _protectionWriteGate.WaitAsync(ct);
        try {
            // The full candidate is checked before any write; untouched fields come from the device.
            var current=await ReadProtectionAllAsync(ct);
            var plan = ProtectionBatch.Plan(current, changes, IsD015, UsesC060Protocol, IsC077);
            if(IsC077) {
                var candidate=(ushort[])current.Clone();
                foreach(var change in changes)candidate[change.Key-0x2100]=change.Value;
                C077Parameters.ValidateProtection(candidate);
                if((candidate[3]!=current[3]||candidate[8]!=current[8])&&
                   (candidate[8]==0||candidate[8]>candidate[3]||candidate[8]>=4260||candidate[3]<=2600))
                    throw new ArgumentException("C077单体恢复窗口须满足0<UV≤OV、UV<4260mV、OV>2600mV。");
                if(C077Parameters.OvercurrentCode(candidate[22])!=C077Parameters.OvercurrentCode(current[22])||
                   C077Parameters.OvercurrentCode(candidate[27])!=C077Parameters.OvercurrentCode(current[27]))await RequireC077OutputsOffAsync(ct);
            }
            int completed = 0;
            foreach (var group in plan) {
                string stage = "发送/等待提交应答";
                try {
                    ct.ThrowIfCancellationRequested();
                    await WriteRegistersAsync(group.Address, group.Values, ct);
                    stage = "回读校验";
                    ushort[] actual = await ReadRegistersAsync(group.Address, 5, ct);
                    if (!actual.SequenceEqual(group.Values)) throw new System.IO.IOException("整组回读与写入值不一致。");
                    verified(group.Address, actual);
                    completed++;
                } catch (OperationCanceledException ex) {
                    throw new OperationCanceledException($"已取消；此前{completed}组已确认，当前组{ProtectionBatch.Name(group.Address)}可能已保存，请重新读取。", ex, ct);
                } catch (Exception ex) {
                    throw new System.IO.IOException($"{ProtectionBatch.Name(group.Address)}（0x{group.Address:X4}）{stage}失败；此前{completed}组已确认成功。当前组可能已写入，请重新读取设备参数确认。{ex.Message}", ex);
                }
            }
        } finally { _protectionWriteGate.Release(); }
    }
    private readonly SemaphoreSlim _protectionWriteGate = new(1,1);
}
