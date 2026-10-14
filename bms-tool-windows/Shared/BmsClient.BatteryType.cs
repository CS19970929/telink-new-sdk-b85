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
    private const ushort BatteryTypeCurrentBase = 0x2E2A;
    private const int BatteryTypeReadySamples = 3;
    private static readonly TimeSpan BatteryTypeReadyTimeout = TimeSpan.FromSeconds(5);

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
        ushort[] before = await ReadBatteryTypeAsync(ct);
        await WaitForBatteryTypeSwitchReadyAsync(before[5], ct);
        // 17 bytes，适配 MTU=23；单条命令完成 Config 提交，不分组写入，也不自动重试。
        try
        {
            await WriteRegistersAsync(BatteryTypeCommand, new ushort[] { chemistry, 0x6873, 0x3435, 0x3600 }, ct);
        }
        catch (IOException ex) when (ex.Message.IndexOf("0x04", StringComparison.OrdinalIgnoreCase) >= 0)
        {
            string detail = await DescribeBatteryTypeFailureAsync(before[4], ct);
            throw new IOException("设备拒绝电池类型切换。" + detail, ex);
        }
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

    private async Task WaitForBatteryTypeSwitchReadyAsync(ushort currentDeadbandMa, CancellationToken ct)
    {
        DateTime deadline = DateTime.UtcNow + BatteryTypeReadyTimeout;
        uint? previousTick = null;
        int qualified = 0;
        int currentMa = 0;
        bool sampleValid = false;

        while (DateTime.UtcNow < deadline)
        {
            ushort[] sample = await ReadRegistersAsync(BatteryTypeCurrentBase, 5, ct);
            currentMa = unchecked((int)((uint)sample[0] | ((uint)sample[1] << 16)));
            sampleValid = sample[2] != 0;
            uint tick = (uint)sample[3] | ((uint)sample[4] << 16);
            bool currentIdle = currentMa <= currentDeadbandMa && currentMa >= -(int)currentDeadbandMa;

            if (sampleValid && currentIdle)
            {
                if (previousTick != tick)
                {
                    previousTick = tick;
                    qualified++;
                    if (qualified >= BatteryTypeReadySamples) return;
                }
            }
            else
            {
                qualified = 0;
                previousTick = tick;
            }
            await Task.Delay(150, ct);
        }

        if (!sampleValid)
            throw new InvalidOperationException("AFE 电流样本未就绪，未发送切换命令。请保持设备唤醒并等待采样恢复后重试。");
        if (currentMa > currentDeadbandMa || currentMa < -(int)currentDeadbandMa)
            throw new InvalidOperationException($"当前电流 {currentMa} mA 超出允许的 ±{currentDeadbandMa} mA，未发送切换命令。请断开充电器和负载后重试。");
        throw new InvalidOperationException("5 秒内未取得三个连续的新鲜静置样本，未发送切换命令。请保持设备唤醒并等待采样稳定后重试。");
    }

    private async Task<string> DescribeBatteryTypeFailureAsync(ushort statusBeforeWrite, CancellationToken ct)
    {
        try
        {
            ushort[] state = await ReadRegistersAsync(BatteryTypeBase, 8, ct);
            ushort[] sample = await ReadRegistersAsync(BatteryTypeCurrentBase, 5, ct);
            MosControlState? mos = await TryReadMosControlAsync(ct);
            int currentMa = unchecked((int)((uint)sample[0] | ((uint)sample[1] << 16)));
            if (sample[2] == 0)
                return " AFE 样本已失效；命令没有被确认，请恢复通信/采样后先重新读取设备。";
            if (currentMa > state[5] || currentMa < -(int)state[5])
                return $" 当前电流 {currentMa} mA，超过允许的 ±{state[5]} mA；请断开充电器和负载。";
            if (mos is { SamplesQualified: false })
                return " AFE guard 尚未取得三个合格样本；请保持设备唤醒并等待采样稳定后重试。";
            if (mos is { CommunicationInhibited: true })
                return " AFE 总线当前被通信故障或静默门禁阻断；请先恢复设备通信。";
            if (statusBeforeWrite != 3 && state[4] == 3)
                return $" CFG2/SCP1 保存或 AFE 应用未确认；当前类型 chemistry={state[2]}、profile={state[3]}。请先重新读取，勿盲目重复切换。";
            return $" 保护参数资格或 AFE 访问条件未满足；当前类型 chemistry={state[2]}、profile={state[3]}、状态={state[4]}。请先重新读取设备，勿盲目重复切换。";
        }
        catch (Exception diagnosticError) when (diagnosticError is not OperationCanceledException)
        {
            return " 失败后的设备状态也无法读取，请恢复通信后重新读取，勿盲目重复切换。";
        }
    }

}
