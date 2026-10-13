using System.Diagnostics;
using System.IO;
namespace BmsTool.Windows;
public sealed partial class BmsClient
{
    // 沿用四 word 原子参数组；先读当前设备值，避免开关操作带入编辑框中的阈值。
    public async Task SetD008BalanceAllowedAsync(bool allowed,CancellationToken ct=default)
    {
        await RequireD008Async(ct);
        var values=await ReadRegistersAsync(D008Parameters.BalanceBase,4,ct);
        D008Parameters.ValidateBalance(values);
        values[0]=(ushort)(allowed?1:0);
        await WriteD008VerifiedAsync(D008Parameters.BalanceBase,values,false,ct);
    }
    public async Task WriteD008BalanceThresholdsAsync(string startMv,string startDeltaMv,string stopDeltaMv,CancellationToken ct=default)
    {
        var values=D008Parameters.Balance(false,startMv,startDeltaMv,stopDeltaMv);
        await RequireD008Async(ct);
        var current=await ReadRegistersAsync(D008Parameters.BalanceBase,4,ct);
        D008Parameters.ValidateBalance(current);
        values[0]=current[0]; // 保存阈值不得顺带改变持久许可。
        await WriteD008VerifiedAsync(D008Parameters.BalanceBase,values,false,ct);
    }
    public async Task WriteD008VerifiedAsync(ushort address,ushort[] values,bool requiresAfeAccess,CancellationToken ct=default)
    {
        if(address==BmsSleepParameters.Base)BmsSleepParameters.Validate(values);
        if(address==D008Parameters.BalanceBase)D008Parameters.ValidateBalance(values);
        if(address==D008Parameters.HeaterBase)D008Parameters.ValidateHeater(values);
        await RequireD008Async(ct);
        if(address==D008Parameters.BalanceBase || address==D008Parameters.HeaterBase || address==BmsSleepParameters.Base) {
            var header=await ReadRegistersAsync(D008Parameters.Base,12,ct);
            var capability=new D008ParameterCapture{Supported=header[0]==D008Parameters.Magic &&
                D008Parameters.SupportsProtocol(header[1]),ProtocolVersion=header[1]};
            capability.Blocks["Capability"]=header;
            bool available=address==BmsSleepParameters.Base ? BmsSleepParameters.Available(capability) :
                address==D008Parameters.BalanceBase ? D008Parameters.BalanceAvailable(capability) :
                                                     D008Parameters.HeaterAvailable(capability);
            if(!available)
                throw new NotSupportedException("设备未声明支持该功能，未发送参数写命令。");
        }
        AfeHardwareAccessSession? session=null;
        try {
            if(requiresAfeAccess) {
                session=await OpenAfeHardwareAccessAsync(ct);
            }
            if(values.Length==1)await WriteSingleRegisterAsync(address,values[0],ct);
            else await WriteRegistersAsync(address,values,ct);
            var actual=await ReadRegistersAsync(address,(ushort)values.Length,ct);
            if(!actual.SequenceEqual(values))throw new IOException("设备写入后读回不一致；请重新读取，勿盲目重复写入。");
        } finally {if(session is not null)await TryCloseAfeHardwareAccessAsync(session.Token,CancellationToken.None);}
    }
    public async Task RestoreSleepDefaultsAsync(CancellationToken ct=default)
    {
        await RequireD008Async(ct);
        var values=await ReadRegistersAsync(BmsSleepParameters.DefaultsBase,BmsSleepParameters.Count,ct);
        await WriteD008VerifiedAsync(BmsSleepParameters.Base,values,false,ct);
    }
    private async Task RequireD008Async(CancellationToken ct)
    {
        var h=await ReadRegistersAsync(D008Parameters.Base,2,ct);
        if(h[0]!=D008Parameters.Magic || !D008Parameters.SupportsProtocol(h[1]))
            throw new NotSupportedException($"设备未声明受支持的 D008 参数协议 v1/v2（设备报告 v{h[1]}），未发送写命令。");
    }
    public async Task WriteD008SerialAsync(string text,CancellationToken ct=default)
    {
        var words=D008Parameters.SerialWords(text);await RequireD008Async(ct);
        var session=await OpenAfeHardwareAccessAsync(ct);
        try {
            await WriteSingleRegisterAsync(0x2E40,0,ct);
            ushort generation=(await ReadRegistersAsync(0x2E06,1,ct))[0];
            for(int i=0;i<16;i+=4)await WriteRegistersAsync((ushort)(0x2E50+i),words.Skip(i).Take(4).ToArray(),ct);
            await WriteSingleRegisterAsync(0x2E40,generation,ct);
            if(!(await ReadRegistersAsync(0x2E30,16,ct)).SequenceEqual(words))throw new IOException("SN 读回不一致");
        } finally {await TryCloseAfeHardwareAccessAsync(session.Token,CancellationToken.None);}
    }
    public async Task ResetD008GroupAsync(ushort group,CancellationToken ct=default)
    {
        if(group is <1 or >3)throw new ArgumentOutOfRangeException(nameof(group));
        await RequireD008Async(ct);
        var session=await OpenAfeHardwareAccessAsync(ct);
        try {await WriteSingleRegisterAsync(0x2E10,group,ct);}
        finally {await TryCloseAfeHardwareAccessAsync(session.Token,CancellationToken.None);}
    }
    public async Task<(double Mean,int Min,int Max)> ReadD008CalibrationSamplesAsync(CancellationToken ct=default)
    {
        await RequireD008Async(ct);
        var samples=new List<int>();uint? last=null;var timer=Stopwatch.StartNew();
        while(samples.Count<10 && timer.Elapsed<TimeSpan.FromSeconds(20)) {
            var w=await ReadRegistersAsync(0x2E28,7,ct);uint tick=D008Parameters.U32(w,5);
            if(w[4]!=0 && last!=tick){samples.Add(D008Parameters.I32(w,0));last=tick;}
            await Task.Delay(200,ct);
        }
        if(samples.Count!=10)throw new IOException("20 秒内未取得 10 个新鲜电流样本，未生成校准值。");
        return (samples.Average(),samples.Min(),samples.Max());
    }
}
