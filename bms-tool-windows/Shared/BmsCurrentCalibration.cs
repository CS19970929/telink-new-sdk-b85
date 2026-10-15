using System.Diagnostics;
using System.Globalization;
using System.IO;

namespace BmsTool.Windows;

public sealed record CurrentCalibrationCoefficients(int OffsetMa, uint GainPpm)
{
    public ushort[] Words => D008Parameters.Calibration(OffsetMa, GainPpm);
    public double Apply(double rawMa) => (rawMa - OffsetMa) * GainPpm / 1000000d;
    public int ApplyMilliampere(int rawMa)
    {
        long delta = Math.Max(-int.MaxValue, Math.Min(int.MaxValue, (long)rawMa - OffsetMa));
        long magnitude = Math.Min(int.MaxValue, Math.Abs(delta) * GainPpm / 1000000L);
        return (int)(delta < 0 ? -magnitude : magnitude);
    }
    public static CurrentCalibrationCoefficients Decode(ushort[] words)
    {
        if (words.Length != 4) throw new IOException("电流校准参数长度错误。");
        var value = new CurrentCalibrationCoefficients(D008Parameters.I32(words, 0), D008Parameters.U32(words, 2));
        value.Validate();
        return value;
    }
    public void Validate()
    {
        // 与现有固件存储范围一致；恢复旧参数不能误用向导的较窄候选范围。
        if (OffsetMa < -1000000 || OffsetMa > 1000000 || GainPpm < 100000 || GainPpm > 10000000)
            throw new ArgumentException("电流校准参数超出固件范围。");
    }
}

public sealed record CurrentCalibrationSamples(double MeanMa, int MinMa, int MaxMa,
    double DriftMa, uint LastTick, DateTimeOffset CapturedUtc);

// 这些是采集质量门限，不是产品安全电流或精度承诺。后续产品可传入自己的策略。
public sealed record CurrentCalibrationPolicy(int SampleCount = 20, int TimeoutSeconds = 20,
    int ZeroMaxAbsMa = 1500, int MinLoadedMa = 1000, int SpreadFloorMa = 200,
    double LoadedSpreadFraction = 0.01, uint MinCandidateGainPpm = 500000,
    uint MaxCandidateGainPpm = 2000000)
{
    public static CurrentCalibrationPolicy Default { get; } = new();
    public void Validate()
    {
        if (SampleCount < 4 || SampleCount > 100 || TimeoutSeconds < 1 || TimeoutSeconds > 120 ||
            ZeroMaxAbsMa < 0 || ZeroMaxAbsMa > 1000000 || MinLoadedMa < 1 ||
            SpreadFloorMa < 1 || double.IsNaN(LoadedSpreadFraction) ||
            LoadedSpreadFraction <= 0 || LoadedSpreadFraction > 0.1 ||
            MinCandidateGainPpm < 100000 || MaxCandidateGainPpm > 10000000 ||
            MinCandidateGainPpm > MaxCandidateGainPpm)
            throw new ArgumentException("电流校准采集策略无效。");
    }
    public CurrentCalibrationCoefficients Calculate(CurrentCalibrationSamples zero,
        CurrentCalibrationSamples loaded, int referenceMa)
    {
        Validate();
        int offset = checked((int)Math.Round(zero.MeanMa, MidpointRounding.AwayFromZero));
        double delta = loaded.MeanMa - offset;
        if (Math.Abs((double)referenceMa) < MinLoadedMa || Math.Abs(delta) < MinLoadedMa ||
            Math.Sign(delta) != Math.Sign(referenceMa))
            throw new ArgumentException("加载电流过小或方向不一致；请核对充放电方向和外部电流表。");
        double gain = Math.Round(referenceMa / delta * 1000000d, MidpointRounding.AwayFromZero);
        if (double.IsNaN(gain) || gain < MinCandidateGainPpm || gain > MaxCandidateGainPpm)
            throw new ArgumentException("计算增益异常，未生成候选；请检查参考电流、单位及采样回路。");
        var result = new CurrentCalibrationCoefficients(offset, checked((uint)gain));
        result.Validate();
        return result;
    }
    public CurrentCalibrationCoefficients CalculateZeroOnly(CurrentCalibrationSamples zero,
        CurrentCalibrationCoefficients original)
    {
        Validate();
        original.Validate();
        if (double.IsNaN(zero.MeanMa) || double.IsInfinity(zero.MeanMa) || Math.Abs(zero.MeanMa) > ZeroMaxAbsMa)
            throw new ArgumentException("零点残差超出采集范围，请确认实际电流为零。");
        // 零点校准不能重新计算增益，也不能将合法旧增益限制到两点候选范围。
        var result = new CurrentCalibrationCoefficients(
            checked((int)Math.Round(zero.MeanMa, MidpointRounding.AwayFromZero)), original.GainPpm);
        result.Validate();
        return result;
    }
}

public sealed class CurrentCalibrationSession
{
    internal CurrentCalibrationSession(BmsClient client, DeviceIdentity identity,
        CurrentCalibrationCoefficients original, AfeHardwareAccessSession access)
    { Client = client; Identity = identity; Original = original; Access = access; }
    public BmsClient Client { get; }
    public DeviceIdentity Identity { get; }
    public CurrentCalibrationCoefficients Original { get; }
    public AfeHardwareAccessSession Access { get; }
    public DateTimeOffset StartedUtc { get; } = DateTimeOffset.UtcNow;
    public CurrentCalibrationSamples? Zero { get; set; }
    public CurrentCalibrationSamples? Loaded { get; set; }
    public CurrentCalibrationCoefficients? Candidate { get; set; }
    public int ReferenceMa { get; set; }
    public bool ZeroOnly { get; set; }
    public bool WriteAttempted { get; internal set; }
    public bool SaveVerified { get; internal set; }
    public bool ConnectionLost { get; internal set; }
    public bool Closed { get; internal set; }
    private uint? _lastSampleTick;
    private long _lastSampleObservedAt;
    internal uint ObserveSample(ushort[] words)
    {
        if (words.Length != 7 || words[4] != 1) throw new IOException("电流采样失效，请重新读取设备状态。");
        uint tick = D008Parameters.U32(words, 5);
        long now = Stopwatch.GetTimestamp();
        if (_lastSampleTick is uint previous)
        {
            double elapsedTicks = (now - _lastSampleObservedAt) * (32000d / Stopwatch.Frequency);
            // 缓存允许两秒差异，并留 5% 时钟裕量；使用单调时钟，不受 PC 校时影响。
            double tolerance = 64000d + elapsedTicks * 0.05;
            uint delta = unchecked(tick - previous);
            if (delta > elapsedTicks + tolerance || delta + tolerance < elapsedTicks)
            {
                ConnectionLost = true;
                throw new IOException("设备采样时钟不连续，可能已重新上电；旧候选和恢复备份已失效，请重新采集。");
            }
        }
        if (_lastSampleTick != tick) { _lastSampleTick = tick; _lastSampleObservedAt = now; }
        return tick;
    }
    public string Outcome { get; set; } = "尚未保存";
    public string Report => string.Join(Environment.NewLine, new[] {
        "BMS 电流校准记录（保存读回不等于精度验收）",
        "方式: " + (ZeroOnly ? "仅校准零点，保留原增益" : "两点校准"),
        "UTC: " + StartedUtc.ToString("O", CultureInfo.InvariantCulture),
        "上位机: " + BuildIdentity.Display,
        $"设备: SN={Identity.Serial}; MAC={Identity.Mac}; HW={Identity.Hardware}; SW={Identity.Software}",
        $"原参数: offset_ma={Original.OffsetMa}; gain_ppm={Original.GainPpm}",
        "零点: " + Describe(Zero), "加载点: " + Describe(Loaded),
        ZeroOnly ? "外部参考: 确认实际电流为零，无加载点" : $"外部实测参考: {ReferenceMa} mA（正放电、负充电）",
        Candidate is null ? "候选: 无" : $"候选: offset_ma={Candidate.OffsetMa}; gain_ppm={Candidate.GainPpm}",
        $"写入已尝试: {WriteAttempted}; 保存核对完成: {SaveVerified}; 连接/时钟失效: {ConnectionLost}",
        "结果: " + Outcome
    });
    private static string Describe(CurrentCalibrationSamples? value) => value is null ? "未采集" :
        FormattableString.Invariant($"mean={value.MeanMa:F3} mA; min={value.MinMa}; max={value.MaxMa}; drift={value.DriftMa:F3}; tick={value.LastTick}; UTC={value.CapturedUtc:O}");
}

public sealed partial class BmsClient
{
    public const ushort LinearCurrentCalibrationCapability = 0x0400;
    private async Task RequireLinearCurrentCalibrationAsync(CancellationToken ct)
    {
        var header = await ReadRegistersAsync(0x2E00, 3, ct);
        if (header.Length != 3 || header[0] != D008Parameters.Magic ||
            !D008Parameters.SupportsProtocol(header[1]) || (header[2] & LinearCurrentCalibrationCapability) == 0)
            throw new NotSupportedException("当前固件未声明支持持久电流校准；请使用配套固件。未发送参数写入。");
    }
    public async Task<CurrentCalibrationSession> BeginCurrentCalibrationAsync(CancellationToken ct)
    {
        await RequireLinearCurrentCalibrationAsync(ct);
        var identity = await ReadIdentityAsync(ct);
        var original = CurrentCalibrationCoefficients.Decode(await ReadRegistersAsync(0x2E24, 4, ct));
        var access = await OpenAfeHardwareAccessAsync(ct);
        var session = new CurrentCalibrationSession(this, identity, original, access);
        try { await VerifyCurrentCalibrationContinuityAsync(session, ct); return session; }
        catch { await EndCurrentCalibrationAsync(session); throw; }
    }
    private void RequireCurrentCalibrationConnection(CurrentCalibrationSession session)
    {
        if (!ReferenceEquals(session.Client, this)) throw new IOException("校准会话不属于当前连接。");
        if (!_transport.IsConnected) session.ConnectionLost = true;
        if (session.ConnectionLost) throw new IOException("校准连接已失效，禁止继续使用旧候选或恢复备份。");
    }
    private async Task HeartbeatCurrentCalibrationAsync(CurrentCalibrationSession session, CancellationToken ct)
    {
        RequireCurrentCalibrationConnection(session);
        try { await AfeHardwareHeartbeatAsync(session.Access.Token, ct); }
        catch (IOException) { session.ConnectionLost = true; throw; }
        catch (TimeoutException) { session.ConnectionLost = true; throw; }
        catch (ObjectDisposedException) { session.ConnectionLost = true; throw; }
    }
    public async Task VerifyCurrentCalibrationContinuityAsync(CurrentCalibrationSession session, CancellationToken ct)
    {
        // 已正常关闭授权的保存记录仍可校验时钟，但不能再用于原 token 的写入。
        await ReadCurrentCalibrationSampleAsync(session, ct);
    }
    private async Task<ushort[]> ReadCurrentCalibrationSampleAsync(CurrentCalibrationSession session, CancellationToken ct)
    {
        RequireCurrentCalibrationConnection(session);
        var words = await ReadRegistersAsync(0x2E28, 7, ct);
        RequireCurrentCalibrationConnection(session);
        session.ObserveSample(words);
        return words;
    }
    public async Task CheckCurrentCalibrationAsync(CurrentCalibrationSession session, CancellationToken ct)
    {
        if (session.Closed || !ReferenceEquals(session.Client, this)) throw new IOException("校准会话已失效。");
        // 沿用 RAM 授权 token：MCU 重启、其他会话接管或超时后，不重新 OPEN 冒充原会话。
        await HeartbeatCurrentCalibrationAsync(session, ct);
        await RequireLinearCurrentCalibrationAsync(ct);
        var current = CurrentCalibrationCoefficients.Decode(await ReadRegistersAsync(0x2E24, 4, ct));
        if (current != session.Original) throw new IOException("采集期间设备校准参数已变化，请重新采集。");
        await VerifyCurrentCalibrationContinuityAsync(session, ct);
    }
    public async Task<CurrentCalibrationSamples> CollectCurrentCalibrationAsync(CurrentCalibrationSession session,
        bool zero, CurrentCalibrationPolicy policy, Action<int, int>? progress, CancellationToken ct)
    {
        policy.Validate();
        await CheckCurrentCalibrationAsync(session, ct);
        var values = new List<int>();
        uint? last = null;
        var timer = Stopwatch.StartNew();
        while (values.Count < policy.SampleCount && timer.Elapsed < TimeSpan.FromSeconds(policy.TimeoutSeconds))
        {
            ct.ThrowIfCancellationRequested();
            var words = await ReadCurrentCalibrationSampleAsync(session, ct);
            uint tick = D008Parameters.U32(words, 5);
            if (last is uint previous)
            {
                uint delta = unchecked(tick - previous);
                if (delta > 64000u) throw new IOException("电流采样时钟中断或设备重启，请重新开始。");
                if (delta != 0) { values.Add(D008Parameters.I32(words, 0)); progress?.Invoke(values.Count, policy.SampleCount); }
            }
            last = tick; // 首帧只建立基准，缓存不能算作本次新转换。
            if (values.Count < policy.SampleCount) await Task.Delay(100, ct);
        }
        if (values.Count != policy.SampleCount) throw new IOException("未在限定时间内取得足够的新鲜电流样本，请重新开始。");
        await CheckCurrentCalibrationAsync(session, ct);
        double mean = values.Average();
        int half = values.Count / 2;
        double drift = values.Skip(half).Average() - values.Take(half).Average();
        double limit = zero ? policy.SpreadFloorMa : Math.Max(policy.SpreadFloorMa, Math.Abs(mean) * policy.LoadedSpreadFraction);
        if ((double)values.Max() - values.Min() > limit || Math.Abs(drift) > limit / 2)
            throw new IOException("电流波动或持续漂移，未生成校准候选；请稳定电流后重新采集。");
        if (zero && values.Any(value => Math.Abs((double)value) > policy.ZeroMaxAbsMa))
            throw new IOException("零点残差超出采集范围；请确认外部电流为零并检查采样回路。");
        return new CurrentCalibrationSamples(mean, values.Min(), values.Max(), drift, last!.Value, DateTimeOffset.UtcNow);
    }
    public async Task<int> SaveCurrentCalibrationAsync(CurrentCalibrationSession session,
        CurrentCalibrationCoefficients candidate, CancellationToken ct)
    {
        candidate.Validate();
        if (session.ZeroOnly && candidate.GainPpm != session.Original.GainPpm)
            throw new ArgumentException("仅校准零点必须保留设备原增益，未发送写命令。");
        await CheckCurrentCalibrationAsync(session, ct);
        await ReadCurrentCalibrationSampleAsync(session, ct);
        ct.ThrowIfCancellationRequested();
        session.WriteAttempted = true;
        await WriteRegistersAsync(0x2E24, candidate.Words, ct);
        var actual = CurrentCalibrationCoefficients.Decode(await ReadRegistersAsync(0x2E24, 4, ct));
        if (actual != candidate) throw new IOException("校准写入后读回不一致，请重新读取；不要盲目重复写入。");
        await HeartbeatCurrentCalibrationAsync(session, ct);
        // 以写入/读回之后的缓存为界；写命令前取得的 tick 不能排除途中产生的旧系数帧。
        var fence = await ReadCurrentCalibrationSampleAsync(session, ct);
        uint fenceTick = D008Parameters.U32(fence, 5);
        var timer = Stopwatch.StartNew();
        while (timer.Elapsed < TimeSpan.FromSeconds(3))
        {
            var sample = await ReadCurrentCalibrationSampleAsync(session, ct);
            if (sample[4] == 1 && D008Parameters.U32(sample, 5) != fenceTick)
            {
                int finalMa = D008Parameters.I32(sample, 2);
                if (finalMa != candidate.ApplyMilliampere(D008Parameters.I32(sample, 0)))
                    throw new IOException("新样本与保存的校准公式不一致，请检查固件；禁止盲目重写。");
                await HeartbeatCurrentCalibrationAsync(session, ct);
                session.SaveVerified = true;
                return finalMa;
            }
            await Task.Delay(100, ct);
        }
        throw new IOException("参数已读回，但没有取得校准后的新鲜样本；请重新读取确认。");
    }
    public async Task EndCurrentCalibrationAsync(CurrentCalibrationSession session)
    {
        if (session.Closed) return;
        session.Closed = true;
        if (!_transport.IsConnected) session.ConnectionLost = true;
        if (session.ConnectionLost) return; // 不向断线/重启后的设备发送旧 token 的 CLOSE。
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(2));
        try { await TryCloseAfeHardwareAccessAsync(session.Access.Token, timeout.Token); }
        catch (ObjectDisposedException) { /* 连接已释放，RAM 会话由设备超时清除。 */ }
        catch (InvalidOperationException) { /* 已断开的传输无法关闭会话，由设备超时清除。 */ }
    }
}
