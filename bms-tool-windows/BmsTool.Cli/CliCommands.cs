using BmsTool.Windows;
using System.Diagnostics;
using System.Text.Json;

namespace BmsTool.Cli;

internal static class CliCommands
{
    private sealed record ConnectionTestAttempt(
        int Attempt,
        DateTimeOffset StartedUtc,
        bool Success,
        long DurationMs,
        string? Hardware,
        string? Software,
        string? FirmwareBuildId,
        bool? D008,
        string? Error);

    private static readonly JsonSerializerOptions StreamJsonOptions = new(JsonSerializerDefaults.Web);

    public const string HelpText =
@"bms-cli - BMS command-line tool for humans, scripts and AI agents

Usage:
  bms-cli capabilities [--json]
  bms-cli scan [--scan-seconds 4] [--json]
  bms-cli capture (--mac MAC | --serial COMx) --output DIRECTORY
                  [--count 0] [--interval 5] [--reconnect] [--max-reconnects 10] [--json]
  bms-cli info (--mac MAC | --name NAME | --auto | --serial COMx) [--baud 19200] [--json]
  bms-cli soc (--mac MAC | --name NAME | --auto | --serial COMx) [--json]
  bms-cli monitor soc (--mac MAC | --name NAME | --auto | --serial COMx)
                  [--interval 5] [--count 0] [--reconnect] [--max-reconnects 10]
                  [--output monitor.jsonl] [--json]
  bms-cli record soc (--mac MAC | --name NAME | --auto | --serial COMx)
                  --output soc_record.csv [--interval 5] [--count 0] [--json]
  bms-cli health (--mac MAC | --name NAME | --auto | --serial COMx) [--quick] [--output health.zip] [--json]
  bms-cli diag (--mac MAC | --name NAME | --auto | --serial COMx) [--output diag.zip] [--quick] [--json]
  bms-cli test connection (--mac MAC | --name NAME | --auto | --serial COMx)
                  [--count 10] [--delay-ms 500] [--output report.json] [--json]
  bms-cli test soc (--mac MAC | --name NAME | --auto | --serial COMx)
                  [--count 10] [--interval 1] [--output report.json] [--json]
  bms-cli test diag (--mac MAC | --name NAME | --auto | --serial COMx)
                  [--count 3] [--interval 1] [--full] [--output report.json] [--json]
  bms-cli compare <before-diag.zip> <after-diag.zip>
                  [--scope all|identity|configuration|runtime] [--output report.md] [--json]
  bms-cli parameters get (--mac MAC | --name NAME | --auto | --serial COMx) [--json]
  bms-cli parameters export (--mac MAC | --name NAME | --auto | --serial COMx)
                  --output parameters.zip [--json]
  bms-cli ota <firmware.bin> (--mac MAC | --name NAME | --auto | --serial COMx)
              [--target auto|telink|stm32] [--mode auto|legacy|extend64]
              [--expected-version VERSION] [--evidence-dir DIR] [--yes] [--json]

Common:
  --json          stdout contains only the final JSON envelope; suitable for Codex/scripts
  --verbose       protocol/transport diagnostics are written to stderr
  --scan-seconds  BLE scan duration, default 4 seconds
  Ctrl+C          cancel the current operation

Safety:
  --auto only proceeds when exactly one compatible BLE BMS is found.
  OTA in --json mode requires --yes so automation never blocks on an interactive prompt.
";

    public static Task<int> ExecuteAsync(CliOptions options, CliReporter reporter, CancellationToken ct) =>
        options.Command switch
        {
            "capabilities" => CapabilitiesAsync(options, reporter, ct),
            "scan" => ScanAsync(options, reporter, ct),
            "capture" => CliCapture.RunAsync(options, reporter, ct),
            "info" => InfoAsync(options, reporter, ct),
            "soc" => SocAsync(options, reporter, ct),
            "monitor" => MonitorAsync(options, reporter, ct),
            "record" => RecordAsync(options, reporter, ct),
            "health" => HealthAsync(options, reporter, ct),
            "diag" => DiagAsync(options, reporter, ct),
            "test" => TestAsync(options, reporter, ct),
            "compare" => CompareAsync(options, reporter, ct),
            "parameters" => ParametersAsync(options, reporter, ct),
            "ota" => OtaAsync(options, reporter, ct),
            _ => throw new CliException(ExitCodes.Usage, "usage", $"Unknown command '{options.Command}'. Run bms-cli help.")
        };

    private static async Task<int> ScanAsync(CliOptions options, CliReporter reporter, CancellationToken ct)
    {
        int seconds = options.GetInt("scan-seconds", 4, 1, 30);
        IReadOnlyList<DiscoveredDevice> devices = await CliRuntime.ScanAsync(seconds, reporter, ct);
        var data = new
        {
            scanSeconds = seconds,
            count = devices.Count,
            devices = devices.Select(CliRuntime.ToScanRow).ToArray()
        };

        reporter.Success("scan", data);
        if (!reporter.Json)
        {
            if (devices.Count == 0)
            {
                Console.WriteLine("No compatible BMS found.");
            }
            else
            {
                foreach (DiscoveredDevice d in devices)
                    Console.WriteLine($"{BmsBleTransport.FormatBluetoothAddress(d.Address)}  {d.Rssi,4} dBm  {d.Name}");
            }
        }
        return ExitCodes.Success;
    }

    private static async Task<int> InfoAsync(CliOptions options, CliReporter reporter, CancellationToken ct)
    {
        CliEndpoint endpoint = await CliRuntime.ResolveEndpointAsync(options, reporter, ct);
        reporter.Status("Connecting " + endpoint.Display + "...");
        await using CliBmsConnection connection = await CliRuntime.ConnectBmsAsync(endpoint, reporter, ct);
        DeviceSnapshot snapshot = await CliRuntime.ReadSnapshotAsync(connection, ct);

        var data = SnapshotData(snapshot);
        reporter.Success("info", data);
        if (!reporter.Json)
            PrintSnapshot(snapshot);
        return ExitCodes.Success;
    }

    private static async Task<int> SocAsync(CliOptions options, CliReporter reporter, CancellationToken ct)
    {
        CliEndpoint endpoint = await CliRuntime.ResolveEndpointAsync(options, reporter, ct);
        reporter.Status("Connecting " + endpoint.Display + "...");
        await using CliBmsConnection connection = await CliRuntime.ConnectBmsAsync(endpoint, reporter, ct);
        SocDiagnosticSnapshot soc = await ReadSocAsync(connection, endpoint, ct);
        var data = new { endpoint = endpoint.Display, capturedUtc = DateTimeOffset.UtcNow, soc };
        reporter.Success("soc", data);
        if (!reporter.Json) PrintSoc(soc);
        return ExitCodes.Success;
    }

    private static async Task<int> MonitorAsync(CliOptions options, CliReporter reporter, CancellationToken ct)
    {
        if (options.Positionals.Count != 1 ||
            !string.Equals(options.Positionals[0], "soc", StringComparison.OrdinalIgnoreCase))
            throw new CliException(ExitCodes.Usage, "usage", "monitor currently requires the 'soc' subcommand.");
        int interval = options.GetInt("interval", 5, 1, 3600);
        int count = options.GetInt("count", 0, 0, 1000000);
        bool reconnect = options.Has("reconnect");
        int maxReconnects = options.GetInt("max-reconnects", 10, 1, 1000000);
        string? output = options.Get("output");
        string? outputPath = null;
        StreamWriter? evidenceWriter = null;
        if (!string.IsNullOrWhiteSpace(output))
        {
            outputPath = Path.GetFullPath(output);
            string? outputDirectory = Path.GetDirectoryName(outputPath);
            if (!string.IsNullOrWhiteSpace(outputDirectory))
                Directory.CreateDirectory(outputDirectory);
            evidenceWriter = new StreamWriter(outputPath, append: false, new System.Text.UTF8Encoding(false));
        }

        CliEndpoint endpoint = await CliRuntime.ResolveEndpointAsync(options, reporter, ct);
        CliBmsConnection? connection = null;
        int sample = 0;
        int reconnectCount = 0;
        int consecutiveFailures = 0;

        async Task EmitAsync(object row)
        {
            string line = JsonSerializer.Serialize(row, StreamJsonOptions);
            if (reporter.Json) Console.WriteLine(line);
            if (evidenceWriter is not null)
            {
                await evidenceWriter.WriteLineAsync(line.AsMemory(), ct);
                await evidenceWriter.FlushAsync(ct);
            }
        }

        try
        {
            while (count == 0 || sample < count)
            {
                try
                {
                    if (connection is null)
                    {
                        reporter.Status((reconnectCount == 0 ? "Connecting " : "Reconnecting ") + endpoint.Display + "...");
                        connection = await CliRuntime.ConnectBmsAsync(endpoint, reporter, ct);
                    }

                    SocDiagnosticSnapshot soc = await ReadSocAsync(connection, endpoint, ct);
                    sample++;
                    consecutiveFailures = 0;
                    var row = new
                    {
                        schema = 1,
                        ok = true,
                        command = "monitor soc",
                        data = new
                        {
                            endpoint = endpoint.Display,
                            sample,
                            capturedUtc = DateTimeOffset.UtcNow,
                            reconnectCount,
                            soc
                        }
                    };
                    await EmitAsync(row);
                    if (!reporter.Json)
                    {
                        Console.WriteLine($"[{DateTime.Now:HH:mm:ss}] sample {sample} (reconnects {reconnectCount})");
                        PrintSoc(soc);
                    }
                    if (count != 0 && sample >= count) break;
                    await Task.Delay(TimeSpan.FromSeconds(interval), ct);
                }
                catch (OperationCanceledException) when (ct.IsCancellationRequested)
                {
                    throw;
                }
                catch (Exception ex) when (reconnect)
                {
                    reconnectCount++;
                    consecutiveFailures++;
                    var row = new
                    {
                        schema = 1,
                        ok = false,
                        command = "monitor soc",
                        error = new
                        {
                            kind = "sample_error",
                            message = ex.Message,
                            endpoint = endpoint.Display,
                            sample = sample + 1,
                            capturedUtc = DateTimeOffset.UtcNow,
                            reconnectCount,
                            consecutiveFailures
                        }
                    };
                    await EmitAsync(row);
                    reporter.Status($"SOC monitor sample failed: {ex.Message}");

                    if (connection is not null)
                    {
                        await connection.DisposeAsync();
                        connection = null;
                    }
                    if (consecutiveFailures >= maxReconnects)
                        throw new CliException(ExitCodes.ConnectFailed, "reconnect_exhausted",
                            $"SOC monitor failed {consecutiveFailures} consecutive reconnect attempts.",
                            new { endpoint = endpoint.Display, reconnectCount, output = outputPath }, ex);
                    await Task.Delay(TimeSpan.FromSeconds(1), ct);
                }
            }
        }
        catch (OperationCanceledException) when (ct.IsCancellationRequested)
        {
            return ExitCodes.Success;
        }
        finally
        {
            if (connection is not null)
                await connection.DisposeAsync();
            if (evidenceWriter is not null)
                await evidenceWriter.DisposeAsync();
        }
        return ExitCodes.Success;
    }

    private static async Task<SocDiagnosticSnapshot> ReadSocAsync(
        CliBmsConnection connection, CliEndpoint endpoint, CancellationToken ct)
    {
        var result = await ReadSocCaptureAsync(connection, endpoint, ct);
        return result.Soc;
    }

    private static async Task<(DiagnosticCapture Capture, SocDiagnosticSnapshot Soc)> ReadSocCaptureAsync(
        CliBmsConnection connection, CliEndpoint endpoint, CancellationToken ct)
    {
        DiagnosticCapture capture = await connection.Client.ReadDiagnosticsAsync(false, endpoint.Display, ct);
        if (!capture.Supported || capture.Words is null)
            throw new CliException(ExitCodes.ConnectFailed, "soc_diagnostics_unavailable", capture.Status);
        try { return (capture, BmsDiagnostics.DecodeSocSnapshot(capture.Words)); }
        catch (InvalidDataException ex) {
            throw new CliException(ExitCodes.ConnectFailed, "soc_diagnostics_unavailable", ex.Message, inner: ex);
        }
    }

    private static async Task<int> RecordAsync(CliOptions options, CliReporter reporter, CancellationToken ct)
    {
        if (options.Positionals.Count != 1 ||
            !string.Equals(options.Positionals[0], "soc", StringComparison.OrdinalIgnoreCase))
            throw new CliException(ExitCodes.Usage, "usage", "record currently requires the 'soc' subcommand.");
        string output = Path.GetFullPath(options.RequireValue("output"));
        int interval = options.GetInt("interval", 5, 1, 3600);
        int count = options.GetInt("count", 0, 0, 1000000);
        Directory.CreateDirectory(Path.GetDirectoryName(output) ?? ".");
        CliEndpoint endpoint = await CliRuntime.ResolveEndpointAsync(options, reporter, ct);
        reporter.Status("Connecting " + endpoint.Display + "...");
        await using CliBmsConnection connection = await CliRuntime.ConnectBmsAsync(endpoint, reporter, ct);
        int samples = 0;
        await using var writer = new StreamWriter(output, false, new System.Text.UTF8Encoding(false));
        await writer.WriteLineAsync(SocRecord.CsvHeader);
        try
        {
            while (count == 0 || samples < count)
            {
                var result = await ReadSocCaptureAsync(connection, endpoint, ct);
                BatterySnapshot battery = await connection.Client.ReadBatteryAsync(ct);
                await writer.WriteLineAsync(SocRecord.From(result.Capture, result.Soc, battery).ToCsvLine());
                await writer.FlushAsync(ct);
                samples++;
                reporter.Status($"SOC record {samples}: est={result.Soc.SocEstimate}% display={result.Soc.SocDisplay}%");
                if (count != 0 && samples >= count) break;
                await Task.Delay(TimeSpan.FromSeconds(interval), ct);
            }
        }
        catch (OperationCanceledException) when (ct.IsCancellationRequested) { }
        reporter.Success("record soc", new { endpoint = endpoint.Display, output, samples });
        if (!reporter.Json) Console.WriteLine($"SOC record saved: {output} ({samples} samples)");
        return ExitCodes.Success;
    }

    private static void PrintSoc(SocDiagnosticSnapshot s)
    {
        Console.WriteLine($"SOC:       estimate {s.SocEstimate}% / display {s.SocDisplay}% / endpoint {s.EndpointState}");
        Console.WriteLine($"Capacity:  remaining {s.RemainingCapacityAh:F1} Ah / effective {s.EffectiveCapacityAh:F1} Ah / nominal {s.NominalCapacityAh:F1} Ah");
        Console.WriteLine($"Profile:   {s.Chemistry} / {s.ProfileId} v{s.ProfileVersion}; OCV {s.OcvCenter}% [{s.OcvLow}..{s.OcvHigh}] confidence {s.OcvConfidence}%");
        Console.WriteLine($"Current:   filtered {s.FilteredCurrentMa} mA / variation {s.CurrentVariationMa} mA");
        Console.WriteLine($"ETA:       {s.EtaState} {s.EtaDirection} confidence {s.EtaConfidence}% / TTE {s.TimeToEmptyMinutes?.ToString() ?? "unavailable"} min / TTF {s.TimeToFullMinutes?.ToString() ?? "unavailable"} min");
        Console.WriteLine($"SOH:       {s.Soh}% / {s.SohSource} / confidence {s.SohConfidence}%");
        Console.WriteLine($"Learning:  enabled={s.CapacityLearningEnable}, state={s.LearningState}, candidate={s.CandidateCapacityAh:F1} Ah, accepted={s.LearnedCapacityAh:F1} Ah, confidence={s.LearningConfidence}%");
        Console.WriteLine($"Rejects:   valid {s.ValidLearningCount} / rejected {s.RejectedLearningCount} / last {s.LastLearningRejectReason}");
    }

    private static async Task<int> DiagAsync(CliOptions options, CliReporter reporter, CancellationToken ct)
    {
        CliEndpoint endpoint = await CliRuntime.ResolveEndpointAsync(options, reporter, ct);
        reporter.Status("Connecting " + endpoint.Display + "...");
        await using CliBmsConnection connection = await CliRuntime.ConnectBmsAsync(endpoint, reporter, ct);

        bool full = !options.Has("quick");
        reporter.Status(full ? "Collecting full diagnostic snapshot..." : "Collecting quick diagnostic snapshot...");
        DiagnosticCapture capture = await connection.Client.ReadDiagnosticsAsync(full, endpoint.Display, ct);
        DeviceIdentity? healthIdentity = null;
        if (capture.Identity.Count != 0)
        {
            capture.Identity.TryGetValue("Serial", out string? serial);
            capture.Identity.TryGetValue("Hardware", out string? hardware);
            capture.Identity.TryGetValue("Software", out string? software);
            healthIdentity = new DeviceIdentity(endpoint.Display, serial ?? "", hardware ?? "", software ?? "", "");
        }
        BmsHealthReport health = BmsHealth.Evaluate(capture, healthIdentity);

        string? output = options.Get("output");
        string? outputPath = null;
        if (!string.IsNullOrWhiteSpace(output))
        {
            outputPath = Path.GetFullPath(output);
            string? dir = Path.GetDirectoryName(outputPath);
            if (!string.IsNullOrWhiteSpace(dir))
                Directory.CreateDirectory(dir);
            BmsDiagnostics.Export(outputPath, capture, health);
        }

        uint? buildId = null;
        if (capture.Words is { Length: >= 24 } words)
        {
            uint rawBuildId = BmsDiagnostics.U32(words, 22);
            if (rawBuildId != 0) buildId = rawBuildId;
        }

        var data = new
        {
            endpoint = endpoint.Display,
            full,
            capture.Status,
            capture.Supported,
            capture.SnapshotConsistent,
            capture.TraceConsistent,
            firmwareBuildId = buildId?.ToString("x8"),
            capture.Identity,
            capture.Current,
            capture.Soc,
            capture.Power,
            capture.Protection,
            capture.Mos,
            capture.Boot,
            capture.Storage,
            capture.Trace,
            capture.Errors,
            health,
            evidence = capture.EvidenceBlocks,
            rawFrames = capture.Frames,
            output = outputPath
        };

        reporter.Success("diag", data);
        if (!reporter.Json)
        {
            Console.WriteLine($"Status: {capture.Status}");
            Console.WriteLine($"Supported: {capture.Supported}");
            Console.WriteLine($"Snapshot consistent: {capture.SnapshotConsistent}");
            Console.WriteLine($"Trace consistent: {capture.TraceConsistent}");
            if (buildId.HasValue) Console.WriteLine($"Build ID: {buildId.Value:x8}");
            foreach (DiagnosticField f in capture.Current) Console.WriteLine($"Current/{f.Field}: {f.Value}");
            foreach (DiagnosticField f in capture.Soc) Console.WriteLine($"SOC/{f.Field}: {f.Value}");
            foreach (DiagnosticField f in capture.Power) Console.WriteLine($"Power/{f.Field}: {f.Value}");
            foreach (DiagnosticField f in capture.Protection) Console.WriteLine($"Protection/{f.Field}: {f.Value}");
            foreach (string e in capture.Errors) Console.WriteLine("WARN: " + e);
            if (outputPath is not null) Console.WriteLine("Diagnostic bundle: " + outputPath);
        }
        return ExitCodes.Success;
    }

    private static async Task<int> HealthAsync(CliOptions options, CliReporter reporter, CancellationToken ct)
    {
        CliEndpoint endpoint = await CliRuntime.ResolveEndpointAsync(options, reporter, ct);
        reporter.Status("Connecting " + endpoint.Display + "...");
        await using CliBmsConnection connection = await CliRuntime.ConnectBmsAsync(endpoint, reporter, ct);
        DeviceSnapshot snapshot = await CliRuntime.ReadSnapshotAsync(connection, ct);
        bool full = !options.Has("quick");
        reporter.Status(full ? "Collecting full health evidence..." : "Collecting quick health evidence...");
        DiagnosticCapture capture = await connection.Client.ReadDiagnosticsAsync(full, endpoint.Display, ct);
        BmsHealthReport health = BmsHealth.Evaluate(capture, snapshot.Identity, snapshot.Battery);

        string? output = options.Get("output");
        string? outputPath = null;
        if (!string.IsNullOrWhiteSpace(output))
        {
            outputPath = Path.GetFullPath(output);
            string? dir = Path.GetDirectoryName(outputPath);
            if (!string.IsNullOrWhiteSpace(dir)) Directory.CreateDirectory(dir);
            BmsDiagnostics.Export(outputPath, capture, health);
        }

        var data = new
        {
            endpoint = endpoint.Display,
            full,
            identity = snapshot.Identity,
            firmwareBuildId = snapshot.FirmwareBuildId?.ToString("x8"),
            d008 = snapshot.IsD008,
            battery = snapshot.Battery,
            health,
            capture.SnapshotConsistent,
            capture.TraceConsistent,
            capture.Errors,
            output = outputPath
        };

        reporter.Success("health", data);
        if (!reporter.Json)
        {
            Console.WriteLine($"Health: {health.Overall} - {health.Summary}");
            foreach (BmsHealthCheck check in health.Checks)
                Console.WriteLine($"[{check.Status.ToUpperInvariant()}] {check.Category}/{check.Title}: {check.Evidence}");
            if (outputPath is not null) Console.WriteLine("Health bundle: " + outputPath);
        }
        return ExitCodes.Success;
    }

    private static Task<int> CapabilitiesAsync(CliOptions options, CliReporter reporter, CancellationToken ct)
    {
        ct.ThrowIfCancellationRequested();
        if (options.Positionals.Count != 0)
            throw new CliException(ExitCodes.Usage, "usage", "capabilities does not accept positional arguments.");
        var data = new
        {
            schema = ProductSupportMatrix.Schema,
            source = "client_support_catalog",
            deviceProbed = false,
            products = ProductSupportMatrix.Products
        };
        reporter.Success("capabilities", data);
        if (!reporter.Json)
        {
            foreach (ProductSupportProfile product in ProductSupportMatrix.Products)
            {
                string parameters = product.D008ParametersVersion is int version ? $"v{version}" : "n/a";
                Console.WriteLine($"{product.Product}: {product.Afe} {product.AfeModel}; diagnostics v{product.DiagnosticsSchema}; runtime v{product.RuntimeVersion}; AFE HW V2={product.AfeHardwareV2}; D008 params={parameters}");
                Console.WriteLine("  Software: " + product.SoftwareEvidence);
                Console.WriteLine("  Hardware: " + product.HardwareEvidence);
            }
        }
        return Task.FromResult(ExitCodes.Success);
    }

    private static async Task<int> TestAsync(CliOptions options, CliReporter reporter, CancellationToken ct)
    {
        if (options.Positionals.Count != 1)
            throw new CliException(ExitCodes.Usage, "usage", "test requires connection, soc or diag.");

        string test = options.Positionals[0].ToLowerInvariant();
        if (test == "soc" || test == "diag")
            return await RunSharedTestAsync(test, options, reporter, ct);
        if (test != "connection")
            throw new CliException(ExitCodes.Usage, "usage", "test requires connection, soc or diag.");

        int count = options.GetInt("count", 10, 1, 1000);
        int delayMs = options.GetInt("delay-ms", 500, 0, 60000);
        CliEndpoint endpoint = await CliRuntime.ResolveEndpointAsync(options, reporter, ct);
        var attempts = new List<ConnectionTestAttempt>(count);
        DateTimeOffset startedUtc = DateTimeOffset.UtcNow;

        for (int attempt = 1; attempt <= count; attempt++)
        {
            ct.ThrowIfCancellationRequested();
            reporter.Status($"Connection test {attempt}/{count}: {endpoint.Display}");
            DateTimeOffset attemptStartedUtc = DateTimeOffset.UtcNow;
            var timer = Stopwatch.StartNew();
            try
            {
                await using CliBmsConnection connection = await CliRuntime.ConnectBmsAsync(endpoint, reporter, ct);
                DeviceSnapshot snapshot = await CliRuntime.ReadSnapshotAsync(connection, ct);
                timer.Stop();
                attempts.Add(new ConnectionTestAttempt(attempt, attemptStartedUtc, true, timer.ElapsedMilliseconds,
                    snapshot.Identity.Hardware, snapshot.Identity.Software,
                    snapshot.FirmwareBuildId?.ToString("x8"), snapshot.IsD008, null));
            }
            catch (OperationCanceledException) { throw; }
            catch (Exception ex)
            {
                timer.Stop();
                attempts.Add(new ConnectionTestAttempt(attempt, attemptStartedUtc, false, timer.ElapsedMilliseconds,
                    null, null, null, null, ex.GetType().Name + ": " + ex.Message));
            }

            if (attempt < count && delayMs != 0)
                await Task.Delay(delayMs, ct);
        }

        int succeeded = attempts.Count(x => x.Success);
        int failed = attempts.Count - succeeded;
        long[] successfulDurations = attempts.Where(x => x.Success).Select(x => x.DurationMs).Order().ToArray();
        double? averageMs = successfulDurations.Length == 0 ? null : successfulDurations.Average();
        DateTimeOffset finishedUtc = DateTimeOffset.UtcNow;
        var report = new
        {
            schema = 1,
            test = "connection",
            endpoint = endpoint.Display,
            startedUtc,
            finishedUtc,
            durationMs = (long)(finishedUtc - startedUtc).TotalMilliseconds,
            count,
            delayMs,
            succeeded,
            failed,
            passed = failed == 0,
            successRatePercent = Math.Round(succeeded * 100.0 / count, 2),
            averageSuccessDurationMs = averageMs is null ? (double?)null : Math.Round(averageMs.Value, 1),
            minSuccessDurationMs = successfulDurations.Length == 0 ? (long?)null : successfulDurations[0],
            p50SuccessDurationMs = Percentile(successfulDurations, 0.50),
            p95SuccessDurationMs = Percentile(successfulDurations, 0.95),
            maxSuccessDurationMs = successfulDurations.Length == 0 ? (long?)null : successfulDurations[^1],
            failureKinds = attempts.Where(x => !x.Success)
                .GroupBy(x => x.Error?.Split(':', 2)[0] ?? "Unknown", StringComparer.Ordinal)
                .ToDictionary(x => x.Key, x => x.Count(), StringComparer.Ordinal),
            attempts
        };
        string? outputPath = SaveJsonOutput(options.Get("output"), report);
        var data = new
        {
            report.schema,
            report.test,
            report.endpoint,
            report.startedUtc,
            report.finishedUtc,
            report.durationMs,
            report.count,
            report.delayMs,
            report.succeeded,
            report.failed,
            report.passed,
            report.successRatePercent,
            report.averageSuccessDurationMs,
            report.minSuccessDurationMs,
            report.p50SuccessDurationMs,
            report.p95SuccessDurationMs,
            report.maxSuccessDurationMs,
            report.failureKinds,
            report.attempts,
            output = outputPath
        };
        reporter.Success("test connection", data);
        if (!reporter.Json)
        {
            foreach (ConnectionTestAttempt attempt in attempts)
                Console.WriteLine($"#{attempt.Attempt}: {(attempt.Success ? "PASS" : "FAIL")} {attempt.DurationMs} ms" +
                    (attempt.Error is null ? $" {attempt.Hardware}/{attempt.Software} {attempt.FirmwareBuildId}" : " " + attempt.Error));
            Console.WriteLine($"Connection test: {succeeded}/{count} passed, success rate {succeeded * 100.0 / count:F2}%");
            if (successfulDurations.Length != 0)
                Console.WriteLine($"Latency: min {successfulDurations[0]} ms / P50 {Percentile(successfulDurations, 0.50)} ms / P95 {Percentile(successfulDurations, 0.95)} ms / max {successfulDurations[^1]} ms");
            if (outputPath is not null) Console.WriteLine("Connection test report: " + outputPath);
        }
        return failed == 0 ? ExitCodes.Success : ExitCodes.TestFailed;
    }

    private static long? Percentile(long[] sortedValues, double percentile)
    {
        if (sortedValues.Length == 0) return null;
        int index = (int)Math.Ceiling(percentile * sortedValues.Length) - 1;
        return sortedValues[Math.Clamp(index, 0, sortedValues.Length - 1)];
    }

    private static async Task<int> RunSharedTestAsync(
        string test, CliOptions options, CliReporter reporter, CancellationToken ct)
    {
        int defaultCount = test == "soc" ? 10 : 3;
        int count = options.GetInt("count", defaultCount, 1, 1000);
        int interval = options.GetInt("interval", 1, 0, 3600);
        CliEndpoint endpoint = await CliRuntime.ResolveEndpointAsync(options, reporter, ct);
        reporter.Status("Connecting " + endpoint.Display + "...");
        await using CliBmsConnection connection = await CliRuntime.ConnectBmsAsync(endpoint, reporter, ct);
        BmsTestReport report;
        if (test == "soc")
        {
            reporter.Status($"Running SOC diagnostics test: {count} sample(s)...");
            report = await BmsTestEngine.RunSocAsync(connection.Client, endpoint.Display, count,
                TimeSpan.FromSeconds(interval), ct);
        }
        else
        {
            bool full = options.Has("full");
            reporter.Status($"Running {(full ? "full" : "quick")} diagnostics test: {count} capture(s)...");
            report = await BmsTestEngine.RunDiagnosticsAsync(connection.Client, endpoint.Display, count,
                full, TimeSpan.FromSeconds(interval), ct);
        }

        string? outputPath = SaveJsonOutput(options.Get("output"), report);
        var data = new { endpoint = endpoint.Display, report, output = outputPath };
        reporter.Success("test " + test, data);
        if (!reporter.Json)
        {
            Console.WriteLine(report.Summary);
            foreach (BmsTestCheck check in report.Checks)
                Console.WriteLine($"[{check.Status.ToUpperInvariant()}] {check.Title}: {check.Evidence}");
            if (outputPath is not null) Console.WriteLine("Test report: " + outputPath);
        }
        return report.Passed ? ExitCodes.Success : ExitCodes.TestFailed;
    }

    private static Task<int> CompareAsync(CliOptions options, CliReporter reporter, CancellationToken ct)
    {
        ct.ThrowIfCancellationRequested();
        if (options.Positionals.Count != 2)
            throw new CliException(ExitCodes.Usage, "usage", "compare requires before and after diagnostic ZIP paths.");
        string before = Path.GetFullPath(options.Positionals[0]);
        string after = Path.GetFullPath(options.Positionals[1]);
        if (!File.Exists(before) || !File.Exists(after))
            throw new CliException(ExitCodes.Usage, "bundle_not_found", "Both diagnostic ZIP files must exist.");
        string scope = (options.Get("scope") ?? "all").Trim().ToLowerInvariant();
        DiagnosticBundleComparison comparison;
        try
        {
            comparison = DiagnosticBundleComparer.Filter(
                DiagnosticBundleComparer.Compare(before, after), scope);
        }
        catch (Exception ex) when (ex is InvalidDataException or IOException)
        {
            throw new CliException(ExitCodes.Usage, "bundle_invalid", ex.Message, inner: ex);
        }
        catch (ArgumentOutOfRangeException ex)
        {
            throw new CliException(ExitCodes.Usage, "usage", ex.Message, inner: ex);
        }
        string? outputPath = SaveComparisonMarkdown(options.Get("output"), comparison, scope);
        reporter.Success("compare", new
        {
            comparison.BeforePath,
            comparison.AfterPath,
            comparison.DifferenceCount,
            comparison.Differences,
            comparison.CategoryCounts,
            scope,
            output = outputPath
        });
        if (!reporter.Json)
        {
            Console.WriteLine($"Diagnostic differences ({scope}): {comparison.DifferenceCount}");
            foreach (DiagnosticBundleDifference difference in comparison.Differences)
                Console.WriteLine($"[{difference.Category}] {difference.Entry} {difference.Path}: {difference.Before ?? "<missing>"} -> {difference.After ?? "<missing>"}");
            if (outputPath is not null) Console.WriteLine("Comparison report: " + outputPath);
        }
        return Task.FromResult(ExitCodes.Success);
    }

    private static string? SaveComparisonMarkdown(
        string? output,
        DiagnosticBundleComparison comparison,
        string scope)
    {
        if (string.IsNullOrWhiteSpace(output)) return null;
        string path = Path.GetFullPath(output);
        string? directory = Path.GetDirectoryName(path);
        if (!string.IsNullOrWhiteSpace(directory)) Directory.CreateDirectory(directory);
        var lines = new List<string>
        {
            "# BMS diagnostic comparison",
            "",
            $"- Scope: `{scope}`",
            $"- Before: `{comparison.BeforePath}`",
            $"- After: `{comparison.AfterPath}`",
            $"- Differences: {comparison.DifferenceCount}",
            ""
        };
        foreach (IGrouping<string, DiagnosticBundleDifference> group in
                 comparison.Differences.GroupBy(x => x.Category, StringComparer.OrdinalIgnoreCase))
        {
            lines.Add("## " + group.Key);
            lines.Add("");
            lines.Add("| Entry | Path | Before | After |");
            lines.Add("|---|---|---|---|");
            foreach (DiagnosticBundleDifference difference in group)
                lines.Add($"| {MarkdownCell(difference.Entry)} | {MarkdownCell(difference.Path)} | {MarkdownCell(difference.Before)} | {MarkdownCell(difference.After)} |");
            lines.Add("");
        }
        if (comparison.DifferenceCount == 0)
            lines.Add("No differences in the selected scope.");
        File.WriteAllLines(path, lines, new System.Text.UTF8Encoding(false));
        return path;
    }

    private static string MarkdownCell(string? value) =>
        (value ?? "<missing>").Replace("|", "\\|").Replace("\r", " ").Replace("\n", " ");

    private static async Task<int> ParametersAsync(CliOptions options, CliReporter reporter, CancellationToken ct)
    {
        if (options.Positionals.Count != 1 ||
            options.Positionals[0].ToLowerInvariant() is not ("get" or "export"))
            throw new CliException(ExitCodes.Usage, "usage", "parameters requires get or export.");
        string action = options.Positionals[0].ToLowerInvariant();
        CliEndpoint endpoint = await CliRuntime.ResolveEndpointAsync(options, reporter, ct);
        reporter.Status("Connecting " + endpoint.Display + "...");
        await using CliBmsConnection connection = await CliRuntime.ConnectBmsAsync(endpoint, reporter, ct);
        D008ParameterCapture capture = await connection.Client.ReadD008ParametersAsync(ct);
        string? outputPath = null;
        if (action == "export")
        {
            string output = options.RequireValue("output");
            outputPath = Path.GetFullPath(output);
            string? directory = Path.GetDirectoryName(outputPath);
            if (!string.IsNullOrWhiteSpace(directory)) Directory.CreateDirectory(directory);
            D008Parameters.Export(outputPath, capture);
        }
        var data = new
        {
            endpoint = endpoint.Display,
            capture.Status,
            capture.Supported,
            capture.ProtocolVersion,
            capture.Blocks,
            capture.Errors,
            output = outputPath
        };
        reporter.Success("parameters " + action, data);
        if (!reporter.Json)
        {
            Console.WriteLine(capture.Status);
            Console.WriteLine($"Protocol v{capture.ProtocolVersion}; supported={capture.Supported}; blocks={capture.Blocks.Count}; errors={capture.Errors.Count}");
            foreach (string error in capture.Errors) Console.WriteLine("WARN: " + error);
            if (outputPath is not null) Console.WriteLine("Parameter bundle: " + outputPath);
        }
        return capture.Blocks.Count == 0 ? ExitCodes.ConnectFailed : ExitCodes.Success;
    }

    private static string? SaveJsonOutput(string? output, object value)
    {
        if (string.IsNullOrWhiteSpace(output)) return null;
        string path = Path.GetFullPath(output);
        string? directory = Path.GetDirectoryName(path);
        if (!string.IsNullOrWhiteSpace(directory)) Directory.CreateDirectory(directory);
        File.WriteAllText(path, JsonSerializer.Serialize(value, new JsonSerializerOptions(JsonSerializerDefaults.Web)
        {
            WriteIndented = true
        }));
        return path;
    }

    private static async Task<int> OtaAsync(CliOptions options, CliReporter reporter, CancellationToken ct)
    {
        if (options.Positionals.Count != 1)
            throw new CliException(ExitCodes.Usage, "usage", "ota requires exactly one firmware .bin path.");

        string firmwarePath = Path.GetFullPath(options.Positionals[0]);
        if (!File.Exists(firmwarePath))
            throw new CliException(ExitCodes.FirmwareInvalid, "firmware_not_found", $"Firmware file not found: {firmwarePath}");

        CliEndpoint endpoint = await CliRuntime.ResolveEndpointAsync(options, reporter, ct);
        DeviceSnapshot? pre = await CliRuntime.TryReadSnapshotAsync(endpoint, reporter, ct);

        OtaTargetKind requestedTarget = ParseTarget(options.Get("target"));
        OtaTargetKind target;
        if (endpoint.IsSerial)
        {
            if (requestedTarget == OtaTargetKind.Telink)
                throw new CliException(ExitCodes.Usage, "usage", "Telink OTA requires BLE; --serial can only use STM32 serial IAP.");
            target = OtaTargetKind.Stm32SerialIap;
        }
        else
        {
            try
            {
                target = await OtaTargetDetector.DetectAsync(endpoint.Address!.Value, requestedTarget, ct);
            }
            catch (Exception ex) when (ex is not OperationCanceledException)
            {
                throw new CliException(ExitCodes.ConnectFailed, "ota_target_detection_failed", ex.Message, inner: ex);
            }
        }

        FirmwareImage image;
        try
        {
            image = FirmwareImage.LoadForTarget(firmwarePath, target);
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            throw new CliException(ExitCodes.FirmwareInvalid, "firmware_invalid", ex.Message, inner: ex);
        }

        if (target == OtaTargetKind.Telink && pre?.IsD008 == true)
        {
            if (!image.HasD008TlnkStartupMarker)
                throw new CliException(ExitCodes.ProductMismatch, "d008_marker_missing",
                    "Target is D008 but firmware does not contain the current D008 TLNK startup marker at 0x08.");
            if (image.ImageSize > TelinkOtaProtocol.D008DefaultMaxFirmwareBytes)
                throw new CliException(ExitCodes.FirmwareTooLarge, "firmware_too_large",
                    $"D008 firmware is {image.ImageSize:N0} bytes; current Telink OTA Server default limit is 124 KiB.");
        }

        OtaTransferMode mode = ParseMode(options.Get("mode"));
        string? expectedVersion = options.Get("expected-version");

        var preflight = new
        {
            target = endpoint.Display,
            architecture = target.ToString(),
            mode = mode.ToString(),
            firmware = Path.GetFileName(firmwarePath),
            bytes = image.ImageSize,
            d008 = pre?.IsD008,
            tlnkMarker = image.HasD008TlnkStartupMarker,
            oldVersion = pre?.Identity.Software,
            oldBuildId = pre?.FirmwareBuildId?.ToString("x8"),
            packVoltageV = pre?.Battery.PackVoltageV,
            minCellMv = pre?.Battery.MinCellMv
        };

        if (!options.Has("yes"))
        {
            if (reporter.Json)
                throw new CliException(ExitCodes.Usage, "confirmation_required", "OTA with --json requires --yes.", preflight);

            Console.WriteLine($"Target:       {preflight.target}");
            Console.WriteLine($"Architecture: {preflight.architecture}");
            Console.WriteLine($"Mode:         {preflight.mode}");
            Console.WriteLine($"Firmware:     {preflight.firmware} ({preflight.bytes:N0} bytes)");
            Console.WriteLine($"Old version:  {preflight.oldVersion ?? "unknown"}");
            Console.WriteLine($"Old Build ID: {preflight.oldBuildId ?? "unknown"}");
            if (pre?.Battery is not null)
                Console.WriteLine($"Battery:      {pre.Battery.PackVoltageV:F2} V / min cell {pre.Battery.MinCellMv} mV");
            Console.Write("Proceed with OTA? [y/N] ");
            string answer = Console.ReadLine()?.Trim() ?? string.Empty;
            if (!string.Equals(answer, "y", StringComparison.OrdinalIgnoreCase) &&
                !string.Equals(answer, "yes", StringComparison.OrdinalIgnoreCase))
                throw new OperationCanceledException(ct);
        }

        string? evidenceDirectory = options.Get("evidence-dir");
        if (!string.IsNullOrWhiteSpace(evidenceDirectory))
        {
            evidenceDirectory = Path.GetFullPath(evidenceDirectory);
            Directory.CreateDirectory(evidenceDirectory);
            WriteJsonFile(Path.Combine(evidenceDirectory, "firmware.json"), new
            {
                capturedUtc = DateTimeOffset.UtcNow,
                path = firmwarePath,
                file = Path.GetFileName(firmwarePath),
                bytes = image.ImageSize,
                sha256 = Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(firmwarePath))).ToLowerInvariant(),
                image.HasD008TlnkStartupMarker,
                architecture = target.ToString(),
                mode = mode.ToString()
            });
            if (pre is not null)
                WriteJsonFile(Path.Combine(evidenceDirectory, "before-snapshot.json"), SnapshotData(pre));
            await CollectOtaEvidenceAsync(endpoint, pre, evidenceDirectory, "before", reporter, ct);
        }

        reporter.Status($"Starting OTA: {Path.GetFileName(firmwarePath)} -> {endpoint.Display}");
        bool serverConfirmed;
        try
        {
            serverConfirmed = await RunOtaWithFallbackAsync(endpoint, image, target, mode, reporter, ct);
        }
        catch (CliException) { throw; }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            throw new CliException(ExitCodes.OtaFailed, "ota_failed", ex.Message, preflight, ex);
        }

        DeviceSnapshot post = await CliRuntime.VerifyAfterOtaAsync(endpoint, reporter, ct);
        if (evidenceDirectory is not null)
        {
            WriteJsonFile(Path.Combine(evidenceDirectory, "after-snapshot.json"), SnapshotData(post));
            await CollectOtaEvidenceAsync(endpoint, post, evidenceDirectory, "after", reporter, ct);
        }

        if (!string.IsNullOrWhiteSpace(expectedVersion) &&
            !string.Equals(post.Identity.Software, expectedVersion, StringComparison.OrdinalIgnoreCase))
        {
            throw new CliException(
                ExitCodes.VersionMismatch,
                "version_mismatch",
                $"Device restarted but software version is '{post.Identity.Software}', expected '{expectedVersion}'.",
                new
                {
                    expectedVersion,
                    actualVersion = post.Identity.Software,
                    actualBuildId = post.FirmwareBuildId?.ToString("x8"),
                    serverConfirmed
                });
        }

        bool versionChanged =
            pre is not null &&
            CliRuntime.IsKnownVersion(pre.Identity.Software) &&
            CliRuntime.IsKnownVersion(post.Identity.Software) &&
            !string.Equals(pre.Identity.Software, post.Identity.Software, StringComparison.OrdinalIgnoreCase);
        bool buildChanged =
            pre?.FirmwareBuildId is uint oldBuild &&
            post.FirmwareBuildId is uint newBuild &&
            oldBuild != newBuild;

        var result = new
        {
            status = serverConfirmed || versionChanged || buildChanged ? "success" : "unconfirmed",
            serverConfirmed,
            architecture = target.ToString(),
            mode = mode.ToString(),
            firmware = Path.GetFileName(firmwarePath),
            bytes = image.ImageSize,
            oldVersion = pre?.Identity.Software,
            newVersion = post.Identity.Software,
            oldBuildId = pre?.FirmwareBuildId?.ToString("x8"),
            newBuildId = post.FirmwareBuildId?.ToString("x8"),
            versionChanged,
            buildChanged,
            d008 = post.IsD008,
            evidenceDirectory,
            battery = new
            {
                post.Battery.PackVoltageV,
                post.Battery.MinCellMv,
                post.Battery.SocPercent
            }
        };

        if (!(serverConfirmed || versionChanged || buildChanged))
        {
            if (evidenceDirectory is not null)
                WriteJsonFile(Path.Combine(evidenceDirectory, "ota-result.json"), result);
            throw new CliException(
                ExitCodes.OtaUnconfirmed,
                "ota_unconfirmed",
                "Device returned to normal communication, but there is no positive evidence that new firmware started. OTA_SUCCESS was not received and version/Build ID did not change.",
                result);
        }

        if (evidenceDirectory is not null)
            WriteJsonFile(Path.Combine(evidenceDirectory, "ota-result.json"), result);

        reporter.Success("ota", result);
        if (!reporter.Json)
        {
            Console.WriteLine("OTA SUCCESS");
            Console.WriteLine($"Version:  {pre?.Identity.Software ?? "unknown"} -> {post.Identity.Software}");
            Console.WriteLine($"Build ID: {pre?.FirmwareBuildId?.ToString("x8") ?? "unknown"} -> {post.FirmwareBuildId?.ToString("x8") ?? "unknown"}");
            Console.WriteLine($"Evidence: server={serverConfirmed}, versionChanged={versionChanged}, buildChanged={buildChanged}");
        }
        return ExitCodes.Success;
    }

    private static async Task CollectOtaEvidenceAsync(
        CliEndpoint endpoint,
        DeviceSnapshot? snapshot,
        string directory,
        string prefix,
        CliReporter reporter,
        CancellationToken ct)
    {
        var errors = new List<string>();
        try
        {
            await using CliBmsConnection connection = await CliRuntime.ConnectBmsAsync(endpoint, reporter, ct);
            try
            {
                DiagnosticCapture capture = await connection.Client.ReadDiagnosticsAsync(true, endpoint.Display, ct);
                BmsHealthReport health = BmsHealth.Evaluate(capture, snapshot?.Identity, snapshot?.Battery);
                BmsDiagnostics.Export(Path.Combine(directory, prefix + "-diag.zip"), capture, health);
            }
            catch (OperationCanceledException) { throw; }
            catch (Exception ex)
            {
                errors.Add("diagnostics: " + ex.GetType().Name + ": " + ex.Message);
            }

            try
            {
                D008ParameterCapture parameters = await connection.Client.ReadD008ParametersAsync(ct);
                D008Parameters.Export(Path.Combine(directory, prefix + "-parameters.zip"), parameters);
            }
            catch (OperationCanceledException) { throw; }
            catch (Exception ex)
            {
                errors.Add("parameters: " + ex.GetType().Name + ": " + ex.Message);
            }
        }
        catch (OperationCanceledException) { throw; }
        catch (Exception ex)
        {
            errors.Add("connection: " + ex.GetType().Name + ": " + ex.Message);
        }

        WriteJsonFile(Path.Combine(directory, prefix + "-evidence-status.json"), new
        {
            capturedUtc = DateTimeOffset.UtcNow,
            ok = errors.Count == 0,
            errors
        });
    }

    private static void WriteJsonFile(string path, object value)
    {
        File.WriteAllText(path, JsonSerializer.Serialize(value, new JsonSerializerOptions(JsonSerializerDefaults.Web)
        {
            WriteIndented = true
        }), new System.Text.UTF8Encoding(false));
    }

    private static async Task<bool> RunOtaWithFallbackAsync(
        CliEndpoint endpoint,
        FirmwareImage image,
        OtaTargetKind target,
        OtaTransferMode requested,
        CliReporter reporter,
        CancellationToken ct)
    {
        try
        {
            return await RunOtaOnceAsync(endpoint, image, target, requested, reporter, ct);
        }
        catch (Exception ex) when (
            requested == OtaTransferMode.Auto &&
            IsExtendCompatibilityFailure(ex) &&
            !ct.IsCancellationRequested)
        {
            reporter.Status("Extended OTA not compatible; retrying with Legacy 16B...");
            await Task.Delay(700, ct);
            return await RunOtaOnceAsync(endpoint, image, target, OtaTransferMode.LegacyFast, reporter, ct);
        }
    }

    private static async Task<bool> RunOtaOnceAsync(
        CliEndpoint endpoint,
        FirmwareImage image,
        OtaTargetKind target,
        OtaTransferMode mode,
        CliReporter reporter,
        CancellationToken ct)
    {
        if (target == OtaTargetKind.Stm32SerialIap)
        {
            if (endpoint.IsSerial)
            {
                await using var transport = new BmsSerialTransport();
                transport.ConnectionProgress += reporter.VerboseLog;
                await transport.ConnectAsync(endpoint.PortName!, endpoint.BaudRate, ct);
                var client = new Stm32SerialBleOtaClient(transport, chunkForBle: false);
                client.Log += reporter.VerboseLog;
                client.Progress += p => reporter.Progress($"STM32 IAP {p.Percent:F1}% page {p.PageIndex}/{p.PageCount}");
                return await client.UpgradeAsync(image, ct);
            }

            await using var ble = new BmsBleTransport();
            ble.ConnectionProgress += reporter.VerboseLog;
            await ble.ConnectAsync(endpoint.Address!.Value, ct);
            var bleClient = new Stm32SerialBleOtaClient(ble);
            bleClient.Log += reporter.VerboseLog;
            bleClient.Progress += p => reporter.Progress($"STM32 IAP {p.Percent:F1}% page {p.PageIndex}/{p.PageCount}");
            return await bleClient.UpgradeAsync(image, ct);
        }

        await using var ota = new OtaBleTransport();
        await ota.ConnectAsync(endpoint.Address!.Value, ct);
        var telink = new TelinkOtaClient(ota);
        telink.Log += reporter.VerboseLog;
        telink.Progress += p =>
        {
            string eta = p.Eta is null ? string.Empty : $" ETA {p.Eta.Value.TotalSeconds:F1}s";
            reporter.Progress($"Telink {p.Mode} {p.Percent:F1}% {p.BytesPerSecond / 1024.0:F1} KiB/s{eta}");
        };
        return await telink.UpgradeAsync(image, mode, ct);
    }

    private static OtaTransferMode ParseMode(string? value) =>
        (value ?? "auto").Trim().ToLowerInvariant() switch
        {
            "auto" => OtaTransferMode.Auto,
            "legacy" or "legacyfast" or "16" or "16b" => OtaTransferMode.LegacyFast,
            "extend64" or "extended64" or "64" or "64b" => OtaTransferMode.Extend64,
            _ => throw new CliException(ExitCodes.Usage, "usage", $"Unknown OTA mode '{value}'. Use auto, legacy or extend64.")
        };

    private static OtaTargetKind ParseTarget(string? value) =>
        (value ?? "auto").Trim().ToLowerInvariant() switch
        {
            "auto" => OtaTargetKind.Auto,
            "telink" => OtaTargetKind.Telink,
            "stm32" or "stm32serialiap" => OtaTargetKind.Stm32SerialIap,
            _ => throw new CliException(ExitCodes.Usage, "usage", $"Unknown OTA target '{value}'. Use auto, telink or stm32.")
        };

    private static bool IsExtendCompatibilityFailure(Exception ex)
    {
        string message = ex.Message;
        return message.Contains("OTA_PDU_LEN_ERR", StringComparison.OrdinalIgnoreCase) ||
               message.Contains("OTA_MCU_NOT_SUPPORTED", StringComparison.OrdinalIgnoreCase) ||
               message.Contains("OTA_PACKET_INVALID", StringComparison.OrdinalIgnoreCase) ||
               message.Contains("requires MTU", StringComparison.OrdinalIgnoreCase);
    }

    private static object SnapshotData(DeviceSnapshot s) => new
    {
        endpoint = s.Endpoint.Display,
        transport = s.Endpoint.IsSerial ? "serial" : "ble",
        s.Identity,
        firmwareBuildId = s.FirmwareBuildId?.ToString("x8"),
        d008 = s.IsD008,
        battery = new
        {
            s.Battery.PackVoltageV,
            s.Battery.CurrentA,
            s.Battery.SocPercent,
            s.Battery.SohPercent,
            s.Battery.MinCellMv,
            s.Battery.MaxCellMv,
            s.Battery.CellDeltaMv,
            s.Battery.ValidCellCount,
            s.Battery.ProtectionSummary,
            s.Battery.ProtectionLevel1Text,
            s.Battery.ProtectionLevel2Text,
            s.Battery.ProtectionLevel3Text,
            s.Battery.WorkState
        }
    };

    private static void PrintSnapshot(DeviceSnapshot s)
    {
        Console.WriteLine($"Endpoint:  {s.Endpoint.Display}");
        Console.WriteLine($"Name:      {s.Identity.BluetoothName}");
        Console.WriteLine($"MAC:       {s.Identity.Mac}");
        Console.WriteLine($"Serial:    {s.Identity.Serial}");
        Console.WriteLine($"Hardware:  {s.Identity.Hardware}");
        Console.WriteLine($"Software:  {s.Identity.Software}");
        Console.WriteLine($"Build ID:  {s.FirmwareBuildId?.ToString("x8") ?? "unknown"}");
        Console.WriteLine($"D008:      {s.IsD008}");
        Console.WriteLine($"Battery:   {s.Battery.PackVoltageV:F2} V, {s.Battery.CurrentA:+0.0;-0.0;0.0} A, SOC {s.Battery.SocPercent}%");
        Console.WriteLine($"Cells:     min {s.Battery.MinCellMv} mV, max {s.Battery.MaxCellMv} mV, delta {s.Battery.CellDeltaMv} mV");
        Console.WriteLine($"Protection:{s.Battery.ProtectionSummary}");
    }
}
