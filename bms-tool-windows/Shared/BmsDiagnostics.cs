using System.IO;
using System.IO.Compression;
using System.Text.Json;

namespace BmsTool.Windows;

public sealed record DiagnosticField(string Field, string Value);
public sealed record DiagnosticTrace(uint Sequence, uint Tick32k, ushort EventId, string Event, uint Arg0, uint Arg1);
public sealed record DiagnosticFrame(DateTimeOffset Time, string Direction, string Hex);
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
    public List<DiagnosticTrace> Trace { get; } = new();
    public Dictionary<string,string> Identity { get; } = new();
    public ushort[]? Events { get; set; }
    public List<string> Errors { get; } = new();
    public List<DiagnosticFrame> Frames { get; } = new();
}
public static class BmsDiagnostics
{
    public const ushort Base = 0x2A00, TraceBase = 0x2B00, Magic = 0x4447, Schema = 1;
    public static uint U32(ushort[] w, int at) => (uint)w[at] | ((uint)w[at+1] << 16);
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
    private static string On(bool v)=>v?"ON":"OFF";
    public static void Decode(DiagnosticCapture c, ushort[] w)
    {
        if(w.Length!=256 || w[0]!=Magic || w[1]!=Schema) throw new InvalidDataException("诊断长度/magic/schema 不匹配");
        c.Words=w; c.Boot.Clear();c.Storage.Clear();c.Mos.Clear();
        void B(string k,string v)=>c.Boot.Add(new(k,v));
        void S(string k,string v)=>c.Storage.Add(new(k,v));
        void M(string k,string v)=>c.Mos.Add(new(k,v));
        B("Schema / Capabilities",$"{w[1]} / 0x{w[2]:X4}");
        B("启动快照",(w[3]&1)!=0?"已冻结":"启动未完成");
        B("MCU / AFE",$"0x{w[15]:X4} / 0x{w[14]:X4}");
        B("SW/HW 编译开关",$"{w[13]&1}/{(w[13]>>1)&1}");
        B("Flash 容量",$"{U32(w,20)} bytes（SDK code 0x{w[16]:X2}；0 bytes 表示未知）");
        B("Boot Address",$"0x{U32(w,18):X8}（FFFFFFFF=不可用）");
        B("布局允许",w[17]!=0?"是":"否");
        B("启动保护参数 / 存储升级",$"{(w[24]&1)!=0} / {(w[24]&2)!=0}");
        B("AFE 配置初始化",Result(w[25]));
        B("参数加载/校验",Result(w[26]));
        B("Firmware Build ID",U32(w,22)==0?"未知":$"{U32(w,22):x8}");
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
        M("运行保护参数 / 存储升级",$"{(w[144]&1)!=0} / {(w[144]&2)!=0}");
        M("Requested CHG / DSG",$"{On((w[128]&1)!=0)} / {On((w[128]&2)!=0)}");
        M("软件允许 CHG / DSG",$"{On((w[129]&1)!=0)} / {On((w[129]&2)!=0)}（不代表物理导通）");
        M("CHG 阻断原因",Reasons(U32(w,136)));M("DSG 阻断原因",Reasons(U32(w,138)));
        M("AFE Command R81",w[131]!=0?$"0x{w[130]:X2}（最近成功命令/读回）":"未知/无效");
        M("AFE Driver R6",w[133]!=0?$"0x{w[132]:X2}（CHGF/DSGF，非物理反馈）":"未知/无效");
        M("AFE 采样年龄",$"{unchecked(U32(w,6)-U32(w,140))} ticks32k；有效位={w[133]}");
        M("Physical Feedback","不可用 / unknown");
        M("Trace 条数 / 覆盖次数",$"{w[12]} / {U32(w,10)}");
    }
    public static List<DiagnosticTrace> DecodeTrace(ushort[] w,uint last,ushort count)
    {
        if(w.Length!=768 || count>64) throw new InvalidDataException("Trace 长度/条数错误");
        string[] names={"Unknown","BOOT","INIT","STORAGE","PARAMS","MOS","AFE","BOOT_DONE","DRIVER"};
        var entries=new List<DiagnosticTrace>();
        for(int slot=0;slot<64;slot++) {
            int a=slot*12;uint seq=U32(w,a);
            // Modulo subtraction also orders entries across sequence rollover.
            if(unchecked(last-seq)>=count) continue;
            ushort id=w[a+4]; entries.Add(new(seq,U32(w,a+2),id,id<names.Length?names[id]:"Unknown",U32(w,a+6),U32(w,a+8)));
        }
        return entries.OrderByDescending(e=>unchecked(last-e.Sequence)).ToList();
    }
    public static void Export(string path,DiagnosticCapture c)
    {
        using var file=new FileStream(path,FileMode.Create,FileAccess.Write,FileShare.None);
        using var zip=new ZipArchive(file,ZipArchiveMode.Create);
        void Add(string name,object? data) {
            using var stream=zip.CreateEntry(name).Open();
            JsonSerializer.Serialize(stream,data,new JsonSerializerOptions {WriteIndented=true});
        }
        Add("manifest.json",new {bundle_schema=1,c.StartedUtc,c.FinishedUtc,c.Endpoint,c.Status,c.Supported,
            c.SnapshotConsistent,c.TraceConsistent,c.Identity,c.Errors,tool_version=typeof(BmsDiagnostics).Assembly.GetName().Version?.ToString(),
            firmware_git_commit=c.Words is null || U32(c.Words,22)==0 ? "unknown" : U32(c.Words,22).ToString("x8"), physical_feedback="unavailable"});
        Add("boot.json",c.Boot);Add("storage.json",c.Storage);Add("mos.json",c.Mos);
        Add("runtime.json",new {c.Words});Add("trace.json",c.Trace);Add("events.json",c.Events);
        Add("raw_frames.json",c.Frames);
    }
}
