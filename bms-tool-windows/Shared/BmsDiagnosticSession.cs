using System.IO;
using System.IO.Compression;
using System.Text;
using System.Text.Json;

namespace BmsTool.Windows;

public sealed record DiagnosticSessionSample(DiagnosticCapture Capture, BmsHealthReport Health,
    DeviceIdentity Identity, bool Complete, int Segment, string? Boundary);

public sealed class DiagnosticIdentityMismatchException : IOException
{
    public DiagnosticIdentityMismatchException() : base("设备身份改变；已停止会话，禁止将另一设备的数据并入记录。") { }
}

public sealed class DiagnosticSessionReadException : IOException
{
    public DiagnosticSessionReadException(Exception inner) : base(inner.Message, inner) { }
}

/// <summary>
/// Read-only observation recording shared by CLI and both WPF editions.
/// The caller serializes calls and owns connection/reconnect/cancellation. No writes to BMS.
/// Legacy firmware has no boot ID: boundaries are conservative observations, not proven resets.
/// </summary>
public sealed class BmsDiagnosticSession : IDisposable
{
    private static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web);
    private readonly StreamWriter _journal;
    private DeviceIdentity? _identity;
    private uint? _lastTick, _lastTrace, _lastBuild;
    private DateTimeOffset? _lastUtc;
    private string? _lastCondition;
    private bool _gap, _fullNext = true, _finished;
    private long _sequence;
    public string Id { get; } = Guid.NewGuid().ToString("N");
    public string DirectoryPath { get; }
    public string Endpoint { get; }
    public int Segment { get; private set; } = 1;
    public int Samples { get; private set; }
    public int CompleteSamples { get; private set; }
    public int Gaps { get; private set; }
    public bool HasGaps => Gaps != 0;

    public BmsDiagnosticSession(string outputDirectory, string endpoint)
    {
        Endpoint = endpoint;
        DirectoryPath = Path.Combine(Path.GetFullPath(outputDirectory), "session-" + Id);
        Directory.CreateDirectory(DirectoryPath);
        _journal = new StreamWriter(new FileStream(Path.Combine(DirectoryPath, "observations.jsonl"),
            FileMode.CreateNew, FileAccess.Write, FileShare.Read), new UTF8Encoding(false)) { AutoFlush = true };
        try
        {
            Write("start", new { endpoint, recordKind = "observation", replayReady = false,
                atomicSnapshot = false, bootIdentityAvailable = false,
                identityAssurance = "pinned endpoint and reported SN/HW; serial uniqueness not proven",
                rawFramesScope = "diagnostic reads only; identity probes are not raw-frame captured" });
        }
        catch { _journal.Dispose(); throw; }
    }

    private void Write(string kind, object data)
    {
        if (_finished) throw new ObjectDisposedException(nameof(BmsDiagnosticSession));
        _journal.WriteLine(JsonSerializer.Serialize(new { schema = 1, sessionId = Id,
            sequence = ++_sequence, segment = Segment, capturedUtc = DateTimeOffset.UtcNow, kind, data }, Json));
    }

    public void NoteGap(string reason)
    {
        if (_gap) return;
        Gaps++;
        _gap = true;
        _fullNext = true;
        Write("gap", new { reason, missingSamples = (int?)null });
    }

    private static bool Known(string? text) => !string.IsNullOrWhiteSpace(text) &&
        !string.Equals(text, "未知", StringComparison.OrdinalIgnoreCase) &&
        !string.Equals(text, "unknown", StringComparison.OrdinalIgnoreCase);

    public async Task<DiagnosticSessionSample> CaptureAsync(BmsClient client,
        CancellationToken ct = default, bool fullEvidence = false)
    {
        if (_finished) throw new ObjectDisposedException(nameof(BmsDiagnosticSession));
        // Recheck identity even after reconnecting the same COM port/MAC.
        DeviceIdentity identity;
        try { identity = await client.ReadIdentityAsync("", "", ct); }
        catch (OperationCanceledException) { throw; }
        catch (Exception ex) { throw new DiagnosticSessionReadException(ex); }
        if (!Known(identity.Serial) || !Known(identity.Hardware))
            throw new DiagnosticSessionReadException(new IOException("SN/HW 不完整，无法绑定诊断会话目标。"));
        if (_identity is not null && (identity.Serial != _identity.Serial || identity.Hardware != _identity.Hardware))
        {
            Write("identity_mismatch", new { expected = _identity, observed = identity });
            throw new DiagnosticIdentityMismatchException();
        }
        _identity ??= identity;
        bool full = fullEvidence || _fullNext || Samples % 12 == 0;
        DiagnosticCapture capture = await client.ReadDiagnosticsAsync(full, Endpoint, ct);
        if ((capture.Identity.TryGetValue("Serial", out string? serial) && serial != identity.Serial) ||
            (capture.Identity.TryGetValue("Hardware", out string? hardware) && hardware != identity.Hardware))
        {
            Write("identity_mismatch", new { expected = identity, observed = capture.Identity, capture });
            throw new DiagnosticIdentityMismatchException();
        }
        return Record(capture, identity, full);
    }

    private DiagnosticSessionSample Record(DiagnosticCapture capture, DeviceIdentity identity, bool full)
    {
        ushort[]? w = capture.Words;
        bool available = capture.Supported && w is { Length: 256 };
        bool complete = available && capture.SnapshotConsistent && capture.Errors.Count == 0 &&
            (!full || (w![2] & BmsDiagnostics.TraceCapability) == 0 || capture.TraceConsistent);
        string? boundary = null;
        if (available)
        {
            uint tick = BmsDiagnostics.U32(w!, 6), trace = BmsDiagnostics.U32(w!, 8), build = BmsDiagnostics.U32(w!, 22);
            DateTimeOffset now = DateTimeOffset.UtcNow;
            if (_gap) boundary = "after_gap_boot_continuity_unknown";
            else if (_lastBuild is uint oldBuild && oldBuild != build) boundary = "firmware_identity_changed";
            else if (_lastUtc is DateTimeOffset last && (now - last).TotalSeconds >= 67108)
                boundary = "clock_continuity_unknown"; // half the 32-bit 32k clock range
            else if ((_lastTick is uint oldTick && unchecked(tick - oldTick) > int.MaxValue) ||
                     (_lastTrace is uint oldTrace && unchecked(trace - oldTrace) > int.MaxValue))
                boundary = "possible_reset_or_clock_discontinuity";
            if (boundary is not null)
            {
                Segment++;
                Write("segment", new { reason = boundary, resetConfirmed = false });
            }
            _lastTick = tick; _lastTrace = trace; _lastBuild = build; _lastUtc = now;
            _gap = false;
        }
        var health = BmsHealth.Evaluate(capture, identity);
        Samples++;
        if (complete) CompleteSamples++;
        string condition = available
            ? $"{health.Overall}:{w![193] & 1}:{w[222]}:{w[223]}:{w[224]}:{w[136]}:{w[137]}:{w[138]}:{w[139]}:{w[132]}:{w[133]}"
            : "unavailable";
        bool changed = _lastCondition is not null && condition != _lastCondition;
        Write("sample", new { number = Samples, identity,
            quality = new { complete, diagnosticAvailable = available, fullEvidenceRequested = full,
                atomicSnapshot = false, bootCheck = capture.SnapshotConsistent,
                traceCheck = full && available && (w![2] & BmsDiagnostics.TraceCapability) != 0
                    ? (bool?)capture.TraceConsistent : null,
                captureSpanMs = (capture.FinishedUtc - capture.StartedUtc).TotalMilliseconds },
            device = available ? new { afeModel = w![14], runtimeVersion = w[192], capabilities = w[2],
                firmwareBuildId = BmsDiagnostics.U32(w, 22).ToString("x8"), sampleTick32k = BmsDiagnostics.U32(w, 198),
                sampleValid = (w[2] & BmsDiagnostics.RuntimeCapability) != 0 ? (bool?)((w[193] & 1) != 0) : null } : null,
            health, capture });
        // Initial, periodic, transition and post-transition evidence. Every sample remains in the
        // journal, so a ZIP never silently replaces the preceding/following observations.
        if (full || changed || boundary is not null || !complete && _lastCondition != condition)
        {
            string name = $"evidence-{Samples:D6}.zip";
            BmsDiagnostics.Export(Path.Combine(DirectoryPath, name), capture, health);
            using (var zip = ZipFile.Open(Path.Combine(DirectoryPath, name), ZipArchiveMode.Update))
            using (var entry = zip.CreateEntry("session.json").Open())
                JsonSerializer.Serialize(entry, new { schema = 1, sessionId = Id, segment = Segment,
                    sample = Samples, journal = "observations.jsonl", recordKind = "observation",
                    fullEvidenceRequested = full, complete, replayReady = false }, Json);
            Write("evidence", new { file = name, sample = Samples, fullEvidenceRequested = full, complete });
        }
        _fullNext = changed || boundary is not null || !complete;
        _lastCondition = condition;
        if (!available || !complete) NoteGap(capture.Status);
        return new(capture, health, identity, complete, Segment, boundary);
    }

    public void Finish(string outcome)
    {
        if (_finished) return;
        var summary = new { schema = 1, sessionId = Id, endpoint = Endpoint, outcome,
            samples = Samples, completeSamples = CompleteSamples, gaps = Gaps, segments = Segment,
            captureComplete = outcome == "complete" && Samples > 0 && CompleteSamples == Samples && Gaps == 0,
            replayReady = false, finishedUtc = DateTimeOffset.UtcNow };
        try
        {
            Write("end", summary);
            File.WriteAllText(Path.Combine(DirectoryPath, "summary.json"), JsonSerializer.Serialize(summary, Json), new UTF8Encoding(false));
        }
        finally { _finished = true; _journal.Dispose(); }
    }

    public void Dispose() { if (!_finished) Finish("interrupted"); }
}
