using System.IO;
using System.Text.Json;
using BmsTool.Windows;

namespace BmsFactoryTest.Windows;

public sealed record FactoryTestStepResult(
    string CaseName,
    int Level,
    string Stage,
    bool Passed,
    string Detail,
    DateTimeOffset At)
{
    public string Result => Passed ? "PASS" : "FAIL";
    public string LevelText => Level > 0 ? $"L{Level}" : "—";
}

public sealed class FactoryTestReport
{
    public DateTimeOffset StartedAt { get; init; }
    public DateTimeOffset FinishedAt { get; set; }
    public string Device { get; init; } = string.Empty;
    public string Firmware { get; init; } = string.Empty;
    public bool Passed { get; set; }
    public bool CleanupCompleted { get; set; }
    public List<FactoryTestStepResult> Steps { get; } = new();

    public string ToJson() => JsonSerializer.Serialize(this, new JsonSerializerOptions { WriteIndented = true });
}

public sealed record FactoryTestCaseInfo(string Name, string Input, string Direction);

public sealed class FactoryTestEngine
{
    private enum InputKind : byte
    {
        CellMv = 1,
        PackCv = 2,
        CurrentTenthA = 3,
        TemperatureRaw = 4,
        MosTemperatureRaw = 5,
        SocPercent = 6
    }

    private sealed record TestCase(string Name, int Group, ushort FaultBit, InputKind Input, bool HighTrip, bool IsDelta = false, bool IsMos = false);

    private static readonly TestCase[] Cases =
    {
        new("单体过压", 0, 1 << 0, InputKind.CellMv, true),
        new("单体欠压", 1, 1 << 1, InputKind.CellMv, false),
        new("总压过压", 2, 1 << 2, InputKind.PackCv, true),
        new("总压欠压", 3, 1 << 3, InputKind.PackCv, false),
        new("充电过流", 4, 1 << 4, InputKind.CurrentTenthA, true),
        new("放电过流", 5, 1 << 5, InputKind.CurrentTenthA, true),
        new("充电高温", 6, 1 << 6, InputKind.TemperatureRaw, true),
        new("充电低温", 7, 1 << 8, InputKind.TemperatureRaw, false),
        new("放电高温", 8, 1 << 7, InputKind.TemperatureRaw, true),
        new("放电低温", 9, 1 << 9, InputKind.TemperatureRaw, false),
        new("MOS高温", 10, 1 << 13, InputKind.MosTemperatureRaw, true, IsMos: true),
        new("单体压差过大", 11, 1 << 10, InputKind.CellMv, true, IsDelta: true),
        new("SOC过低", 12, 1 << 12, InputKind.SocPercent, false),
    };

    public static IReadOnlyList<FactoryTestCaseInfo> AvailableCases { get; } = Cases
        .Select(c => new FactoryTestCaseInfo(c.Name, c.Input.ToString(), c.HighTrip ? "高于阈值" : "低于阈值"))
        .ToArray();

    private readonly Action<string> _log;
    private readonly Action<FactoryStatus>? _statusUpdated;
    private readonly Action<FactoryTestStepResult>? _stepCompleted;
    private readonly Action<string>? _sessionUpdated;
    private readonly Action<int>? _parametersUpdated;

    public FactoryTestEngine(Action<string> log, Action<FactoryStatus>? statusUpdated = null,
        Action<FactoryTestStepResult>? stepCompleted = null, Action<string>? sessionUpdated = null,
        Action<int>? parametersUpdated = null)
    {
        _log = log;
        _statusUpdated = statusUpdated;
        _stepCompleted = stepCompleted;
        _sessionUpdated = sessionUpdated;
        _parametersUpdated = parametersUpdated;
    }

    public async Task<FactoryTestReport> RunAsync(BmsClient client, string device, string firmware,
        CancellationToken ct = default, string? onlyCaseName = null)
    {
        var report = new FactoryTestReport { StartedAt = DateTimeOffset.Now, Device = device, Firmware = firmware };
        FactorySession? session = null;
        try
        {
            if (onlyCaseName is not null && Cases.All(c => !string.Equals(c.Name, onlyCaseName, StringComparison.Ordinal)))
                throw new ArgumentException($"未知的出厂测试项：{onlyCaseName}", nameof(onlyCaseName));

            _log(onlyCaseName is null ? "进入 RAM-only Factory Session..." : $"进入 RAM-only Factory Session，单项测试：{onlyCaseName}...");
            session = await client.FactoryOpenAsync(ct);
            _log($"会话已建立 token=0x{session.Token:X4} timeout={session.TimeoutSeconds}s protocol=v{session.ProtocolVersion}");
            _sessionUpdated?.Invoke($"已建立 · token=0x{session.Token:X4} · 超时 {session.TimeoutSeconds}s · 协议 v{session.ProtocolVersion}");
            ushort[] parameters = await client.ReadProtectionAllAsync(ct);
            if (parameters.Length != 65) throw new IOException($"正式保护参数数量错误：{parameters.Length}");
            _parametersUpdated?.Invoke(parameters.Length);
            _log("已读取正式保护参数 0x2100..0x2140；测试过程不写入参数。");

            FactoryStatus baseline = await ReadStatusAsync(client, session.Token, ct);
            if ((baseline.ProtectionLevel1 | baseline.ProtectionLevel2 | baseline.ProtectionLevel3) != 0)
                throw new IOException("测试前设备已有保护标志，请先排除真实故障后再测试。");

            IEnumerable<TestCase> selectedCases = onlyCaseName is null
                ? Cases
                : Cases.Where(c => string.Equals(c.Name, onlyCaseName, StringComparison.Ordinal));
            foreach (TestCase testCase in selectedCases)
                await RunCaseAsync(client, session.Token, parameters, baseline, testCase, report, ct);

            report.Passed = report.Steps.Count > 0 && report.Steps.All(x => x.Passed);
        }
        catch (Exception ex)
        {
            report.Passed = false;
            AddStep(report, "总流程", 0, "异常", false, ex.Message);
            _log("流程失败：" + ex.Message);
        }
        finally
        {
            if (session is not null)
            {
                try
                {
                    await client.FactoryClearAsync(session.Token, CancellationToken.None);
                    _log("finally：已清除全部 RAM 注入。");
                }
                catch (Exception ex) { _log("finally：清理注入失败：" + ex.Message); }
                try
                {
                    await client.FactoryCloseAsync(session.Token, CancellationToken.None);
                    report.CleanupCompleted = true;
                    _log("finally：Factory Session 已关闭。");
                    _sessionUpdated?.Invoke("已关闭 · 注入已清除");
                }
                catch (Exception ex) { _log("finally：关闭会话失败：" + ex.Message); }
            }
            report.FinishedAt = DateTimeOffset.Now;
        }
        return report;
    }

    private async Task RunCaseAsync(BmsClient client, ushort token, ushort[] p, FactoryStatus baseline,
        TestCase testCase, FactoryTestReport report, CancellationToken ct)
    {
        _log($"开始：{testCase.Name}，自动验证一级/二级/三级及恢复。");
        await client.FactoryClearAsync(token, ct);
        await WaitHealthyAsync(client, token, testCase.FaultBit, p[testCase.Group * 5 + 3], IsOcp(testCase), ct);

        for (int level = 0; level < 3; level++)
        {
            ushort threshold = p[testCase.Group * 5 + level];
            if (threshold == 0)
            {
                AddStep(report, testCase.Name, level + 1, "跳过", true, "正式参数为 0，按固件语义表示关闭本级保护");
                continue;
            }

            await InjectTripValueAsync(client, token, testCase, threshold, baseline, ct);
            int filterMs = p[testCase.Group * 5 + 4];
            FactoryStatus tripped = await WaitForFlagAsync(client, token, testCase.FaultBit, level, true, filterMs + 1500, ct);
            bool levelFlag = (GetLevel(tripped, level) & testCase.FaultBit) != 0;
            bool mosSafe = !testCase.IsMos || level < 2 || (tripped.MosState & 0x0003) == 0;
            AddStep(report, testCase.Name, level + 1, "触发", levelFlag && mosSafe,
                $"L1=0x{tripped.ProtectionLevel1:X4} L2=0x{tripped.ProtectionLevel2:X4} L3=0x{tripped.ProtectionLevel3:X4} MOS=0x{tripped.MosState:X4}");

            await client.FactoryClearAsync(token, ct);
            FactoryStatus recovered = await WaitForFlagAsync(client, token, testCase.FaultBit, level, false,
                IsOcp(testCase) ? 32000 : Math.Max(3000, p[testCase.Group * 5 + 3] > 0 ? filterMs + 3000 : 3000), ct);
            AddStep(report, testCase.Name, level + 1, "恢复", (GetLevel(recovered, level) & testCase.FaultBit) == 0,
                $"恢复后 L{level + 1}=0x{GetLevel(recovered, level):X4}");
        }
    }

    private static bool IsOcp(TestCase c) => c.Group is 4 or 5;

    private async Task InjectTripValueAsync(BmsClient client, ushort token, TestCase c, ushort threshold,
        FactoryStatus baseline, CancellationToken ct)
    {
        ushort value = c.HighTrip ? checked((ushort)Math.Min(ushort.MaxValue, threshold + 1)) : (ushort)(threshold == 0 ? 0 : threshold - 1);
        if (c.IsDelta)
        {
            ushort low = (ushort)Math.Min(60000, Math.Max(1000, (int)baseline.CellMinMv));
            ushort high = checked((ushort)Math.Min(65535, low + threshold + 1));
            await client.FactoryInjectAsync(token, (byte)InputKind.CellMv, 0, low, ct);
            await client.FactoryInjectAsync(token, (byte)InputKind.CellMv, 1, high, ct);
            return;
        }
        if (c.Group == 5) value = unchecked((ushort)(-(short)value));
        await client.FactoryInjectAsync(token, (byte)c.Input, 0, value, ct);
    }

    private async Task WaitHealthyAsync(BmsClient client, ushort token, ushort bit, int recover, bool ocp, CancellationToken ct)
    {
        await WaitForFlagAsync(client, token, bit, -1, false, ocp ? 32000 : 8000, ct);
    }

    private async Task<FactoryStatus> WaitForFlagAsync(BmsClient client, ushort token, ushort bit, int level, bool expected,
        int timeoutMs, CancellationToken ct)
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        FactoryStatus last = await ReadStatusAsync(client, token, ct);
        while (DateTime.UtcNow < deadline)
        {
            ushort levelValue = level < 0 ? (ushort)(last.ProtectionLevel1 | last.ProtectionLevel2 | last.ProtectionLevel3) : GetLevel(last, level);
            bool active = (levelValue & bit) != 0;
            if (active == expected) return last;
            await Task.Delay(250, ct);
            await client.FactoryHeartbeatAsync(token, ct);
            last = await ReadStatusAsync(client, token, ct);
        }
        throw new TimeoutException($"等待保护 {(expected ? "触发" : "恢复")} 超时 level={(level < 0 ? "all" : (level + 1).ToString())} bit=0x{bit:X4}; L1=0x{last.ProtectionLevel1:X4}; L2=0x{last.ProtectionLevel2:X4}; L3=0x{last.ProtectionLevel3:X4}");
    }

    private static ushort GetLevel(FactoryStatus status, int level) => level switch
    {
        0 => status.ProtectionLevel1,
        1 => status.ProtectionLevel2,
        _ => status.ProtectionLevel3
    };

    private void AddStep(FactoryTestReport report, string name, int level, string stage, bool passed, string detail)
    {
        FactoryTestStepResult result = new(name, level, stage, passed, detail, DateTimeOffset.Now);
        report.Steps.Add(result);
        _stepCompleted?.Invoke(result);
    }

    private async Task<FactoryStatus> ReadStatusAsync(BmsClient client, ushort token, CancellationToken ct)
    {
        FactoryStatus status = await client.ReadFactoryStatusAsync(token, ct);
        _statusUpdated?.Invoke(status);
        return status;
    }
}
