using System.IO;

namespace BmsTool.Windows;

internal sealed record C073ProtectionReadback(ushort[] Saved,int[] Actual,int Tr,bool Matches);

internal static class C073Protection
{
    private const ushort Diagnostic=0x2A40, Service=0x2A7F;

    private static async Task RequireProfileAsync(BmsClient client,CancellationToken ct)
    {
        if (!client.IsC073) throw new InvalidOperationException("设备身份与 C073 不匹配。");
        ushort[] header=await client.ReadRegistersAsync(C077Hardware.Header,7,ct);
        if (header is not [0xC073,1,32,1,_,_,7])
            throw new InvalidDataException("设备未提供 C073 单片 SH367309 参数档案。");
        if (((await client.ReadRegistersAsync(0x2A28,1,ct))[0]&1)==0)
            throw new IOException("C073 Flash 参数尚未就绪。");
    }

    internal static void Validate(ushort[] values,int tr)
    {
        C077Hardware.Validate(values,tr);
        if ((values[0]&1)==0) throw new ArgumentException("C073 同口板要求 ENMOS 保持开启。");
    }

    private static async Task<ushort[]> WaitForIdleAsync(BmsClient client,CancellationToken ct)
    {
        for (int i=0;i<60;++i) {
            var diag=await client.ReadRegistersAsync(Diagnostic,15,ct);
            if (diag is not [0xC073,3,..]) throw new InvalidDataException("C073 芯片诊断版本不符。");
            if (diag[2]==0) return diag;
            await Task.Delay(200,ct);
        }
        throw new TimeoutException("309 维护仍在进行，请重新读取结果。");
    }

    internal static async Task<C073ProtectionReadback> ReadAsync(BmsClient client,CancellationToken ct)
    {
        await RequireProfileAsync(client,ct);
        await WaitForIdleAsync(client,ct);
        await client.WriteSingleRegisterAsync(Service,0xC087,ct); // 芯片只读复核
        var diag=await WaitForIdleAsync(client,ct);
        if (diag[3] is not (0 or 4)) throw new IOException($"309 芯片读取失败，错误码 {diag[3]}。");
        var saved=await client.ReadRegistersAsync(C077Hardware.ConfigBase,32,ct);
        var actual=await client.ReadRegistersAsync(0x2A60,13,ct);
        var expected=await client.ReadRegistersAsync(C077Hardware.ExpectedBase,15,ct);
        byte[] bytes=new byte[26];
        for (int i=0;i<actual.Length;++i) {
            bytes[2*i]=(byte)(actual[i]>>8);
            bytes[2*i+1]=(byte)actual[i];
        }
        int tr=bytes[25]&127;
        bool matches=diag[3]==0 && diag[8]==1 && expected[13]==0 && expected[14]==0 &&
            actual.AsSpan(0,12).SequenceEqual(expected.AsSpan(0,12)) && bytes[24]==(byte)(expected[12]>>8);
        return new(saved,C077Hardware.Decode(bytes,tr),tr,matches);
    }

    internal static async Task ApplyAsync(BmsClient client,ushort[] observed,ushort[] values,CancellationToken ct)
    {
        var before=await ReadAsync(client,ct);
        if (!before.Saved.SequenceEqual(observed)) throw new IOException("Flash 参数已改变，请重新读取。");
        Validate(values,before.Tr);
        if (!values.SequenceEqual(before.Saved)) {
            await client.WriteRegistersAsync(C077Hardware.ConfigBase,values,ct);
            if (!(await client.ReadRegistersAsync(C077Hardware.ConfigBase,32,ct)).SequenceEqual(values))
                throw new IOException("Flash 参数保存后回读不一致。");
        } else if (before.Matches) return;

        // 固件可在保存后自行编程，避免重复触发 EEPROM 写入。
        var service=await WaitForIdleAsync(client,ct);
        if (service[3] is not (0 or 4))
            throw new IOException($"Flash 已保存，309 自动维护报告错误 {service[3]}，请重新读取。");
        var after=await ReadAsync(client,ct);
        if (!after.Saved.SequenceEqual(values)) throw new IOException("Flash 参数在应用期间发生变化。");
        if (!after.Matches) {
            await client.WriteSingleRegisterAsync(Service,0xC073,ct);
            var result=await WaitForIdleAsync(client,ct);
            if (result[3]!=0) throw new IOException($"309 编程失败，错误码 {result[3]}，请读取芯片状态。");
            after=await ReadAsync(client,ct);
            if (!after.Matches || !after.Saved.SequenceEqual(values))
                throw new IOException("Flash 已保存，309 芯片生效值尚未确认。");
        }
    }
}
