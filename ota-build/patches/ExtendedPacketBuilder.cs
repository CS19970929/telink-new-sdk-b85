using System.Buffers.Binary;
using Bms.Ota.Core.Firmware;

namespace Bms.Ota.Core.Telink;

public static class ExtendedPacketBuilder
{
    public const int PayloadSize64 = 64;

    public static byte[] BuildStart(int payloadSize, bool versionCompare = false)
    {
        ValidatePayloadSize(payloadSize);
        return [0x03, 0xFF, checked((byte)payloadSize), versionCompare ? (byte)0x01 : (byte)0x00];
    }

    public static int GetPacketCount(int imageSize, int payloadSize)
    {
        ValidatePayloadSize(payloadSize);
        if (imageSize <= 0) throw new ArgumentOutOfRangeException(nameof(imageSize));
        return (imageSize + payloadSize - 1) / payloadSize;
    }

    public static byte[] BuildData(FirmwareImage image, int zeroBasedPacketIndex, int payloadSize = PayloadSize64)
    {
        ArgumentNullException.ThrowIfNull(image);
        ValidatePayloadSize(payloadSize);

        int packetCount = GetPacketCount(image.ImageSize, payloadSize);
        if (zeroBasedPacketIndex < 0 || zeroBasedPacketIndex >= packetCount)
            throw new ArgumentOutOfRangeException(nameof(zeroBasedPacketIndex));

        int offset = checked(zeroBasedPacketIndex * payloadSize);
        int remaining = image.ImageSize - offset;
        int actual = Math.Min(payloadSize, remaining);
        int padded = ((actual + 15) / 16) * 16;
        if (padded <= 0) padded = 16;

        var packet = new byte[2 + padded + 2];
        ushort adrIndex = checked((ushort)(zeroBasedPacketIndex + 1));
        BinaryPrimitives.WriteUInt16LittleEndian(packet.AsSpan(0, 2), adrIndex);
        packet.AsSpan(2, padded).Fill(0xFF);
        image.Bytes.AsSpan(offset, actual).CopyTo(packet.AsSpan(2, actual));

        ushort crc = TelinkCrc16.Compute(packet.AsSpan(0, 2 + padded));
        BinaryPrimitives.WriteUInt16LittleEndian(packet.AsSpan(2 + padded, 2), crc);
        return packet;
    }

    private static void ValidatePayloadSize(int payloadSize)
    {
        if (payloadSize < 16 || payloadSize > 80 || payloadSize % 16 != 0)
            throw new ArgumentOutOfRangeException(nameof(payloadSize), "B85 Extend OTA payload must be 16..80 bytes and 16-byte aligned.");
    }
}
