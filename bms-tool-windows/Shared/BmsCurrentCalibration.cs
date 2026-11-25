using System.Diagnostics;
using System.Globalization;
using System.IO;

namespace BmsTool.Windows;

public sealed record CurrentCalibrationCoefficients(int OffsetMa, uint GainPpm, uint? ChargeGainPpm = null)
{
    public bool Directional => ChargeGainPpm.HasValue;
    public string Display => ChargeGainPpm is uint charge
        ? $"零偏 {OffsetMa} mA，充电 {charge} ppm，放电 {GainPpm} ppm"
        : $"零偏 {OffsetMa} mA，共用增益 {GainPpm} ppm";
    public ushort[] Words => ChargeGainPpm is uint charge
        ? [.. D008Parameters.Calibration(OffsetMa, GainPpm), (ushort)charge, (ushort)(charge >> 16)]
        : D008Parameters.Calibration(OffsetMa, GainPpm);
    private uint GainFor(double delta) => delta < 0 ? ChargeGainPpm ?? GainPpm : GainPpm;
    public double Apply(double rawMa)
    {
        double delta = rawMa - OffsetMa;
        return delta * GainFor(delta) / 1000000d;
    }
    public int ApplyMilliampere(int rawMa)
    {
        long delta = Math.Max(-int.MaxValue, Math.Min(int.MaxValue, (long)rawMa - OffsetMa));
        long magnitude = Math.Min(int.MaxValue, Math.Abs(delta) * GainFor(delta) / 1000000L);
        return (int)(delta < 0 ? -magnitude : magnitude);
    }
    public static CurrentCalibrationCoefficients Decode(ushort[] words)
    {
        if (words.Length is not (4 or 6)) throw new IOException("电流校准参数长度错误。");
        var value = new CurrentCalibrationCoefficients(D008Parameters.I32(words, 0), D008Parameters.U32(words, 2),
            words.Length == 6 ? D008Parameters.U32(words, 4) : null);
        value.Validate();
        return value;
    }
    public void Validate()
    {
        // 与现有固件存储范围一致；恢复旧参数不能误用向导的较窄候选范围。
        if (OffsetMa < -1000000 || OffsetMa > 1000000 || GainPpm is < 100000 or > 10000000 ||
            ChargeGainPpm is < 100000 or > 10000000)
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
        CurrentCalibrationSamples loaded, int referenceMa, CurrentCalibrationCoefficients? original = null)
    {
        Validate();
        // 双向标定只改当前方向的斜率。公共零偏由“仅保存零点”单独更新。
        int offset = original is { Directional: true } ? original.OffsetMa :
            checked((int)Math.Round(zero.MeanMa, MidpointRounding.AwayFromZero));
        double delta = loaded.MeanMa - offset;
        if (Math.Abs((double)referenceMa) < MinLoadedMa || Math.Abs(delta) < MinLoadedMa ||
            Math.Sign(delta) != Math.Sign(referenceMa))
            throw new ArgumentException("加载电流过小或方向不一致；请核对充放电方向和外部电流表。");
        double gain = Math.Round(referenceMa / delta * 1000000d, MidpointRounding.AwayFromZero);
        if (double.IsNaN(gain) || gain < MinCandidateGainPpm || gain > MaxCandidateGainPpm)
            throw new ArgumentException("计算增益异常，未生成候选；请检查参考电流、单位及采样回路。");
        uint gainPpm = checked((uint)gain);
        var result = original is { Directional: true }
            ? referenceMa < 0 ? original with { ChargeGainPpm = gainPpm } : original with { GainPpm = gainPpm }
            : new CurrentCalibrationCoefficients(offset, gainPpm);
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
        var result = original with { OffsetMa = checked((int)Math.Round(zero.MeanMa, MidpointRounding.AwayFromZero)) };
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
    public ushort NativeProduct { get; internal init; }
    public bool UsesNativeProtocol => NativeProduct != 0;
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
    public void InvalidateConnection() => ConnectionLost=true;
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
        $"原参数: {Original.Display}",
        "零点: " + Describe(Zero), "加载点: " + Describe(Loaded),
        ZeroOnly ? "外部参考: 确认实际电流为零，无加载点" : $"外部实测参考: {ReferenceMa} mA（正放电、负充电）",
        Candidate is null ? "候选: 无" : $"候选: {Candidate.Display}",
        $"写入已尝试: {WriteAttempted}; 保存核对完成: {SaveVerified}; 连接/时钟失效: {ConnectionLost}",
        "结果: " + Outcome
    });
    private static string Describe(CurrentCalibrationSamples? value) => value is null ? "未采集" :
        FormattableString.Invariant($"mean={value.MeanMa:F3} mA; min={value.MinMa}; max={value.MaxMa}; drift={value.DriftMa:F3}; tick={value.LastTick}; UTC={value.CapturedUtc:O}");
}

public sealed partial class BmsClient
{
    public const ushort LinearCurrentCalibrationCapability = 0x0400;
    private async Task<(ushort Product, bool Directional)> RequireLinearCurrentCalibrationAsync(CancellationToken ct)
    {
        var header = await ReadRegistersAsync(0x2E00, 3, ct);
        if (header is [0xC077,2,1]) return (header[0], true);
        if (header is [0xC060 or 0xC077,1,1]) return (header[0], false);
        if (header.Length != 3 || header[0] != D008Parameters.Magic ||
            !D008Parameters.SupportsProtocol(header[1]) || (header[2] & LinearCurrentCalibrationCapability) == 0)
            throw new NotSupportedException("当前固件未声明支持持久电流校准；请使用配套固件。未发送参数写入。");
        return (0, false);
    }

    private Task<ushort[]> ReadCurrentCalibrationWordsAsync(bool directional, CancellationToken ct) =>
        ReadRegistersAsync(directional ? (ushort)0x2E34 : (ushort)0x2E24, directional ? (ushort)6 : (ushort)4, ct);

    public async Task<CurrentCalibrationCoefficients> ReadCurrentCalibrationAsync(CancellationToken ct)
    {
        var capability = await RequireLinearCurrentCalibrationAsync(ct);
        return CurrentCalibrationCoefficients.Decode(await ReadCurrentCalibrationWordsAsync(capability.Directional, ct));
    }

    public async Task<CurrentCalibrationCoefficients> ReadCurrentCalibrationDefaultsAsync(CancellationToken ct)
    {
        var capability = await RequireLinearCurrentCalibrationAsync(ct);
        if (!capability.Directional) throw new NotSupportedException("当前固件未提供校准默认值。");
        return CurrentCalibrationCoefficients.Decode(await ReadRegistersAsync(0x2E3C, 6, ct));
    }

    public async Task<CurrentCalibrationSession> BeginCurrentCalibrationAsync(CancellationToken ct)
    {
        var (nativeProduct, directional) = await RequireLinearCurrentCalibrationAsync(ct);
        bool native=nativeProduct!=0;
        var identity = await ReadIdentityAsync(ct);
        if(native && (nativeProduct==0xC077 ? !IsC077 : !IsC060 || IsC077))throw new IOException("校准能力与当前设备身份不一致。");
        var original = CurrentCalibrationCoefficients.Decode(await ReadCurrentCalibrationWordsAsync(directional, ct));
        AfeHardwareAccessSession access;
        if(native) {
            await WriteSingleRegisterAsync(0x2E30,0xCA01,ct);
            var lease=await ReadRegistersAsync(0x2E30,4,ct);
            if(lease.Length!=4 || lease[0]==0 || lease[1]!=30 || lease[2]!=1 || lease[3]!=(nativeProduct==0xC077?0x0309:0x7693))
                throw new IOException("校准授权无效；请等待原授权超时后重新开始。");
            access=new(lease[0],lease[1],(byte)lease[2],lease[3]);
        } else access=await OpenAfeHardwareAccessAsync(ct);
        var session = new CurrentCalibrationSession(this, identity, original, access){NativeProduct=nativeProduct};
        try {
            if(native) {
                var ready=Stopwatch.StartNew();
                while(true) {
                    var sample=await ReadRegistersAsync(0x2E28,7,ct);
                    if(sample[4]==1){session.ObserveSample(sample);break;}
                    if(ready.Elapsed>TimeSpan.FromSeconds(2))throw new IOException("尚未取得有效的新电流转换，请检查 AFE 状态。");
                    await Task.Delay(100,ct);
                }
            } else await VerifyCurrentCalibrationContinuityAsync(session, ct);
            return session;
        }
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
        try
        {
            if(session.UsesNativeProtocol){await WriteSingleRegisterAsync(0x2E31,session.Access.Token,ct);return;}
            try { await AfeHardwareHeartbeatAsync(session.Access.Token, ct); }
            catch (TimeoutException) when (IsD015 && _transport.IsConnected && !ct.IsCancellationRequested)
            {
                // D015的485可能已关电；首帧仅唤醒MCU。只续租原token，不重新OPEN或重发系数写入。
                Log?.Invoke("[CALIBRATION] 心跳首帧未响应，等待485稳定后重试原会话一次。");
                await Task.Delay(150, ct);
                RequireCurrentCalibrationConnection(session);
                await AfeHardwareHeartbeatAsync(session.Access.Token, ct);
            }
        }
        catch (IOException) { session.ConnectionLost = true; throw; }
        catch (TimeoutException ex) { throw new TimeoutException("校准心跳未收到回应，请检查通信连接后重新开始。", ex); }
        catch (ObjectDisposedException) { session.ConnectionLost = true; throw; }
    }
    public async Task VerifyCurrentCalibrationContinuityAsync(CurrentCalibrationSession session, CancellationToken ct)
    {
        // 已正常关闭授权的保存记录仍可校验时钟，但不能再用于原 token 的写入。
        await ReadCurrentCalibrationSampleAsync(session, ct);
    }
    private async Task<ushort[]> ReadCurrentCalibrationSampleAsync(CurrentCalibrationSession session, CancellationToken ct, bool allowInvalid = false)
    {
        RequireCurrentCalibrationConnection(session);
        var words = await ReadRegistersAsync(0x2E28, 7, ct);
        RequireCurrentCalibrationConnection(session);
        if (!allowInvalid || words[4] == 1) session.ObserveSample(words);
        return words;
    }
    public async Task CheckCurrentCalibrationAsync(CurrentCalibrationSession session, CancellationToken ct)
    {
        if (session.Closed || !ReferenceEquals(session.Client, this)) throw new IOException("校准会话已失效。");
        // 沿用 RAM 授权 token：MCU 重启、其他会话接管或超时后，不重新 OPEN 冒充原会话。
        await HeartbeatCurrentCalibrationAsync(session, ct);
        if (await RequireLinearCurrentCalibrationAsync(ct) != (session.NativeProduct, session.Original.Directional))
            throw new IOException("校准协议已变化，旧会话失效。");
        var current = CurrentCalibrationCoefficients.Decode(await ReadCurrentCalibrationWordsAsync(session.Original.Directional, ct));
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
        if (candidate.Directional != session.Original.Directional)
            throw new ArgumentException("校准系数与当前设备协议不匹配。");
        if (session.ZeroOnly && (candidate.GainPpm != session.Original.GainPpm ||
            candidate.ChargeGainPpm != session.Original.ChargeGainPpm))
            throw new ArgumentException("仅校准零点必须保留设备原增益，未发送写命令。");
        await CheckCurrentCalibrationAsync(session, ct);
        await ReadCurrentCalibrationSampleAsync(session, ct);
        ct.ThrowIfCancellationRequested();
        session.WriteAttempted = true;
        await WriteRegistersAsync(candidate.Directional ? (ushort)0x2E34 : (ushort)0x2E24,
            session.UsesNativeProtocol ? [session.Access.Token, .. candidate.Words] : candidate.Words, ct);
        var actual = CurrentCalibrationCoefficients.Decode(await ReadCurrentCalibrationWordsAsync(candidate.Directional, ct));
        if (actual != candidate) throw new IOException("校准写入后读回不一致，请重新读取；不要盲目重复写入。");
        await HeartbeatCurrentCalibrationAsync(session, ct);
        // 以写入/读回之后的缓存为界；写命令前取得的 tick 不能排除途中产生的旧系数帧。
        var fence = await ReadCurrentCalibrationSampleAsync(session, ct, allowInvalid: true);
        uint fenceTick = D008Parameters.U32(fence, 5);
        var timer = Stopwatch.StartNew();
        while (timer.Elapsed < TimeSpan.FromSeconds(3))
        {
            var sample = await ReadCurrentCalibrationSampleAsync(session, ct, allowInvalid: true);
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
    public async Task EndCurrentCalibrationAsync(CurrentCalibrationSession session, bool verifyConnection = false)
    {
        if (session.Closed) return;
        session.Closed = true;
        if (!_transport.IsConnected) session.ConnectionLost = true;
        if (session.ConnectionLost) return; // 不向断线/重启后的设备发送旧 token 的 CLOSE。
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(3));
        try
        {
            if (verifyConnection)
            {
                // 显示过期本身不能证明MCU已重启。先读采样时钟核对连续性，绝不以新OPEN接管旧授权。
                ushort[] sample;
                try { sample = await ReadRegistersAsync(0x2E28, 7, timeout.Token, TimeSpan.FromMilliseconds(600)); }
                catch (TimeoutException) when (IsD015 && _transport.IsConnected && !timeout.IsCancellationRequested)
                {
                    await Task.Delay(150, timeout.Token);
                    sample = await ReadRegistersAsync(0x2E28, 7, timeout.Token, TimeSpan.FromMilliseconds(600));
                }
                session.ObserveSample(sample);
                // CLOSE本身由固件核对token及所属串口；旧授权已过期/被替换时不会关闭别人的会话。
            }
            if(session.UsesNativeProtocol)await WriteSingleRegisterAsync(0x2E32,session.Access.Token,timeout.Token);
            else await TryCloseAfeHardwareAccessAsync(session.Access.Token, timeout.Token);
        }
        catch (Exception ex) when (ex is IOException or TimeoutException or OperationCanceledException)
        {
            session.ConnectionLost = true;
            Log?.Invoke($"[CALIBRATION] 无法确认原设备连续性，停止清理，授权等待设备超时：{ex.Message}");
        }
        catch (ObjectDisposedException) { /* 连接已释放，RAM 会话由设备超时清除。 */ }
        catch (InvalidOperationException) { /* 已断开的传输无法关闭会话，由设备超时清除。 */ }
    }
}
