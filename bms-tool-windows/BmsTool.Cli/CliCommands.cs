using BmsTool.Windows;
using System.Text.Json;

namespace BmsTool.Cli;

internal static class CliCommands
{
    private static readonly JsonSerializerOptions StreamJsonOptions = new(JsonSerializerDefaults.Web);

    public const string HelpText =
@"bms-cli - BMS command-line tool for humans, scripts and AI agents

Usage:
  bms-cli scan [--scan-seconds 4] [--json]
  bms-cli info (--mac MAC | --name NAME | --auto | --serial COMx) [--baud 19200] [--json]
  bms-cli soc (--mac MAC | --name NAME | --auto | --serial COMx) [--json]
  bms-cli monitor soc (--mac MAC | --name NAME | --auto | --serial COMx)
                  [--interval 5] [--count 0] [--json]
  bms-cli diag (--mac MAC | --name NAME | --auto | --serial COMx) [--output diag.zip] [--quick] [--json]
  bms-cli ota <firmware.bin> (--mac MAC | --name NAME | --auto | --serial COMx)
              [--target auto|telink|stm32] [--mode auto|legacy|extend64]
              [--expected-version VERSION] [--yes] [--json]

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
            "scan" => ScanAsync(options, reporter, ct),
            "info" => InfoAsync(options, reporter, ct),
            "soc" => SocAsync(options, reporter, ct),
            "monitor" => MonitorAsync(options, reporter, ct),
            "diag" => DiagAsync(options, reporter, ct),
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
        CliEndpoint endpoint = await CliRuntime.ResolveEndpointAsync(options, reporter, ct);
        reporter.Status("Connecting " + endpoint.Display + "...");
        await using CliBmsConnection connection = await CliRuntime.ConnectBmsAsync(endpoint, reporter, ct);
        int sample = 0;
        try {
            while (count == 0 || sample < count)
            {
                SocDiagnosticSnapshot soc = await ReadSocAsync(connection, endpoint, ct);
                sample++;
                var row = new { schema = 1, ok = true, command = "monitor soc",
                    data = new { endpoint = endpoint.Display, sample, capturedUtc = DateTimeOffset.UtcNow, soc } };
                if (reporter.Json) Console.WriteLine(JsonSerializer.Serialize(row, StreamJsonOptions));
                else { Console.WriteLine($"[{DateTime.Now:HH:mm:ss}] sample {sample}"); PrintSoc(soc); }
                if (count != 0 && sample >= count) break;
                await Task.Delay(TimeSpan.FromSeconds(interval), ct);
            }
        }
        catch (OperationCanceledException) when (ct.IsCancellationRequested)
        {
            return ExitCodes.Success;
        }
        return ExitCodes.Success;
    }

    private static async Task<SocDiagnosticSnapshot> ReadSocAsync(
        CliBmsConnection connection, CliEndpoint endpoint, CancellationToken ct)
    {
        DiagnosticCapture capture = await connection.Client.ReadDiagnosticsAsync(false, endpoint.Display, ct);
        if (!capture.Supported || capture.Words is null)
            throw new CliException(ExitCodes.ConnectFailed, "soc_diagnostics_unavailable", capture.Status);
        try { return BmsDiagnostics.DecodeSocSnapshot(capture.Words); }
        catch (InvalidDataException ex) {
            throw new CliException(ExitCodes.ConnectFailed, "soc_diagnostics_unavailable", ex.Message, inner: ex);
        }
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

        string? output = options.Get("output");
        string? outputPath = null;
        if (!string.IsNullOrWhiteSpace(output))
        {
            outputPath = Path.GetFullPath(output);
            string? dir = Path.GetDirectoryName(outputPath);
            if (!string.IsNullOrWhiteSpace(dir))
                Directory.CreateDirectory(dir);
            BmsDiagnostics.Export(outputPath, capture);
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
            battery = new
            {
                post.Battery.PackVoltageV,
                post.Battery.MinCellMv,
                post.Battery.SocPercent
            }
        };

        if (!(serverConfirmed || versionChanged || buildChanged))
        {
            throw new CliException(
                ExitCodes.OtaUnconfirmed,
                "ota_unconfirmed",
                "Device returned to normal communication, but there is no positive evidence that new firmware started. OTA_SUCCESS was not received and version/Build ID did not change.",
                result);
        }

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
