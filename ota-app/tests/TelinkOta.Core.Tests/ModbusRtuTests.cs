using NUnit.Framework;
using TelinkOta.Core.Ota;

namespace TelinkOta.Core.Tests;

public class ModbusRtuTests
{
    [Test]
    public void BuildReadRequest_Format()
    {
        var frame = ModbusRtu.BuildReadRequest(0xC022, 16);
        Assert.That(frame[..6], Is.EqualTo(new byte[] { 0x01, 0x03, 0x22, 0xC0, 0x10, 0x00 }));
        ushort crc = Crc16.Compute(frame.AsSpan(0, 6));
        Assert.That(frame[6], Is.EqualTo((byte)(crc & 0xFF)));
        Assert.That(frame[7], Is.EqualTo((byte)(crc >> 8)));
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
}
