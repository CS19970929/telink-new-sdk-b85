using BmsTool.Android.Deployer;

namespace BmsTool.Android.Sender;

internal static class Program
{
    public static async Task<int> Main(string[] args)
    {
        try
        {
            string? requestedDevice = GetOptionalValue(args, "--device");
            var sender = new AndroidFirmwareSender(AndroidFirmwareSender.FindAdb());
            IReadOnlyList<AndroidDeviceInfo> devices = await sender.ListDevicesAsync(CancellationToken.None);
            AndroidDeviceInfo device = SelectDevice(devices, requestedDevice);

            Console.WriteLine($"PHONE {device.DisplayName}");
            if (args.Any(argument => string.Equals(argument, "--connect-only", StringComparison.OrdinalIgnoreCase)))
            {
                Console.WriteLine("WIRELESS_READY " + device.Id);
                return 0;
            }

            string firmware = GetRequiredValue(args, "--firmware");
            var progress = new InlineProgress(value =>
                Console.WriteLine($"SEND {value.Percent,6:F1}% {value.Message}"));
            DeploymentResult result = await sender.SendAsync(
                device.Id, firmware, progress, CancellationToken.None);
            Console.WriteLine($"SEND_OK file={result.RemoteName} bytes={result.Size} sha256={result.Sha256}");
            Console.WriteLine("手机已打开 OTA 页面，请选择明确的 BT_ / BT- 设备并确认升级。");
            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine("SEND_FAILED " + ex.Message);
            return 1;
        }
    }

    private static AndroidDeviceInfo SelectDevice(
        IReadOnlyList<AndroidDeviceInfo> devices, string? requestedDevice)
    {
        if (!string.IsNullOrWhiteSpace(requestedDevice))
            return devices.FirstOrDefault(device =>
                       string.Equals(device.Id, requestedDevice, StringComparison.OrdinalIgnoreCase))
                   ?? throw new InvalidOperationException($"未找到指定手机：{requestedDevice}");

        return devices.Count switch
        {
            0 => throw new InvalidOperationException("未发现已授权手机。请连接 USB，并在手机上允许 USB 调试。"),
            1 => devices[0],
            _ => throw new InvalidOperationException(
                "检测到多台手机，为避免误发已拒绝自动选择：" +
                string.Join(", ", devices.Select(device => device.DisplayName)))
        };
    }

    private static string GetRequiredValue(string[] args, string name) =>
        GetOptionalValue(args, name) ?? throw new ArgumentException($"缺少参数 {name}。");

    private static string? GetOptionalValue(string[] args, string name)
    {
        for (int index = 0; index + 1 < args.Length; index++)
            if (string.Equals(args[index], name, StringComparison.OrdinalIgnoreCase))
                return args[index + 1];
        return null;
    }

    private sealed class InlineProgress(Action<DeploymentProgress> report)
        : IProgress<DeploymentProgress>
    {
        public void Report(DeploymentProgress value) => report(value);
    }
}
