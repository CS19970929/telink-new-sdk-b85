namespace BmsTool.Windows;

public sealed record ProtectionGroupWrite(ushort Address, ushort[] Values);

public static class ProtectionBatch
{
    private static readonly string[] Names = { "单体过压", "单体欠压", "总压过压", "总压欠压", "充电过流", "放电过流", "充电高温", "充电低温", "放电高温", "放电低温", "MOS高温", "压差过大", "SOC低保护" };
    public static string Name(ushort address) => Names[(address - 0x2100) / 5];

    public static IReadOnlyList<ProtectionGroupWrite> Plan(ushort[] current, IReadOnlyDictionary<ushort, ushort> changes)
    {
        if (current.Length != 65) throw new ArgumentException("保护参数读取长度必须为65。");
        var candidate = (ushort[])current.Clone();
        foreach (var change in changes) {
            if (change.Key < 0x2100 || change.Key > 0x2140) throw new ArgumentOutOfRangeException(nameof(changes));
            candidate[change.Key - 0x2100] = change.Value;
        }
        // Match the existing 65-word firmware validator. SOC legacy fields are not evaluated.
        for (int group = 0; group < 12; group++) {
            int n = group * 5;
            ushort first = candidate[n], second = candidate[n+1], third = candidate[n+2], recover = candidate[n+3];
            bool low = group is 1 or 3 or 7 or 9;
            if (low ? first < second || second < third : first > second || second > third)
                throw new ArgumentException($"{Names[group]}：三级顺序不合法，应满足一级{(low ? "≥" : "≤")}二级{(low ? "≥" : "≤")}三级。尚未写入。" );
            if (third != 0 && (low ? recover <= third : recover >= third))
                throw new ArgumentException($"{Names[group]}：恢复值必须{(low ? "大于" : "小于")}三级阈值。尚未写入。");
            if (group is >= 6 and <= 10 && (third > 1450 || recover > 1450))
                throw new ArgumentException($"{Names[group]}：温度编码超出固件范围。尚未写入。");
        }
        var result = new List<ProtectionGroupWrite>();
        for (int n=0; n<65; n+=5)
            if (!candidate.AsSpan(n,5).SequenceEqual(current.AsSpan(n,5)))
                result.Add(new((ushort)(0x2100+n), candidate.AsSpan(n,5).ToArray()));
        return result;
    }
}

public sealed partial class BmsClient
{
    public async Task WriteProtectionChangesAsync(IReadOnlyDictionary<ushort, ushort> changes,
        Action<ushort, ushort[]> verified, CancellationToken ct = default)
    {
        // Fresh values preserve all unedited fields. Each group is one atomic
        // 0x10 write: 9 + 5*2 = 19 bytes, within the MTU23 payload limit.
        var plan = ProtectionBatch.Plan(await ReadProtectionAllAsync(ct), changes);
        int completed = 0;
        foreach (var group in plan) {
            try {
                await WriteRegistersAsync(group.Address, group.Values, ct);
                ushort[] actual = await ReadRegistersAsync(group.Address, 5, ct);
                if (!actual.SequenceEqual(group.Values)) throw new System.IO.IOException("整组回读与写入值不一致。");
                verified(group.Address, actual);
                completed++;
            } catch (Exception ex) when (ex is not OperationCanceledException) {
                throw new System.IO.IOException($"{ProtectionBatch.Name(group.Address)}（0x{group.Address:X4}）写入/校验失败；此前{completed}组已确认成功。当前组可能已写入，请重新读取设备参数确认。{ex.Message}", ex);
            }
        }
    }
}
