using System.Diagnostics;
using System.IO;
using System.Text.Json;
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
    public ushort Schema {get;init;}=2;
    public bool UsesPersistentCalibration=>Schema>=3;
    public bool ServiceFailed=>ServiceError!=0;
    public bool ZeroReady=>Chips.All(c=>c.ProfileMatches)&&Chips[0].ZeroState==2&&!Chips[1].CurrentEnabled;
    public string Summary=>$"维修状态{ServiceState}，结果={C077Parameters.ErrorText(ServiceError)}，位置=AFE{(ServicePosition>>8)+1}/0x{ServicePosition&255:X2}；软件输出=0x{OutputRequest:X2}，AFE许可/状态=0x{Permissions:X2}，聚合保护=0x{Protection:X2}，采样有效={SampleValid}，开启待确认={OpeningPending}，控制故障={DriverFault}\n"+
        string.Join("\n",Chips.Select((c,i)=>$"AFE{i+1}：档案{(c.ProfileMatches?"匹配":"不匹配")}，{(UsesPersistentCalibration?"启动检查":"校零")}{c.ZeroStateText}"+
            (c.CurrentEnabled?$"，样本{c.ZeroSamples}/8，电流(充正/放负){c.CurrentTenthA/10m}A":"，电流采样未启用")+
            $"，AFE 状态有效={c.Valid}；CONF=0x{c.Conf:X2}，BSTATUS={c.Status1:X2}/{c.Status2:X2}/{c.Status3:X2}；最近错误={C077Parameters.ErrorText(c.LastError)}@0x{c.LastRegister:X2}；{c.HardwareCurrentText}"));
    public static C077AfeSnapshot Parse(ushort[] words)
    {
        if(words.Length!=64||words[0]!=0xC077||words[1] is not (2 or 3)||words[2] > (words[1] == 3 ? 4 : 3)||words[3]>9||words[7]>1||words[58]>1||words[59]>3||words[61]>8||words[62]>7)
            throw new InvalidDataException("C077 309诊断版本或长度不匹配，支持schema 2/3。");
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
        return new(words[2],words[3],words[4],words[5],words[6],words[7]!=0,words[58]!=0,(words[62]&2)!=0,(words[62]&4)!=0,chips,words[63]) {Schema=words[1]};
    }
}
public static class C077Parameters
{
    public const ushort DiagnosticBase=0x2A40,ServiceRegister=0x2A7F,ProgramProfile=0xC077,RetryZero=0x5A07;
    public static readonly ushort[] OvercurrentMillivolts={20,30,40,50,60,70,80,90,100,110,120,130,140,160,180,200};
    public static readonly ushort[] ShortMillivolts={50,80,110,140,170,200,230,260,290,320,350,400,500,600,800,1000};
    public static string ErrorText(ushort error)=>error switch {0=>"正常",1=>"参数/前置条件",2=>"I2C传输",3=>"CRC",4=>"配置不匹配",5=>"读回不一致",6=>"复位/RAM异常",7=>"采样超时",8=>"NTC无效",9=>"启动采样检查未完成",_=>"未知"};
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
    public async Task<BatterySnapshot> ReadC077ReadyBatteryAsync(CancellationToken ct=default)
    {
        var timer=Stopwatch.StartNew();
        while(true) {
            var state=await ReadC077AfeAsync(ct);
            if(state.SampleValid&&state.Chips[0] is {ProfileMatches:true,ZeroState:2,Valid:true})
                return await ReadBatteryAsync(ct);
            if(timer.Elapsed>=TimeSpan.FromSeconds(10))
                throw new IOException("C077 APP已启动，但AFE采样/校零尚未就绪；实时数据暂不可用。");
            await Task.Delay(250,ct);
        }
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
            // 5A07 only recalibrates AFE1 CADC. AFE2 health remains visible in the
            // returned snapshot, but cannot turn a completed primary zero into a timeout.
            bool ready=programProfile
                ? state.ZeroReady&&state.SampleValid&&state.Chips.All(c=>c.Valid)
                : state.Chips[0] is {ProfileMatches:true,ZeroState:2,Valid:true,CurrentEnabled:true};
            if(state.ServiceState==0&&ready&&!state.OpeningPending&&!state.DriverFault&&state.BalanceOff)return state;
        }
        throw new TimeoutException(programProfile
            ? "309事务完成尚未确认；可能已有部分EEPROM写入，未自动重发，请重新读取诊断。"
            : "AFE1零偏完成尚未确认；仅操作RAM校零，未自动重发，请重新读取诊断。");
    }
}


public sealed record C077HardwareField(int Index,string Key,string Name,string Unit,int Minimum,int Maximum,int Step=1,int[]? Choices=null)
{
    public bool Signed=>Index is >=23 and <=30;
    public int Value(ushort word)=>Signed?unchecked((short)word):word;
    public string Limits=>Choices is null?$"{Minimum}..{Maximum}，步进{Step}":string.Join(" / ",Choices);
}
public sealed record C077HardwareSnapshot(ushort[][] Saved,ushort[][] Expected,C077AfeSnapshot Actual)
{
    // Bit 0 = AFE1, bit 1 = AFE2. Legacy APPs lack per-chip evidence.
    public byte? EepromReadbackMask { get; init; }
}
public sealed class C077HardwareFile
{
    public string Product {get;set;}="";
    public int Schema {get;set;}
    public Dictionary<string,int>[] Chips {get;set;}=Array.Empty<Dictionary<string,int>>();
}
public static class C077Hardware
{
    public const ushort Header=0x2B00,ConfigBase=0x2B10,ExpectedBase=0x2B80;
    public static readonly int[] VoltageDelay={100,200,300,400,600,800,1000,2000,3000,4000,6000,8000,10000,20000,30000,40000};
    public static readonly int[] Ocd1Delay={50,100,200,400,600,800,1000,2000,4000,6000,8000,10000,15000,20000,30000,40000};
    public static readonly int[] FastDelay={10,20,40,60,80,100,200,400,600,800,1000,2000,4000,8000,10000,20000};
    public static readonly int[] Ocd2Mv={30,40,50,60,70,80,90,100,120,140,160,180,200,300,400,500};
    public static readonly string[] FeatureNames={"ENMOS 充电MOS反向恢复","OCPM 过流双路关闭","E0VB 低压电芯禁止充电","UV_OP 欠压双路关闭","DIS_PF 禁止二次过充/断线保护","OCRA 电流定时自恢复","EUVR 欠压恢复要求负载移除"};
    public static readonly C077HardwareField[] Fields={
        new(0,"features","保护功能位","位掩码",0,127),
        new(1,"ov_mv","过压动作","mV",1000,5115,5),
        new(2,"ov_delay_ms","过压延时","ms",100,40000,1,VoltageDelay),
        new(3,"ov_recovery_mv","过压恢复","mV",1000,5115,5),
        new(4,"uv_mv","欠压动作","mV",1000,5100,20),
        new(5,"uv_delay_ms","欠压延时","ms",100,40000,1,VoltageDelay),
        new(6,"uv_recovery_mv","欠压恢复","mV",1000,5100,20),
        new(7,"balance_mv","AFE自主均衡阈值（MCU模式不使用）","mV",0,5100,20),
        new(8,"precharge_mv","AFE内部预充阈值（本板禁用）","mV",0,5100,20),
        new(9,"low_charge_mv","低压电芯禁充阈值","mV",0,5100,20),
        new(10,"pf_mv","二次过充阈值","mV",0,5100,20),
        new(11,"pf_delay_s","二次过充延时","s",8,64,1,new[]{8,16,32,64}),
        new(12,"ocd1_mv","放电过流1（0.5mΩ：mV×2=A）","mV",20,200,1,C077Parameters.OvercurrentMillivolts.Select(x=>(int)x).ToArray()),
        new(13,"ocd1_delay_ms","放电过流1延时","ms",50,40000,1,Ocd1Delay),
        new(14,"ocd2_mv","放电过流2（mV×2=A）","mV",30,500,1,Ocd2Mv),
        new(15,"ocd2_delay_ms","放电过流2延时","ms",10,20000,1,FastDelay),
        new(16,"short_mv","短路（mV×2=A）","mV",50,1000,1,C077Parameters.ShortMillivolts.Select(x=>(int)x).ToArray()),
        new(17,"short_delay_us","短路延时","μs",0,960,64),
        new(18,"occ_mv","充电过流（mV×2=A）","mV",20,200,1,C077Parameters.OvercurrentMillivolts.Select(x=>(int)x).ToArray()),
        new(19,"occ_delay_ms","充电过流延时","ms",10,20000,1,FastDelay),
        new(20,"charge_detect_uv","充放电方向检测","μV",200,2000,1,new[]{200,500,1000,2000}),
        new(21,"mos_delay_us","AFE MOS开启延时","μs",64,512,1,new[]{64,128,256,512}),
        new(22,"current_recovery_s","电流自动恢复延时","s",8,64,1,new[]{8,16,32,64}),
        new(23,"charge_hot_decic","充电高温","0.1℃",-400,1000,10),
        new(24,"charge_hot_recovery_decic","充电高温恢复","0.1℃",-400,1000,10),
        new(25,"charge_cold_decic","充电低温","0.1℃",-400,1000,10),
        new(26,"charge_cold_recovery_decic","充电低温恢复","0.1℃",-400,1000,10),
        new(27,"discharge_hot_decic","放电高温","0.1℃",-400,1000,10),
        new(28,"discharge_hot_recovery_decic","放电高温恢复","0.1℃",-400,1000,10),
        new(29,"discharge_cold_decic","放电低温","0.1℃",-400,1000,10),
        new(30,"discharge_cold_recovery_decic","放电低温恢复","0.1℃",-400,1000,10),
        new(31,"load_release_ms","负载释放延时","ms",100,2000,1,new[]{100,500,1000,2000})
    };
    private static readonly int[] Ntc10Ohm={20375,19204,18115,17100,16152,15266,14437,13661,12934,12251,11611,11008,10442,9909,9407,8935,8489,8068,7672,7297,6943,6608,6292,5993,5710,5442,5188,4948,4720,4504,4300,4105,3921,3746,3580,3422,3272,3130,2994,2866,2751,2627,2516,2410,2310,2214,2123,2036,1953,1874,1801,1726,1658,1592,1530,1470,1413,1358,1306,1256,1209,1163,1119,1078,1038,1000,963,928,894,862,831,801,773,746,719,694,670,647,625,604,583,563,544,526,509,492,476,460,445,431,416,403,390,378,366,355,343,333,322,312,303,294,285,276,268,260,252,244,237,230,224,217,211,205,199,193,188,182,177,172,167,163,158,154,150,146,142,138,134,131,127,124,120,117,114,111,108,106,103,100,98};
    public static byte TemperatureCode(int decic,int tr,bool cold)
    {
        if(decic< -400||decic>1000||decic%10!=0)throw new ArgumentException("温度必须为−40..100℃整℃档位。");
        int resistance=Ntc10Ohm[(decic+400)/10]*10;
        int code=(int)((long)resistance*512/(6800+(tr&127)*50+resistance))-(cold?256:0);
        if(code<0||code>255)throw new ArgumentException("温度超出该片TR可编码范围。");
        return (byte)code;
    }
    public static void Validate(ushort[] w,int tr=64)
    {
        if(w.Length!=32)throw new ArgumentException("每片需要完整32字硬保护配置。");
        foreach(var f in Fields) {
            int v=f.Value(w[f.Index]);
            if(v<f.Minimum||v>f.Maximum||v%f.Step!=0||f.Choices is not null&&!f.Choices.Contains(v))
                throw new ArgumentException(f.Name+"不在允许档位："+f.Limits);
        }
        int V(int n)=>unchecked((short)w[n]);
        if((w[0]&16)==0&&w[10]<=w[1]||w[3]>w[1]||w[6]<w[4]||w[6]>=w[3]||V(24)>V(23)||V(26)<V(25)||V(28)>V(27)||V(30)<V(29)||V(26)>=V(24)||V(30)>=V(28))
            throw new ArgumentException("动作/恢复窗口顺序无效。");
        for(int i=0;i<8;i++)TemperatureCode(V(23+i),tr,(i&2)!=0);
    }
    public static Dictionary<string,int> Values(ushort[] words)=>Fields.ToDictionary(f=>f.Key,f=>f.Value(words[f.Index]));
    public static ushort[] Words(Dictionary<string,int> values)
    {
        if(values is null||values.Count!=32||Fields.Any(f=>!values.ContainsKey(f.Key)))throw new InvalidDataException("参数名称/数量必须与C077 schema1完整匹配。");
        var w=Fields.Select(f=>checked((ushort)(f.Signed?unchecked((ushort)(short)checked((short)values[f.Key])):values[f.Key]))).ToArray();
        Validate(w);return w;
    }
    public static C077HardwareFile File(ushort[][] words)=>new(){Product="C077",Schema=1,Chips=words.Select(Values).ToArray()};
    public static C077HardwareFile ParseFile(string text)
    {
        var file=JsonSerializer.Deserialize<C077HardwareFile>(text,new JsonSerializerOptions{PropertyNameCaseInsensitive=true})??throw new InvalidDataException("硬保护JSON为空。");
        Import(file);return file;
    }
    public static ushort[][] Import(C077HardwareFile file)
    {
        if(file.Product!="C077"||file.Schema!=1||file.Chips is null||file.Chips.Length!=2||file.Chips.Any(c=>c is null))throw new InvalidDataException("仅接受双片C077硬保护schema1文件。");
        return file.Chips.Select(Words).ToArray();
    }
    public static int[] Decode(byte[] p,int tr)
    {
        if(p.Length!=26)throw new InvalidDataException("309读回必须为26字节。");
        var w=new int[32];
        w[0]=((p[0]>>6)&1)|((p[0]>>4)&2)|((p[1]>>5)&4)|((p[1]>>2)&8)|(p[1]&16)|((p[1]<<4)&32)|((p[1]<<6)&64);
        w[1]=((p[2]&3)*256+p[3])*5;w[2]=VoltageDelay[p[2]>>4];w[3]=((p[4]&3)*256+p[5])*5;
        w[4]=p[6]*20;w[5]=VoltageDelay[p[4]>>4];w[6]=p[7]*20;
        w[7]=p[8]*20;w[8]=p[9]*20;w[9]=p[10]*20;w[10]=p[11]*20;w[11]=8<< (p[16]&3);
        w[12]=C077Parameters.OvercurrentMillivolts[p[12]>>4];w[13]=Ocd1Delay[p[12]&15];
        w[14]=Ocd2Mv[p[13]>>4];w[15]=FastDelay[p[13]&15];
        w[16]=C077Parameters.ShortMillivolts[p[14]>>4];w[17]=(p[14]&15)*64;
        w[18]=C077Parameters.OvercurrentMillivolts[p[15]>>4];w[19]=FastDelay[p[15]&15];
        w[20]=new[]{200,500,1000,2000}[p[16]>>6];w[21]=new[]{64,128,256,512}[(p[16]>>4)&3];w[22]=8<<((p[16]>>2)&3);
        w[31]=new[]{100,500,1000,2000}[(p[2]>>2)&3];
        for(int i=0;i<8;i++) {
            int best=0,error=int.MaxValue;
            for(int t= -400;t<=1000;t+=10)try {int e=Math.Abs(TemperatureCode(t,tr,(i&2)!=0)-p[17+i]);if(e<error){error=e;best=t;}}catch(ArgumentException){}
            w[23+i]=best;
        }
        return w;
    }
}
public sealed partial class BmsClient
{
    private const ushort C077ReadbackEvidence = 0x8000;

    private async Task<ushort> RequireC077HardwareAsync(CancellationToken ct)
    {
        if (!IsC077) throw new NotSupportedException("设备必须明确识别为 C077。");
        var header = await ReadRegistersAsync(C077Hardware.Header, 7, ct);
        if (header[0] != 0xC077 || header[1] != 1 || header[2] != 32 || header[3] != 2)
            throw new NotSupportedException("C077 双片 SH367309 硬保护协议头不匹配，要求 schema 1、每片 32 字。");
        if (header[6] is not (6 or 7 or 11))
            throw new NotSupportedException($"当前上位机未验证 C077 的 MCU Flash 配置格式 {header[6]}，硬保护操作已停止。");
        var status = (await ReadRegistersAsync(0x2A28, 1, ct))[0];
        if ((status >> 8) == 4)
            throw new IOException("Flash 已保存串数与 C077 固件的生产串数不一致。请核对板子实装串数和对应固件。");
        if ((status & 1) == 0)
            throw new IOException("MCU Flash 参数尚未就绪。当前 RAM 默认值不能作为已保存的 Flash 配置。");
        return header[5];
    }

    public async Task<C077HardwareSnapshot> ReadC077HardwareAsync(bool verifyEeprom=false,CancellationToken ct=default)
    {
        ushort flags = await RequireC077HardwareAsync(ct);
        bool hasEvidence = (flags & C077ReadbackEvidence) != 0;
        if (verifyEeprom) await WriteSingleRegisterAsync(C077Parameters.ServiceRegister, 0xC087, ct);
        ushort[]? before = hasEvidence && verifyEeprom
            ? await ReadRegistersAsync(C077Hardware.Header + 5, 4, ct) : null;
        var saved = await ReadRegistersAsync(C077Hardware.ConfigBase, 64, ct);
        var expected = await ReadRegistersAsync(C077Hardware.ExpectedBase, 32, ct);
        var actual = await ReadC077AfeAsync(ct);

        byte? confirmed = hasEvidence ? (byte)0 : null;
        if (before is not null && actual.ServiceState == 0)
        {
            var after = await ReadRegistersAsync(C077Hardware.Header + 5, 4, ct);
            for (int chip = 0; chip < 2; chip++)
            {
                int requiredFlags = C077ReadbackEvidence | (0x0100 << chip);
                if ((before[0] & after[0] & requiredFlags) == requiredFlags && before[2 + chip] == after[2 + chip])
                    confirmed = (byte)(confirmed.GetValueOrDefault() | (1 << chip));
            }
        }
        return new(new[] { saved[..32], saved[32..] }, new[] { expected[..16], expected[16..] }, actual)
        {
            EepromReadbackMask = confirmed
        };
    }
    public async Task SaveC077HardwareAsync(int chip,ushort[] values,ushort[]? observed=null,CancellationToken ct=default)
    {
        if(chip is <0 or >1)throw new ArgumentOutOfRangeException(nameof(chip));
        await RequireC077HardwareAsync(ct);
        var state=await ReadC077AfeAsync(ct);C077Hardware.Validate(values,state.Chips[chip].Tr);
        ushort address=(ushort)(C077Hardware.ConfigBase+chip*32);
        var before=await ReadRegistersAsync(address,32,ct);
        if(observed is not null&&!before.SequenceEqual(observed))throw new IOException("Flash配置已由其他入口修改，请重新读取后编辑。");
        if(before.SequenceEqual(values))return;
        await RequireC077OutputsOffAsync(ct);
        await WriteRegistersAsync(address,values,ct);
        if(!(await ReadRegistersAsync(address,32,ct)).SequenceEqual(values))throw new IOException("Flash保存后读回不一致；未下发AFE，未自动重试。");
    }
    public async Task<C077AfeSnapshot> ProgramC077HardwareAsync(int targets,CancellationToken ct=default)
    {
        if(targets is <1 or >3)throw new ArgumentOutOfRangeException(nameof(targets));
        await RequireC077HardwareAsync(ct);await RequireC077OutputsOffAsync(ct);
        await WriteSingleRegisterAsync(C077Parameters.ServiceRegister,(ushort)(targets==1?0xC071:targets==2?0xC072:0xC077),ct);
        var timer=Stopwatch.StartNew();
        while(timer.Elapsed<TimeSpan.FromSeconds(20)) {
            await Task.Delay(250,ct);var state=await ReadC077AfeAsync(ct);
            if(state.ServiceFailed)throw new IOException($"EEPROM失败：{C077Parameters.ErrorText(state.ServiceError)}，AFE{(state.ServicePosition>>8)+1}/0x{state.ServicePosition&255:X2}；未自动重发。");
            var selected=state.Chips.Where((_,i)=>(targets&(1<<i))!=0);
            if((targets&1)!=0&&state.Chips[0].ZeroState==3)throw new IOException("AFE1校零失败，EEPROM可能已经写入，请重新读取诊断。");
            if(state.ServiceState==0&&selected.All(c=>c.ProfileMatches&&c.Valid)&&((targets&1)==0||state.Chips[0].ZeroState==2)&&!state.DriverFault&&!state.OpeningPending)return state;
        }
        throw new TimeoutException("下发结果未确认，可能已部分写入；请读取实际配置，未自动重发。");
    }
}
