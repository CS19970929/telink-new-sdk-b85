namespace TelinkOta.Core.Ota;

/// <summary>
/// OTA 数据包编码器。分包/补齐/CRC16/Index/END 全部依据官方 OTA App 2.1.2 行为：
///  - 首包 Index = 0；
///  - 尾包数据不足 PDU_Length 时以 0xFF 补齐到 16 的整数倍（默认），或补齐到完整 PDU（可选）；
///  - CRC16（Modbus）覆盖 Index+数据+补齐位；
///  - END = [index_max(2 LE)][index_max ^ 0xFFFF(2 LE)]。
/// </summary>
public sealed class OtaPacketEncoder
{
    private readonly byte[] _payload;
    public int PduLength { get; }
    public int TotalPackets { get; }
    public int LastIndex => TotalPackets - 1;

    /// <param name="payload">完整发送载荷（含尾部 CRC32 4 字节）。</param>
    /// <param name="pduLength">协商的 PDU 长度（16..240，16 的整数倍）。</param>
    /// <param name="padToFullPdu">尾包是否补齐到完整 PDU（默认 false：补齐到 16 的倍数，官方 App 行为）。</param>
    public OtaPacketEncoder(byte[] payload, int pduLength, bool padToFullPdu = false)
    {
        if (payload is null || payload.Length == 0)
            throw new ArgumentException("payload 不能为空", nameof(payload));
        if (pduLength < OtaConstants.PduMin || pduLength > OtaConstants.PduMax)
            throw new ArgumentOutOfRangeException(nameof(pduLength), $"PDU 长度必须在 {OtaConstants.PduMin}~{OtaConstants.PduMax} 之间");
        if (pduLength % OtaConstants.PduStep != 0)
            throw new ArgumentOutOfRangeException(nameof(pduLength), "PDU 长度必须为 16 的整数倍");

        _payload = payload;
        PduLength = pduLength;
        PadToFullPdu = padToFullPdu;
        TotalPackets = (payload.Length + pduLength - 1) / pduLength;
    }

    public bool PadToFullPdu { get; }

    /// <summary>生成指定 Index 的完整 ATT 写入值（含 Index+CRC16）。</summary>
    public byte[] BuildPacket(int index)
    {
        if (index < 0 || index >= TotalPackets)
            throw new ArgumentOutOfRangeException(nameof(index), $"index={index} 超出 [0,{TotalPackets})");

        int offset = index * PduLength;
        int remaining = _payload.Length - offset;
        int dataLen = Math.Min(remaining, PduLength);
        int padded = PadToFullPdu
            ? PduLength
            : RoundUpTo16(dataLen);
        if (padded < OtaConstants.PduMin)
            padded = OtaConstants.PduMin;

        var packet = new byte[padded + OtaConstants.PduOverhead];
        packet[0] = (byte)(index & 0xFF);
        packet[1] = (byte)((index >> 8) & 0xFF);
        Array.Copy(_payload, offset, packet, 2, dataLen);
        Array.Fill(packet, (byte)0xFF, 2 + dataLen, padded - dataLen); // 0xFF 补齐

        ushort crc = Crc16.Compute(packet.AsSpan(0, packet.Length - 2));
        packet[^2] = (byte)(crc & 0xFF);
        packet[^1] = (byte)((crc >> 8) & 0xFF);
        return packet;
    }

    /// <summary>CMD_OTA_END：[opcode(2)][index_max(2 LE)][index_max ^ 0xFFFF(2 LE)]。</summary>
    public static byte[] BuildEnd(int indexMax)
    {
        ushort xor = (ushort)(indexMax ^ 0xFFFF);
        return new byte[]
        {
            (byte)(OtaConstants.CmdOtaEnd & 0xFF), (byte)(OtaConstants.CmdOtaEnd >> 8),
            (byte)(indexMax & 0xFF), (byte)((indexMax >> 8) & 0xFF),
            (byte)(xor & 0xFF), (byte)((xor >> 8) & 0xFF),
        };
    }

    /// <summary>CMD_OTA_START（Legacy）：仅 2 字节 opcode。</summary>
    public static byte[] BuildStart() =>
        new byte[] { (byte)(OtaConstants.CmdOtaStart & 0xFF), (byte)(OtaConstants.CmdOtaStart >> 8) };

    /// <summary>CMD_OTA_VERSION（Legacy）：仅 2 字节 opcode。</summary>
    public static byte[] BuildVersion() =>
        new byte[] { (byte)(OtaConstants.CmdOtaVersion & 0xFF), (byte)(OtaConstants.CmdOtaVersion >> 8) };

    /// <summary>CMD_OTA_START_EXT：[opcode][pdu_length(1)][version_compare(1)][rsvd(16)]，共 20 字节。</summary>
    public static byte[] BuildStartExt(int pduLength, bool versionCompare)
    {
        var buf = new byte[20];
        buf[0] = (byte)(OtaConstants.CmdOtaStartExt & 0xFF);
        buf[1] = (byte)(OtaConstants.CmdOtaStartExt >> 8);
        buf[2] = (byte)pduLength;
        buf[3] = versionCompare ? (byte)1 : (byte)0;
        return buf;
    }

    /// <summary>CMD_OTA_FW_VERSION_REQ：[opcode][version(2 LE)][version_compare(1)]，共 5 字节。</summary>
    public static byte[] BuildFwVersionReq(ushort version, bool versionCompare)
    {
        return new byte[]
        {
            (byte)(OtaConstants.CmdOtaFwVersionReq & 0xFF), (byte)(OtaConstants.CmdOtaFwVersionReq >> 8),
            (byte)(version & 0xFF), (byte)(version >> 8),
            versionCompare ? (byte)1 : (byte)0,
        };
    }

    /// <summary>CMD_OTA_SET_FW_INDEX：[opcode][fw_index(1)]。</summary>
    public static byte[] BuildSetFwIndex(byte fwIndex) =>
        new byte[] { (byte)(OtaConstants.CmdOtaSetFwIndex & 0xFF), (byte)(OtaConstants.CmdOtaSetFwIndex >> 8), fwIndex };

    private static int RoundUpTo16(int len) => (len + 15) / 16 * 16;
}

/// <summary>
/// 设备 Notify 解析结果。
/// </summary>
public sealed class OtaNotify
{
    public ushort Opcode { get; init; }
    public byte[] Raw { get; init; } = Array.Empty<byte>();

    /// <summary>CMD_OTA_FW_VERSION_RSP：设备本地版本。</summary>
    public ushort? LocalVersion { get; init; }

    /// <summary>CMD_OTA_FW_VERSION_RSP：1=接受升级，0=拒绝。</summary>
    public bool? VersionAccepted { get; init; }

    /// <summary>CMD_OTA_RESULT：结果码。</summary>
    public byte? Result { get; init; }

    /// <summary>CMD_OTA_SCHEDULE_PDU_NUM：已成功接收的 PDU 数。</summary>
    public ushort? SuccessPduCount { get; init; }

    public static bool TryParse(byte[] data, out OtaNotify? notify)
    {
        notify = null;
        if (data is null || data.Length < 2)
            return false;

        ushort opcode = (ushort)(data[0] | (data[1] << 8));
        notify = new OtaNotify { Opcode = opcode, Raw = data };

        switch (opcode)
        {
            case OtaConstants.CmdOtaFwVersionRsp:
                if (data.Length >= 5)
                {
                    notify = new OtaNotify
                    {
                        Opcode = opcode,
                        Raw = data,
                        LocalVersion = (ushort)(data[2] | (data[3] << 8)),
                        VersionAccepted = data[4] == 1,
                    };
                }
                return true;
            case OtaConstants.CmdOtaResult:
                if (data.Length >= 3)
                {
                    notify = new OtaNotify { Opcode = opcode, Raw = data, Result = data[2] };
                }
                return true;
            case OtaConstants.CmdOtaSchedulePduNum:
                if (data.Length >= 4)
                {
                    notify = new OtaNotify
                    {
                        Opcode = opcode,
                        Raw = data,
                        SuccessPduCount = (ushort)(data[2] | (data[3] << 8)),
                    };
                }
                return true;
            default:
                return true;
        }
    }

    public override string ToString() => $"opcode=0x{Opcode:X4} {Convert.ToHexString(Raw)}";
}
