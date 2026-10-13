using System.IO;

namespace BmsTool.Windows;

public sealed record MosControlState(
    bool ChargeAllowed,
    bool DischargeAllowed,
    bool ChargeDriverOn,
    bool DischargeDriverOn,
    bool DriverStateValid,
    ushort GuardBits,
    bool ChargeRequested,
    bool DischargeRequested)
{
    public bool SamplesQualified => (GuardBits & 0x0010) != 0;
    public bool OutputEnabled => (GuardBits & 0x0001) != 0;
    public bool CommunicationInhibited => (GuardBits & 0x0006) != 0;
    public bool FaultLatched => (GuardBits & 0x0008) != 0;
}

public sealed partial class BmsClient
{
    public const ushort MosControlBase = 0x2F10;
    public const ushort MosControlMagic = 0x4D43;

    public async Task<MosControlState?> TryReadMosControlAsync(CancellationToken ct = default)
    {
        ushort[] words;
        try
        {
            words = await ReadRegistersAsync(MosControlBase, 8, ct).ConfigureAwait(false);
        }
        catch (BmsModbusException ex) when (ex.Function == 0x03 && ex.Code is 1 or 2)
        {
            return null;
        }

        if (words[0] != MosControlMagic || words[1] != 1)
            return null;

        return new MosControlState(
            words[2] == 1,
            words[3] == 1,
            (words[4] & 0x0001) != 0,
            (words[4] & 0x0002) != 0,
            words[5] == 1,
            words[6],
            (words[7] & 0x0001) != 0,
            (words[7] & 0x0002) != 0);
    }

    public async Task<MosControlState> SetMosChannelAllowedAsync(
        bool chargeChannel,
        bool allowed,
        CancellationToken ct = default)
    {
        if (await TryReadMosControlAsync(ct).ConfigureAwait(false) is null)
            throw new NotSupportedException("当前固件不支持独立充放电 MOS 控制。");

        ushort register = (ushort)(MosControlBase + (chargeChannel ? 2 : 3));
        await WriteSingleRegisterAsync(register, allowed ? (ushort)1 : (ushort)0, ct).ConfigureAwait(false);

        MosControlState after = await TryReadMosControlAsync(ct).ConfigureAwait(false)
            ?? throw new IOException("MOS 控制写入后无法读取能力窗口。");
        bool readback = chargeChannel ? after.ChargeAllowed : after.DischargeAllowed;
        if (readback != allowed)
            throw new IOException($"MOS 控制读回不一致：写入 {(allowed ? 1 : 0)}，读回 {(readback ? 1 : 0)}。");
        return after;
    }
}
