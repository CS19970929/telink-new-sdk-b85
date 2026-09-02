using System.Buffers.Binary;
using System.IO;
using System.Text;

namespace BmsTool.Windows;

public static class BmsRegisters
{
    public const byte DeviceAddress = 0x01;
    public const ushort Mac = 0x0000;
    public const ushort BtName = 0x0100;
    public const ushort Serial = 0xC002;
    public const ushort Hardware = 0xC012;
    public const ushort Software = 0xC022;
    public const ushort Protect = 0x2100;
    public const ushort Legacy = 0xD000;
    public const ushort SystemStatus = 0xD115;
    public const ushort Realtime = 0xD120;
    public const ushort RealtimeMagic = 0x4253;
    public const int SeriesCount = 10;
    public const int BtNameReadWords = 12;
    public const int BtNameMaxSuffixBytesPerBleRequest = 10;
}

public static class ModbusRtu
{
    public static byte[] ReadHolding(ushort start, ushort quantity)
    {
        Span<byte> body = stackalloc byte[6];
        body[0] = BmsRegisters.DeviceAddress; body[1] = 0x03;
        BinaryPrimitives.WriteUInt16BigEndian(body[2..4], start);
        BinaryPrimitives.WriteUInt16BigEndian(body[4..6], quantity);
        return Frame(body);
    }

    public static byte[] WriteMultiple(ushort start, ReadOnlySpan<byte> rawRegisterBytes)
    {
        if (rawRegisterBytes.Length == 0 || (rawRegisterBytes.Length & 1) != 0) throw new ArgumentException("Register payload must contain whole words.");
        ushort qty = checked((ushort)(rawRegisterBytes.Length / 2));
        byte[] body = new byte[7 + rawRegisterBytes.Length];
        body[0] = BmsRegisters.DeviceAddress; body[1] = 0x10;
        BinaryPrimitives.WriteUInt16BigEndian(body.AsSpan(2, 2), start);
        BinaryPrimitives.WriteUInt16BigEndian(body.AsSpan(4, 2), qty);
        body[6] = checked((byte)rawRegisterBytes.Length);
        rawRegisterBytes.CopyTo(body.AsSpan(7));
        return Frame(body);
    }

    public static ushort[] ParseRead(byte[] frame, ushort expectedQuantity)
    {
        ValidateFrame(frame);
        if (frame[0] != BmsRegisters.DeviceAddress) throw new IOException("Unexpected Modbus slave address.");
        if ((frame[1] & 0x80) != 0) throw new IOException($"Modbus exception function=0x{frame[1] & 0x7F:X2}, code=0x{frame[2]:X2}.");
        if (frame[1] != 0x03) throw new IOException($"Expected function 0x03, got 0x{frame[1]:X2}.");
        int bytes = frame[2];
        if (bytes != expectedQuantity * 2 || frame.Length != bytes + 5) throw new IOException("Modbus read response length mismatch.");
        ushort[] words = new ushort[expectedQuantity];
        for (int i = 0; i < words.Length; i++) words[i] = BinaryPrimitives.ReadUInt16BigEndian(frame.AsSpan(3 + i * 2, 2));
        return words;
    }

    public static void ValidateWriteMultipleAck(byte[] frame, ushort expectedRegister, ushort expectedQuantity)
    {
        ValidateFrame(frame);
        if ((frame[1] & 0x80) != 0) throw new IOException($"Modbus exception code=0x{frame[2]:X2}.");
        if (frame[0] != BmsRegisters.DeviceAddress || frame[1] != 0x10 || frame.Length != 8) throw new IOException("Invalid write acknowledgement.");
        if (BinaryPrimitives.ReadUInt16BigEndian(frame.AsSpan(2,2)) != expectedRegister || BinaryPrimitives.ReadUInt16BigEndian(frame.AsSpan(4,2)) != expectedQuantity) throw new IOException("Write acknowledgement mismatch.");
    }

    public static int? InferExpectedLength(IReadOnlyList<byte> buffer)
    {
        if (buffer.Count < 2) return null;
        byte f = buffer[1];
        if ((f & 0x80) != 0) return 5;
        if (f == 0x03) return buffer.Count >= 3 ? buffer[2] + 5 : null;
        if (f == 0x06 || f == 0x10) return 8;
        return null;
    }

    public static byte[] Frame(ReadOnlySpan<byte> body)
    {
        byte[] result = new byte[body.Length + 2]; body.CopyTo(result);
        ushort crc = Crc16(body); result[^2] = (byte)crc; result[^1] = (byte)(crc >> 8); return result;
    }

    public static void ValidateFrame(ReadOnlySpan<byte> frame)
    {
        if (frame.Length < 4) throw new IOException("Modbus response too short.");
        ushort rx = (ushort)(frame[^2] | frame[^1] << 8); ushort calc = Crc16(frame[..^2]);
        if (rx != calc) throw new IOException($"Modbus CRC mismatch: rx=0x{rx:X4}, calc=0x{calc:X4}.");
    }

    public static ushort Crc16(ReadOnlySpan<byte> data)
    {
        ushort crc = 0xFFFF;
        foreach (byte b in data) { crc ^= b; for (int i=0;i<8;i++) crc = (crc & 1) != 0 ? (ushort)((crc >> 1) ^ 0xA001) : (ushort)(crc >> 1); }
        return crc;
    }

    public static string DecodeAscii(IEnumerable<ushort> words)
    {
        var bytes = words.SelectMany(w => new[]{(byte)(w>>8),(byte)w}).ToList(); int zero = bytes.IndexOf(0); if (zero >= 0) bytes.RemoveRange(zero, bytes.Count-zero); return Encoding.UTF8.GetString(bytes.ToArray()).Trim();
    }
}
