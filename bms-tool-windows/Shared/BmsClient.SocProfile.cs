using System.IO;
using System.Text.Json;

namespace BmsTool.Windows;

public sealed record SocOcvPoint(ushort Millivolts, ushort Percent);

public sealed class SocProfileParameters
{
    public ushort Chemistry { get; set; }
    public ushort ProfileId { get; set; }
    public ushort ProfileVersion { get; set; }
    public ushort CurrentDeadbandMa { get; set; }
    public ushort RestPrepareSeconds { get; set; }
    public ushort ErrorBandPercent { get; set; }
    public ushort ValidMinMv { get; set; }
    public ushort ValidMaxMv { get; set; }
    public ushort FullSyncMv { get; set; }
    public ushort FullMinMarginMv { get; set; }
    public ushort EmptySyncMv { get; set; }
    public ushort EmptyMaxMarginMv { get; set; }
    public ushort MinimumErrorBandPercent { get; set; }
    public ushort FullCellDeltaMaxMv { get; set; }
    public ushort TerminalCellDeltaMaxMv { get; set; }
    public ushort TerminalStartOffsetMv { get; set; }
    public ushort TerminalL1OffsetMv { get; set; }
    public ushort TerminalL2OffsetMv { get; set; }
    public ushort TerminalL3OffsetMv { get; set; }
    public List<SocOcvPoint> OcvPoints { get; set; } = new();
    public bool SupportsPolicyV3 { get; internal set; }
    public bool Persisted { get; internal set; }
}

public sealed class SocProfileBundle
{
    public int FormatVersion { get; set; } = 1;
    public string Name { get; set; } = "";
    public string Source { get; set; } = "";
    public bool EngineeringCandidate { get; set; } = true;
    public SocProfileParameters Profile { get; set; } = new();
}

public sealed partial class BmsClient
{
    public static SocProfileBundle CreateSocTrialBundle(ushort chemistry)
    {
        if (chemistry is < 1 or > 2) throw new ArgumentException("不支持的电池类型。");
        var p = new SocProfileParameters {
            Chemistry=chemistry, ProfileId=chemistry, ProfileVersion=3,
            CurrentDeadbandMa=200, RestPrepareSeconds=3600,
            ErrorBandPercent=(ushort)(chemistry==1?7:5), MinimumErrorBandPercent=(ushort)(chemistry==1?7:5),
            ValidMinMv=2000, ValidMaxMv=(ushort)(chemistry==1?3800:4300),
            FullSyncMv=(ushort)(chemistry==1?3500:4180), FullMinMarginMv=200, FullCellDeltaMaxMv=200,
            EmptySyncMv=(ushort)(chemistry==1?2850:3100), EmptyMaxMarginMv=300,
            // 旧末段字段只保留原兼容值，策略v3不执行多级末端拉低。
            TerminalCellDeltaMaxMv=50, TerminalStartOffsetMv=(ushort)(chemistry==1?150:300),
            TerminalL1OffsetMv=(ushort)(chemistry==1?100:200), TerminalL2OffsetMv=(ushort)(chemistry==1?50:150),
            TerminalL3OffsetMv=(ushort)(chemistry==1?20:50)
        };
        ushort[] mv = chemistry==1 ? new ushort[]{2844,3163,3204,3223,3245,3259,3277,3288,3291,3294,3315,3328,3330,3445}
            : new ushort[]{3000,3300,3450,3550,3650,3700,3750,3800,3850,3900,4000,4100,4180};
        ushort[] percent = chemistry==1 ? new ushort[]{0,5,10,15,20,25,30,35,55,60,65,75,95,100}
            : new ushort[]{0,5,10,20,30,40,50,60,70,80,90,96,100};
        for(int i=0;i<mv.Length;i++)p.OcvPoints.Add(new SocOcvPoint(mv[i],percent[i]));
        return new SocProfileBundle {
            Name=chemistry==1?"LFP 通用评估候选 v3":"NMC 4.20V 工程对照 v3",
            Source=chemistry==1?"国轩314Ah规格书25℃静置数据压缩14点；非所有LFP通用精度模型":"既有13点通用表，仅作首轮对照",
            EngineeringCandidate=true, Profile=p
        };
    }

    public static string ExportSocBundle(SocProfileBundle bundle)
    {
        if(bundle is null)throw new ArgumentNullException(nameof(bundle));
        ValidateSocProfile(bundle.Profile);
        return JsonSerializer.Serialize(bundle,new JsonSerializerOptions{WriteIndented=true});
    }

    public static SocProfileBundle ImportSocBundle(string json)
    {
        if (string.IsNullOrWhiteSpace(json) || json.Length>1024*1024) throw new ArgumentException("SOC配置文件为空或过大。");
        var bundle=JsonSerializer.Deserialize<SocProfileBundle>(json) ?? throw new ArgumentException("SOC配置文件无效。");
        if(bundle.FormatVersion!=1 || string.IsNullOrWhiteSpace(bundle.Name) || string.IsNullOrWhiteSpace(bundle.Source) ||
            bundle.Name.Length>256 || bundle.Source.Length>2048) throw new ArgumentException("SOC配置版本、名称或来源无效。");
        ValidateSocProfile(bundle.Profile);
        if(bundle.Profile.ProfileVersion!=3) throw new ArgumentException("请先转换并审核为SOC策略v3模型；不能静默改写旧模型语义。");
        return bundle;
    }

    private const ushort SocProfileBase = 0x2EB0;
    private const ushort SocProfilePersisted = 0x2EE4;
    private const ushort SocProfileGeneration = 0x2EE5;
    private const ushort SocProfileCommand = 0x2EF8;
    private const ushort SocProfileStageBase = 0x2F20;
    private const int SocProfileWords = 50;

    public async Task<SocProfileParameters> ReadSocProfileAsync(CancellationToken ct = default)
    {
        ushort[] capability = await ReadRegistersAsync(0x2E00, 3, ct);
        if (capability[0] != D008Parameters.Magic || (capability[2] & 0x0200) == 0)
            throw new NotSupportedException("当前设备固件不支持 SOC/OCV profile 读写。未发送写命令。");
        ushort[] signature = await ReadRegistersAsync(SocProfileBase, 2, ct);
        if (signature[0] != 0x534F || signature[1] != 1)
            throw new NotSupportedException("SOC/OCV profile 接口版本不匹配。未发送写命令。");
        ushort generationBefore=(await ReadRegistersAsync(SocProfileGeneration,1,ct))[0];
        var words = new List<ushort>(SocProfileWords);
        for (int offset = 0; offset < SocProfileWords; offset += 7)
        {
            ushort count = (ushort)Math.Min(7, SocProfileWords - offset);
            words.AddRange(await ReadRegistersAsync((ushort)(SocProfileBase + 2 + offset), count, ct));
        }
        ushort[] metadata = await ReadRegistersAsync(SocProfilePersisted, 3, ct);
        if(metadata[1]!=generationBefore)throw new IOException("读取期间SOC模型已变化，请重新读取后再操作。");
        SocProfileParameters result = DecodeSocProfile(words.ToArray());
        result.Persisted = metadata[0] != 0;
        result.SupportsPolicyV3 = (capability[2] & 0x0800) != 0;
        return result;
    }

    public async Task<SocProfileParameters> SaveSocProfileAsync(
        SocProfileParameters profile, string password, CancellationToken ct = default)
    {
        if (!BmsServiceAccess.PasswordValid(password)) throw new UnauthorizedAccessException("服务密码错误。");
        ushort[] words = EncodeSocProfile(profile);
        SocProfileParameters current = await ReadSocProfileAsync(ct);
        if (!current.SupportsPolicyV3 || profile.ProfileVersion != 3)
            throw new NotSupportedException("设备与模型必须同时支持 SOC 策略 v3。未发送写命令。");
        if (current.Chemistry != profile.Chemistry)
            throw new ArgumentException("SOC 模型类型必须与设备一致。请通过完整电池类型切换操作更改类型。");
        await WriteRegistersAsync(SocProfileCommand, CommandWords(1, 0), ct);
        ushort generation = (await ReadRegistersAsync(SocProfileGeneration, 1, ct))[0];
        for (int offset = 0; offset < words.Length; offset += 4)
            await WriteRegistersAsync((ushort)(SocProfileStageBase + offset),
                words.Skip(offset).Take(Math.Min(4, words.Length - offset)).ToArray(), ct);
        await WriteRegistersAsync(SocProfileCommand, CommandWords(2, generation), ct);
        SocProfileParameters readback = await ReadSocProfileAsync(ct);
        if (!EncodeSocProfile(readback).SequenceEqual(words) || !readback.Persisted)
            throw new IOException("SOC/OCV profile 保存后的设备读回不一致；请勿重复写入，先重新读取设备。");
        return readback;
    }

    public async Task<SocProfileParameters> ResetSocProfileAsync(string password, CancellationToken ct = default)
    {
        if (!BmsServiceAccess.PasswordValid(password)) throw new UnauthorizedAccessException("服务密码错误。");
        if (!(await ReadSocProfileAsync(ct)).SupportsPolicyV3)
            throw new NotSupportedException("当前固件尚不支持 SOC 策略 v3。未发送恢复命令。");
        await WriteRegistersAsync(SocProfileCommand, CommandWords(3, 0), ct);
        return await ReadSocProfileAsync(ct);
    }

    private static ushort[] CommandWords(ushort operation, ushort generation) =>
        new[] { operation, generation, (ushort)0x6873, (ushort)0x3435, (ushort)0x3600 };

    private static SocProfileParameters DecodeSocProfile(ushort[] w)
    {
        if (w.Length != SocProfileWords || w[3] is < 2 or > 15)
            throw new IOException("设备返回的 SOC/OCV profile 长度或 OCV 点数无效。");
        var p = new SocProfileParameters {
            Chemistry=w[0], ProfileId=w[1], ProfileVersion=w[2], CurrentDeadbandMa=w[4],
            RestPrepareSeconds=w[5], ErrorBandPercent=w[6], ValidMinMv=w[7], ValidMaxMv=w[8],
            FullSyncMv=w[9], FullMinMarginMv=w[10], EmptySyncMv=w[11], EmptyMaxMarginMv=w[12],
            MinimumErrorBandPercent=w[13], FullCellDeltaMaxMv=w[14], TerminalCellDeltaMaxMv=w[15],
            TerminalStartOffsetMv=w[16], TerminalL1OffsetMv=w[17], TerminalL2OffsetMv=w[18],
            TerminalL3OffsetMv=w[19]
        };
        for (int i=0;i<w[3];i++) p.OcvPoints.Add(new SocOcvPoint(w[20+i*2],w[21+i*2]));
        ValidateSocProfile(p);
        return p;
    }

    private static ushort[] EncodeSocProfile(SocProfileParameters p)
    {
        ValidateSocProfile(p);
        ushort[] w = new ushort[SocProfileWords];
        w[0]=p.Chemistry;w[1]=p.ProfileId;w[2]=p.ProfileVersion;w[3]=(ushort)p.OcvPoints.Count;
        w[4]=p.CurrentDeadbandMa;w[5]=p.RestPrepareSeconds;w[6]=p.ErrorBandPercent;
        w[7]=p.ValidMinMv;w[8]=p.ValidMaxMv;w[9]=p.FullSyncMv;w[10]=p.FullMinMarginMv;
        w[11]=p.EmptySyncMv;w[12]=p.EmptyMaxMarginMv;w[13]=p.MinimumErrorBandPercent;
        w[14]=p.FullCellDeltaMaxMv;w[15]=p.TerminalCellDeltaMaxMv;w[16]=p.TerminalStartOffsetMv;
        w[17]=p.TerminalL1OffsetMv;w[18]=p.TerminalL2OffsetMv;w[19]=p.TerminalL3OffsetMv;
        for(int i=0;i<p.OcvPoints.Count;i++){w[20+i*2]=p.OcvPoints[i].Millivolts;w[21+i*2]=p.OcvPoints[i].Percent;}
        return w;
    }

    private static void ValidateSocProfile(SocProfileParameters p)
    {
        if (p is not null && p.ProfileVersion == 3 && p.RestPrepareSeconds != 3600)
            throw new ArgumentException("SOC策略v3的静置资格固定为3600秒。");
        if (p is null || p.OcvPoints is null || p.Chemistry is < 1 or > 2 || p.ProfileId != p.Chemistry || p.ProfileVersion == 0 ||
            p.OcvPoints.Count is < 2 or > 15 || p.CurrentDeadbandMa > 2000 ||
            p.RestPrepareSeconds is < 60 or > 3600 || p.ErrorBandPercent is < 1 or > 20 ||
            p.MinimumErrorBandPercent is < 1 or > 20 || p.ValidMinMv < 1000 ||
            p.ValidMinMv >= p.ValidMaxMv || p.ValidMaxMv > 5000 ||
            p.EmptySyncMv < p.ValidMinMv || p.EmptySyncMv >= p.FullSyncMv || p.FullSyncMv > p.ValidMaxMv ||
            p.FullMinMarginMv > 1000 || p.EmptyMaxMarginMv > 1000 ||
            p.FullCellDeltaMaxMv > 2000 || p.TerminalCellDeltaMaxMv > 2000 ||
            p.TerminalStartOffsetMv < p.TerminalL1OffsetMv || p.TerminalL1OffsetMv < p.TerminalL2OffsetMv ||
            p.TerminalL2OffsetMv < p.TerminalL3OffsetMv)
            throw new ArgumentException("SOC/OCV 算法参数超出设备允许范围或前后关系无效。");
        for (int i=0;i<p.OcvPoints.Count;i++)
        {
            SocOcvPoint point=p.OcvPoints[i];
            if(point is null)throw new ArgumentException("OCV点不得为空。");
            if(point.Millivolts<p.ValidMinMv||point.Millivolts>p.ValidMaxMv||point.Percent>100||
               (i>0&&(point.Millivolts<=p.OcvPoints[i-1].Millivolts||point.Percent<p.OcvPoints[i-1].Percent)))
                throw new ArgumentException($"OCV 第 {i+1} 点无效；电压必须严格递增，SOC 不得下降。");
        }
        if(p.OcvPoints[0].Percent!=0||p.OcvPoints[^1].Percent!=100)
            throw new ArgumentException("OCV 曲线首点必须为 0%，末点必须为 100%。");
    }
}
