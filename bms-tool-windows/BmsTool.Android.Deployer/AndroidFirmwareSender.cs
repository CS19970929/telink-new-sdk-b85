using BmsTool.Windows;
using System.Diagnostics;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;

namespace BmsTool.Android.Deployer;

public sealed record AndroidDeviceInfo(string Id, string Model)
{
    public string DisplayName => string.IsNullOrWhiteSpace(Model) ? Id : $"{Model} · {Id}";
}

public sealed record DeploymentProgress(double Percent, string Message);
public sealed record DeploymentResult(string RemoteName, int Size, string Sha256);
internal sealed record AdbResult(int ExitCode, string Output, string Error);

public sealed class AndroidFirmwareSender(string adbPath)
{
    private const string PackageName = "com.cs.bmstool.android";
    private const string ImportAction = "com.cs.bmstool.android.IMPORT_FIRMWARE";

    public static string FindAdb()
    {
        var candidates = new List<string>();
        foreach (string variable in new[] { "ANDROID_SDK_ROOT", "ANDROID_HOME" })
        {
            string? root = Environment.GetEnvironmentVariable(variable);
            if (!string.IsNullOrWhiteSpace(root)) candidates.Add(Path.Combine(root, "platform-tools", "adb.exe"));
        }
        string? local = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        if (!string.IsNullOrWhiteSpace(local)) candidates.Add(Path.Combine(local, "Android", "Sdk", "platform-tools", "adb.exe"));
        foreach (string directory in (Environment.GetEnvironmentVariable("PATH") ?? string.Empty)
                     .Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
            candidates.Add(Path.Combine(directory, "adb.exe"));
        string? found = candidates.FirstOrDefault(File.Exists);
        return found ?? throw new FileNotFoundException("未找到 adb.exe。请安装 Android platform-tools，或配置 ANDROID_SDK_ROOT。GUI 不会弹出命令行窗口。");
    }

    public async Task<IReadOnlyList<AndroidDeviceInfo>> ListDevicesAsync(CancellationToken ct)
    {
        IReadOnlyList<AndroidDeviceInfo> devices = await ReadDevicesAsync(ct);
        if (devices.Count > 0) return devices;

        for (int attempt = 1; attempt <= 5; attempt++)
        {
            AdbResult mdns = await RunAsync(null, new[] { "mdns", "services" }, ct);
            if (mdns.ExitCode == 0)
            {
                var services = Regex.Matches(mdns.Output,
                        @"^(?<instance>.+?)\s+_adb-tls-connect\._tcp\s+(?<endpoint>\S+)\s*$",
                        RegexOptions.Multiline)
                    .Select(match => new
                    {
                        Instance = Regex.Replace(match.Groups["instance"].Value.Trim(), @"\s+\(\d+\)$", ""),
                        Endpoint = match.Groups["endpoint"].Value
                    })
                    .ToArray();
                var deviceGroups = services
                    .GroupBy(service => service.Instance, StringComparer.OrdinalIgnoreCase)
                    .ToArray();
                if (deviceGroups.Length > 1)
                    throw new InvalidOperationException("发现多台可配对的无线 Android 设备，为避免误连已拒绝自动选择：" +
                                                        string.Join(", ", deviceGroups.Select(group => group.Key)));
                if (deviceGroups.Length == 1)
                {
                    foreach (string endpoint in deviceGroups[0]
                                 .Select(service => service.Endpoint)
                                 .Distinct(StringComparer.OrdinalIgnoreCase))
                    {
                        AdbResult connect = await RunAsync(null, new[] { "connect", endpoint }, ct);
                        if (connect.ExitCode == 0)
                        {
                            await Task.Delay(500, ct);
                            devices = await ReadDevicesAsync(ct);
                            if (devices.Count > 0) return devices;
                        }
                    }
                }
            }
            if (attempt < 5) await Task.Delay(TimeSpan.FromSeconds(1), ct);
        }

        return Array.Empty<AndroidDeviceInfo>();
    }

    private async Task<IReadOnlyList<AndroidDeviceInfo>> ReadDevicesAsync(CancellationToken ct)
    {
        AdbResult result = await RunAsync(null, new[] { "devices", "-l" }, ct);
        EnsureSuccess(result, "读取 Android 设备列表");
        var devices = new List<AndroidDeviceInfo>();
        foreach (string line in result.Output.Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries))
        {
            Match match = Regex.Match(line, "^(?<id>\\S+)\\s+device(?:\\s|$)");
            if (!match.Success) continue;
            Match model = Regex.Match(line, "(?:^|\\s)model:(?<model>\\S+)");
            devices.Add(new AndroidDeviceInfo(match.Groups["id"].Value,
                model.Success ? model.Groups["model"].Value.Replace('_', ' ') : string.Empty));
        }
        return devices;
    }

    public async Task<DeploymentResult> SendAsync(string deviceId, string firmwarePath, bool autoOtaIfConnected,
        IProgress<DeploymentProgress>? progress, CancellationToken ct)
    {
        FirmwareImage image = FirmwareImage.LoadStrictTelink(firmwarePath);
        byte[] bytes = image.Bytes;
        string sha256 = Convert.ToHexString(SHA256.HashData(bytes));
        string remoteName = $"ota-{DateTimeOffset.Now:yyyyMMdd-HHmmss}-{sha256[..8]}.bin";
        string uploadId = Guid.NewGuid().ToString("N")[..16];

        progress?.Report(new DeploymentProgress(2, "正在检查手机上的 BMS Tool…"));
        AdbResult package = await RunAsync(deviceId, new[] { "shell", "pm", "path", PackageName }, ct);
        if (package.ExitCode != 0 || !package.Output.Contains("package:", StringComparison.Ordinal))
            throw new InvalidOperationException("手机尚未安装 BMS Tool Android v0.3.1 或更高版本。");
        if (autoOtaIfConnected)
        {
            AdbResult packageInfo = await RunAsync(deviceId,
                new[] { "shell", "dumpsys", "package", PackageName }, ct);
            EnsureSuccess(packageInfo, "检查手机 BMS Tool 版本");
            Match version = Regex.Match(packageInfo.Output, @"versionCode=(?<code>\d+)");
            if (!version.Success || !int.TryParse(version.Groups["code"].Value, out int versionCode) || versionCode < 5)
                throw new InvalidOperationException("自动 OTA 需要 BMS Tool Android v0.3.2 或更高版本。");
        }

        AdbResult resolve = await RunAsync(deviceId,
            new[] { "shell", "cmd", "package", "resolve-activity", "--brief", PackageName }, ct);
        EnsureSuccess(resolve, "定位手机 BMS Tool");
        string activity = resolve.Output.Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries).LastOrDefault()?.Trim()
            ?? string.Empty;
        if (!activity.Contains('/')) throw new InvalidOperationException("无法定位手机 BMS Tool 主页面。");

        await RunCheckedAsync(deviceId, new[] { "logcat", "-c" }, "清理本次传输日志", ct);
        await RunCheckedAsync(deviceId, new[] { "shell", "am", "start", "-n", activity }, "启动手机 BMS Tool", ct);
        await Task.Delay(TimeSpan.FromSeconds(1), ct);

        const int chunkSize = 4096;
        int totalChunks = (bytes.Length + chunkSize - 1) / chunkSize;
        for (int index = 0; index < totalChunks; index++)
        {
            int offset = index * chunkSize;
            int count = Math.Min(chunkSize, bytes.Length - offset);
            string data = Convert.ToBase64String(bytes, offset, count);
            string[] arguments =
            {
                "shell", "am", "broadcast", "-a", ImportAction, "-p", PackageName,
                "--es", "upload_id", uploadId, "--es", "file_name", remoteName,
                "--es", "sha256", sha256, "--ei", "index", index.ToString(),
                "--ei", "total", totalChunks.ToString(), "--ei", "offset", offset.ToString(),
                "--es", "data", data
            };
            await RunCheckedAsync(deviceId, arguments, $"发送固件分片 {index + 1}/{totalChunks}", ct);
            double percent = 5 + 85.0 * (index + 1) / totalChunks;
            progress?.Report(new DeploymentProgress(percent, $"正在发送 {index + 1}/{totalChunks} · {offset + count:N0}/{bytes.Length:N0} bytes"));
        }

        await Task.Delay(500, ct);
        AdbResult log = await RunAsync(deviceId,
            new[] { "logcat", "-d", "-s", "BmsTool.Android:I", "*:S" }, ct);
        EnsureSuccess(log, "核对手机导入结果");
        if (!log.Output.Contains($"FIRMWARE_IMPORT_OK upload={uploadId}", StringComparison.Ordinal))
            throw new IOException("手机未确认固件导入成功。请检查手机空间、BMS Tool 版本和日志。");

        progress?.Report(new DeploymentProgress(95, autoOtaIfConnected
            ? "导入校验通过，正在请求已连接设备自动 OTA…"
            : "导入校验通过，正在打开手机固件页…"));
        var launchArguments = new List<string>
        {
            "shell", "am", "start", "-f", "0x20000000", "-n", activity,
            "--es", "firmware_inbox_name", remoteName, "--ez", "show_tools", "true"
        };
        if (autoOtaIfConnected)
        {
            launchArguments.Add("--ez");
            launchArguments.Add("auto_ota_if_connected");
            launchArguments.Add("true");
            launchArguments.Add("--es");
            launchArguments.Add("auto_ota_upload_id");
            launchArguments.Add(uploadId);
            launchArguments.Add("--es");
            launchArguments.Add("auto_ota_sha256");
            launchArguments.Add(sha256);
        }
        await RunCheckedAsync(deviceId, launchArguments, "打开手机固件页", ct);
        progress?.Report(new DeploymentProgress(100, autoOtaIfConnected
            ? "固件已发送；App 仅在目标 BMS 已保持连接时自动 OTA。"
            : "固件已发送，手机端等待选择设备和人工确认。"));
        return new DeploymentResult(remoteName, image.ImageSize, sha256);
    }

    private async Task RunCheckedAsync(string? deviceId, IReadOnlyList<string> arguments,
        string operation, CancellationToken ct)
    {
        AdbResult result = await RunAsync(deviceId, arguments, ct);
        EnsureSuccess(result, operation);
    }

    private static void EnsureSuccess(AdbResult result, string operation)
    {
        if (result.ExitCode == 0) return;
        string detail = string.IsNullOrWhiteSpace(result.Error) ? result.Output : result.Error;
        throw new IOException($"{operation}失败（adb exit {result.ExitCode}）：{detail.Trim()}");
    }

    private async Task<AdbResult> RunAsync(string? deviceId, IReadOnlyList<string> arguments, CancellationToken ct)
    {
        var start = new ProcessStartInfo
        {
            FileName = adbPath,
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            StandardOutputEncoding = Encoding.UTF8,
            StandardErrorEncoding = Encoding.UTF8
        };
        if (!string.IsNullOrWhiteSpace(deviceId))
        {
            start.ArgumentList.Add("-s");
            start.ArgumentList.Add(deviceId);
        }
        foreach (string argument in arguments) start.ArgumentList.Add(argument);

        using var process = new Process { StartInfo = start };
        if (!process.Start()) throw new IOException("无法启动 adb.exe。");
        Task<string> stdout = process.StandardOutput.ReadToEndAsync(ct);
        Task<string> stderr = process.StandardError.ReadToEndAsync(ct);
        await process.WaitForExitAsync(ct);
        return new AdbResult(process.ExitCode, await stdout, await stderr);
    }
}
