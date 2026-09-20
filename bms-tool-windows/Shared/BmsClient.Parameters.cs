using System.Diagnostics;
using System.IO;
namespace BmsTool.Windows;
public sealed partial class BmsClient
{
    public async Task<D008ParameterCapture> ReadD008ParametersAsync(CancellationToken ct=default)
    {
        var c=new D008ParameterCapture();
        try {
            var header=await ReadRegistersAsync(D008Parameters.Base,12,ct);
            c.ProtocolVersion=header[1];
            header[6]=0; // SN staging generation is operational state, not exported.
            c.Blocks["Capability"]=header;
            if(header[0]!=D008Parameters.Magic || !D008Parameters.SupportsProtocol(header[1])) {
                c.Status=$"固件不支持 D008 参数协议 v1/v2（设备报告 v{header[1]}），仅备份已有参数";
            }
            else {
                c.Supported=true;
                if(header[1]>=2) {
                    try {
                        var tail=await ReadRegistersAsync(0x2E0C,4,ct);
                        c.Blocks["Capability"]=header.Concat(tail).ToArray();
                    } catch(Exception ex) {c.Errors.Add("CapabilityDefaults: "+ex.Message);}
                }
            }
        } catch(BmsModbusException ex) when(ex.Code is 1 or 2) {c.Status="固件不支持参数协议，仅备份已有参数";}
        catch(Exception ex) {c.Status="能力读取失败";c.Errors.Add(ex.Message);return c;}
        var blocks=new List<(string Name,ushort Address,ushort Count)>{
            ("FactoryMode",0x2AE1,1),("Capacity",0x2318,2),("SOC",0x1005,1),("Heater",0x2E20,3),
            ("Calibration",0x2E24,4),("Current",0x2E28,7),("Serial",0x2E30,16),
            ("SoftwareProtection",0x2100,65),("AfeRequested",0x2500,35),("AfeEffective",0x2540,35),
            ("BluetoothName",0x0100,12),("DeviceSerial",0xC002,16),("Hardware",BmsRegisters.Hardware,16),("Software",BmsRegisters.Software,16)};
        if(c.Supported && c.ProtocolVersion>=2 && (c.Blocks["Capability"][2]&D008Parameters.BalanceCapability)!=0)
            blocks.Insert(7,("Balance",0x2E70,4));
        foreach(var b in blocks) {
            if(!c.Supported && b.Name is not ("SoftwareProtection" or "AfeRequested" or "AfeEffective" or "BluetoothName" or "DeviceSerial" or "Hardware" or "Software"))continue;
            if(ct.IsCancellationRequested){c.Errors.Add("读取取消，保留已读参数");break;}
            try {c.Blocks[b.Name]=await ReadRegistersAsync(b.Address,b.Count,ct);}
            catch(Exception ex){c.Errors.Add(b.Name+": "+ex.Message);}
        }
        if(c.Supported)c.Status=c.Errors.Count==0 ? $"参数读取完成（协议 v{c.ProtocolVersion}）" : $"部分参数读取失败（协议 v{c.ProtocolVersion}），可导出已读内容";
        return c;
    }
    public async Task<bool> ReadD008FactoryModeAsync(CancellationToken ct=default)
    {
        await RequireD008Async(ct);
        try { return (await ReadRegistersAsync(0x2AE1,1,ct))[0]!=0; }
        catch(BmsModbusException ex) when(ex.Function==0x03 && ex.Code is 1 or 2) {
            throw new NotSupportedException("设备固件未提供 Factory Mode 运行态诊断，请升级固件后再执行工厂写操作。",ex);
        }
    }
    public async Task EnterD008FactoryModeAsync(CancellationToken ct=default)
    {
        await RequireD008Async(ct);
        var session=await OpenAfeHardwareAccessAsync(ct);
        try { await WriteSingleRegisterAsync(0x2E10,6,ct); }
        finally { await TryCloseAfeHardwareAccessAsync(session.Token,CancellationToken.None); }
        await Task.Delay(120,ct);
        if(!await ReadD008FactoryModeAsync(ct))
            throw new IOException("设备未确认进入 Factory Mode；未继续执行工厂参数写入。");
    }
    private async Task RequireD008FactoryModeAsync(CancellationToken ct)
    {
        if(!await ReadD008FactoryModeAsync(ct))
            throw new InvalidOperationException("设备当前为 NORMAL Mode。请先在内部测试版显式进入 Factory Mode，再执行 SN/电流校准。");
    }
    public async Task WriteD008VerifiedAsync(ushort address,ushort[] values,bool factory,CancellationToken ct=default)
    {
        await RequireD008Async(ct);
        AfeHardwareAccessSession? session=null;
        try {
            if(factory) {
                await RequireD008FactoryModeAsync(ct);
                session=await OpenAfeHardwareAccessAsync(ct);
            }
            if(values.Length==1)await WriteSingleRegisterAsync(address,values[0],ct);
            else await WriteRegistersAsync(address,values,ct);
            var actual=await ReadRegistersAsync(address,(ushort)values.Length,ct);
            if(!actual.SequenceEqual(values))throw new IOException("设备写入后读回不一致；请重新读取，勿盲目重复写入。");
        } finally {if(session is not null)await TryCloseAfeHardwareAccessAsync(session.Token,CancellationToken.None);}
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
        await RequireD008FactoryModeAsync(ct);
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
