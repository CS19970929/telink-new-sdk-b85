using System.IO;

namespace BmsTool.Windows;

public static class BmsServiceAccess
{
    public const string DefaultPassword = "hs456";
    public static bool PasswordValid(string? password) =>
        string.Equals(password, DefaultPassword, StringComparison.Ordinal);
}

public sealed class BatteryTypeCapture
{
    public ushort Chemistry { get; init; }
    public ushort[] SoftwareProtection { get; init; } = Array.Empty<ushort>();
    public AfeHardwareSnapshot HardwareProtection { get; init; } = null!;
    public SocProfileParameters SocConfiguration { get; init; } = null!;
    public ushort[] Balance { get; init; } = Array.Empty<ushort>();
    public string Summary => $"已切换为{(Chemistry == 1 ? "磷酸铁锂" : "三元锂")}；软件保护、硬件保护、均衡及 SOC/OCV 配置已保存并读回。输出仍受采样资格及保护条件控制。";
}

public sealed partial class BmsClient
{
    private const ushort BatteryTypeBase = 0x2EA0;
    private const ushort BatteryTypeCommand = 0x2EA8;

    public async Task<ushort[]> ReadBatteryTypeAsync(CancellationToken ct = default)
    {
        var capability = await ReadRegistersAsync(0x2E00, 3, ct);
        if (capability[0] != D008Parameters.Magic || (capability[2] & 0x0100) == 0)
            throw new NotSupportedException("当前设备固件不支持整套电池类型切换，请先升级配套固件。未发送写命令。");
        var state = await ReadRegistersAsync(BatteryTypeBase, 8, ct);
        if (state[0] != 0x4348 || state[1] != 1 || state[2] > 2 || state[3] > 2 ||
            (state[2] == 1 && state[3] == 2) || (state[2] == 2 && state[3] == 1))
            throw new NotSupportedException("设备电池类型接口版本或类型无效，未发送写命令。");
        return state;
    }

    public async Task<BatteryTypeCapture> SwitchBatteryTypeAsync(ushort chemistry, string password, CancellationToken ct = default)
    {
        if (chemistry is < 1 or > 2) throw new ArgumentOutOfRangeException(nameof(chemistry));
        if (!BmsServiceAccess.PasswordValid(password)) throw new UnauthorizedAccessException("服务密码错误。");
        await ReadBatteryTypeAsync(ct);
        // 17 bytes，适配 MTU=23；单条命令完成 Config 提交，不分组写入，也不自动重试。
        await WriteRegistersAsync(BatteryTypeCommand, new ushort[] { chemistry, 0x6873, 0x3435, 0x3600 }, ct);
        var state = await ReadBatteryTypeAsync(ct);
        if (state[2] != chemistry || state[3] != chemistry || state[4] != 2)
            throw new IOException("类型切换未确认完成；请重新读取设备，勿盲目重复切换。");
        var software = await ReadProtectionAllAsync(ct);
        var hardware = await new AfeHardwareClient(this).ReadAllAsync(ct);
        var balance = await ReadRegistersAsync(D008Parameters.BalanceBase, 4, ct);
        var soc = await ReadSocProfileAsync(ct);
        if (!hardware.Info.ProfileValid || hardware.Requested[0] != hardware.Effective[0] ||
            hardware.Requested[1] != hardware.Effective[1])
            throw new IOException("硬件保护读回未确认，请重新读取设备状态。");
        return new BatteryTypeCapture { Chemistry = chemistry, SoftwareProtection = software,
            HardwareProtection = hardware, SocConfiguration = soc, Balance = balance };
    }

}
