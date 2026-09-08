using System.Globalization;
using System.IO;
namespace BmsTool.Windows;

// SH3673520 schema v1. Physical-unit editors never expose packed bitfields.
public sealed class Sh3520Field
{
    public string Name { get; }
    public string Unit { get; }
    public string Range { get; }
    public string Current { get; }
    public string Edit { get; set; }
    public bool Editable { get; }
    private readonly Action<ushort[], decimal> _set;
    public Sh3520Field(string name, string unit, string range, decimal value,
        Action<ushort[], decimal> set, bool editable = true)
    {
        Name=name; Unit=unit; Range=range; Current=value.ToString(CultureInfo.InvariantCulture);
        Edit=Current; _set=set; Editable=editable;
    }
    public void Apply(ushort[] words)
    {
        if (!Editable) return;
        if (!decimal.TryParse(Edit, NumberStyles.Number, CultureInfo.InvariantCulture, out var value))
            throw new ArgumentException(Name+"：请输入有效数字。");
        try { _set(words,value); }
        catch (Exception ex) { throw new ArgumentException(Name+"："+Range,ex); }
    }
}

public static class Sh3520Parameters
{
    public const ushort HardwareBase=0x2500, SoftwareBase=0x2400, Magic=0x3520, Version=1;
    public const int ParameterWords=24, ReadWords=32;
    public static readonly int[] NormalDelay={140,280,490,980,2030,3010,4970,10010};
    public static readonly int[] UvDelay={490,770,980,1470,2030,3010,4970,10010};
    public static readonly int[] ScDelay={0,32,64,96,128,192,224,256,288,320,384,448,480,512,544,576};
    public static readonly int[] ScMultiplier={2,3,4,6};
    private static readonly int[] Ntc10Ohm={
    20375, 19204, 18115, 17100, 16152, 15266, 14437, 13661, 12934, 12251,
    11611, 11008, 10442, 9909, 9407, 8935, 8489, 8068, 7672, 7297,
    6943, 6608, 6292, 5993, 5710, 5442, 5188, 4948, 4720, 4504,
    4300, 4105, 3921, 3746, 3580, 3422, 3272, 3130, 2994, 2866,
    2751, 2627, 2516, 2410, 2310, 2214, 2123, 2036, 1953, 1874,
    1801, 1726, 1658, 1592, 1530, 1470, 1413, 1358, 1306, 1256,
    1209, 1163, 1119, 1078, 1038, 1000, 963, 928, 894, 862,
    831, 801, 773, 746, 719, 694, 670, 647, 625, 604,
    583, 563, 544, 526, 509, 492, 476, 460, 445, 431,
    416, 403, 390, 378, 366, 355, 343, 333, 322, 312,
    303, 294, 285, 276, 268, 260, 252, 244, 237, 230,
    224, 217, 211, 205, 199, 193, 188, 182, 177, 172,
    167, 163, 158, 154, 150, 146, 142, 138, 134, 131,
    127, 124, 120, 117, 114, 111, 108, 106, 103, 100,
    98
};
    public static void CheckIdentity(ushort[] w)
    {
        if(w.Length!=ReadWords || w[0]!=Magic || w[1]!=Version || w[31]!=ParameterWords)
            throw new IOException("设备未提供受支持的 SH3673520 参数 v1；禁止写入。");
    }
    private static int Integral(decimal v, int min, int max, int step=1)
    {
        if(v<min || v>max || v%step!=0) throw new ArgumentOutOfRangeException();
        return (int)v;
    }
    private static uint U32(ushort[] w,int i)=>(uint)w[i]|((uint)w[i+1]<<16);
    private static void SetBits(ushort[] w,int i,int shift,int mask,int value)
        =>w[i]=(ushort)((w[i]&~(mask<<shift))|((value&mask)<<shift));
    public static List<Sh3520Field> HardwareFields(ushort[] w)
    {
        CheckIdentity(w); var f=new List<Sh3520Field>();
        void Number(string name,string unit,int i,int shift,int mask,int scale,int offset,int min,int max)
        {
            f.Add(new(name,unit,$"{min}～{max}，步进 {scale}",((w[i]>>shift)&mask)*scale+offset,
                (a,v)=>SetBits(a,i,shift,mask,(Integral(v,min,max)-offset)%scale==0
                    ? ((int)v-offset)/scale : throw new ArgumentOutOfRangeException())));
        }
        void Choice(string name,string unit,int i,int shift,int mask,int[] table)
        {
            f.Add(new(name,unit,string.Join(" / ",table),table[(w[i]>>shift)&mask],(a,v)=>{
                int code=Array.IndexOf(table,Integral(v,0,65535));
                if(code<0) throw new ArgumentOutOfRangeException(); SetBits(a,i,shift,mask,code);
            }));
        }
        Number("过压触发","mV",2,0,1023,5,0,5,5115);
        Choice("过压延时","ms",2,10,7,NormalDelay);
        Number("欠压触发","mV",3,0,1023,5,0,5,5115);
        Choice("欠压延时","ms",3,10,7,UvDelay);
        Number("放电过流 1 分流压差","mV",4,0,15,5,5,5,80);
        Choice("放电过流 1 延时","ms",4,4,7,NormalDelay);
        Number("放电过流 2 分流压差","mV",4,8,15,10,10,10,160);
        Number("放电过流 2 延时","ms",4,12,15,25,25,25,400);
        Choice("短路阈值 / 放电过流 2","倍",5,4,3,ScMultiplier);
        Choice("短路延时","µs",5,0,15,ScDelay);
        Number("充电过流分流压差","µV",5,8,31,1375,1375,1375,44000);
        Choice("充电过流延时","ms",5,13,7,NormalDelay);
        string[] temps={"充电高温","放电高温","充电低温","放电低温"};
        for(int n=0;n<4;n++) {
            int i=6+n*2,min=n<2?980:10000,max=n<2?9999:203750;
            f.Add(new(temps[n]+" NTC 触发电阻","Ω",$"{min}～{max}；芯片分压档位会量化",U32(w,i),(a,v)=>{
                uint value=(uint)Integral(v,min,max);a[i]=(ushort)value;a[i+1]=(ushort)(value>>16);
            }));
        }
        Number("过压恢复","mV",14,0,65535,1,0,1,5114);
        Number("欠压恢复","mV",15,0,65535,1,0,1,5114);
        for(int n=0;n<4;n++) {
            int i=16+n;
            f.Add(new(temps[n]+"恢复","℃","-40～100；必须满足独立硬件滞回",(short)w[i],
                (a,v)=>a[i]=unchecked((ushort)(short)Integral(v,-40,100))));
        }
        f.Add(new("过流恢复电流上限","mA","100～65500，步进 100",w[20],(a,v)=>a[20]=(ushort)Integral(v,100,65500,100)));
        f.Add(new("硬件恢复稳定时间","ms","200～65400，步进 200",w[21],(a,v)=>a[21]=(ushort)Integral(v,200,65400,200)));
        string[] flags={"过压","欠压","放电过流","短路","TS1 温度","TS2 温度","TS3 温度","TS4 温度","充电过流"};
        for(int bit=0;bit<flags.Length;bit++) Number(flags[bit]+"使能","0/1",22,bit,1,1,0,0,1);
        return f;
    }
    public static List<Sh3520Field> SoftwareFields(ushort[] w)
    {
        if(w.Length!=24) throw new IOException("软件参数长度错误。");
        string[] names={"过压触发","过压恢复","过压延时","欠压触发","欠压恢复","欠压延时","充电过流 1","充电过流 1 延时","充电过流 2（兼容，未使用）","充电过流 2 延时（兼容，未使用）","放电过流 1","放电过流 1 延时","放电过流 2（兼容，未使用）","放电过流 2 延时（兼容，未使用）","充电高温","充电高温恢复","充电低温","充电低温恢复","放电高温","放电高温恢复","放电低温","放电低温恢复","短路电流（兼容，未使用）","短路延时（兼容，未使用）"};
        var f=new List<Sh3520Field>();
        for(int n=0;n<24;n++) {
            int i=n; bool inactive=i==8||i==9||i==12||i==13||i>=22;
            bool temp=i>=14&&i<=21, current=i==6||i==8||i==10||i==12;
            bool delay=i==2||i==5||i==7||i==9||i==11||i==13;
            decimal scale=temp||current?0.1m:delay?10m:1m,offset=temp?-40m:0m;
            int min=i<6&&!delay?1000:current?10:temp?(i==16||i==20?0:i==14||i==18?400:1):1;
            int max=i<6&&!delay?5000:temp?(i==16||i==20?800:i==14||i==18?2000:50000):50000;
            if(temp) max=Math.Min(max,1400);
            f.Add(new(names[i],temp?"℃":current?"A":delay?"ms":i<6?"mV":"原始值",
                inactive?"只读，不参与软件保护":$"{min*scale+offset}～{max*scale+offset}；步进 {scale}",w[i]*scale+offset,
                (a,v)=>a[i]=(ushort)Integral((v-offset)/scale,min,max),!inactive));
        }
        return f;
    }
    public static ushort[] Build(ushort[] original,IEnumerable<Sh3520Field> fields,bool hardware)
    {
        var w=original.Take(24).ToArray(); foreach(var f in fields) f.Apply(w);
        if(hardware) {
            int ov=(w[2]&1023)*5,uv=(w[3]&1023)*5;
            if(!(uv<w[15]&&w[15]<w[14]&&w[14]<ov)) throw new ArgumentException("必须：欠压触发 < 欠压恢复 < 过压恢复 < 过压触发。");
            for(int n=0;n<4;n++) {
                int c=(short)w[16+n]; if(c< -40||c>100) throw new ArgumentException("温度恢复范围错误。");
                uint ohm=U32(w,6+n*2); int r=Ntc10Ohm[c+40]*10;
                if(n<2?r<=ohm:r>=ohm) throw new ArgumentException("温度恢复与触发 NTC 电阻没有正确滞回。");
            }
            if((short)w[18]>=(short)w[16] || (short)w[19]>=(short)w[17]) throw new ArgumentException("低温恢复必须低于高温恢复。");
        } else {
            if(!(w[3]<w[4]&&w[4]<w[1]&&w[1]<w[0]) ||
                !(w[16]<w[17]&&w[17]<w[15]&&w[15]<w[14]) ||
                !(w[20]<w[21]&&w[21]<w[19]&&w[19]<w[18])) throw new ArgumentException("软件保护触发和恢复必须保留正确的滞回区间。");
        }
        return w;
    }
}
