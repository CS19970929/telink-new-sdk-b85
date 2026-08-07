namespace TelinkOta.Core.Ota;

/// <summary>
/// CRC-16/MODBUS（poly 0xA001, init 0xFFFF, 无最终异或, wire 低字节在前）。
/// 依据 ota.h crc16() 与官方 OTA App OtaPacketParser.crc16（fill-array {0x0000, 0xA001}）。
/// 测试向量：crc16("123456789") == 0x4B37。
/// </summary>
public static class Crc16
{
    private static readonly ushort[] Table = BuildTable();

    public static ushort Compute(ReadOnlySpan<byte> data)
    {
        ushort crc = 0xFFFF;
        foreach (byte b in data)
        {
            crc = (ushort)((crc >> 8) ^ Table[(crc ^ b) & 0xFF]);
        }
        return crc;
    }

    private static ushort[] BuildTable()
    {
        var table = new ushort[256];
        for (int i = 0; i < 256; i++)
        {
            ushort c = (ushort)i;
            for (int k = 0; k < 8; k++)
            {
                c = (c & 1) != 0 ? (ushort)((c >> 1) ^ 0xA001) : (ushort)(c >> 1);
            }
            table[i] = c;
        }
        return table;
    }
}
