using System.IO;
using System.IO.Compression;
using System.Text.Json;

namespace BmsTool.Windows;

public static class BmsTestStatus
{
    public const string Pass = "pass";
    public const string Warning = "warning";
    public const string Fail = "fail";
    public const string Blocked = "blocked";
}

public sealed record BmsTestCheck(
    string Id,
    string Status,
    string Title,
    string Evidence,
    string Recommendation = "");

public sealed record BmsTestSample(
    int Number,
    DateTimeOffset CapturedUtc,
    bool Success,
    long DurationMs,
    string? FirmwareBuildId,
    SocDiagnosticSnapshot? Soc,
    string? Error);

public sealed class BmsTestReport
{
    public string Test { get; init; } = "";
    public string Endpoint { get; init; } = "";
    public DateTimeOffset StartedUtc { get; init; }
    public DateTimeOffset FinishedUtc { get; init; }
    public int RequestedSamples { get; init; }
    public int SuccessfulSamples { get; init; }
    public string? FirmwareBuildId { get; init; }
    public bool Passed { get; init; }
    public string Summary { get; init; } = "";
    public IReadOnlyList<BmsTestCheck> Checks { get; init; } = Array.Empty<BmsTestCheck>();
    public IReadOnlyList<BmsTestSample> Samples { get; init; } = Array.Empty<BmsTestSample>();
}

public static class BmsTestEngine
{
    public static async Task<BmsTestReport> RunSocAsync(
        BmsClient client,
        string endpoint,
        int count,
        TimeSpan interval,
        CancellationToken ct = default)
    {
        if (count < 1) throw new ArgumentOutOfRangeException(nameof(count));
        DateTimeOffset started = DateTimeOffset.UtcNow;
        var samples = new List<BmsTestSample>(count);
        var checks = new List<BmsTestCheck>();

        for (int number = 1; number <= count; number++)
        {
            ct.ThrowIfCancellationRequested();
            var timer = System.Diagnostics.Stopwatch.StartNew();
            try
            {
                DiagnosticCapture capture = await client.ReadDiagnosticsAsync(false, endpoint, ct);
                if (!capture.Supported || capture.Words is null)
                    throw new InvalidDataException(capture.Status);
                SocDiagnosticSnapshot soc = BmsDiagnostics.DecodeSocSnapshot(capture.Words);
                uint build = BmsDiagnostics.U32(capture.Words, 22);
                timer.Stop();
                samples.Add(new BmsTestSample(number, DateTimeOffset.UtcNow, true,
                    timer.ElapsedMilliseconds, build == 0 ? null : build.ToString("x8"), soc, null));
            }
            catch (OperationCanceledException) { throw; }
            catch (Exception ex)
            {
                timer.Stop();
                samples.Add(new BmsTestSample(number, DateTimeOffset.UtcNow, false,
                    timer.ElapsedMilliseconds, null, null, ex.GetType().Name + ": " + ex.Message));
            }

            if (number < count && interval > TimeSpan.Zero)
                await Task.Delay(interval, ct);
        }

        int successful = samples.Count(x => x.Success);
        Add(checks, "soc.samples", successful == count ? BmsTestStatus.Pass : BmsTestStatus.Fail,
            "SOC 样本完整", $"{successful}/{count} 次读取成功",
            successful == count ? "" : "检查 BLE/串口连接和固件诊断支持后重试。 ");

        SocDiagnosticSnapshot[] socSamples = samples.Where(x => x.Soc is not null).Select(x => x.Soc!).ToArray();
        bool rangeOk = socSamples.All(x => x.SocEstimate <= 100 && x.SocDisplay <= 100 && x.Soh <= 100);
        Add(checks, "soc.range", rangeOk ? BmsTestStatus.Pass : BmsTestStatus.Fail,
            "SOC/SOH 范围", rangeOk ? "全部在 0..100%" : "检测到超出 0..100% 的值",
            rangeOk ? "" : "保存诊断包并检查固件 SOC 状态。 ");

        bool capacityOk = socSamples.All(x => x.NominalCapacityAh > 0 && x.EffectiveCapacityAh > 0 &&
            x.RemainingCapacityAh >= 0 && x.RemainingCapacityAh <= x.EffectiveCapacityAh + 0.1);
        Add(checks, "soc.capacity", capacityOk ? BmsTestStatus.Pass : BmsTestStatus.Fail,
            "容量关系", capacityOk ? "0 <= Remaining <= Effective，且 Nominal/Effective > 0" : "容量字段关系无效",
            capacityOk ? "" : "核对容量参数、持久化状态及 runtime diagnostics。 ");

        bool profileOk = socSamples.All(x => x.ProfileId != 0 && x.ProfileVersion != 0 &&
            x.Chemistry is "LFP" or "NMC");
        Add(checks, "soc.profile", profileOk ? BmsTestStatus.Pass : BmsTestStatus.Fail,
            "SOC Profile", profileOk ? "Chemistry/Profile ID/Profile Version 有效" : "Profile 身份缺失或未知",
            profileOk ? "" : "核对编译产品 profile 与 SOC 配置。 ");

        bool etaOk = socSamples.All(x => !x.EtaValid ||
            (x.EtaState == "VALID" && x.EtaDirection is "CHARGE" or "DISCHARGE" &&
             (x.TimeToEmptyMinutes.HasValue || x.TimeToFullMinutes.HasValue)));
        Add(checks, "soc.eta", etaOk ? BmsTestStatus.Pass : BmsTestStatus.Fail,
            "ETA 一致性", etaOk ? "ETA valid/state/direction/time 字段一致" : "ETA 有效位与状态或时间不一致",
            etaOk ? "" : "检查 ETA 状态机和 runtime 字段打包。 ");

        string[] builds = samples.Where(x => x.Success && x.FirmwareBuildId is not null)
            .Select(x => x.FirmwareBuildId!).Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
        bool buildStable = builds.Length <= 1;
        Add(checks, "firmware.build_stable", buildStable ? BmsTestStatus.Pass : BmsTestStatus.Fail,
            "Build ID 稳定", builds.Length == 0 ? "Build ID unavailable" : string.Join(", ", builds),
            buildStable ? "" : "测试期间设备可能重启或被 OTA；拆分证据并重新测试。 ");

        int maxJump = 0;
        for (int i = 1; i < socSamples.Length; i++)
            maxJump = Math.Max(maxJump, Math.Abs(socSamples[i].SocEstimate - socSamples[i - 1].SocEstimate));
        Add(checks, "soc.jump_observation", maxJump <= 5 ? BmsTestStatus.Pass : BmsTestStatus.Warning,
            "相邻 SOC 变化观察", $"最大变化 {maxJump} percentage point(s)",
            maxJump <= 5 ? "" : "该项不直接判失败；结合端点/OCV 校正状态和电流证据判断是否合理。 ");

        return Build("soc", endpoint, started, checks, samples);
    }

    public static async Task<BmsTestReport> RunDiagnosticsAsync(
        BmsClient client,
        string endpoint,
        int count,
        bool full,
        TimeSpan interval,
        CancellationToken ct = default)
    {
        if (count < 1) throw new ArgumentOutOfRangeException(nameof(count));
        DateTimeOffset started = DateTimeOffset.UtcNow;
        var samples = new List<BmsTestSample>(count);
        var checks = new List<BmsTestCheck>();
        var captures = new List<DiagnosticCapture>(count);

        for (int number = 1; number <= count; number++)
        {
            ct.ThrowIfCancellationRequested();
            var timer = System.Diagnostics.Stopwatch.StartNew();
            try
            {
                DiagnosticCapture capture = await client.ReadDiagnosticsAsync(full, endpoint, ct);
                timer.Stop();
                captures.Add(capture);
                uint? build = capture.Words is { Length: 256 } w ? BmsDiagnostics.U32(w, 22) : null;
                bool success = capture.Supported && capture.Words is not null;
                samples.Add(new BmsTestSample(number, DateTimeOffset.UtcNow, success,
                    timer.ElapsedMilliseconds, build is null or 0 ? null : build.Value.ToString("x8"),
                    null, success ? null : capture.Status + ": " + string.Join("; ", capture.Errors)));
            }
            catch (OperationCanceledException) { throw; }
            catch (Exception ex)
            {
                timer.Stop();
                samples.Add(new BmsTestSample(number, DateTimeOffset.UtcNow, false,
                    timer.ElapsedMilliseconds, null, null, ex.GetType().Name + ": " + ex.Message));
            }
            if (number < count && interval > TimeSpan.Zero)
                await Task.Delay(interval, ct);
        }

        Add(checks, "diag.supported", captures.All(x => x.Supported && x.Words is not null) ? BmsTestStatus.Pass : BmsTestStatus.Fail,
            "诊断协议", $"{captures.Count(x => x.Supported && x.Words is not null)}/{count} 次可用",
            "失败时检查固件版本和通信错误。 ");
        Add(checks, "diag.snapshot", captures.All(x => x.SnapshotConsistent) ? BmsTestStatus.Pass : BmsTestStatus.Fail,
            "快照一致性", $"{captures.Count(x => x.SnapshotConsistent)}/{count} 次一致",
            "不一致表示采集期间可能发生重启或快照尚未冻结。 ");
        if (full)
            Add(checks, "diag.trace", captures.All(x => x.TraceConsistent) ? BmsTestStatus.Pass : BmsTestStatus.Fail,
                "Trace 一致性", $"{captures.Count(x => x.TraceConsistent)}/{count} 次一致",
                "Trace 分页变化时保留证据并重采。 ");
        Add(checks, "diag.errors", captures.All(x => x.Errors.Count == 0) ? BmsTestStatus.Pass : BmsTestStatus.Fail,
            "诊断错误", $"总计 {captures.Sum(x => x.Errors.Count)} 条错误",
            "检查每轮 Errors；可单独导出失败轮诊断包。 ");

        string[] builds = samples.Where(x => x.FirmwareBuildId is not null).Select(x => x.FirmwareBuildId!)
            .Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
        Add(checks, "firmware.build_stable", builds.Length <= 1 ? BmsTestStatus.Pass : BmsTestStatus.Fail,
            "Build ID 稳定", builds.Length == 0 ? "Build ID unavailable" : string.Join(", ", builds),
            "变化表示测试期间固件身份发生改变。 ");
        return Build(full ? "diag-full" : "diag-quick", endpoint, started, checks, samples);
    }

    private static void Add(List<BmsTestCheck> checks, string id, string status,
        string title, string evidence, string recommendation) =>
        checks.Add(new BmsTestCheck(id, status, title, evidence, recommendation));

    private static BmsTestReport Build(string test, string endpoint, DateTimeOffset started,
        List<BmsTestCheck> checks, List<BmsTestSample> samples)
    {
        bool passed = checks.All(x => x.Status is not BmsTestStatus.Fail and not BmsTestStatus.Blocked);
        int fail = checks.Count(x => x.Status == BmsTestStatus.Fail);
        int warning = checks.Count(x => x.Status == BmsTestStatus.Warning);
        string[] buildIds = samples.Where(x => x.Success && x.FirmwareBuildId is not null)
            .Select(x => x.FirmwareBuildId!).Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
        return new BmsTestReport
        {
            Test = test,
            Endpoint = endpoint,
            StartedUtc = started,
            FinishedUtc = DateTimeOffset.UtcNow,
            RequestedSamples = samples.Count,
            SuccessfulSamples = samples.Count(x => x.Success),
            FirmwareBuildId = buildIds.Length == 1 ? buildIds[0] : null,
            Passed = passed,
            Summary = passed ? $"PASS · {warning} warning(s)" : $"FAIL · {fail} failure(s), {warning} warning(s)",
            Checks = checks,
            Samples = samples
        };
    }
}

public sealed record DiagnosticBundleDifference(
    string Entry,
    string Path,
    string? Before,
    string? After);

public sealed class DiagnosticBundleComparison
{
    public string BeforePath { get; init; } = "";
    public string AfterPath { get; init; } = "";
    public int DifferenceCount => Differences.Count;
    public IReadOnlyList<DiagnosticBundleDifference> Differences { get; init; } = Array.Empty<DiagnosticBundleDifference>();
}

public static class DiagnosticBundleComparer
{
    private static readonly string[] Entries =
    {
        "manifest.json", "boot.json", "storage.json", "current.json", "soc.json",
        "power.json", "protection_runtime.json", "parameters.json", "afe.json", "health.json"
    };

    private static readonly HashSet<string> IgnoredPaths = new(StringComparer.OrdinalIgnoreCase)
    {
        "$.startedUtc", "$.finishedUtc", "$.capturedUtc"
    };

    public static DiagnosticBundleComparison Compare(string beforePath, string afterPath)
    {
        beforePath = Path.GetFullPath(beforePath);
        afterPath = Path.GetFullPath(afterPath);
        var differences = new List<DiagnosticBundleDifference>();
        using var before = ZipFile.OpenRead(beforePath);
        using var after = ZipFile.OpenRead(afterPath);

        foreach (string entryName in Entries)
        {
            Dictionary<string, string?> left = ReadFlat(before, entryName);
            Dictionary<string, string?> right = ReadFlat(after, entryName);
            foreach (string path in left.Keys.Concat(right.Keys).Distinct(StringComparer.Ordinal).OrderBy(x => x, StringComparer.Ordinal))
            {
                if (IgnoredPaths.Contains(path)) continue;
                left.TryGetValue(path, out string? oldValue);
                right.TryGetValue(path, out string? newValue);
                if (!string.Equals(oldValue, newValue, StringComparison.Ordinal))
                    differences.Add(new DiagnosticBundleDifference(entryName, path, oldValue, newValue));
            }
        }

        return new DiagnosticBundleComparison
        {
            BeforePath = beforePath,
            AfterPath = afterPath,
            Differences = differences
        };
    }

    private static Dictionary<string, string?> ReadFlat(ZipArchive zip, string entryName)
    {
        var result = new Dictionary<string, string?>(StringComparer.Ordinal);
        ZipArchiveEntry? entry = zip.GetEntry(entryName);
        if (entry is null)
        {
            result["$missing"] = null;
            return result;
        }
        using Stream stream = entry.Open();
        using JsonDocument document = JsonDocument.Parse(stream);
        Flatten(document.RootElement, "$", result);
        return result;
    }

    private static void Flatten(JsonElement element, string path, Dictionary<string, string?> output)
    {
        switch (element.ValueKind)
        {
            case JsonValueKind.Object:
                foreach (JsonProperty property in element.EnumerateObject())
                    Flatten(property.Value, path + "." + property.Name, output);
                break;
            case JsonValueKind.Array:
                int index = 0;
                foreach (JsonElement item in element.EnumerateArray())
                {
                    string itemPath = $"{path}[{index}]";
                    if (item.ValueKind == JsonValueKind.Object)
                    {
                        foreach (JsonProperty property in item.EnumerateObject())
                        {
                            if ((string.Equals(property.Name, "id", StringComparison.OrdinalIgnoreCase) ||
                                 string.Equals(property.Name, "field", StringComparison.OrdinalIgnoreCase)) &&
                                property.Value.ValueKind == JsonValueKind.String)
                            {
                                itemPath = $"{path}[{property.Name.ToLowerInvariant()}={property.Value.GetString()}]";
                                break;
                            }
                        }
                    }
                    Flatten(item, itemPath, output);
                    index++;
                }
                if (index == 0) output[path] = "[]";
                break;
            case JsonValueKind.Null:
            case JsonValueKind.Undefined:
                output[path] = null;
                break;
            default:
                output[path] = element.GetRawText();
                break;
        }
    }
}
