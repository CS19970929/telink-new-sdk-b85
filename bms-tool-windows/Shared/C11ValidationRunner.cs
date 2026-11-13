using System.Diagnostics;
using System.IO;
using System.Net;
using System.Text;
using System.Text.Json;
using System.Globalization;

namespace BmsTool.Windows;

public sealed class C11ValidationRunner
{
    public static readonly string[] Names = {
        "单体过压",      "单体欠压",   "充电过流", "放电过流",         "充电过温",
        "充电低温",      "放电过温",   "放电低温", "AFE 内部诊断",     "AFE 通信异常",
        "Cell 采样断线", "NTC 开短路", "电流漂移", "一级关断反馈异常", "电流采样失效",
        "CPU 寄存器",    "PC 控制流",  "RAM",      "Flash 完整性",     "时钟",
        "中断活性",      "ADC"
    };
    private readonly Action<C11CaseResult> _result;
    private readonly Action<C11Status> _status;
    private readonly Action<string> _log;
    private readonly StreamWriter? _trace;
    private readonly StreamWriter? _curves;
    private bool _curveHeader;
    private uint _eventCursor;
    private readonly int _selfTestCycles;
    private readonly Func<int, CancellationToken, Task> _delay;
    public C11ValidationRunner(Action<C11CaseResult> result, Action<C11Status> status,
                               Action<string> log, StreamWriter? trace = null,
                               Func<int, CancellationToken, Task>? delay = null,
                               StreamWriter? curves = null, int selfTestCycles = 1000)
    {
        _result = result;
        _status = status;
        _log = log;
        _trace = trace;
        _curves = curves;
        _delay = delay ?? Task.Delay;
        _selfTestCycles = selfTestCycles;
    }

    public async Task<C11Report> RunAsync(IC11ValidationDevice device, string identity,
                                          int? onlyCase, CancellationToken ct)
    {
        var report = new C11Report { Device = identity, SelfTestCyclesRequired = _selfTestCycles };
        var connection = new C11Connection(device);
        try
        {
            await connection.OpenAsync(ct);
            report.Capabilities = connection.Capabilities;
            report.FirmwareIntegrity =
                Convert.ToHexString(await connection.Command(0x1c, Array.Empty<byte>(), ct));
            C11Status baseline = await connection.StatusAsync(ct);
            if (baseline.RealFaults != 0 || baseline.RealDiagnostics != 0 ||
                baseline.RawSequence == 0 || baseline.Hold || baseline.WriteFailed)
                throw new IOException("真实设备存在故障/无有效样本/安全保持；先处理真实状态。");
            foreach (int index in onlyCase.HasValue ? new[] { onlyCase.Value }
                                                    : Enumerable.Range(0, 22))
            {
                ct.ThrowIfCancellationRequested();
                if (index < 0 || index >= 22)
                    throw new ArgumentOutOfRangeException(nameof(onlyCase));
                try
                {
                    await connection.ClearAsync(ct);
                    if (index < 8)
                        await RunProtection(connection, index, ct);
                    else if (index < 15)
                        await RunDiagnostic(connection, index - 8, ct);
                    else
                        await RunSelfTest(connection, index - 14, ct);
                    Add(report, index, C11Outcome.Pass,
                        index >= 8 && index <= 14
                            ? "诊断输入注入检测通过；实际故障覆盖、独立参考和物理后备仍需台架证据。"
                            : "MCU 软件判据通过；实际采样精度/物理关断另行验收。");
                }
                catch (NotSupportedException ex)
                {
                    Add(report, index, C11Outcome.Blocked, ex.Message);
                }
                catch (OperationCanceledException)
                {
                    Add(report, index, C11Outcome.Aborted, "用户中止；正在确认清理。");
                    throw;
                }
                catch (Exception ex)
                {
                    Add(report, index,
                        ex is TimeoutException ? C11Outcome.Inconclusive : C11Outcome.Fail,
                        ex.Message);
                    break;
                }
            }
        }
        catch (Exception ex)
        {
            if (report.Results.Count == 0)
                report.Results.Add(
                    new("SETUP", "前置条件",
                        ct.IsCancellationRequested ? C11Outcome.Aborted : C11Outcome.Blocked,
                        "未执行", ex.Message, DateTimeOffset.Now));
            _log(ex.Message);
        }
        finally
        {
            try
            {
                await connection.DisposeAsync();
                report.CleanupConfirmed = true;
            }
            catch (Exception ex)
            {
                report.CleanupConfirmed = false;
                _log("清理结果未知：" + ex.Message);
            }
            report.Finished = DateTimeOffset.Now;
        }
        return report;
    }

    private void Add(C11Report report, int index, C11Outcome outcome, string detail)
    {
        var row = new C11CaseResult($"SW-VD-{index+1:000}", Names[index], outcome, "MCU_INJECTION",
                                    detail, DateTimeOffset.Now);
        report.Results.Add(row);
        _result(row);
        _log(row.Id + " " + row.Outcome + " " + row.Detail);
    }

    private async Task<C11Status> Observe(C11Connection c, CancellationToken ct)
    {
        var s = await c.StatusAsync(ct);
        _status(s);
        if (_trace is not null || _curves is not null)
        {
            var raw = await c.SampleAsync(false, ct);
            var effective = await c.SampleAsync(true, ct);
            bool coherent = raw.Sequence == s.RawSequence &&
                            (!s.InjectionActive || effective.Sequence == s.AppliedSequence);
            if (_trace is not null)
            {
                await _trace.WriteLineAsync(
                    JsonSerializer.Serialize(new { At = DateTimeOffset.Now, Status = s, Raw = raw,
                                                   Effective = effective, Coherent = coherent }));
                var rawDiagnostics = await c.Command(0x15, new byte[] { 4 }, ct);
                var testDiagnostics = await c.Command(0x15, new byte[] { 5 }, ct);
                if (rawDiagnostics.Length != 40 || testDiagnostics.Length != 40)
                    throw new IOException("原始诊断页长度不匹配。");
                await _trace.WriteLineAsync(JsonSerializer.Serialize(
                    new { Kind = "DIAGNOSTIC_INPUTS", Raw = Convert.ToHexString(rawDiagnostics),
                          Effective = Convert.ToHexString(testDiagnostics) }));
                var events = await c.Command(
                    0x18, C11Wire.Sequence(0, _eventCursor).AsSpan(2).ToArray(), ct);
                if (events.Length < 9 || events.Length != 9 + events[8] * 14)
                    throw new IOException("事件页长度不匹配。");
                _eventCursor = C11Wire.U32(events, 4);
                await _trace.WriteLineAsync(JsonSerializer.Serialize(
                    new { Kind = "MCU_EVENTS", Lost = C11Wire.U32(events, 0), Cursor = _eventCursor,
                          Payload = Convert.ToHexString(events) }));
                await _trace.FlushAsync(ct);
            }
            if (_curves is not null)
            {
                if (!_curveHeader)
                {
                    await _curves.WriteLineAsync(
                        "utc,mcu_ms,raw_sequence,applied_sequence,coherent,raw_valid,test_valid," +
                        "real_faults,test_faults,real_diag,test_diag,requested,final,afe_status," +
                        "raw_current_ma,test_current_ma," +
                        string.Join(",",
                                    Enumerable.Range(1, 16).Select(i => "raw_cell" + i + "_mv")) +
                        "," +
                        string.Join(",",
                                    Enumerable.Range(1, 16).Select(i => "test_cell" + i + "_mv")) +
                        "," +
                        string.Join(",",
                                    Enumerable.Range(1, 3).Select(i => "raw_temp" + i + "_decic")) +
                        "," +
                        string.Join(
                            ",", Enumerable.Range(1, 3).Select(i => "test_temp" + i + "_decic")));
                    _curveHeader = true;
                }
                var values = new object[] { DateTimeOffset.UtcNow.ToString("O"),
                                            s.NowMs,
                                            s.RawSequence,
                                            s.AppliedSequence,
                                            coherent ? 1 : 0,
                                            raw.Valid,
                                            effective.Valid,
                                            s.RealFaults,
                                            s.TestFaults,
                                            s.RealDiagnostics,
                                            s.TestDiagnostics,
                                            s.Requested,
                                            s.Final,
                                            s.Readback,
                                            raw.CurrentMa,
                                            effective.CurrentMa }
                                 .Concat(raw.CellMv.Cast<object>())
                                 .Concat(effective.CellMv.Cast<object>())
                                 .Concat(raw.TemperatureDecic.Cast<object>())
                                 .Concat(effective.TemperatureDecic.Cast<object>());
                await _curves.WriteLineAsync(string.Join(
                    ",", values.Select(v => Convert.ToString(v, CultureInfo.InvariantCulture))));
                await _curves.FlushAsync(ct);
            }
        }
        if (s.RealFaults != 0 || s.RealDiagnostics != 0 || s.WriteFailed || s.Hold)
            throw new IOException("真实保护/诊断/安全保持/输出写入异常，停止测试。");
        return s;
    }

    private async Task<C11Status> Hold(C11Connection c, uint applied, uint duration,
                                       Action<C11Status>? check, CancellationToken ct)
    {
        var timer = Stopwatch.StartNew();
        C11Status s;
        do
        {
            await c.HeartbeatAsync(ct);
            s = await Observe(c, ct);
            if (s.InjectionActive)
                await c.Command(0x1b, C11Wire.Sequence(0, s.AppliedSequence).AsSpan(2).ToArray(),
                                ct);
            check?.Invoke(s);
            if (unchecked(s.NowMs - applied) >= duration)
                return s;
            if (timer.ElapsedMilliseconds > duration + 5000)
                throw new TimeoutException("设备时基没有按预期前进。");
            await _delay(100, ct);
        } while (true);
    }

    private static List<(byte Kind, byte Index, int Value)> NormalFields(C11Connection c,
                                                                         int direction)
    {
        var f = new List<(byte, byte, int)>();
        for (byte i = 0; i < c.Capabilities.Cells; i++)
            f.Add((1, i, 3700));
        for (byte i = 0; i < c.Capabilities.Temperatures; i++)
            f.Add((4, i, 250));
        f.Add((3, 0, direction * 1000));
        return f;
    }

    private static List<(byte Kind, byte Index, int Value)> Input(C11Connection c, int rule,
                                                                  int channel, int value)
    {
        var f = NormalFields(c, rule is 3 or 6 or 7 ? -1 : 1);
        if (rule < 2)
            f.Add((1, checked((byte)channel), value));
        else if (rule < 4)
            f.Add((3, 0, rule == 3 ? -value : value));
        else
            f.Add((4, checked((byte)channel), value));
        return f;
    }

    private async Task RunProtection(C11Connection c, int rule, CancellationToken ct)
    {
        C11Rule config = await c.RuleAsync(rule, ct);
        bool high = rule is not(1 or 5 or 7);
        ushort bit = (ushort)(1 << rule);
        int[] channels = rule < 2   ? Enumerable.Range(0, c.Capabilities.Cells).ToArray()
                         : rule < 4 ? new[] { 0 }
                                    : Enumerable.Range(0, c.Capabilities.Temperatures).ToArray();
        foreach (int channel in channels)
            for (int repeat = 0; repeat < 3; repeat++)
            {
                _log(
                    $"SW-VD-{rule+1:000} 通道 {channel+1} 第 {repeat+1}/3 次；阈值 {config.Trip}，延时 {config.TripMs} ms。");
                await c.ClearAsync(ct);
                int normal = rule == 0 ? 4240 : config.Trip + (high ? -1 : 1);
                uint start = await c.ApplyAsync(Input(c, rule, channel, normal), ct);
                await Hold(c, start, Math.Max(2000u, config.TripMs + 100),
                           s =>
                           {
                               if ((s.TestFaults & bit) != 0)
                                   throw new IOException("正常侧提前触发。");
                           },
                           ct);
                // Exactly at threshold is normal for the released strict comparisons.
                start = await c.ApplyAsync(Input(c, rule, channel, config.Trip), ct);
                await Hold(c, start, config.TripMs + 100,
                           s =>
                           {
                               if ((s.TestFaults & bit) != 0)
                                   throw new IOException("阈值等号误触发。");
                           },
                           ct);
                int trip = rule == 0 ? 4260 : config.Trip + (high ? 1 : -1);
                // The MCU restores the preceding input after half the trip delay.
                // BLE staging/response latency cannot stretch this pulse.
                start = await c.ApplyAsync(Input(c, rule, channel, trip), ct,
                                           pulseMs: config.TripMs / 2);
                await Hold(c, start, config.TripMs + 200,
                           s =>
                           {
                               if ((s.TestFaults & bit) != 0)
                                   throw new IOException("设备定时短脉冲被错误累计。");
                           },
                           ct);
                start = await c.ApplyAsync(Input(c, rule, channel, trip), ct);
                C11Status active = await Hold(c, start, config.TripMs + 200, null, ct);
                C11Rule actual = await c.RuleAsync(rule, ct);
                uint elapsed = unchecked(actual.FaultSetMs - start);
                if ((active.TestFaults & bit) == 0 || (actual.Channels & (1 << channel)) == 0 ||
                    elapsed <= config.TripMs || elapsed > config.TripMs + 200 ||
                    (active.TestInhibit & config.Action) != config.Action)
                    throw new IOException($"触发、通道、禁止请求或时间不匹配：{elapsed} ms。");
                _log($"MCU 触发时间差 {elapsed} ms，Applied={start}，Fault={actual.FaultSetMs}。");
                int hysteresis = rule == 0 ? 4160 : config.Recover;
                start = await c.ApplyAsync(Input(c, rule, channel, hysteresis), ct);
                await Hold(c, start, config.RecoverMs + 100,
                           s =>
                           {
                               if ((s.TestFaults & bit) == 0)
                                   throw new IOException("回差区提前恢复。");
                           },
                           ct);
                if (config.Latched)
                {
                    start = await c.ApplyAsync(Input(c, rule, channel, 0), ct);
                    await Hold(c, start, config.RecoverMs + 100,
                               s =>
                               {
                                   if ((s.TestFaults & bit) == 0)
                                       throw new IOException("锁存故障被零电流自动解除。");
                               },
                               ct);
                    _log("锁存保持正确；仅清除测试实例，真实解除仍受独立条件控制。");
                }
                else
                {
                    int recovery = rule == 0 ? 4140 : config.Recover + (high ? -1 : 1);
                    start = await c.ApplyAsync(Input(c, rule, channel, recovery), ct);
                    await Hold(c, start, config.RecoverMs / 2,
                               s =>
                               {
                                   if ((s.TestFaults & bit) == 0)
                                       throw new IOException("恢复持续时间未到即解除。");
                               },
                               ct);
                    C11Status restored = await Hold(c, start, config.RecoverMs + 200, null, ct);
                    actual = await c.RuleAsync(rule, ct);
                    uint delay = unchecked(actual.RecoveredMs - start);
                    if ((restored.TestFaults & bit) != 0 || delay < config.RecoverMs ||
                        delay > config.RecoverMs + 200)
                        throw new IOException($"恢复或恢复延时不匹配：{delay} ms。");
                }
            }
    }

    private async Task RunDiagnostic(C11Connection c, int diagnostic, CancellationToken ct)
    {
        if ((c.Capabilities.DiagnosticMask & (1u << diagnostic)) == 0)
            throw new NotSupportedException("设备未声明此诊断测试能力。");
        int channelCount = diagnostic == 2   ? c.Capabilities.Cells
                           : diagnostic == 3 ? c.Capabilities.Temperatures
                                             : 1;
        int variants = diagnostic is 2 or 3 ? 2 : 1;
        for (int channel = 0; channel < channelCount; channel++)
            for (int variant = 0; variant < variants; variant++)
                for (int repeat = 0; repeat < 3; repeat++)
                {
                    await c.ClearAsync(ct);
                    var fields = NormalFields(c, 1);
                    if (diagnostic == 2)
                        fields.Add((1, (byte)channel, variant == 0 ? 1999 : 4501));
                    else if (diagnostic == 3)
                        fields.Add((9, (byte)channel, variant == 0 ? 0 : 32768));
                    else
                        fields.Add((7, (byte)diagnostic, 1));
                    var timing = await c.Command(0x1e, new[] { (byte)diagnostic }, ct);
                    if (timing.Length != 18)
                        throw new IOException("诊断时序响应长度不匹配。");
                    uint delay = C11Wire.U32(timing, 0), recovery = C11Wire.U32(timing, 4);
                    uint start = await c.ApplyAsync(fields, ct);
                    C11Status active =
                        await Hold(c, start, delay + 200,
                                   s =>
                                   {
                                       if (unchecked(s.NowMs - start) <= delay &&
                                           (s.TestDiagnostics & (1 << diagnostic)) != 0)
                                           throw new IOException("诊断提前触发。");
                                   },
                                   ct);
                    timing = await c.Command(0x1e, new[] { (byte)diagnostic }, ct);
                    uint elapsed = unchecked(C11Wire.U32(timing, 8) - start);
                    if ((active.TestDiagnostics & (1 << diagnostic)) == 0 ||
                        active.TestInhibit != 3)
                        throw new IOException("故障输入未被诊断或未产生双路禁止请求。");
                    if (elapsed <= delay || elapsed > delay + 200)
                        throw new IOException($"诊断触发时间不符：{elapsed} ms。");
                    _log(
                        $"诊断{diagnostic+1} 通道{channel+1} 极值{variant+1} 重复{repeat+1}/3，MCU确认时间差{elapsed} ms。");
                    start = await c.ApplyAsync(NormalFields(c, 1), ct);
                    C11Status recovered = await Hold(c, start, recovery + 200, null, ct);
                    bool remains = (recovered.TestDiagnostics & (1 << diagnostic)) != 0;
                    if (remains != (diagnostic == 5))
                        throw new IOException("诊断恢复/关断失效锁存策略错误。");
                    if (diagnostic != 5)
                    {
                        timing = await c.Command(0x1e, new[] { (byte)diagnostic }, ct);
                        elapsed = unchecked(C11Wire.U32(timing, 12) - start);
                        if (elapsed < recovery || elapsed > recovery + 200)
                            throw new IOException("诊断恢复延时不符。");
                    }
                }
    }

    private async Task RunSelfTest(C11Connection c, int item, CancellationToken ct)
    {
        uint bit = 1u << (item - 1);
        if ((c.Capabilities.SelfTestMask & bit) == 0)
            throw new NotSupportedException("自检未覆盖或无对应测试入口。");
        if ((c.SelfTestAcceptanceMask & bit) == 0)
            throw new NotSupportedException(
                "仅有范围/活性健康检查，缺少规范要求的可信基准或完整检测覆盖。");
        for (int cycle = 0; cycle <= _selfTestCycles; cycle++)
        {
            byte inject = (byte)(cycle == _selfTestCycles ? 1 : 0);
            await c.Command(0x19, new[] { (byte)item, inject }, ct);
            var timer = Stopwatch.StartNew();
            byte[] p;
            do
            {
                await c.HeartbeatAsync(ct);
                p = await c.Command(0x15, new byte[] { 3 }, ct);
                if (p.Length != 46)
                    throw new IOException("自检状态长度不匹配。");
                if (C11Wire.U16(p, 36) == 0)
                    break;
                if (timer.ElapsedMilliseconds > 15000)
                    throw new TimeoutException("自检没有在窗口内完成。");
                await _delay(100, ct);
            } while (true);
            uint expected = C11Wire.U32(p, inject == 0 ? 24 : 28);
            if ((expected & bit) == 0)
                throw new IOException("实际自检/比较失败注入结果不符合预期，或覆盖尚不可用。");
            if (inject != 0 && (await c.StatusAsync(ct)).TestInhibit != 3)
                throw new IOException("比较失败后未产生影子双路禁止。");
            if (cycle % 100 == 0)
                _log(
                    $"自检 {item} 正常周期 {Math.Min(cycle+1,_selfTestCycles)}/{_selfTestCycles}。");
        }
    }

    public static async Task SaveReportAsync(string folder, C11Report report)
    {
        Directory.CreateDirectory(folder);
        await File.WriteAllTextAsync(Path.Combine(folder, "result.json"), report.ToJson());
        string E(string s) => WebUtility.HtmlEncode(s);
        var html = new StringBuilder(
            "<!doctype html><meta charset='utf-8'><title>C11 " +
            "软件验证报告</title><style>body{font:16px sans-serif;margin:36px}td,th{border:1px " +
            "solid #bbb;padding:8px}table{border-collapse:collapse}</style><h1>C11 " +
            "软件验证报告</h1>");
        html.Append(
            $"<p>{E(report.Overall)} · {E(report.Evidence)} · 清理确认 {report.CleanupConfirmed}</p><p>{E(report.Device)}</p><p>本报告不替代电压/电流精度、MOS物理关断、真实断线、独立参考和保险丝证明试验。</p><table><tr><th>编号</th><th>项目</th><th>结果</th><th>证据</th></tr>");
        foreach (var row in report.Results)
            html.Append(
                $"<tr><td>{E(row.Id)}</td><td>{E(row.Name)}</td><td>{E(row.Outcome.ToString())}</td><td>{E(row.Detail)}</td></tr>");
        html.Append("</table>");
        await File.WriteAllTextAsync(Path.Combine(folder, "report.html"), html.ToString());
    }
}
