using NUnit.Framework;
using TelinkOta.Core.Ota;

namespace TelinkOta.Core.Tests;

public class ModbusRtuTests
{
    [Test]
    public void BuildReadRequest_Format_BigEndian()
    {
        // 固件 u16be 解析：寄存器/数量均为大端。0xC022 → C0 22；16 → 00 10
        var frame = ModbusRtu.BuildReadRequest(0xC022, 16);
        Assert.That(frame[..6], Is.EqualTo(new byte[] { 0x01, 0x03, 0xC0, 0x22, 0x00, 0x10 }));
        ushort crc = Crc16.Compute(frame.AsSpan(0, 6));
        Assert.That(frame[6], Is.EqualTo((byte)(crc & 0xFF)));
        Assert.That(frame[7], Is.EqualTo((byte)(crc >> 8)));
    }

    [Test]
    public void BuildReadRequest_MatchesDocExample()
    {
        // 对接文档示例：01 03 C0 02 00 10 D9 C6（读序列号 0xC002，16 寄存器）
        var frame = ModbusRtu.BuildReadRequest(0xC002, 16);
        Assert.That(frame, Is.EqualTo(new byte[] { 0x01, 0x03, 0xC0, 0x02, 0x00, 0x10, 0xD9, 0xC6 }));
    }

    [Test]
    public void BuildReadRequest_QuantityLimit()
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => ModbusRtu.BuildReadRequest(0xD120, 0));
        Assert.Throws<ArgumentOutOfRangeException>(() => ModbusRtu.BuildReadRequest(0xD120, 126));
        _ = ModbusRtu.BuildReadRequest(0xD120, 125); // OK
    }

    [Test]
    public void ParseReadResponse_Ok()
    {
        var data = new byte[32];
        Array.Fill(data, (byte)'A');
        var frame = new byte[3 + 32 + 2];
        frame[0] = 0x01;
        frame[1] = 0x03;
        frame[2] = 32;
        data.CopyTo(frame, 3);
        ushort crc = Crc16.Compute(frame.AsSpan(0, 35));
        frame[35] = (byte)(crc & 0xFF);
        frame[36] = (byte)(crc >> 8);

        Assert.That(ModbusRtu.TryParseReadResponse(frame, out var parsed), Is.True);
        Assert.That(parsed, Is.Not.Null);
        Assert.That(parsed!.Length, Is.EqualTo(32));
        Assert.That(parsed, Has.All.EqualTo((byte)'A'));
    }

    [Test]
    public void ParseReadResponse_Fragmented_ReturnsFalseUntilComplete()
    {
        var full = ModbusRtu.BuildReadRequest(0xC022, 16);
        Assert.That(ModbusRtu.TryParseReadResponse(full[..5], out _), Is.False);
    }

    [Test]
    public void ParseReadResponse_BadCrc_Rejected()
    {
        var frame = new byte[] { 0x01, 0x03, 0x02, 0x41, 0x41, 0x00, 0x00 };
        Assert.That(ModbusRtu.TryParseReadResponse(frame, out _), Is.False);
    }

    [Test]
    public void ParseReadResponse_ExceptionFrame_Rejected()
    {
        var frame = new byte[] { 0x01, 0x83, 0x01 };
        Assert.That(ModbusRtu.TryParseReadResponse(frame, out _), Is.False);
    }

    [Test]
    public void ParseReadResponse_WrongSlaveAddr_Rejected()
    {
        var frame = new byte[] { 0x02, 0x03, 0x02, 0x41, 0x41, 0x00, 0x00 };
        Assert.That(ModbusRtu.TryParseReadResponse(frame, out _), Is.False);
    }

    [Test]
    public void BuildWriteMultipleRequest_UsesBigEndianAndLittleEndianCrc()
    {
        var frame = ModbusRtu.BuildWriteMultipleRequest(0x0100,
            new byte[] { (byte)'c', (byte)'s', (byte)'-', (byte)'1' });
        Assert.That(frame[..7], Is.EqualTo(new byte[] { 0x01, 0x10, 0x01, 0x00, 0x00, 0x02, 0x04 }));
        Assert.That(frame[7..11], Is.EqualTo(new byte[] { (byte)'c', (byte)'s', (byte)'-', (byte)'1' }));
        ushort crc = Crc16.Compute(frame.AsSpan(0, frame.Length - 2));
        Assert.That(frame[^2], Is.EqualTo((byte)(crc & 0xFF)));
        Assert.That(frame[^1], Is.EqualTo((byte)(crc >> 8)));
    }

    [Test]
    public void BuildWriteMultipleRequest_RejectsInvalidDataLength()
    {
        Assert.Throws<ArgumentException>(() => ModbusRtu.BuildWriteMultipleRequest(0x0100, Array.Empty<byte>()));
        Assert.Throws<ArgumentException>(() => ModbusRtu.BuildWriteMultipleRequest(0x0100, new byte[3]));
        Assert.Throws<ArgumentOutOfRangeException>(() => ModbusRtu.BuildWriteMultipleRequest(0x0100, new byte[248]));
    }

    [Test]
    public void ParseWriteMultipleResponse_ValidatesEchoAndCrc()
    {
        var response = new byte[] { 0x01, 0x10, 0x01, 0x00, 0x00, 0x02, 0, 0 };
        ushort crc = Crc16.Compute(response.AsSpan(0, 6));
        response[6] = (byte)(crc & 0xFF);
        response[7] = (byte)(crc >> 8);

        Assert.That(ModbusRtu.TryParseWriteMultipleResponse(response, 0x0100, 2), Is.True);
        Assert.That(ModbusRtu.TryParseWriteMultipleResponse(response, 0x0101, 2), Is.False);
        response[7] ^= 0x01;
        Assert.That(ModbusRtu.TryParseWriteMultipleResponse(response, 0x0100, 2), Is.False);
    }
}
