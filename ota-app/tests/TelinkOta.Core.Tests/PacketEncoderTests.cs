using NUnit.Framework;
using TelinkOta.Core.Ota;

namespace TelinkOta.Core.Tests;

public class PacketEncoderTests
{
    private static byte[] Payload(int size, byte fill = 0xAA)
    {
        var b = new byte[size];
        Array.Fill(b, fill);
        return b;
    }

    [Test]
    public void PduValidation_RejectsInvalid()
    {
        var p = Payload(64);
        Assert.Throws<ArgumentOutOfRangeException>(() => new OtaPacketEncoder(p, 17));
        Assert.Throws<ArgumentOutOfRangeException>(() => new OtaPacketEncoder(p, 8));
        Assert.Throws<ArgumentOutOfRangeException>(() => new OtaPacketEncoder(p, 256));
        Assert.Throws<ArgumentException>(() => new OtaPacketEncoder(Array.Empty<byte>(), 16));
        _ = new OtaPacketEncoder(p, 16);   // OK
        _ = new OtaPacketEncoder(p, 240);  // OK
    }

    [Test]
    public void TotalPackets_Ceil()
    {
        Assert.That(new OtaPacketEncoder(Payload(64), 16).TotalPackets, Is.EqualTo(4));
        Assert.That(new OtaPacketEncoder(Payload(65), 16).TotalPackets, Is.EqualTo(5));
        Assert.That(new OtaPacketEncoder(Payload(104), 64).TotalPackets, Is.EqualTo(2)); // 64 + 40
        Assert.That(new OtaPacketEncoder(Payload(16), 16).TotalPackets, Is.EqualTo(1));
    }

    [Test]
    public void Index_EncodedLittleEndian_StartsAtZero()
    {
        var enc = new OtaPacketEncoder(Payload(32), 16);
        var p0 = enc.BuildPacket(0);
        Assert.That(p0[..2], Is.EqualTo(new byte[] { 0x00, 0x00 }));

        var p1 = enc.BuildPacket(1);
        Assert.That(p1[..2], Is.EqualTo(new byte[] { 0x01, 0x00 }));
    }

    [Test]
    public void MiddlePacket_StructureAndCrc()
    {
        var enc = new OtaPacketEncoder(Payload(64, 0x11), 16);
        var pkt = enc.BuildPacket(1);
        Assert.That(pkt.Length, Is.EqualTo(20)); // 2 + 16 + 2
        Assert.That(pkt[..2], Is.EqualTo(new byte[] { 0x01, 0x00 }));
        Assert.That(pkt[2..18], Has.All.EqualTo(0x11));
        ushort crc = Crc16.Compute(pkt.AsSpan(0, 18));
        Assert.That(pkt[18], Is.EqualTo((byte)(crc & 0xFF)));
        Assert.That(pkt[19], Is.EqualTo((byte)(crc >> 8)));
    }

    [Test]
    public void LastPacket_PaddedToMultipleOf16_WithFF()
    {
        // 载荷 20 字节（如 fw=16 + crc4），PDU=16 → 尾包数据 4 字节 → 补齐 16 → 包长 20
        var enc = new OtaPacketEncoder(Payload(20, 0x22), 16);
        Assert.That(enc.TotalPackets, Is.EqualTo(2));
        var last = enc.BuildPacket(1);
        Assert.That(last.Length, Is.EqualTo(20));
        Assert.That(last[..2], Is.EqualTo(new byte[] { 0x01, 0x00 }));
        Assert.That(last[2..6], Has.All.EqualTo(0x22));   // 真实数据
        Assert.That(last[6..18], Has.All.EqualTo(0xFF));  // 0xFF 补齐
        ushort crc = Crc16.Compute(last.AsSpan(0, 18));   // CRC 覆盖补齐位
        Assert.That(last[18], Is.EqualTo((byte)(crc & 0xFF)));
        Assert.That(last[19], Is.EqualTo((byte)(crc >> 8)));
    }

    [Test]
    public void LastPacket_ExactMultipleOf16_NoExtraPadding()
    {
        // 载荷 80 字节，PDU=64 → 尾包 16 字节，16 的倍数，不补 0xFF
        var enc = new OtaPacketEncoder(Payload(80, 0x33), 64);
        var last = enc.BuildPacket(1);
        Assert.That(last.Length, Is.EqualTo(20)); // 2 + 16 + 2
        Assert.That(last[2..18], Has.All.EqualTo(0x33));
    }

    [Test]
    public void LastPacket_PadToFullPdu_Option()
    {
        // 载荷 20 字节，PDU=16，padToFullPdu=true → 尾包补齐到 16 数据
        var enc = new OtaPacketEncoder(Payload(20, 0x44), 16, padToFullPdu: true);
        var last = enc.BuildPacket(1);
        Assert.That(last.Length, Is.EqualTo(20));
        Assert.That(last[2..6], Has.All.EqualTo(0x44));
        Assert.That(last[6..18], Has.All.EqualTo(0xFF));
    }

    [Test]
    public void BuildEnd_IndexAndXor()
    {
        var end = OtaPacketEncoder.BuildEnd(5);
        Assert.That(end, Is.EqualTo(new byte[] { 0x02, 0xFF, 0x05, 0x00, 0xFA, 0xFF }));

        var end2 = OtaPacketEncoder.BuildEnd(0x1234);
        Assert.That(end2, Is.EqualTo(new byte[] { 0x02, 0xFF, 0x34, 0x12, 0xCB, 0xED }));
        Assert.That(end2[4] | (end2[5] << 8), Is.EqualTo(0x1234 ^ 0xFFFF));
    }

    [Test]
    public void BuildStartExt_Layout()
    {
        var buf = OtaPacketEncoder.BuildStartExt(64, versionCompare: true);
        Assert.That(buf.Length, Is.EqualTo(20));
        Assert.That(buf[0] | (buf[1] << 8), Is.EqualTo(0xFF03));
        Assert.That(buf[2], Is.EqualTo(64));
        Assert.That(buf[3], Is.EqualTo(1));
        Assert.That(buf[4..], Has.All.EqualTo(0));

        var buf2 = OtaPacketEncoder.BuildStartExt(16, versionCompare: false);
        Assert.That(buf2[3], Is.EqualTo(0));
    }

    [Test]
    public void BuildCommands_Layout()
    {
        Assert.That(OtaPacketEncoder.BuildStart(), Is.EqualTo(new byte[] { 0x01, 0xFF }));
        Assert.That(OtaPacketEncoder.BuildVersion(), Is.EqualTo(new byte[] { 0x00, 0xFF }));
        Assert.That(OtaPacketEncoder.BuildSetFwIndex(1), Is.EqualTo(new byte[] { 0x80, 0xFF, 0x01 }));

        var req = OtaPacketEncoder.BuildFwVersionReq(0x0102, versionCompare: false);
        Assert.That(req, Is.EqualTo(new byte[] { 0x04, 0xFF, 0x02, 0x01, 0x00 }));

        var req2 = OtaPacketEncoder.BuildFwVersionReq(0x0005, versionCompare: true);
        Assert.That(req2[4], Is.EqualTo(1));
    }

    [Test]
    public void Notify_ParseVersionRsp()
    {
        var data = new byte[] { 0x05, 0xFF, 0x10, 0x00, 0x01 }; // version=0x0010, accept=1
        Assert.That(OtaNotify.TryParse(data, out var n), Is.True);
        Assert.That(n, Is.Not.Null);
        Assert.That(n!.Opcode, Is.EqualTo(0xFF05));
        Assert.That(n.LocalVersion, Is.EqualTo(0x0010));
        Assert.That(n.VersionAccepted, Is.True);
    }

    [Test]
    public void Notify_ParseResult()
    {
        var data = new byte[] { 0x06, 0xFF, 0x03 };
        Assert.That(OtaNotify.TryParse(data, out var n), Is.True);
        Assert.That(n!.Result, Is.EqualTo((byte)0x03));
    }

    [Test]
    public void Notify_ShortFrame_Rejected()
    {
        Assert.That(OtaNotify.TryParse(new byte[] { 0x06 }, out _), Is.False);
        Assert.That(OtaNotify.TryParse(Array.Empty<byte>(), out _), Is.False);
    }

    [Test]
    public void SchedulePduNum_Parse()
    {
        var data = new byte[] { 0x08, 0xFF, 0x2A, 0x00 }; // 42 PDUs
        Assert.That(OtaNotify.TryParse(data, out var n), Is.True);
        Assert.That(n!.SuccessPduCount, Is.EqualTo((ushort)42));
    }
}
