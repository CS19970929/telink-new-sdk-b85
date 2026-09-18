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
    public Dictionary<string, ushort[]> Blocks { get; } = new();
    public List<string> Errors { get; } = new();
}

public static class D008Parameters
{
    public const ushort Base=0x2E00, Magic=0xD008, Schema=1;
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
