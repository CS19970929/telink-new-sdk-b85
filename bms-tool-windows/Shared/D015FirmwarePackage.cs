using System.Buffers.Binary;
using System.IO;
namespace BmsTool.Windows;

// Retain the strict D015 API for legacy callers and fixtures. Generic upgrading
// uses SerialFirmwarePackage plus the selected device's advertised layout.
public sealed record D015FirmwarePackage(uint Version,uint PayloadCrc,int PayloadLength)
{
    public const uint Magic=SerialFirmwarePackage.Magic, BoardId=0xD0150320, AppAddress=0x08002000;
    public const int HeaderBytes=32, Capacity=44*1024;
    public static bool HasHeader(ReadOnlySpan<byte> bytes) => SerialFirmwarePackage.HasHeader(bytes) &&
        bytes.Length>=12 && BinaryPrimitives.ReadUInt32LittleEndian(bytes[8..])==BoardId;
    public static uint Crc32(ReadOnlySpan<byte> data) => SerialFirmwarePackage.Crc32(data);
    public static D015FirmwarePackage Parse(ReadOnlySpan<byte> bytes) {
        var package=SerialFirmwarePackage.Parse(bytes);
        uint stack=BinaryPrimitives.ReadUInt32LittleEndian(bytes[HeaderBytes..]);
        if(package.BoardId!=BoardId || package.AppAddress!=AppAddress || package.PayloadLength>Capacity ||
           stack<0x20000140 || stack>0x20002000)
            throw new InvalidDataException("升级包与当前 D015 板型、分区或 SRAM 范围不匹配。");
        return new(package.Version,package.PayloadCrc,package.PayloadLength);
    }
}
