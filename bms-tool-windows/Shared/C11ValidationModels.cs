using System.Buffers.Binary;
using System.IO;
using System.Text.Json;

namespace BmsTool.Windows;

public interface IC11ValidationDevice
{
    Task<byte[]> C11ExchangeAsync(byte command, byte[] payload, CancellationToken ct);
}

public sealed record C11Capabilities(ushort Version, ushort Profile, byte Cells, byte Temperatures,
                                     bool InjectionEnabled, bool ControlledOutputEnabled,
                                     uint ProtectionMask, uint DiagnosticMask, uint SelfTestMask,
                                     uint BootId, ushort TaskMs, ushort LeaseMs,
                                     ushort InputLeaseMs)
{
    public static C11Capabilities Parse(byte[] p)
    {
        if (p.Length != 34 || !p.AsSpan(0, 4).SequenceEqual("C11V"u8))
            throw new IOException("C11 能力响应不匹配。");
        var c = new C11Capabilities(C11Wire.U16(p, 4), C11Wire.U16(p, 6), p[8], p[9], p[10] != 0,
                                    p[11] != 0, C11Wire.U32(p, 12), C11Wire.U32(p, 16),
                                    C11Wire.U32(p, 20), C11Wire.U32(p, 24), C11Wire.U16(p, 28),
                                    C11Wire.U16(p, 30), C11Wire.U16(p, 32));
        if (c.Version != 2 || c.Cells is < 1 or > 16 || c.Temperatures is < 1 or > 3 ||
            c.LeaseMs < 1000 || c.InputLeaseMs < 1000)
            throw new IOException("不支持的 C11 能力或通道范围。");
        return c;
    }
}

public sealed record C11Status(uint NowMs, uint BootId, uint RawSequence, uint AppliedSequence,
                               uint AppliedMs, ushort RealFaults, ushort TestFaults,
                               ushort RealDiagnostics, ushort TestDiagnostics, ushort RealInhibit,
                               ushort TestInhibit, ushort Requested, ushort Final, ushort Readback,
                               bool ReadbackValid, ushort Reverse, bool WriteFailed, ushort Mode,
                               bool Aborted, bool Hold, bool InjectionActive, uint RawValid,
                               uint TestValid, uint InjectionMask)
{
    public static C11Status Parse(byte[] p)
    {
        if (p.Length != 64)
            throw new IOException("C11 状态长度不匹配。");
        ushort W(int i) => C11Wire.U16(p, 20 + i * 2);
        return new(C11Wire.U32(p, 0), C11Wire.U32(p, 4), C11Wire.U32(p, 8), C11Wire.U32(p, 12),
                   C11Wire.U32(p, 16), W(0), W(1), W(2), W(3), W(4), W(5), W(6), W(7), W(8),
                   W(9) != 0, W(10), W(11) != 0, W(12), W(13) != 0, W(14) != 0, W(15) != 0,
                   C11Wire.U32(p, 52), C11Wire.U32(p, 56), C11Wire.U32(p, 60));
    }
}

public sealed record C11Sample(ushort Cells, ushort Temperatures, uint Sequence, uint SampledMs,
                               uint Valid, ushort[] CellMv, short[] TemperatureDecic, int CurrentMa,
                               uint CurrentSampledMs, ushort AfeFaults)
{
    public static C11Sample Parse(byte[] p)
    {
        if (p.Length != 64)
            throw new IOException("C11 采样长度不匹配。");
        ushort cells = C11Wire.U16(p, 0), temps = C11Wire.U16(p, 2);
        if (cells > 16 || temps > 3)
            throw new IOException("C11 采样通道越界。");
        return new(cells, temps, C11Wire.U32(p, 4), C11Wire.U32(p, 8), C11Wire.U32(p, 12),
                   Enumerable.Range(0, 16).Select(i => C11Wire.U16(p, 16 + i * 2)).ToArray(),
                   Enumerable.Range(0, 3)
                       .Select(i => unchecked((short)C11Wire.U16(p, 48 + i * 2)))
                       .ToArray(),
                   unchecked((int)C11Wire.U32(p, 54)), C11Wire.U32(p, 58), C11Wire.U16(p, 62));
    }
}

public sealed record C11Rule(int Trip, int Recover, uint TripMs, uint RecoverMs, byte Action,
                             bool Latched, ushort Channels, uint FaultSetMs, uint RecoveredMs)
{
    public static C11Rule Parse(byte[] p)
    {
        if (p.Length != 28)
            throw new IOException("C11 参数长度不匹配。");
        return new(unchecked((int)C11Wire.U32(p, 0)), unchecked((int)C11Wire.U32(p, 4)),
                   C11Wire.U32(p, 8), C11Wire.U32(p, 12), p[16], p[17] != 0, C11Wire.U16(p, 18),
                   C11Wire.U32(p, 20), C11Wire.U32(p, 24));
    }
}

public enum C11Outcome
{
    NotRun,
    Running,
    Pass,
    Fail,
    Skip,
    Blocked,
    Inconclusive,
    Aborted
}
public sealed record C11CaseResult(string Id, string Name, C11Outcome Outcome, string Evidence,
                                   string Detail, DateTimeOffset At);
public sealed class C11Report
{
    public string Schema { get; init; } = "C11-validation-v2";
    public string Device { get; init; } = "";
    public string Evidence { get; init; } = "MCU_INJECTION_SOFTWARE_ONLY";
    public string Requirements { get; init; } =
        "C11 V1.0.1; SW-VD-016-01..07 aliases SW-VD-016..022";
    public DateTimeOffset Started { get; init; } = DateTimeOffset.Now;
    public DateTimeOffset Finished { get; set; }
    public C11Capabilities? Capabilities { get; set; }
    public string FirmwareIntegrity { get; set; } = "未读取";
    public int SelfTestCyclesRequired { get; init; }
    public bool CleanupConfirmed { get; set; }
    public List<C11CaseResult> Results { get; } = new();
    public string Overall =>
        !CleanupConfirmed                                ? "INCONCLUSIVE"
        : Results.Any(x => x.Outcome == C11Outcome.Fail) ? "FAIL"
        : Results.Any(x => x.Outcome is C11Outcome.Inconclusive or C11Outcome.Aborted)
            ? "INCONCLUSIVE"
        : Results.Count == 0 || Results.Any(x => x.Outcome != C11Outcome.Pass) ? "INCOMPLETE"
                                                                               : "PASS_SOFTWARE";
    public string
    ToJson() => JsonSerializer.Serialize(this, new JsonSerializerOptions { WriteIndented = true });
}

public static class C11Wire
{
    public static ushort U16(byte[] p,
                             int at) => BinaryPrimitives.ReadUInt16BigEndian(p.AsSpan(at, 2));
    public static uint U32(byte[] p,
                           int at) => BinaryPrimitives.ReadUInt32BigEndian(p.AsSpan(at, 4));
    public static byte[] Token(ushort token, params byte[] tail)
    {
        byte[] p = new byte[2 + tail.Length];
        BinaryPrimitives.WriteUInt16BigEndian(p, token);
        tail.CopyTo(p, 2);
        return p;
    }
    public static byte[] Sequence(ushort token, uint seq)
    {
        byte[] p = Token(token, 0, 0, 0, 0);
        BinaryPrimitives.WriteUInt32BigEndian(p.AsSpan(2), seq);
        return p;
    }
    public static byte[] Field(ushort token, uint seq, byte kind, byte index, int value)
    {
        byte[] p = new byte[12];
        BinaryPrimitives.WriteUInt16BigEndian(p, token);
        BinaryPrimitives.WriteUInt32BigEndian(p.AsSpan(2), seq);
        p[6] = kind;
        p[7] = index;
        BinaryPrimitives.WriteInt32BigEndian(p.AsSpan(8), value);
        return p;
    }
}

public sealed class C11Connection : IAsyncDisposable
{
    private readonly IC11ValidationDevice _device;
    public ushort Token { get; private set; }
    public C11Capabilities Capabilities { get; private set; } = null!;
    private uint _sequence;
    private bool _openAttempted;
    public uint SelfTestAcceptanceMask { get; private set; }
    public C11Connection(IC11ValidationDevice device) => _device = device;
    public async Task OpenAsync(CancellationToken ct)
    {
        Capabilities =
            C11Capabilities.Parse(await _device.C11ExchangeAsync(0x10, Array.Empty<byte>(), ct));
        if (!Capabilities.InjectionEnabled)
            throw new IOException("当前为正式固件，未开放主动测试注入。");
        _openAttempted = true;
        var p = await _device.C11ExchangeAsync(1, new byte[] { 0x46, 0x41, 0x43, 0x54 }, ct);
        if (p.Length >= 2)
            Token = C11Wire.U16(p, 0); // Retain the lease for cleanup even if metadata is invalid.
        if (p.Length != 6 || p[4] != 2 || p[5] != Capabilities.Cells)
            throw new IOException("C11 会话版本不匹配。");
        if (Token == 0)
            throw new IOException("设备返回了无效的会话令牌。");
        var coverage = await Command(0x1c, Array.Empty<byte>(), ct);
        if (coverage.Length != 24)
            throw new IOException("固件覆盖声明长度不匹配。");
        SelfTestAcceptanceMask = C11Wire.U32(coverage, 12);
    }
    public Task<byte[]> Command(byte command, byte[] tail, CancellationToken ct) =>
        _device.C11ExchangeAsync(command, C11Wire.Token(Token, tail), ct);
    public async Task<C11Status> StatusAsync(CancellationToken ct)
    {
        C11Status s = C11Status.Parse(await Command(0x15, new byte[] { 0 }, ct));
        if (s.BootId != Capabilities.BootId || s.Aborted)
            throw new IOException("设备复位或会话已终止，禁止沿用旧输入。");
        return s;
    }
    public async Task<C11Sample> SampleAsync(bool test, CancellationToken ct) =>
        C11Sample.Parse(await Command(0x15, new byte[] { test ? (byte)2 : (byte)1 }, ct));
    public async Task<C11Rule> RuleAsync(int rule, CancellationToken ct) =>
        C11Rule.Parse(await Command(0x16, new byte[] { checked((byte)rule) }, ct));
    public Task<byte[]> HeartbeatAsync(CancellationToken ct) => Command(2, Array.Empty<byte>(), ct);
    public async Task<C11Status> KeepAliveAsync(CancellationToken ct)
    {
        await HeartbeatAsync(ct);
        var status = await StatusAsync(ct);
        if (status.InjectionActive)
        {
            var refreshed = await Command(0x1b, C11Wire.Sequence(0, status.AppliedSequence).AsSpan(2).ToArray(), ct);
            if (refreshed.Length != 4 || C11Wire.U32(refreshed, 0) != status.AppliedSequence)
                throw new IOException("注入续租应答序号不匹配，禁止沿用旧输入。");
        }
        return status;
    }
    public async Task<uint> ApplyAsync(IEnumerable<(byte Kind, byte Index, int Value)> fields,
                                       CancellationToken ct, uint pulseMs = 0)
    {
        var current = await KeepAliveAsync(ct);
        if (current.AppliedSequence != _sequence)
            throw new IOException("设备注入序号已改变，禁止继续当前事务。");
        uint next = checked(_sequence + 1);
        var begin = await _device.C11ExchangeAsync(0x11, C11Wire.Sequence(Token, next), ct);
        if (begin.Length != 4 || C11Wire.U32(begin, 0) != next)
            throw new IOException("注入 BEGIN 应答序号不匹配；本次事务已停止，请清除注入并确认固件版本。");
        foreach (var f in fields)
        {
            // A new multi-field snapshot must not let the previous applied
            // overlay expire while fields are staged over a slower link.
            await KeepAliveAsync(ct);
            var field = await _device.C11ExchangeAsync(
                0x12, C11Wire.Field(Token, next, f.Kind, f.Index, f.Value), ct);
            if (field.Length != 4 || C11Wire.U32(field, 0) != next)
                throw new IOException("注入字段应答序号不匹配；本次事务已停止，请清除注入并确认固件版本。");
        }
        byte command = pulseMs == 0 ? (byte)0x13 : (byte)0x1d;
        byte[] commit = C11Wire.Sequence(Token, next);
        if (pulseMs != 0)
        {
            Array.Resize(ref commit, 10);
            BinaryPrimitives.WriteUInt32BigEndian(commit.AsSpan(6), pulseMs);
        }
        try
        {
            byte[] p = await _device.C11ExchangeAsync(command, commit, ct);
            if (p.Length != 8 || C11Wire.U32(p, 0) != next)
                throw new IOException("应用序号不匹配。");
            _sequence = next;
            return C11Wire.U32(p, 4);
        }
        catch (TimeoutException)
        {
            // Query, never retransmit COMMIT or silently start a new session.
            using var verify = new CancellationTokenSource(TimeSpan.FromSeconds(3));
            C11Status status = await StatusAsync(verify.Token);
            if (status.AppliedSequence != next)
                throw new IOException("注入结果未知；停止后续步骤并确认清理。");
            _sequence = next;
            return status.AppliedMs;
        }
    }
    public async Task ClearAsync(CancellationToken ct)
    {
        await Command(4, Array.Empty<byte>(), ct);
        if ((await StatusAsync(ct)).InjectionActive)
            throw new IOException("设备仍有注入，不能报告清理完成。");
    }
    public async ValueTask DisposeAsync()
    {
        if (Token == 0)
        {
            if (_openAttempted)
                throw new IOException("OPEN 结果未知，未取得令牌，无法确认设备租约已清除。");
            return;
        }
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
        Exception? clearError = null;
        try
        {
            await ClearAsync(timeout.Token);
        }
        catch (Exception ex)
        {
            clearError = ex;
        }
        try
        {
            await Command(5, Array.Empty<byte>(), timeout.Token);
            Token = 0;
            _openAttempted = false;
        }
        catch (Exception ex)
        {
            throw new IOException("会话关闭/清理结果未知。", clearError ?? ex);
        }
    }
}
