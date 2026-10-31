using System.Buffers.Binary;
using System.IO;
namespace BmsTool.Windows;

public sealed record D015FirmwarePackage(uint Version, uint PayloadCrc, int PayloadLength)
{
    public const uint Magic = 0x32553044, BoardId = 0xD0150320, AppAddress = 0x08002000;
    public const int HeaderBytes = 32, Capacity = 44 * 1024;
    public static bool HasHeader(ReadOnlySpan<byte> bytes) => bytes.Length >= 4 && BinaryPrimitives.ReadUInt32LittleEndian(bytes) == Magic;
    public static uint Crc32(ReadOnlySpan<byte> data)
    {
        uint crc = uint.MaxValue;
        foreach (byte b in data) { crc ^= b; for (int bit=0;bit<8;bit++) crc=(crc>>1)^((crc&1)!=0?0xEDB88320u:0); }
        return ~crc;
    }
    public static D015FirmwarePackage Parse(ReadOnlySpan<byte> bytes)
    {
        if (bytes.Length < HeaderBytes || bytes.Length > HeaderBytes + Capacity || !HasHeader(bytes) ||
            BinaryPrimitives.ReadUInt16LittleEndian(bytes[4..]) != 2 ||
            BinaryPrimitives.ReadUInt16LittleEndian(bytes[6..]) != HeaderBytes ||
            BinaryPrimitives.ReadUInt32LittleEndian(bytes[8..]) != BoardId ||
            BinaryPrimitives.ReadUInt32LittleEndian(bytes[12..]) != AppAddress ||
            BinaryPrimitives.ReadUInt32LittleEndian(bytes[28..]) != Crc32(bytes[..28]))
            throw new InvalidDataException("D015 升级包头、板型、地址或 CRC 不正确；请使用新工程导出的 .d015 文件。");
        uint size=BinaryPrimitives.ReadUInt32LittleEndian(bytes[16..]);
        uint version=BinaryPrimitives.ReadUInt32LittleEndian(bytes[20..]);
        uint crc=BinaryPrimitives.ReadUInt32LittleEndian(bytes[24..]);
        if(size==0 || size>Capacity || size%1024!=0 || size!=bytes.Length-HeaderBytes || crc!=Crc32(bytes[HeaderBytes..]))
            throw new InvalidDataException("D015 升级包长度或整包 CRC 校验失败。");
        ReadOnlySpan<byte> app=bytes[HeaderBytes..];
        uint stack=BinaryPrimitives.ReadUInt32LittleEndian(app), entry=BinaryPrimitives.ReadUInt32LittleEndian(app[4..]);
        uint actual=BinaryPrimitives.ReadUInt32LittleEndian(app[204..]);
        if(BinaryPrimitives.ReadUInt32LittleEndian(app[192..])!=BoardId ||
           BinaryPrimitives.ReadUInt32LittleEndian(app[196..])!=AppAddress ||
           BinaryPrimitives.ReadUInt32LittleEndian(app[200..])!=version ||
           actual<=208 || actual>size || ((actual+1023)&~1023u)!=size ||
           stack<0x20000140 || stack>0x20002000 || (stack&7)!=0 || (entry&1)==0 ||
           (entry&~1u)<AppAddress+208 || (entry&~1u)>=AppAddress+actual)
            throw new InvalidDataException("D015 APP 身份、向量或链接长度不正确。");
        return new(version,crc,(int)size);
    }
}
