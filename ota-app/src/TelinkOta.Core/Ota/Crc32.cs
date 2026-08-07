namespace TelinkOta.Core.Ota;

/// <summary>
/// Telink Firmware CRC32：init 0xFFFFFFFF，reflected poly 0xEDB88320，无最终异或。
/// 结果按小端存储于 BIN 尾部 4 字节。
/// 依据官方 OTA App Crc.calCrc32（DEX 反汇编）。
/// 测试向量：calCrc32("123456789") == 0xEAE0665A。
/// </summary>
public static class Crc32
{
    private static readonly uint[] Table = BuildTable();

    public static uint Compute(ReadOnlySpan<byte> data, uint init = 0xFFFFFFFF)
    {
        uint crc = init;
        foreach (byte b in data)
        {
            crc = (crc >> 8) ^ Table[(crc ^ b) & 0xFF];
        }
        return crc;
    }

    private static uint[] BuildTable()
    {
        var table = new uint[256];
        for (uint i = 0; i < 256; i++)
        {
            uint c = i;
            for (int k = 0; k < 8; k++)
            {
                c = (c & 1) != 0 ? (c >> 1) ^ 0xEDB88320 : c >> 1;
            }
            table[i] = c;
        }
        return table;
    }
}
