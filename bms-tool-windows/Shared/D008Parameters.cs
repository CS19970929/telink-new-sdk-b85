using System.IO;
using System.Globalization;
using System.IO.Compression;
using System.Text;
using System.Text.Json;

namespace BmsTool.Windows;

public sealed class D008ParameterCapture
{
    public DateTimeOffset CapturedUtc { get; set; } = DateTimeOffset.UtcNow;
    public string Status { get; set; } = "未读取";
    public bool Supported { get; set; }
    public ushort ProtocolVersion { get; set; }
    public Dictionary<string, ushort[]> Blocks { get; } = new();
    public List<string> Errors { get; } = new();
}

public static class BmsSleepParameters
{
    public const ushort Capability=0x0080, Base=0x2E90, DefaultsBase=0x2E98, Count=5;
    public static bool Available(D008ParameterCapture capture)=>capture.Supported &&
        capture.ProtocolVersion>=2 && capture.Blocks.TryGetValue("Capability",out var header) &&
        header.Length>=3 && (header[2]&Capability)!=0;
    public static ushort[] Build(string lowMv,string lowMinutes,string normalMv,string normalMinutes,string afeMinutes)
    {
        var values=new[]{ushort.Parse(lowMv),ushort.Parse(lowMinutes),ushort.Parse(normalMv),
            ushort.Parse(normalMinutes),ushort.Parse(afeMinutes)};
        Validate(values);return values;
    }
    public static void Validate(ushort[] values)
    {
        if(values.Length!=Count || values[0]<1000 || values[0]>=values[2] || values[2]>4500 ||
           values[1]==0 || values[1]>values[3] || values[4]==0)
            throw new ArgumentException("休眠参数须整组五项：1000≤下档电压<上档电压≤4500 mV；延时为1～65535整数分钟，下档延时不能大于上档延时。");
    }
}

public static class D008Parameters
{
    public const ushort Base=0x2E00, Magic=0xD008, MinimumProtocolVersion=1, CurrentProtocolVersion=2;
    public const ushort BalanceCapability=0x0040;
    public const ushort HeaterCapability=0x0004, HeaterBase=0x2E20, BalanceBase=0x2E70;
    public static bool SupportsProtocol(ushort version)=>version is >=MinimumProtocolVersion and <=CurrentProtocolVersion;
    public static uint U32(ushort[] w,int i)=>(uint)w[i]|((uint)w[i+1]<<16);
    public static int I32(ushort[] w,int i)=>unchecked((int)U32(w,i));
    public static ushort[] Calibration(int offset,uint gain)=>new[]{unchecked((ushort)offset),unchecked((ushort)(offset>>16)),(ushort)gain,(ushort)(gain>>16)};
    public static ushort[] SerialWords(string text)
    {
        if(text.Length is <1 or >32 || text.Any(c=>c<32 || c>126))
            throw new ArgumentException("SN 必须为 1–32 个可打印 ASCII 字符。");
        var bytes=new byte[32];Encoding.ASCII.GetBytes(text).CopyTo(bytes,0);
        return Enumerable.Range(0,16).Select(i=>(ushort)((bytes[2*i]<<8)|bytes[2*i+1])).ToArray();
    }
    public static string Serial(ushort[] w)=>Encoding.ASCII.GetString(w.SelectMany(x=>new[]{(byte)(x>>8),(byte)x}).ToArray()).TrimEnd('\0');
    public static ushort Temperature(string text)
    {
        decimal value=decimal.Parse(text,CultureInfo.CurrentCulture)*10m+400m;
        if(value<0 || value>1650 || value!=decimal.Truncate(value))throw new ArgumentException("温度范围 -40.0～125.0 ℃，最多一位小数。");
        return (ushort)value;
    }
    public static ushort Capacity(string text)
    {
        decimal value=decimal.Parse(text,CultureInfo.CurrentCulture)*10m;
        if(value<1 || value>6553 || value!=decimal.Truncate(value))throw new ArgumentException("额定容量范围 0.1～655.3 Ah，最多一位小数。");
        return (ushort)value;
    }
    public static ushort[] Heater(bool enabled,string startC,string stopC)
    {
        var values=new[]{(ushort)(enabled?1:0),Temperature(startC),Temperature(stopC)};
        ValidateHeater(values);return values;
    }
    public static void ValidateHeater(ushort[] values)
    {
        if(values.Length!=3 || values[0]>1 || values[1]>=values[2] || values[2]>1650)
            throw new ArgumentException("加热参数须整组三项：开关为 0/1，温度 -40.0～125.0 ℃，启动温度低于停止温度。");
    }
    public static ushort[] Balance(bool enabled,string startMv,string startDeltaMv,string stopDeltaMv)
    {
        var values=new[]{(ushort)(enabled?1:0),ushort.Parse(startMv),ushort.Parse(startDeltaMv),ushort.Parse(stopDeltaMv)};
        ValidateBalance(values);return values;
    }
    public static void ValidateBalance(ushort[] values)
    {
        if(values.Length!=4 || values[0]>1 || values[1] is <1000 or >4500 ||
           values[2] is <1 or >1000 || values[3]>=values[2])
            throw new ArgumentException("均衡参数须整组四项：开关为 0/1，起始电压 1000～4500 mV，启动压差 1～1000 mV，停止压差须小于启动压差（允许 0）。");
    }
    public static bool HeaterAvailable(D008ParameterCapture capture)=>capture.Supported &&
        capture.Blocks.TryGetValue("Capability",out var h) && h.Length>=12 &&
        (h[2]&HeaterCapability)!=0 && h[9]==1;
    public static bool BalanceAvailable(D008ParameterCapture capture)=>capture.Supported && capture.ProtocolVersion>=2 &&
        capture.Blocks.TryGetValue("Capability",out var h) && h.Length>=3 && (h[2]&BalanceCapability)!=0;
    public static uint Gain(double meanRaw,int offset,int referenceMa)
    {
        double corrected=meanRaw-offset;
        if(referenceMa==0 || corrected==0 || Math.Sign(corrected)!=Math.Sign(referenceMa))
            throw new ArgumentException("参考电流与去零后的原始电流必须同向且非零；放电为正、充电为负。");
        double gain=Math.Round(referenceMa/corrected*1000000d);
        if(gain<100000 || gain>10000000)throw new ArgumentException("计算的增益超出 0.1～10 倍，请检查参考电流及单位。");
        return (uint)gain;
    }
    public static void Export(string path,D008ParameterCapture capture)
    {
        using var file=new FileStream(path,FileMode.Create,FileAccess.Write);
        using var zip=new ZipArchive(file,ZipArchiveMode.Create);
        using var writer=new StreamWriter(zip.CreateEntry("parameters.json").Open(),new UTF8Encoding(false));
        writer.Write(JsonSerializer.Serialize(capture,new JsonSerializerOptions{WriteIndented=true}));
        // Only explicitly read parameter/identity blocks are exported. No password/session tokens.
    }
}
