using BmsTool.Windows;
using System.Text.Json;

namespace BmsTool.Cli;

internal static class CliSocHil
{
    private sealed record SceneResult(
        string Name,
        SocHilInput Input,
        DateTimeOffset StartedUtc,
        DateTimeOffset FinishedUtc,
        SocHilStatus Before,
        SocHilStatus After,
        IReadOnlyList<SocHilStatus> Captures);

    private sealed record CheckResult(string Name, bool Passed, string Evidence);

    public static async Task<int> RunAsync(CliOptions options, CliReporter reporter, CancellationToken ct)
    {
        if (!options.Has("yes"))
            throw new CliException(ExitCodes.Usage, "confirmation_required",
                "SOC HIL drives synthetic SOC inputs on real hardware. Re-run with --yes after confirming the board is idle and safely powered.");
        if (string.IsNullOrWhiteSpace(options.Get("mac")) && string.IsNullOrWhiteSpace(options.Get("serial")))
            throw new CliException(ExitCodes.Usage, "explicit_target_required",
                "SOC HIL requires an explicit --mac or --serial target; --auto and --name are forbidden.");
        if (options.Has("auto") || options.Has("name"))
            throw new CliException(ExitCodes.Usage, "explicit_target_required",
                "SOC HIL does not accept --auto or --name. Use the exact --mac or --serial target.");

        string suite = (options.Get("suite") ?? "all").Trim().ToLowerInvariant();
        if (suite is not ("basic" or "all"))
            throw new CliException(ExitCodes.Usage, "usage", "--suite must be basic or all.");
        int acceleratedCurrentMa = options.GetInt("accelerated-current-ma", 1_500_000, 1_000, 3_000_000);
        string? outputOption = options.Get("output");
        string? output = string.IsNullOrWhiteSpace(outputOption) ? null : Path.GetFullPath(outputOption);

        CliEndpoint endpoint = await CliRuntime.ResolveEndpointAsync(options, reporter, ct);
        reporter.Status("Connecting " + endpoint.Display + "...");
        await using CliBmsConnection connection = await CliRuntime.ConnectBmsAsync(endpoint, reporter, ct);
        DeviceSnapshot beforeDevice = await CliRuntime.ReadSnapshotAsync(connection, ct);
        int seed = options.GetInt("seed", beforeDevice.Battery.SocPercent, 1, 99);
        var scenes = new List<SceneResult>();
        var checks = new List<CheckResult>();
        SocHilSession? session = null;
        string? failure = null;
        bool closeConfirmed = false;
        BatterySnapshot? afterBattery = null;
        DateTimeOffset startedUtc = DateTimeOffset.UtcNow;
        byte sequence = 0;

        async Task<SceneResult> RunSceneAsync(string name, SocHilInput input, int sampleCount)
        {
            SocHilStatus before = await connection.Client.SocHilReadStatusAsync(session!.Token, ct);
            var captures = new List<SocHilStatus>();
            DateTimeOffset sceneStarted = DateTimeOffset.UtcNow;
            for (int i = 0; i < sampleCount; i++)
            {
                sequence++;
                uint appliedBefore = await connection.Client.SocHilSetSampleAsync(
                    session.Token, sequence, input, ct);
                DateTimeOffset deadline = DateTimeOffset.UtcNow.AddSeconds(3);
                SocHilStatus status;
                do
                {
                    await Task.Delay(50, ct);
                    status = await connection.Client.SocHilReadStatusAsync(session.Token, ct);
                    if (status.Sequence == sequence && status.AppliedCount > appliedBefore) break;
                }
                while (DateTimeOffset.UtcNow < deadline);
                if (status.Sequence != sequence || status.AppliedCount <= appliedBefore)
                    throw new TimeoutException($"SOC HIL sample {sequence} was not applied within 3 seconds.");
                captures.Add(status);
                reporter.Status($"SOC HIL {name} {i + 1}/{sampleCount}: est={status.SocEstimate}% display={status.SocDisplay}% state={status.LastSampleState} applied={status.AppliedCount}");
            }
            SocHilStatus after = captures[^1];
            var result = new SceneResult(name, input, sceneStarted, DateTimeOffset.UtcNow, before, after, captures);
            scenes.Add(result);
            return result;
        }

        try
        {
            session = await connection.Client.SocHilOpenAsync((byte)seed, learningEnable: false, ct);
            checks.Add(new("protocol_version", session.ProtocolVersion == 1,
                $"version={session.ProtocolVersion}, timeout={session.TimeoutSeconds}s, seed={session.SeedSoc}%"));

            SceneResult idle = await RunSceneAsync("idle", SocHilInput.Normal(0), 2);
            checks.Add(new("idle_stable", idle.After.SocEstimate == idle.Before.SocEstimate,
                $"{idle.Before.SocEstimate}% -> {idle.After.SocEstimate}%"));

            SceneResult deadband = await RunSceneAsync("deadband", SocHilInput.Normal(150), 2);
            checks.Add(new("deadband_stable", deadband.After.SocEstimate == deadband.Before.SocEstimate,
                $"{deadband.Before.SocEstimate}% -> {deadband.After.SocEstimate}% at +150mA"));

            SceneResult discharge = await RunSceneAsync("accelerated_discharge",
                SocHilInput.Normal(acceleratedCurrentMa, loadPresent: true), 12);
            checks.Add(new("discharge_decreases_soc", discharge.After.SocEstimate < discharge.Before.SocEstimate,
                $"{discharge.Before.SocEstimate}% -> {discharge.After.SocEstimate}% at +{acceleratedCurrentMa}mA"));

            SceneResult charge = await RunSceneAsync("accelerated_charge",
                SocHilInput.Normal(-acceleratedCurrentMa, chargerPresent: true), 12);
            checks.Add(new("charge_increases_soc", charge.After.SocEstimate > charge.Before.SocEstimate,
                $"{charge.Before.SocEstimate}% -> {charge.After.SocEstimate}% at -{acceleratedCurrentMa}mA"));

            if (suite == "all")
            {
                SceneResult imbalance = await RunSceneAsync("cell_imbalance",
                    SocHilInput.Normal(0, 3000, 3500), 2);
                checks.Add(new("imbalance_input_applied",
                    imbalance.After.CellMinMv == 3000 && imbalance.After.CellMaxMv == 3500,
                    $"min={imbalance.After.CellMinMv}mV max={imbalance.After.CellMaxMv}mV state={imbalance.After.LastSampleState}"));

                var invalidInput = new SocHilInput(0, 3250, 3260, 650,
                    SocHilSampleFlags.VoltageValid | SocHilSampleFlags.TemperatureValid |
                    SocHilSampleFlags.ChargerKnown | SocHilSampleFlags.LoadKnown);
                SceneResult invalid = await RunSceneAsync("invalid_sample", invalidInput, 1);
                checks.Add(new("invalid_sample_rejected", invalid.After.LastSampleState == 1,
                    $"lastSampleState={invalid.After.LastSampleState}"));

                SceneResult recovery = await RunSceneAsync("invalid_recovery", SocHilInput.Normal(0), 2);
                checks.Add(new("invalid_sample_recovers", recovery.After.LastSampleState is 2 or 5 or 6,
                    $"lastSampleState={recovery.After.LastSampleState}"));

                var openWireInput = SocHilInput.Normal(0) with
                {
                    Flags = SocHilSampleFlags.Normal | SocHilSampleFlags.OpenWireSuspected
                };
                SceneResult openWire = await RunSceneAsync("open_wire_suspected", openWireInput, 2);
                checks.Add(new("open_wire_scenario_applied", openWire.After.Sequence == sequence,
                    $"sequence={openWire.After.Sequence}, applied={openWire.After.AppliedCount}"));
            }

            IReadOnlyList<SocHilStatus> validCaptures = scenes
                .Where(x => x.Name != "invalid_sample")
                .SelectMany(x => x.Captures)
                .ToArray();
            checks.Add(new("scheduler_has_no_gap", validCaptures.All(x => x.LastSampleState != 4),
                $"captures={validCaptures.Count}, gaps={validCaptures.Count(x => x.LastSampleState == 4)}"));
            checks.Add(new("samples_applied", scenes.Count != 0 && scenes[^1].After.AppliedCount > 0,
                $"applied={scenes.LastOrDefault()?.After.AppliedCount ?? 0}"));
        }
        catch (OperationCanceledException) { throw; }
        catch (Exception ex)
        {
            failure = ex.GetType().Name + ": " + ex.Message;
            checks.Add(new("session_completed", false, failure));
        }
        finally
        {
            if (session is not null)
            {
                try
                {
                    await connection.Client.SocHilCloseAsync(session.Token, CancellationToken.None);
                    closeConfirmed = true;
                }
                catch (Exception ex)
                {
                    failure ??= "Close failed: " + ex.Message;
                    checks.Add(new("close_confirmed", false, ex.Message));
                }
            }
        }

        try
        {
            await Task.Delay(300, ct);
            afterBattery = await connection.Client.ReadBatteryAsync(ct);
            checks.Add(new("physical_soc_restored", afterBattery.SocPercent == beforeDevice.Battery.SocPercent,
                $"before={beforeDevice.Battery.SocPercent}% after={afterBattery.SocPercent}%"));
            checks.Add(new("physical_capacity_restored",
                afterBattery.CapacityNowAh == beforeDevice.Battery.CapacityNowAh &&
                afterBattery.CapacityFullAh == beforeDevice.Battery.CapacityFullAh &&
                afterBattery.CycleCount == beforeDevice.Battery.CycleCount,
                $"now {beforeDevice.Battery.CapacityNowAh}->{afterBattery.CapacityNowAh}Ah; full {beforeDevice.Battery.CapacityFullAh}->{afterBattery.CapacityFullAh}Ah; cycle {beforeDevice.Battery.CycleCount}->{afterBattery.CycleCount}"));
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            checks.Add(new("physical_state_readback", false, ex.Message));
            failure ??= "Post-close readback failed: " + ex.Message;
        }

        checks.Add(new("close_confirmed", closeConfirmed, closeConfirmed ? "firmware acknowledged CLOSE" : "CLOSE was not acknowledged; firmware timeout restore remains active"));
        bool passed = failure is null && checks.All(x => x.Passed);
        DateTimeOffset finishedUtc = DateTimeOffset.UtcNow;
        var report = new
        {
            schema = 1,
            test = "soc_hil",
            suite,
            endpoint = endpoint.Display,
            startedUtc,
            finishedUtc,
            durationMs = (long)(finishedUtc - startedUtc).TotalMilliseconds,
            passed,
            failure,
            firmwareBuildId = beforeDevice.FirmwareBuildId?.ToString("x8"),
            before = new
            {
                beforeDevice.Identity,
                beforeDevice.Battery.SocPercent,
                beforeDevice.Battery.CapacityNowAh,
                beforeDevice.Battery.CapacityFullAh,
                beforeDevice.Battery.CycleCount
            },
            after = afterBattery is null ? null : new
            {
                afterBattery.SocPercent,
                afterBattery.CapacityNowAh,
                afterBattery.CapacityFullAh,
                afterBattery.CycleCount
            },
            session,
            acceleratedCurrentMa,
            scenes,
            checks
        };

        if (output is not null)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(output) ?? ".");
            await File.WriteAllTextAsync(output, JsonSerializer.Serialize(report,
                new JsonSerializerOptions(JsonSerializerDefaults.Web) { WriteIndented = true }), ct);
        }
        reporter.Success("test soc-hil", new { report, output });
        if (!reporter.Json)
        {
            foreach (CheckResult check in checks)
                Console.WriteLine($"[{(check.Passed ? "PASS" : "FAIL")}] {check.Name}: {check.Evidence}");
            Console.WriteLine($"SOC HIL {suite}: {(passed ? "PASS" : "FAIL")}");
            if (output is not null) Console.WriteLine("SOC HIL report: " + output);
        }
        return passed ? ExitCodes.Success : ExitCodes.TestFailed;
    }
}
