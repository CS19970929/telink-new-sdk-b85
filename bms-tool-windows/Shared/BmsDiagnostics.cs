using System.IO;
using System.IO.Compression;
using System.Text;
using System.Text.Json;

namespace BmsTool.Windows;

public sealed record DiagnosticField(string Field, string Value);
public sealed record DiagnosticTrace(uint Sequence, uint Tick32k, ushort EventId, string Event, uint Arg0, uint Arg1);
public sealed record DiagnosticFrame(DateTimeOffset Time, string Direction, string Hex);
public sealed record SocDiagnosticSnapshot(
    ushort RuntimeVersion,
    string Chemistry,
    ushort ProfileId,
    ushort ProfileVersion,
    ushort SocEstimate,
    ushort SocDisplay,
    double NominalCapacityAh,
    double EffectiveCapacityAh,
    double RemainingCapacityAh,
    string OcvState,
    ushort OcvCenter,
    ushort OcvLow,
    ushort OcvHigh,
    ushort OcvConfidence,
    ushort OcvCellMv,
    ushort RestSeconds,
    string EndpointState,
    ushort EndpointEventFlags,
    int FilteredCurrentMa,
    ushort CurrentVariationMa,
    int? TimeToEmptyMinutes,
    int? TimeToFullMinutes,
    string EtaState,
    string EtaDirection,
    ushort EtaConfidence,
    bool EtaValid,
    ushort Soh,
    string SohSource,
    ushort SohConfidence,
    bool CapacityLearningEnable,
    string LearningState,
    bool CandidateValid,
    double CandidateCapacityAh,
    double LearnedCapacityAh,
    ushort LearningConfidence,
    ushort ValidLearningCount,
    ushort RejectedLearningCount,
    string LastLearningRejectReason,
    string LastSampleState,
    string LastIntegralDirection,
    string LastSocAction,
    uint LastSampleElapsed32k,
    uint LastIntegralDeltaAs10,
    ushort LastSocBefore,
    ushort LastSocAfter,
    ushort LastSocTarget,
    ushort LastDecisionDetail);
public sealed class DiagnosticCapture
{
    public DateTimeOffset StartedUtc { get; } = DateTimeOffset.UtcNow;
    public DateTimeOffset FinishedUtc { get; set; }
    public string Endpoint { get; set; } = "";
    public string Status { get; set; } = "未读取";
    public bool Supported { get; set; }
    public bool SnapshotConsistent { get; set; }
    public bool TraceConsistent { get; set; }
    public ushort[]? Words { get; set; }
    public List<DiagnosticField> Boot { get; } = new();
    public List<DiagnosticField> Storage { get; } = new();
    public List<DiagnosticField> Mos { get; } = new();
    public List<DiagnosticField> Current { get; } = new();
    public List<DiagnosticField> Soc { get; } = new();
    public List<DiagnosticField> Power { get; } = new();
    public List<DiagnosticField> Protection { get; } = new();
    public List<DiagnosticTrace> Trace { get; } = new();
    public Dictionary<string,string> Identity { get; } = new();
    public Dictionary<string,ushort[]> EvidenceBlocks { get; } = new();
    public ushort[]? Events { get; set; }
    public ushort[]? SoftwareProtectionWords { get; set; }
    public List<string> Errors { get; } = new();
    public List<DiagnosticFrame> Frames { get; } = new();
}
public static class BmsDiagnostics
{
    public const ushort Base = 0x2A00, TraceBase = 0x2B00, RuntimeBase = 0x2AC0, Magic = 0x4447, Schema = 1;
    public const ushort BootCapability = 0x0001, TraceCapability = 0x0002, StorageCapability = 0x0004,
        MosCapability = 0x0008, UpgradeCapability = 0x0010;
    public const ushort RuntimeCapability = 0x0020;
    public const ushort GenericFetBitsInfo = 0x0001;
    public const ushort ShFetDetailInfo = 0x0002;
    public static uint U32(ushort[] w, int at) => (uint)w[at] | ((uint)w[at+1] << 16);
    public static int I32(ushort[] w, int at) => unchecked((int)U32(w,at));
    public static string Result(ushort n) => n switch {
        0=>"未执行",1=>"成功",2=>"PORT 不可用",3=>"布局拒绝",4=>"区域无效",5=>"record_open 失败",
        6=>"未加载有效记录，使用默认值（不等于硬件故障）",7=>"保存失败（查看底层原因）",
        8=>"编程后校验失败",9=>"擦除后校验失败",10=>"OTA 延后",11=>"Flash 锁状态延后",
        12=>"失败退避",13=>"校验/初始化失败",14=>"已开始，尚未完成",_=>$"未知({n})"};
    public static string Reasons(uint bits)
    {
        string[] names={"保护参数无效","启动存储升级未完成","输出未授权","AFE 通信未通过资格确认",
            "软件保护","AFE 硬件保护","Open-Wire","加热","关机保持","温度无效","其他 backend 阻断"};
        var items=new List<string>();
        for(int i=0;i<names.Length;i++) if((bits&(1u<<i))!=0) items.Add(names[i]);
        if((bits&~0x7FFu)!=0) items.Add($"未知位 0x{bits&~0x7FFu:X8}");
        return items.Count==0 ? "无" : string.Join("；",items);
    }
    private static string NamedBits(ushort bits, params string[] names)
    {
        var items = new List<string>();
        for (int i = 0; i < names.Length; i++)
            if ((bits & (1u << i)) != 0) items.Add(names[i]);
        if ((bits >> names.Length) != 0) items.Add($"未知位 0x{bits & ~((1 << names.Length) - 1):X4}");
        return items.Count == 0 ? "无" : string.Join("；", items);
    }
    private static string On(bool v)=>v?"ON":"OFF";
    private static string Mode(int code)=>code switch {2=>"AUTO_DIODE",3=>"ON",_=>"OFF"};
    private static string OcvState(ushort value)=>value switch {0=>"WAIT_CURRENT",1=>"PREPARE",2=>"READY",3=>"CORRECT_DOWN",_=>$"未知({value})"};
    private static string LearningState(ushort value)=>value switch {0=>"NONE",1=>"EMPTY_TO_FULL",2=>"FULL_TO_EMPTY",_=>$"未知({value})"};
    private static string EndpointState(ushort value)=>value switch {0=>"NORMAL",1=>"FULL_APPROACH",2=>"CONFIRMED_FULL",3=>"EMPTY_APPROACH",4=>"CONFIRMED_EMPTY",_=>$"未知({value})"};
    private static string EtaState(ushort value)=>value switch {0=>"INVALID",1=>"STABILIZING",2=>"VALID",3=>"LOW_CONFIDENCE",_=>$"未知({value})"};
    private static string EtaDirection(ushort value)=>value switch {0=>"NONE",1=>"CHARGE",2=>"DISCHARGE",_=>$"未知({value})"};
    private static string SohSource(ushort value)=>value switch {1=>"SOH_ESTIMATED / cycle model",2=>"SOH_CAPACITY_LEARNED",_=>$"未知({value})"};
    private static string LearningReject(ushort value)=>value switch {
        0=>"NONE",1=>"INVALID_SAMPLE",2=>"SAMPLE_GAP",3=>"REBOOT",4=>"DIRECTION_REVERSE",
        5=>"OPEN_WIRE",6=>"CELL_IMBALANCE",7=>"TEMPERATURE",8=>"PROTECTION",
        9=>"LOW_QUALITY_EMPTY",10=>"LOW_QUALITY_FULL",11=>"CAPACITY_RANGE",
        12=>"CANDIDATE_INCONSISTENT",13=>"AFE_COMMUNICATION",
        14=>"CALIBRATION_CHANGED",15=>"BALANCING",16=>"HEATING",
        17=>"CHARGER_CHANGE",18=>"LOAD_CHANGE",_=>$"未知({value})"};
    private static string SocSampleState(ushort value)=>value switch {
        0=>"NONE",1=>"INVALID",2=>"FIRST",3=>"DUPLICATE",4=>"GAP",
        5=>"ACCEPTED",6=>"DIRECTION_CHANGE",_=>$"未知({value})"};
    private static string SocAction(ushort value)=>value switch {
        0=>"NONE",1=>"INTEGRATE",2=>"OCV_DOWN",3=>"TERMINAL_DOWN",
        4=>"FULL_ANCHOR",5=>"FORCED_EMPTY",6=>"IDLE_EMPTY",
        7=>"PARAMETER_SET",8=>"STATE_RESTORE",_=>$"未知({value})"};
    private static string EndpointEvents(ushort flags)
    {
        var values=new List<string>();
        if((flags&1)!=0)values.Add("unexpected early UVP");
        if((flags&2)!=0)values.Add("large sag");
        if((flags&4)!=0)values.Add("cell imbalance / weak cell");
        if((flags&8)!=0)values.Add("capacity mismatch / SOC estimation error");
        if((flags&16)!=0)values.Add("capacity learning rejected");
        return values.Count==0?"无":string.Join("；",values);
    }
    private static string EtaMinutes(ushort value)=>value==0xFFFF?"unavailable":$"{value} min（based on recent current）";
    private static string D008CurrentText(int value)
        => value>0?$"{value} mA（放电）":value<0?$"{value} mA（充电）":"0 mA（静置）";

    public static SocDiagnosticSnapshot DecodeSocSnapshot(ushort[] w)
    {
        if(w.Length!=256 || w[0]!=Magic || w[1]!=Schema || (w[2]&RuntimeCapability)==0 || w[192]<2)
            throw new InvalidDataException("固件未提供 SOC diagnostics runtime v2");
        ushort flags=w[230],eta=w[239],decision=w[192]>=3?w[249]:(ushort)0;
        return new SocDiagnosticSnapshot(
            w[192],w[226] switch {1=>"LFP",2=>"NMC",_=>"AUTO/UNKNOWN"},w[227],w[228],
            w[202],w[203],w[231]/10.0,w[232]/10.0,w[233]/10.0,
            OcvState(w[204]),w[205],w[206],w[207],w[208],w[248],w[209],
            EndpointState(w[229]),(ushort)(flags>>8),I32(w,234),w[236],
            w[237]==0xFFFF?null:(int?)w[237],w[238]==0xFFFF?null:(int?)w[238],
            EtaState((ushort)(eta&0x0F)),EtaDirection((ushort)((eta>>4)&0x0F)),
            (ushort)(eta>>8),(flags&4)!=0,w[240],SohSource(w[241]),w[242],
            (flags&1)!=0,LearningState(w[210]),(flags&2)!=0,w[243]/10.0,w[212]/10.0,
            w[247],w[244],w[245],LearningReject(w[246]),
            w[192]>=3?SocSampleState((ushort)(decision&0x0F)):"UNAVAILABLE_V2",
            w[192]>=3?EtaDirection((ushort)((decision>>4)&0x0F)):"UNAVAILABLE_V2",
            w[192]>=3?SocAction((ushort)(decision>>8)):"UNAVAILABLE_V2",
            w[192]>=3?U32(w,250):0u,w[192]>=3?U32(w,252):0u,
            w[192]>=3?(ushort)(w[254]&0xFF):(ushort)0,
            w[192]>=3?(ushort)(w[254]>>8):(ushort)0,
            w[192]>=3?(ushort)(w[255]&0xFF):(ushort)0,
            w[192]>=3?(ushort)(w[255]>>8):(ushort)0);
    }
    public static string PmReasons(uint bits)
    {
        string[] names={"采样无效/过期","OTA进行中","Flash事务未恢复","OWC/总线忙",
            "电流达到suspend门槛","采样待处理","显式关机流程","ACC休眠流程"};
        var items=new List<string>();
        for(int i=0;i<names.Length;i++) if((bits&(1u<<i))!=0) items.Add(names[i]);
        if((bits&~0xFFu)!=0) items.Add($"未知位 0x{bits&~0xFFu:X8}");
        return items.Count==0?"无":string.Join("；",items);
    }
    private static string ProtectionText(ushort bits)
    {
        string[] names={"单体过压","单体欠压","总压过压","总压欠压","充电过流","放电过流",
            "充电高温","放电高温","充电低温","放电低温","单体压差过大","温差过大","SOC过低","MOS高温"};
        var items=new List<string>();
        for(int i=0;i<names.Length;i++) if((bits&(1u<<i))!=0)items.Add(names[i]);
        return items.Count==0?"无":string.Join("、",items);
    }
    public static void Decode(DiagnosticCapture c, ushort[] w)
    {
        if(w.Length!=256 || w[0]!=Magic || w[1]!=Schema) throw new InvalidDataException("诊断长度/magic/schema 不匹配");
        c.Words=w; c.Boot.Clear();c.Storage.Clear();c.Mos.Clear();
        void B(string k,string v)=>c.Boot.Add(new(k,v));
        void S(string k,string v)=>c.Storage.Add(new(k,v));
        void M(string k,string v)=>c.Mos.Add(new(k,v));
        B("Schema / Capabilities",$"{w[1]} / 0x{w[2]:X4}");
        B("启动快照",(w[3]&1)!=0?"已冻结":"启动未完成");
        bool genericFetBits=(w[29]&GenericFetBitsInfo)!=0 || w[14]==0x3510;
        string afeName=w[14] switch {0x1124=>"DVC1124",0x3510=>"SH3673510",_=>$"unknown 0x{w[14]:X4}"};
        B("MCU / AFE",$"0x{w[15]:X4} / {afeName}");
        B("SW/HW 编译开关",$"{w[13]&1}/{(w[13]>>1)&1}");
        B("构建类型",(w[13]&4)!=0?"PRODUCTION":"DEVELOPMENT / TEST");
        B("Git 工作区",(w[13]&8)!=0?"DIRTY（提交号不足以唯一复现）":"CLEAN");
        B("Flash 容量",$"{U32(w,20)} bytes（SDK code 0x{w[16]:X2}；0 bytes 表示未知）");
        B("Boot Address",$"0x{U32(w,18):X8}（FFFFFFFF=不可用）");
        B("布局允许",w[17]!=0?"是":"否");
        B("启动保护参数 / 存储升级",$"{(w[24]&1)!=0} / {(w[24]&2)!=0}");
        B("AFE 配置初始化",Result(w[25]));
        B("参数加载/校验",Result(w[26]));
        if ((w[2] & 16) != 0) {
            string[] stages={"未执行","开始","Config 加载失败","候选参数校验失败","Config 保存失败","Config 完成","State 初始化失败","Event 初始化失败","完成"};
            string stage=w[27]<stages.Length?stages[w[27]]:$"未知({w[27]})";
            var invalid=new List<string>();
            if((w[28]&1)!=0) invalid.Add("软件保护参数");
            if((w[28]&2)!=0) invalid.Add("AFE 硬件参数");
            if((w[28]&4)!=0) invalid.Add("SOC 配置");
            if((w[28]&8)!=0) invalid.Add("容量");
            B("启动升级阶段",stage);
            B("升级校验失败项",invalid.Count==0?"无":string.Join("；",invalid));
            B("软件参数 revision：存储 / 固件",$"{U32(w,106)} / {U32(w,108)}");
            B("启动原有 CUV：First/Second/Third/Recover",$"{w[96]}/{w[97]}/{w[98]}/{w[99]} mV");
            B("升级候选 CUV：First/Second/Third/Recover",$"{w[100]}/{w[101]}/{w[102]}/{w[103]} mV");
        } else B("启动升级详细原因","旧固件未提供，不能由存储初始化成功推断升级成功");
        B("Firmware Build ID",U32(w,22)==0?"未知":$"{U32(w,22):x8}");
        if((w[2]&StorageCapability)!=0) {
            string[] domains={"CONFIG","STATE","FACTORY","EVENT"};
            for(int i=0;i<4;i++) {
                int a=32+16*i;
                S(domains[i]+" 地址/大小",$"0x{U32(w,a):X8} / {U32(w,a+2)} bytes（配置大小，非有效性证明）");
                S(domains[i]+" 启动尝试/最近结果",$"{w[a+4]} / {Result(w[a+6])}");
                S(domains[i]+" 启动首次失败",w[a+5]==0?"无":$"{Result(w[a+5])} @ {U32(w,a+8)} ticks32k");
                S(domains[i]+" 使用过默认值",w[a+7]!=0?"是":"否");
                S(domains[i]+" 运行最近结果",Result(w[180+i]));
            }
            string[] counters={"Program calls","Erase calls","Verify failures","Deferred writes","Max program ticks32k","Max erase ticks32k"};
            for(int i=0;i<6;i++) S(counters[i],U32(w,160+2*i).ToString());
            S("底层首次失败",w[176]==0?"无":$"{Result(w[176])} @ 0x{U32(w,184):X8}");
            S("底层最近失败",w[177]==0?"无":$"{Result(w[177])} @ 0x{U32(w,178):X8}");
        } else {
            S("Storage diagnostics","固件未声明该 capability");
        }
        if((w[2]&MosCapability)!=0) {
            M("运行保护参数 / 存储升级",$"{(w[144]&1)!=0} / {(w[144]&2)!=0}");
            M("Requested CHG / DSG",$"{On((w[128]&1)!=0)} / {On((w[128]&2)!=0)}");
            M("软件允许 CHG / DSG",$"{On((w[129]&1)!=0)} / {On((w[129]&2)!=0)}（不代表物理导通）");
            M("CHG 阻断原因",Reasons(U32(w,136)));M("DSG 阻断原因",Reasons(U32(w,138)));
            if(genericFetBits) {
                M("AFE Command CHG / DSG",w[131]!=0?$"{On((w[130]&1)!=0)} / {On((w[130]&2)!=0)}":"未知/无效");
                M("AFE Command bits",w[131]!=0?$"0x{w[130]:X2}（最近成功 SH command）":"未知/无效");
                M("AFE Status CHG / DSG",w[133]!=0?$"{On((w[132]&1)!=0)} / {On((w[132]&2)!=0)}":"未知/无效");
                M("AFE Status bits",w[133]!=0?$"0x{w[132]:X2}（SH BSTATUS1，非物理反馈）":"未知/无效");
                if ((w[29] & ShFetDetailInfo) != 0) {
                    M("SH FLAG1",$"0x{w[142]:X2} · {NamedBits(w[142], "OV", "UV", "OCD1", "OCD2", "SC", "OCC", "WK", "RST1")}");
                    M("SH FLAG2",$"0x{w[143]:X2} · {NamedBits(w[143], "CADC", "VADC", "WDT", "RST2", "UTC", "OTC", "UTD", "OTD")}");
                    M("SH BSTATUS2",$"0x{w[145]:X2} · {NamedBits(w[145], "LOADOFF", "LOADON", "reserved", "BAL", "IDLE", "SLEEP", "DSGING", "CHGING")}");
                    M("SH backend 状态",$"0x{w[146]:X4} · {NamedBits(w[146], "输出使能", "采样有效", "输出抑制", "E2P 错误", "CHG 硬件阻断", "DSG 硬件阻断", "短路锁存", "等待重配", "温度断线", "AFE 错误", "SPI 错误")}");
                    M("通信保护状态",$"0x{w[147]:X4} · {NamedBits(w[147], "输出授权", "通信抑制", "总线静默", "通信故障锁存", "连续采样合格")}");
                    M("SH 温度传感器状态",$"0x{w[148]:X4} · {NamedBits(w[148], "TS1 有效", "TS2 有效", "MOS NTC 保护启用", "MOS NTC 有效")}");
                    M("TS4 MOS NTC 原始值",$"{unchecked((short)w[149])}；有效={(w[148]&8)!=0}");
                    M("TS4 MOS NTC 电阻",$"{U32(w,150)} Ω（仅有效采样）");
                    M("TS4 MOS 温度",(w[148]&8)!=0?$"{w[152]/10.0-40.0:F1} °C":"无效/断线");
                    M("AFE 标志快照",(w[146] & 2) != 0 ? "最近有效采样缓存；本诊断不直接读取 AFE 寄存器" : "采样无效；FLAG1/2 仅供参考");
                }
            } else {
                M("AFE Command R81",w[131]!=0?$"0x{w[130]:X2}（最近成功命令/读回）":"未知/无效");
                M("AFE Command CHG / DSG",w[131]!=0?$"{Mode(w[130]&3)} / {Mode((w[130]>>2)&3)}":"未知/无效");
                M("AFE Driver CHGF / DSGF",w[133]!=0?$"{On((w[132]&1)!=0)} / {On((w[132]&2)!=0)}":"未知/无效");
                M("AFE Driver R6",w[133]!=0?$"0x{w[132]:X2}（CHGF/DSGF，非物理反馈）":"未知/无效");
            }
            M("AFE 采样年龄",$"{unchecked(U32(w,6)-U32(w,140))} ticks32k；有效位={w[133]}");
            M("Physical Feedback","不可用 / unknown");
            M("Trace 条数 / 覆盖次数",$"{w[12]} / {U32(w,10)}");
        } else {
            M("MOS diagnostics","固件未声明该 capability");
        }

        c.Current.Clear();c.Soc.Clear();c.Power.Clear();c.Protection.Clear();
        if((w[2]&RuntimeCapability)!=0 && w[192]>=1) {
            void C(string k,string v)=>c.Current.Add(new(k,v));
            void O(string k,string v)=>c.Soc.Add(new(k,v));
            void P(string k,string v)=>c.Power.Add(new(k,v));
            void F(string k,string v)=>c.Protection.Add(new(k,v));
            bool sampleValid=(w[193]&1)!=0;
            C("Runtime Version",w[192].ToString());
            C("采样有效/新鲜",sampleValid?"是":"否");
            int rawCurrent=I32(w,194),businessCurrent=I32(w,196);
            C(genericFetBits?"AFE 换算电流（无独立 raw）":"AFE 原始电流",
                w[14]==0x1124?D008CurrentText(rawCurrent):$"{rawCurrent} mA");
            C("业务电流",w[14]==0x1124?D008CurrentText(businessCurrent):$"{businessCurrent} mA");
            C("采样年龄",$"{unchecked(U32(w,6)-U32(w,198))} ticks32k");
            C("SOC deadband",genericFetBits?$"{w[200]} mA（由当前 SH 产品固件上报）":$"{w[200]} mA（D008 另有固定≤200mA不可靠区）");
            C("过流恢复 pending",(w[193]&2)!=0?"是":"否");

            O("SOC estimate",$"{w[202]} %");
            O("SOC display",$"{w[203]} %");
            O("OCV state",OcvState(w[204]));
            O("OCV center / band",$"{w[205]} / {w[206]}..{w[207]} %");
            O("OCV confidence",w[208].ToString());
            O("静置累计",$"{w[209]} s");
            O("容量学习",LearningState(w[210]));
            O("容量已学习",w[211]!=0?"是":"否");
            O("学习容量",$"{w[212]/10.0:F1} Ah");
            if(w[192]>=2) {
                ushort flags=w[230],eta=w[239],endpointFlags=(ushort)(flags>>8);
                O("Chemistry / Profile",$"{(w[226]==1?"LFP":w[226]==2?"NMC":"AUTO/UNKNOWN")} / {w[227]} v{w[228]}");
                O("Nominal / Effective / Remaining Capacity",$"{w[231]/10.0:F1} / {w[232]/10.0:F1} / {w[233]/10.0:F1} Ah");
                O("OCV cell voltage",$"{w[248]} mV");
                O("Full/Empty Endpoint State",EndpointState(w[229]));
                O("Endpoint diagnostic",EndpointEvents(endpointFlags));
                O("Filtered Current / Variation",$"{I32(w,234)} / {w[236]} mA");
                O("Time To Empty",EtaMinutes(w[237]));
                O("Time To Full",EtaMinutes(w[238]));
                O("ETA State / Direction / Confidence",$"{EtaState((ushort)(eta&0x0F))} / {EtaDirection((ushort)((eta>>4)&0x0F))} / {eta>>8}%");
                O("SOH / Source / Confidence",$"{w[240]}% / {SohSource(w[241])} / {w[242]}%");
                O("Capacity Learning Enable",(flags&1)!=0?"是":"否（默认策略）");
                O("Candidate Capacity",$"{w[243]/10.0:F1} Ah · valid={(flags&2)!=0}");
                O("Learning Confidence / Valid / Rejected",$"{w[247]}% / {w[244]} / {w[245]}");
                O("Last Learning Reject Reason",LearningReject(w[246]));
                if(w[192]>=3) {
                    ushort decision=w[249];
                    O("Last Sample Decision",$"{SocSampleState((ushort)(decision&0x0F))} / {EtaDirection((ushort)((decision>>4)&0x0F))} / elapsed={U32(w,250)} ticks32k");
                    O("Last SOC Action",$"{SocAction((ushort)(decision>>8))} · {w[254]&0xFF}% -> {w[254]>>8}% · target={w[255]&0xFF}% · detail={w[255]>>8}");
                    O("Last Integral Delta",$"{U32(w,252)} × 0.1 As");
                }
            }

            if(w[14]==0x1124) {
                uint pm=U32(w,213);
                P("Suspend",w[215]!=0?"允许":"阻止");
                P("阻断原因",PmReasons(pm));
                P("电流门槛",$"±{w[221]} mA");
                P("BLE连接",w[219]!=0?"是":"否（不是suspend阻断条件）");
                P("Sample pending",w[220]!=0?"是":"否");
                P("低压关机 Region / 累计",$"{w[216]} / {U32(w,217)} s");
                P("设备运行模式",w[225]!=0?"FACTORY（允许受控工厂操作）":"NORMAL");
            } else {
                P("Power Management","当前固件未声明 D008 PM 字段；不把保留的 0 值解释为实际状态");
            }

            F("Level 1",$"0x{w[222]:X4} · {ProtectionText(w[222])}");
            F("Level 2",$"0x{w[223]:X4} · {ProtectionText(w[223])}");
            F("Level 3",$"0x{w[224]:X4} · {ProtectionText(w[224])}");
        }
    }
    private static string TraceName(ushort id,uint arg0,uint arg1) => id switch {
        1=>"BOOT",2=>"INIT",3=>"STORAGE",4=>"PARAMS",5=>"MOS",6=>"AFE",7=>"BOOT_DONE",8=>"DRIVER",9=>"UPGRADE",
        10=>$"CURRENT_RECOVERY chg={(arg0&1)!=0} dsg={(arg0&2)!=0} release={(arg1>>24)&0xFF}",
        11=>(arg0&1)!=0?"PM_ALLOW":$"PM_BLOCK · {PmReasons(arg1)}",
        12=>$"PROTECTION L1=0x{arg0&0xFFFF:X4} L2=0x{arg0>>16:X4} L3=0x{arg1&0xFFFF:X4}",
        13=>(arg0&1)!=0?"SAMPLE_VALID":"SAMPLE_INVALID",
        _=>$"Unknown({id})"
    };
    public static List<DiagnosticTrace> DecodeTrace(ushort[] w,uint last,ushort count)
    {
        if(w.Length!=768 || count>64) throw new InvalidDataException("Trace 长度/条数错误");
        var entries=new List<DiagnosticTrace>();
        for(int slot=0;slot<64;slot++) {
            int a=slot*12;uint seq=U32(w,a);
            // Modulo subtraction also orders entries across sequence rollover.
            if(unchecked(last-seq)>=count) continue;
            ushort id=w[a+4];uint arg0=U32(w,a+6),arg1=U32(w,a+8);
            entries.Add(new(seq,U32(w,a+2),id,TraceName(id,arg0,arg1),arg0,arg1));
        }
        return entries.OrderByDescending(e=>unchecked(last-e.Sequence)).ToList();
    }
    public static void ApplyEvidence(DiagnosticCapture c)
    {
        if(c.EvidenceBlocks.TryGetValue("Current",out var current) && current.Length>=7) {
            c.Current.Add(new("参数窗口原始电流",D008CurrentText(I32(current,0))));
            c.Current.Add(new("参数窗口校准电流",D008CurrentText(I32(current,2))));
            c.Current.Add(new("参数窗口有效/新鲜",current[4]!=0?"是":"否"));
        }
        if(c.EvidenceBlocks.TryGetValue("Calibration",out var cal) && cal.Length>=4) {
            c.Current.Add(new("持久化 offset",$"{I32(cal,0)} mA"));
            c.Current.Add(new("持久化 gain",$"{U32(cal,2)} ppm"));
        }
    }
    private static string Summary(DiagnosticCapture c)
    {
        var b=new StringBuilder();
        b.AppendLine("# BMS Diagnostic Summary");
        b.AppendLine($"Status: {c.Status}");
        b.AppendLine($"Endpoint: {c.Endpoint}");
        b.AppendLine($"Started UTC: {c.StartedUtc:O}");
        b.AppendLine($"Finished UTC: {c.FinishedUtc:O}");
        foreach(var group in new[]{("Current",c.Current),("SOC",c.Soc),("Power",c.Power),("MOS",c.Mos),("Protection",c.Protection)}) {
            b.AppendLine();b.AppendLine("## "+group.Item1);
            foreach(var field in group.Item2)b.AppendLine($"- {field.Field}: {field.Value}");
        }
        if(c.Errors.Count!=0){b.AppendLine();b.AppendLine("## Errors");foreach(var e in c.Errors)b.AppendLine("- "+e);}
        return b.ToString();
    }
    public static void Export(string path,DiagnosticCapture c,BmsHealthReport? health=null)
    {
        using var file=new FileStream(path,FileMode.Create,FileAccess.Write,FileShare.None);
        using var zip=new ZipArchive(file,ZipArchiveMode.Create);
        void Add(string name,object? data) {
            using var stream=zip.CreateEntry(name).Open();
            JsonSerializer.Serialize(stream,data,new JsonSerializerOptions {WriteIndented=true});
        }
        void AddText(string name,string data) {
            using var stream=zip.CreateEntry(name).Open();
            using var writer=new StreamWriter(stream,new UTF8Encoding(false));writer.Write(data);
        }
        ushort[]? Block(string name)=>c.EvidenceBlocks.TryGetValue(name,out var value)?value:null;
        Add("manifest.json",new {bundle_schema=2,c.StartedUtc,c.FinishedUtc,c.Endpoint,c.Status,c.Supported,
            c.SnapshotConsistent,c.TraceConsistent,c.Identity,c.Errors,tool_version=typeof(BmsDiagnostics).Assembly.GetName().Version?.ToString(),
            firmware_git_commit=c.Words is null || U32(c.Words,22)==0 ? "unknown" : U32(c.Words,22).ToString("x8"),
            physical_feedback="unavailable",raw_frames_scope="all reads performed by this diagnostic capture"});
        AddText("summary.md",Summary(c));
        Add("boot.json",c.Boot);Add("storage.json",c.Storage);Add("mos.json",c.Mos);
        Add("current.json",new {runtime=c.Current,raw_window=Block("Current")});
        Add("soc.json",c.Soc);Add("power.json",c.Power);Add("protection_runtime.json",c.Protection);
        Add("runtime.json",new {c.Words});Add("trace.json",c.Trace);Add("events.json",c.Events);
        Add("evidence.json",c.EvidenceBlocks);
        Add("health.json",health??BmsHealth.Evaluate(c));
        Add("parameters.json",new {
            capability=Block("D008Capability"),capacity_cycle=Block("CapacityCycle"),soc=Block("SOC"),
            heater=Block("Heater"),calibration=Block("Calibration"),serial=Block("Serial"),
            software_protection=c.SoftwareProtectionWords});
        Add("afe.json",new {requested=Block("AfeRequested"),meta=Block("AfeMeta"),effective=Block("AfeEffective")});
        Add("software_protection.json",new {start_register="0x2100", word_count=65,
            recovery_semantics="Recover applies to Third only; First/Second are threshold alarms",
            words=c.SoftwareProtectionWords});
        Add("raw_frames.json",c.Frames);
    }
}
