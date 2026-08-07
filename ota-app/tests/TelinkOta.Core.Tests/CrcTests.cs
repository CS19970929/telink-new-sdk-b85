using System.Text;
using NUnit.Framework;
using TelinkOta.Core.Ota;

namespace TelinkOta.Core.Tests;

public class CrcTests
{
    [Test]
    public void Crc16_Modbus_TestVector()
    {
        // CRC-16/MODBUS 标准测试向量："123456789" -> 0x4B37
        var data = Encoding.ASCII.GetBytes("123456789");
        Assert.That(Crc16.Compute(data), Is.EqualTo(0x4B37));
    }

    [Test]
    public void Crc16_Empty_Is_Init()
    {
        Assert.That(Crc16.Compute(Array.Empty<byte>()), Is.EqualTo(0xFFFF));
    }

    [Test]
    public void Crc16_Matches_OtaH_Reference_Implementation()
    {
        // 与 ota.h crc16() 逐位实现一致（poly {0, 0xa001}, init 0xffff）
        var data = Encoding.ASCII.GetBytes("Telink BLE OTA");
        Assert.That(Crc16.Compute(data), Is.EqualTo(ReferenceCrc16(data)));
    }

    [Test]
    public void Crc32_Telink_TestVector()
    {
        // 官方 App Crc.calCrc32："123456789" -> 0x340BC6D9（init 0xFFFFFFFF，无 final xor）
        var data = Encoding.ASCII.GetBytes("123456789");
        Assert.That(Crc32.Compute(data), Is.EqualTo(0x340BC6D9));
    }

    [Test]
    public void Crc32_Telink_RealFirmwareVector()
    {
        // 真实固件 BIN 实测：前 91072 字节的 CRC32 = 0x814C0E73（= BIN 尾部 LE 存储值）
        byte[] fw = new byte[91072];
        Array.Fill(fw, (byte)0x5A);
        // 构造一个已知内容的 91072 字节数组没有意义，改用真实文件（TestData）验证
        string? realBin = FindRealBin();
        if (realBin is null)
        {
            Assert.Ignore("未找到真实 BIN 测试数据");
        }
        var data = File.ReadAllBytes(realBin);
        Assert.That(data.Length, Is.EqualTo(91076));
        Assert.That(Crc32.Compute(data.AsSpan(0, 91072)), Is.EqualTo(0x814C0E73));
        Assert.That(BitConverter.ToUInt32(data, 91072), Is.EqualTo(0x814C0E73)); // LE 存储
    }

    private static string? FindRealBin()
    {
        string candidate = Path.Combine(AppContext.BaseDirectory, "TestData", "real_fw.bin");
        return File.Exists(candidate) ? candidate : null;
    }

    [Test]
    public void Crc32_Telink_NoFinalXor()
    {
        // 若加 final xor 结果会不同，确保实现未误用标准 CRC-32
        var data = Encoding.ASCII.GetBytes("123456789");
        uint noFx = Crc32.Compute(data);
        Assert.That(noFx, Is.Not.EqualTo(0xCBF43926)); // 标准 CRC-32("123456789") 为 0xCBF43926
    }

    [Test]
    public void Crc32_StoredLittleEndian_RoundTrip()
    {
        var data = Encoding.ASCII.GetBytes("firmware payload");
        uint crc = Crc32.Compute(data);
        var tail = new byte[4];
        BitConverter.TryWriteBytes(tail, crc); // LE
        Assert.That(BitConverter.ToUInt32(tail, 0), Is.EqualTo(crc));
    }

    /// <summary>ota.h 的逐位参考实现。</summary>
    private static ushort ReferenceCrc16(byte[] pD)
    {
        ushort[] poly = { 0, 0xa001 };
        ushort crc = 0xffff;
        for (int j = pD.Length; j > 0; j--)
        {
            byte ds = pD[pD.Length - j];
            for (int i = 0; i < 8; i++)
            {
                crc = (ushort)((crc >> 1) ^ poly[(crc ^ ds) & 1]);
                ds >>= 1;
            }
        }
        return crc;
    }

    [Test]
    public void Crc16_FrameVectors()
    {
        // 0x03 读帧 CRC（Modbus 标准）：不同字节序必须产生不同结果
        byte[] f1 = { 0x01, 0x03, 0x20, 0xD1, 0x0B, 0x00 };
        byte[] f2 = { 0x01, 0x03, 0xD1, 0x20, 0x00, 0x0B };
        Assert.That(Crc16.Compute(f1), Is.EqualTo(0x0319));
        Assert.That(Crc16.Compute(f2), Is.EqualTo(0xFB3C));
    }
}

