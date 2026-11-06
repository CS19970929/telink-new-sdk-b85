using System.Diagnostics;
using System.IO;
namespace BmsTool.Windows;

public sealed record C077AfeChip(bool ProfileMatches,int ZeroState,short OffsetCounts,int ZeroSamples,
    ushort Conf,ushort Status1,ushort Status2,ushort Status3,ushort Flags,ushort Tr,short CurrentTenthA,bool Valid,byte[] Eeprom)
{
    public string ZeroStateText=>ZeroState switch {0=>"等待稳定",1=>"采集新CADC",2=>"已完成",3=>"失败，保持关闭",_=>"未知"};
}
public sealed record C077AfeSnapshot(ushort ServiceState,bool ServiceFailed,ushort Protection,ushort OutputRequest,
    ushort Permissions,bool BalanceOff,bool SampleValid,bool OpeningPending,bool DriverFault,IReadOnlyList<C077AfeChip> Chips)
{
    public bool ZeroReady=>Chips.All(c=>c.ProfileMatches&&c.ZeroState==2);
    public string Summary=>$"维修状态{ServiceState}，失败={ServiceFailed}；软件请求=0x{OutputRequest:X2}，CONF许可=0x{Permissions:X2}，聚合保护=0x{Protection:X2}，采样有效={SampleValid}，开启待确认={OpeningPending}，驱动故障={DriverFault}\n"+
        string.Join("\n",Chips.Select((c,i)=>$"AFE{i+1}：档案{(c.ProfileMatches?"匹配":"不匹配")}，零偏{c.ZeroStateText}，{c.OffsetCounts} counts，样本{c.ZeroSamples}/8，测量有效={c.Valid}；BSTATUS={c.Status1:X2}/{c.Status2:X2}/{c.Status3:X2}，电流(充正/放负){c.CurrentTenthA/10m}A"));
    public static C077AfeSnapshot Parse(ushort[] words)
    {
        if(words.Length!=64||words[0]!=0xC077||words[1]!=1||words[2]>4||words[3]>1||words[58]>1||words[59]>1||words[60]>1)
            throw new InvalidDataException("C077双309诊断版本或长度不匹配。");
        var chips=new List<C077AfeChip>();
        for(int i=0;i<2;i++) {
            int n=8+i*12;
            if(words[n]>1||words[n+1]>3||words[n+3]>8||words[n+11]>1)
                throw new InvalidDataException("309校零状态编码无效。");
            byte[] eeprom=new byte[26];
            for(int j=0;j<13;j++) {ushort v=words[32+i*13+j];eeprom[j*2]=(byte)(v>>8);eeprom[j*2+1]=(byte)v;}
            chips.Add(new(words[n]!=0,words[n+1],unchecked((short)words[n+2]),words[n+3],words[n+4],words[n+5],words[n+6],words[n+7],words[n+8],words[n+9],unchecked((short)words[n+10]),words[n+11]!=0,eeprom));
        }
        return new(words[2],words[3]!=0,words[4],words[5],words[6],words[7]!=0,words[58]!=0,words[59]!=0,words[60]!=0,chips);
    }
}
public static class C077Parameters
{
    public const ushort DiagnosticBase=0x2A40,ServiceRegister=0x2A7F,ProgramProfile=0xC077,RetryZero=0x5A07;
    public static void ValidateProtection(ushort[] words)
    {
        ProtectionBatch.ValidateC060(words);
        // Units: cell mV; current 0.1A; temperature (C+40)*10.
        if(words[2]>=4300||words[7]<=2600||words[22]>=400||words[27]>=1000||
           words[32]>=1000||words[37]<=350||words[42]>=1150||words[47]<=150)
            throw new ArgumentException("C077软件三级阈值须位于双309硬件档案内：OV<4300mV、UV>2600mV、充电<40A、放电<100A、充电-5～60℃、放电-25～75℃。最低硬件充电门限40A，无法设为13A。");
    }
}
public sealed partial class BmsClient
{
    public async Task<C077AfeSnapshot> ReadC077AfeAsync(CancellationToken ct=default)
    {
        if(!IsC077)throw new NotSupportedException("需D170明确识别C077；不会按3510/3520寄存器访问309。");
        var first=await ReadRegistersAsync(C077Parameters.DiagnosticBase,32,ct);
        var second=await ReadRegistersAsync(C077Parameters.DiagnosticBase+32,32,ct);
        return C077AfeSnapshot.Parse(first.Concat(second).ToArray());
    }
    public async Task<C077AfeSnapshot> RunC077ServiceAsync(bool programProfile,CancellationToken ct=default)
    {
        var before=await ReadC077AfeAsync(ct);
        if(before.OutputRequest!=0||before.ServiceState!=0)
            throw new InvalidOperationException("请先关闭充/放电/预充并确认当前无维修事务，再操作309。");
        await WriteSingleRegisterAsync(C077Parameters.ServiceRegister,programProfile?C077Parameters.ProgramProfile:C077Parameters.RetryZero,ct);
        var timer=Stopwatch.StartNew();
        while(timer.Elapsed<TimeSpan.FromSeconds(20)) {
            await Task.Delay(250,ct);
            var state=await ReadC077AfeAsync(ct);
            if(state.ServiceFailed)throw new IOException("309维修写入/核对失败；保持关闭。先读取两颗实际档案，明确重试，禁止盲目重发。");
            if(state.Chips.Any(c=>c.ZeroState==3))throw new IOException("309零偏校准失败；保持关闭。检查真实零电流和探头/通信后再明确重试。");
            if(state.ServiceState==0 && state.ZeroReady && state.SampleValid && state.Chips.All(c=>c.Valid))return state;
        }
        throw new TimeoutException("309事务完成尚未确认；可能已有部分EEPROM写入，未自动重发，请重新读取诊断。");
    }
}
