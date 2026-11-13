using System.Buffers.Binary;
using System.IO;

namespace BmsTool.Windows;

public sealed record BmsFaultInjectionStatus(bool Enabled, byte McuFaults, bool Active,
    ushort ProtectionFaults, bool Hold, byte FinalMos, byte DriverMos, bool DriverValid,
    uint BootId, uint AppliedSequence, bool WriteFailed, byte ReverseMos)
{
    public static readonly string[] McuNames = {
        "MCU CPU寄存器故障", "MCU 程序计数器故障", "MCU RAM故障", "MCU Flash完整性故障",
        "MCU 时钟故障", "MCU 中断故障", "MCU ADC故障"
    };
    public string McuText => string.Join("、", McuNames.Where((_, i) => (McuFaults & (1 << i)) != 0));
    public bool AfeCommunicationFault => (ProtectionFaults & 0x0100) != 0;
    public bool CurrentShuntDriftFault => (ProtectionFaults & 0x0200) != 0;
    public bool PrimaryCutoffFault => (ProtectionFaults & 0x0400) != 0;
    public bool SecondaryShutdownActive => (ProtectionFaults & 0x0800) != 0;
    public bool HasInjectedFault => McuFaults != 0 || ProtectionFaults != 0;
    public static BmsFaultInjectionStatus Parse(byte[] p)
    {
        if (p.Length != 24 || !p.AsSpan(0, 4).SequenceEqual("FINJ"u8) || p[4] != 1 ||
            p[5] > 1 || (p[6] & 0x80) != 0 || p[7] > 1 || p[10] > 1 ||
            p[11] > 3 || p[12] > 3 || p[13] > 1 || p[22] > 1 || p[23] > 3)
            throw new IOException("故障注入状态格式不匹配。");
        return new(p[5] != 0, p[6], p[7] != 0, BinaryPrimitives.ReadUInt16BigEndian(p.AsSpan(8)),
            p[10] != 0, p[11], p[12], p[13] != 0,
            BinaryPrimitives.ReadUInt32BigEndian(p.AsSpan(14)),
            BinaryPrimitives.ReadUInt32BigEndian(p.AsSpan(18)), p[22] != 0, p[23]);
    }
}

public sealed partial class BmsClient
{
    private bool? _faultInjectionSupported;
    private bool _faultInjectionProbeEnabled;
    public bool FaultInjectionStateUnknown { get; private set; }

    public async Task<BmsFaultInjectionStatus?> TryReadFaultInjectionAsync(CancellationToken ct = default,
                                                                          bool forceProbe = false)
    {
        // Ordinary monitoring never sends project-specific factory commands.
        if (!_faultInjectionProbeEnabled && !forceProbe) return null;
        if (forceProbe) _faultInjectionProbeEnabled = true;
        if (_faultInjectionSupported == false && !forceProbe) return null;
        byte[] response;
        try
        {
            response = await TransactAsync(ModbusRtu.Frame(new byte[] { 1, 0x41, 0x20 }),
                ct, TimeSpan.FromSeconds(2)).ConfigureAwait(false);
        }
        catch (TimeoutException) when (_faultInjectionSupported is null && !forceProbe)
        {
            // Probe once for old devices. A known-capable device never hides a
            // communication failure by falling back to stale injected state.
            _faultInjectionSupported = false;
            FaultInjectionStateUnknown = false; // Optional capability absence is not a device fault.
            return null;
        }
        catch (BmsModbusException ex) when (_faultInjectionSupported is null && ex.Code is 1 or 2)
        {
            _faultInjectionSupported = false;
            FaultInjectionStateUnknown = false;
            return null;
        }
        if (response.Length == 6 && response[1] == 0x41 && response[2] == 0x20 &&
            response[3] is 1 or 3 && (_faultInjectionSupported is null || forceProbe))
        {
            _faultInjectionSupported = false;
            FaultInjectionStateUnknown = false;
            return null;
        }
        if (response.Length != 31 || response[1] != 0x41 || response[2] != 0x20 ||
            response[3] != 0 || response[4] != 24)
            throw new IOException("故障注入状态读取失败，当前故障和 MOS 状态未知。");
        var status = BmsFaultInjectionStatus.Parse(response.AsSpan(5, 24).ToArray());
        _faultInjectionSupported = true;
        FaultInjectionStateUnknown = false;
        return status;
    }
}
