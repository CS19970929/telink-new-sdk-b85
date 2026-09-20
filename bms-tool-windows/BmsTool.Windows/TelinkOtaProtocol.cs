using System.Buffers.Binary;
using System.IO;

namespace BmsTool.Windows;

/// <summary>
/// Telink BLE OTA wire format used by tc_ble_single_sdk V3.4.2.8.
/// This class intentionally has no WinRT dependency so packet rules can be unit-tested.
/// </summary>
public static class TelinkOtaProtocol
{
    public const int LegacyPayloadLength = 16;
    public const int ExtendedPayloadLength64 = 64;
    public const int ExtendedStartLength = 20;
    public const int ExtendedPayloadLengthMin = 16;
    public const int ExtendedPayloadLengthMax = 240;

    // D008 currently does not call blc_ota_setFirmwareSizeAndBootAddress(), so
    // Telink OTA Server keeps the SDK default maximum firmware size (124 KiB).
    public const int D008DefaultMaxFirmwareBytes = 124 * 1024;

    public static byte[] BuildLegacyStart() => new byte[] { 0x01, 0xFF };

    public static byte[] BuildExtendedStart(int pduLength, bool versionCompare = false)
    {
        ValidateExtendedPayloadLength(pduLength);

        // ota_startExt_t:
        // u16 ota_cmd + u8 pdu_length + u8 version_compare + u8 rsvd[16].
        byte[] packet = new byte[ExtendedStartLength];
        packet[0] = 0x03;
        packet[1] = 0xFF;
        packet[2] = checked((byte)pduLength);
        packet[3] = versionCompare ? (byte)1 : (byte)0;
        return packet;
    }

    public static byte[] BuildData(byte[] firmware, int index, int payloadLength, bool extended)
    {
        ArgumentNullException.ThrowIfNull(firmware);
        if (firmware.Length == 0) throw new InvalidDataException("Firmware image is empty.");
        if (index < 0) throw new ArgumentOutOfRangeException(nameof(index));

        if (extended)
            ValidateExtendedPayloadLength(payloadLength);
        else if (payloadLength != LegacyPayloadLength)
            throw new ArgumentOutOfRangeException(nameof(payloadLength), "Legacy Telink OTA payload must be exactly 16 bytes.");

        int offset = checked(index * payloadLength);
        if (offset >= firmware.Length)
            throw new ArgumentOutOfRangeException(nameof(index), "OTA packet index is beyond the firmware image.");

        int actual = Math.Min(payloadLength, firmware.Length - offset);
        int padded = extended
            ? Math.Max(LegacyPayloadLength, ((actual + LegacyPayloadLength - 1) / LegacyPayloadLength) * LegacyPayloadLength)
            : LegacyPayloadLength;

        byte[] packet = new byte[2 + padded + 2];
        ushort wireIndex = checked((ushort)(extended ? index + 1 : index));
        BinaryPrimitives.WriteUInt16LittleEndian(packet.AsSpan(0, 2), wireIndex);
        packet.AsSpan(2, padded).Fill(0xFF);
        firmware.AsSpan(offset, actual).CopyTo(packet.AsSpan(2, actual));

        ushort crc = ModbusRtu.Crc16(packet.AsSpan(0, 2 + padded));
        BinaryPrimitives.WriteUInt16LittleEndian(packet.AsSpan(2 + padded, 2), crc);
        return packet;
    }

    public static byte[] BuildEnd(ushort lastIndex)
    {
        ushort inverse = (ushort)~lastIndex;
        return new byte[]
        {
            0x02, 0xFF,
            (byte)lastIndex, (byte)(lastIndex >> 8),
            (byte)inverse, (byte)(inverse >> 8)
        };
    }

    public static bool HasD008TlnkStartupMarker(ReadOnlySpan<byte> image)
    {
        return image.Length >= 12 &&
               BinaryPrimitives.ReadUInt32LittleEndian(image.Slice(8, 4)) == 0x544C4E4B;
    }

    public static bool HasValidTelinkCrcTrailer(ReadOnlySpan<byte> image)
    {
        if (image.Length < 4) return false;
        uint actual = BinaryPrimitives.ReadUInt32LittleEndian(image[^4..]);
        uint expected = CalculateTelinkCrcTrailer(image[..^4]);
        return actual == expected;
    }

    public static uint CalculateTelinkCrcTrailer(ReadOnlySpan<byte> payload)
    {
        uint crc = 0xFFFFFFFFu;
        foreach (byte value in payload)
        {
            crc ^= value;
            for (int bit = 0; bit < 8; bit++)
                crc = (crc & 1u) != 0 ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
        }

        uint standardCrc32 = crc ^ 0xFFFFFFFFu;
        return ~standardCrc32;
    }

    public static void ValidateExtendedPayloadLength(int pduLength)
    {
        if (pduLength < ExtendedPayloadLengthMin ||
            pduLength > ExtendedPayloadLengthMax ||
            (pduLength % LegacyPayloadLength) != 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(pduLength),
                $"Telink extended OTA PDU length must be 16*n in {ExtendedPayloadLengthMin}..{ExtendedPayloadLengthMax} bytes.");
        }
    }
}
