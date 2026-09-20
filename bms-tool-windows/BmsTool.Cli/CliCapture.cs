using BmsTool.Windows;

namespace BmsTool.Cli;

internal static class CliCapture
{
    public static async Task<int> RunAsync(CliOptions options, CliReporter reporter, CancellationToken ct)
    {
        if (options.Positionals.Count != 0 || options.Has("auto") || options.Has("name") ||
            options.Has("mac") == options.Has("serial"))
            throw new CliException(ExitCodes.Usage, "usage", "capture requires exactly one explicit --mac or --serial target.");
        string output = options.RequireValue("output");
        int count = options.GetInt("count", 0, 0, 1_000_000);
        int interval = options.GetInt("interval", 5, 1, 3600);
        int maxReconnects = options.GetInt("max-reconnects", 10, 0, 1000);
        CliEndpoint endpoint = await CliRuntime.ResolveEndpointAsync(options, reporter, ct);
        using var session = new BmsDiagnosticSession(output, endpoint.Display);
        reporter.Status("Diagnostic session: " + session.DirectoryPath);
        CliBmsConnection? connection = null;
        string outcome = "failed";
        int attempts = 0, reconnects = 0;
        try
        {
            while (count == 0 || attempts < count)
            {
                ct.ThrowIfCancellationRequested();
                bool reconnect = false;
                using var deadline = CancellationTokenSource.CreateLinkedTokenSource(ct);
                deadline.CancelAfter(TimeSpan.FromSeconds(60));
                try
                {
                    connection ??= await CliRuntime.ConnectBmsAsync(endpoint, reporter, deadline.Token);
                    attempts++;
                    DiagnosticSessionSample sample = await session.CaptureAsync(connection.Client, deadline.Token);
                    reporter.Status($"Sample {session.Samples}: complete={sample.Complete}, segment={sample.Segment}, health={sample.Health.Overall}");
                    if (!sample.Capture.Supported && sample.Capture.Errors.Count == 0)
                        throw new CliException(ExitCodes.TestFailed, "diagnostics_unsupported",
                            "Device does not expose a supported diagnostic schema; evidence preserved.", new { session.DirectoryPath });
                    reconnect = !connection.Transport.IsConnected || !sample.Capture.Supported || sample.Capture.Words is null;
                }
                catch (DiagnosticSessionReadException ex) { session.NoteGap(ex.Message); reconnect = true; }
                catch (CliException ex) when (ex.ExitCode == ExitCodes.ConnectFailed) { session.NoteGap(ex.Message); reconnect = true; }
                catch (OperationCanceledException) when (!ct.IsCancellationRequested)
                { session.NoteGap("capture_timeout"); reconnect = true; }
                ct.ThrowIfCancellationRequested();
                if (reconnect)
                {
                    if (!options.Has("reconnect") || reconnects >= maxReconnects)
                        throw new CliException(ExitCodes.ConnectFailed, "capture_connection_lost",
                            "Capture stopped; partial evidence preserved.", new { session.DirectoryPath, reconnects });
                    reconnects++;
                    if (connection is not null) { await connection.DisposeAsync(); connection = null; }
                    reporter.Status($"Reconnect {reconnects}/{maxReconnects}: same pinned endpoint; SN/HW will be rechecked.");
                }
                if (count == 0 || attempts < count) await Task.Delay(TimeSpan.FromSeconds(interval), ct);
            }
            outcome = session.CompleteSamples == session.Samples && !session.HasGaps && session.Samples > 0
                ? "complete" : "partial";
        }
        catch (DiagnosticIdentityMismatchException ex)
        {
            throw new CliException(ExitCodes.ProductMismatch, "identity_changed", ex.Message,
                new { session.DirectoryPath }, ex);
        }
        catch (OperationCanceledException) when (ct.IsCancellationRequested) { outcome = "cancelled"; }
        finally
        {
            try { session.Finish(outcome); }
            finally { if (connection is not null) await connection.DisposeAsync(); }
        }
        var result = new { sessionId = session.Id, output = session.DirectoryPath, outcome,
            session.Samples, session.CompleteSamples, session.Gaps, replayReady = false };
        if (outcome == "cancelled")
            throw new CliException(ExitCodes.Cancelled, "cancelled", "Capture cancelled; evidence preserved.", result);
        if (outcome != "complete")
            throw new CliException(ExitCodes.TestFailed, "capture_partial", "Capture contains incomplete observations; inspect quality and gaps.", result);
        reporter.Success("capture", result);
        if (!reporter.Json) Console.WriteLine("Saved: " + session.DirectoryPath);
        return ExitCodes.Success;
    }
}
