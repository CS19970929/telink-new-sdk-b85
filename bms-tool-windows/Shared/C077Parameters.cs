using System.Diagnostics;
using System.IO;
namespace BmsTool.Windows;

public sealed record C077AfeChip(bool ProfileMatches,int ZeroState,short OffsetCounts,int ZeroSamples,
    ushort Conf,ushort Status1,ushort Status2,ushort Status3,ushort Flags,ushort Tr,short CurrentTenthA,bool Valid,byte[] Eeprom,
    ushort LastError,ushort LastRegister,bool CurrentEnabled)
{
    public string ZeroStateText=>ZeroState switch {0=>"等待稳定",1=>"采集新CADC",2=>"已完成",3=>"失败，保持关闭",4=>"未启用",_=>"未知"};
    public string HardwareCurrentText=>$"OCC {C077Parameters.OvercurrentMillivolts[Eeprom[15]>>4]*2}A / OCD1 {C077Parameters.OvercurrentMillivolts[Eeprom[12]>>4]*2}A / SC {C077Parameters.ShortMillivolts[Eeprom[14]>>4]*2}A、{(Eeprom[14]&15)*64}μs";
}
public sealed record C077AfeSnapshot(ushort ServiceState,ushort ServiceError,ushort Protection,ushort OutputRequest,
    ushort Permissions,bool BalanceOff,bool SampleValid,bool OpeningPending,bool DriverFault,IReadOnlyList<C077AfeChip> Chips,ushort ServicePosition)
{
    public bool ServiceFailed=>ServiceError!=0;
    public bool ZeroReady=>Chips.All(c=>c.ProfileMatches)&&Chips[0].ZeroState==2&&!Chips[1].CurrentEnabled;
    public string Summary=>$"维修状态{ServiceState}，结果={C077Parameters.ErrorText(ServiceError)}，位置=AFE{(ServicePosition>>8)+1}/0x{ServicePosition&255:X2}；软件输出=0x{OutputRequest:X2}，AFE许可/状态=0x{Permissions:X2}，聚合保护=0x{Protection:X2}，采样有效={SampleValid}，开启待确认={OpeningPending}，控制故障={DriverFault}\n"+
        string.Join("\n",Chips.Select((c,i)=>$"AFE{i+1}：档案{(c.ProfileMatches?"匹配":"不匹配")}，校零{c.ZeroStateText}"+
            (c.CurrentEnabled?$"，{c.OffsetCounts} counts，样本{c.ZeroSamples}/8，电流(充正/放负){c.CurrentTenthA/10m}A":"，电流采样未启用")+
            $"，测量有效={c.Valid}；CONF=0x{c.Conf:X2}，BSTATUS={c.Status1:X2}/{c.Status2:X2}/{c.Status3:X2}；最近错误={C077Parameters.ErrorText(c.LastError)}@0x{c.LastRegister:X2}；{c.HardwareCurrentText}"));
    public static C077AfeSnapshot Parse(ushort[] words)
    {
        if(words.Length!=64||words[0]!=0xC077||words[1]!=2||words[2]>3||words[3]>9||words[7]>1||words[58]>1||words[59]>3||words[61]>8||words[62]>7)
            throw new InvalidDataException("C077 309诊断版本或长度不匹配，要求本轮schema 2。");
        var chips=new List<C077AfeChip>();
        for(int i=0;i<2;i++) {
            int n=8+i*12;
            if(words[n]>1||words[n+8]>1||words[n+9]>9||words[n+10]>255||words[n+11]!=(i==0?1:0)||
               Enumerable.Range(1,4).Any(j=>words[n+j]>255)||words[n+6]>255)
                throw new InvalidDataException("309芯片诊断编码无效。");
            byte[] eeprom=new byte[26];
            for(int j=0;j<13;j++) {ushort v=words[32+i*13+j];eeprom[j*2]=(byte)(v>>8);eeprom[j*2+1]=(byte)v;}
            chips.Add(new(words[n]!=0,i==0?words[59]:4,i==0?unchecked((short)words[60]):(short)0,i==0?words[61]:0,
                words[n+1],words[n+2],words[n+3],words[n+4],words[n+5],words[n+6],unchecked((short)words[n+7]),words[n+8]!=0,eeprom,
                words[n+9],words[n+10],words[n+11]!=0));
        }
        return new(words[2],words[3],words[4],words[5],words[6],words[7]!=0,words[58]!=0,(words[62]&2)!=0,(words[62]&4)!=0,chips,words[63]);
    }
}
public static class C077Parameters
{
    public const ushort DiagnosticBase=0x2A40,ServiceRegister=0x2A7F,ProgramProfile=0xC077,RetryZero=0x5A07;
    public static readonly ushort[] OvercurrentMillivolts={20,30,40,50,60,70,80,90,100,110,120,130,140,160,180,200};
    public static readonly ushort[] ShortMillivolts={50,80,110,140,170,200,230,260,290,320,350,400,500,600,800,1000};
    public static string ErrorText(ushort error)=>error switch {0=>"正常",1=>"参数/前置条件",2=>"I2C传输",3=>"CRC",4=>"配置不匹配",5=>"读回不一致",6=>"复位/RAM异常",7=>"采样超时",8=>"NTC无效",9=>"校零未完成",_=>"未知"};
    public static int OvercurrentCode(ushort deciamps)
    {
        if(deciamps==0||deciamps>4000)throw new ArgumentException("C077软件过流阈值必须大于0且不超过400A。");
        return Array.FindIndex(OvercurrentMillivolts,v=>v>=deciamps/20);
    }
    public static void ValidateProtection(ushort[] words)
    {
        if(words.Length!=65)throw new ArgumentException("C077保护参数必须为65字。");
        for(int group=0;group<13;group++) {
            int n=group*5;bool low=group is 1 or 3 or 7 or 9 or 12;
            if(words[n+4]==0||(low?words[n]<words[n+1]||words[n+1]<words[n+2]||words[n+3]<words[n+2]:words[n]>words[n+1]||words[n+1]>words[n+2]||words[n+3]>words[n+2]))
                throw new ArgumentException($"C077第{group+1}组顺序、恢复值或10ms Filter无效。");
        }
        OvercurrentCode(words[22]);OvercurrentCode(words[27]);
    }
    public static ushort[] NormalizeCurrentGroup(ushort[] words,ushort resistance=2,ushort parallel=4)
    {
        if(words.Length!=8||resistance!=2||parallel!=4||words[2]<640||words[2]>8960||words[3]==0||words[3]>16000)
            throw new ArgumentException("C077固定4颗2mΩ，短路延时64～896μs、电流>0且≤1600A。");
        int code=Array.FindIndex(ShortMillivolts,v=>v>=words[3]/20);
        if(code<0||code>=15)throw new ArgumentException("AFE2更高一级短路档位无法表示。");
        var result=(ushort[])words.Clone();result[0]=result[1]=4000;
        result[2]=(ushort)((words[2]+639)/640*640);result[3]=(ushort)(ShortMillivolts[code]*20);
        return result;
    }
}
public sealed partial class BmsClient
{
    public async Task<C077AfeSnapshot> ReadC077AfeAsync(CancellationToken ct=default)
    {
        if(!IsC077)throw new NotSupportedException("需D170明确识别C077；不会按3510/3520寄存器访问309。");
        return C077AfeSnapshot.Parse(await ReadRegistersAsync(C077Parameters.DiagnosticBase,64,ct));
    }
    private async Task RequireC077OutputsOffAsync(CancellationToken ct)
    {
        var before=await ReadC077AfeAsync(ct);
        if(before.OutputRequest!=0||before.ServiceState!=0||!before.BalanceOff)
            throw new InvalidOperationException("硬件档位变更需先关闭充/放电/预充和均衡，并结束工厂事务。");
    }
    public async Task<C077AfeSnapshot> RunC077ServiceAsync(bool programProfile,CancellationToken ct=default)
    {
        await RequireC077OutputsOffAsync(ct);
        await WriteSingleRegisterAsync(C077Parameters.ServiceRegister,programProfile?C077Parameters.ProgramProfile:C077Parameters.RetryZero,ct);
        var timer=Stopwatch.StartNew();
        while(timer.Elapsed<TimeSpan.FromSeconds(20)) {
            await Task.Delay(250,ct);
            var state=await ReadC077AfeAsync(ct);
            if(state.ServiceFailed)throw new IOException($"309工厂事务失败：{C077Parameters.ErrorText(state.ServiceError)}，AFE{(state.ServicePosition>>8)+1}寄存器0x{state.ServicePosition&255:X2}。保持关闭，请读取实际档案后明确重试。");
            if(state.Chips[0].ZeroState==3)throw new IOException("AFE1零偏校准失败；检查真实零电流和通信后明确重试。AFE2 CADC未启用。");
            if(state.ServiceState==0&&state.ZeroReady&&state.SampleValid&&!state.OpeningPending&&!state.DriverFault&&state.BalanceOff&&state.Chips.All(c=>c.Valid))return state;
        }
        throw new TimeoutException("309事务完成尚未确认；可能已有部分EEPROM写入，未自动重发，请重新读取诊断。");
    }
}
