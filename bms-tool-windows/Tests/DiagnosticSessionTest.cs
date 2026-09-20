using System.Text.Json;
using BmsTool.Windows;

static class DiagnosticSessionTest
{
    static void Check(bool value, string message) { if (!value) throw new Exception(message); }

    public static async Task RunAsync()
    {
        string root = Path.Combine(Path.GetTempPath(), "bms-session-test-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try
        {
            var transport = new FakeTransport();
            await using var client = new BmsClient(transport);
            string path;
            using (var session = new BmsDiagnosticSession(root, "pinned-test-endpoint"))
            {
                path = session.DirectoryPath;
                var first = await session.CaptureAsync(client);
                Check(first.Complete && session.Samples == 1, "initial full capture");
                using (var reader = new StreamReader(new FileStream(Path.Combine(path, "observations.jsonl"),
                    FileMode.Open, FileAccess.Read, FileShare.ReadWrite)))
                    Check(reader.ReadToEnd().Split('\n', StringSplitOptions.RemoveEmptyEntries).Length >= 3, "journal readable before close");
                Check(Directory.GetFiles(path, "*.zip").Length == 1, "initial evidence");
                transport.Tick = 200;
                var next = await session.CaptureAsync(client);
                Check(next.Segment == 1 && next.Boundary is null, "continuous sample");
                transport.Tick = 10;
                var reset = await session.CaptureAsync(client);
                Check(reset.Segment == 2 && reset.Boundary == "possible_reset_or_clock_discontinuity", "reset is only suspected");
                session.NoteGap("disconnected"); session.NoteGap("still disconnected");
                Check(session.Gaps == 1, "one gap per disconnected interval");
                var resumed = await session.CaptureAsync(client);
                Check(resumed.Segment == 3 && resumed.Boundary == "after_gap_boot_continuity_unknown", "reconnect segment");
                transport.Serial = "OTHER-DEVICE";
                bool mismatch = false;
                try { await session.CaptureAsync(client); } catch (DiagnosticIdentityMismatchException) { mismatch = true; }
                Check(mismatch && session.Samples == 4, "wrong device must not append a sample");
                session.Finish("failed");
                using var summary = JsonDocument.Parse(File.ReadAllText(Path.Combine(path, "summary.json")));
                Check(!summary.RootElement.GetProperty("captureComplete").GetBoolean(), "gap not reported complete");
                Check(!summary.RootElement.GetProperty("replayReady").GetBoolean(), "observation is not algorithm replay");
            }
            var lines = File.ReadAllLines(Path.Combine(path, "observations.jsonl"));
            long previous = 0;
            foreach (string line in lines)
            {
                using var row = JsonDocument.Parse(line);
                long sequence = row.RootElement.GetProperty("sequence").GetInt64();
                Check(sequence == previous + 1, "journal ordering"); previous = sequence;
            }
            transport.Serial = "SN001";
            transport.Tick = 0xFFFFFFF0;
            using (var session = new BmsDiagnosticSession(root, "wrap"))
            {
                await session.CaptureAsync(client);
                transport.Tick = 64;
                var wrapped = await session.CaptureAsync(client);
                Check(wrapped.Segment == 1, "clock wrap is not a reboot");
                transport.FailEvents = true;
                var partial = await session.CaptureAsync(client, fullEvidence: true);
                Check(!partial.Complete && session.Gaps == 1, "optional read failure recorded as partial");
                session.Finish("partial");
                transport.FailEvents = false;
            }
            using (var session = new BmsDiagnosticSession(root, "cancel"))
            {
                using var cancelled = new CancellationTokenSource(); cancelled.Cancel();
                bool cancelledRead = false;
                try { await session.CaptureAsync(client, cancelled.Token); } catch (OperationCanceledException) { cancelledRead = true; }
                Check(cancelledRead && session.Samples == 0, "cancel does not fabricate samples");
                session.Finish("cancelled");
            }
            transport.ExceptionCode = 2;
            var failed = await BmsTestEngine.RunDiagnosticsAsync(client, "failed", 2, false, TimeSpan.Zero);
            Check(!failed.Passed && failed.SuccessfulSamples == 0 &&
                failed.Checks.Any(x => x.Id == "diag.samples" && x.Status == BmsTestStatus.Fail), "missing samples cannot pass");
            Check(transport.Writes == 0, "all session and diagnostic operations must be read-only");
            Console.WriteLine("PASS diagnostic session: streaming, partial evidence, identity pin, reset uncertainty, tick wrap, gaps, cancel, read-only frames");
        }
        finally { Directory.Delete(root, true); }
    }
}
