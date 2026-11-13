using System.Buffers.Binary;
using System.IO;
namespace BmsTool.Windows;

// Format 2 is an upgrade contract, not a list of product names. Physical SRAM
// limits and Flash programming permissions are checked again by the target Boot.
public sealed record SerialFirmwarePackage(uint BoardId,uint AppAddress,uint Version,uint PayloadCrc,int PayloadLength)
{
    public const uint Magic=0x32553044;
    public const int HeaderBytes=32, PageBytes=1024, MaximumPayloadBytes=16*1024*1024;
    public string BoardName => BoardId switch {
        0xD0150320=>"D015", 0xC0600020=>"C060", _=>$"板号 0x{BoardId:X8}"
    };
    public static bool HasHeader(ReadOnlySpan<byte> bytes) => bytes.Length>=4 && BinaryPrimitives.ReadUInt32LittleEndian(bytes)==Magic;
    internal static bool ValidLayout(uint address,uint capacity) => address>=0x08000000 && address<0x09000000 &&
        address%PageBytes==0 && capacity>0 && capacity<=MaximumPayloadBytes && capacity%PageBytes==0 &&
        (ulong)address+capacity<=0x09000000;
    public static uint Crc32(ReadOnlySpan<byte> data) {
        uint crc=uint.MaxValue;
        foreach(byte b in data) { crc^=b;for(int bit=0;bit<8;bit++)crc=(crc>>1)^((crc&1)!=0?0xEDB88320u:0); }
        return ~crc;
    }
    public static SerialFirmwarePackage Parse(ReadOnlySpan<byte> bytes) {
        if(bytes.Length<HeaderBytes || !HasHeader(bytes) ||
           BinaryPrimitives.ReadUInt16LittleEndian(bytes[4..])!=2 ||
           BinaryPrimitives.ReadUInt16LittleEndian(bytes[6..])!=HeaderBytes ||
           BinaryPrimitives.ReadUInt32LittleEndian(bytes[28..])!=Crc32(bytes[..28]))
            throw new InvalidDataException("YMODEM 升级包头、格式版本或包头 CRC 不正确。");
        uint board=BinaryPrimitives.ReadUInt32LittleEndian(bytes[8..]),address=BinaryPrimitives.ReadUInt32LittleEndian(bytes[12..]);
        uint length=BinaryPrimitives.ReadUInt32LittleEndian(bytes[16..]),version=BinaryPrimitives.ReadUInt32LittleEndian(bytes[20..]);
        uint crc=BinaryPrimitives.ReadUInt32LittleEndian(bytes[24..]);
        if(board is 0 or uint.MaxValue || !ValidLayout(address,length) || length!=(uint)(bytes.Length-HeaderBytes) ||
           crc!=Crc32(bytes[HeaderBytes..]))
            throw new InvalidDataException("YMODEM 升级包身份、地址、长度或整包 CRC 不正确。");
        var app=bytes[HeaderBytes..];
        uint stack=BinaryPrimitives.ReadUInt32LittleEndian(app),entry=BinaryPrimitives.ReadUInt32LittleEndian(app[4..]);
        uint actual=BinaryPrimitives.ReadUInt32LittleEndian(app[204..]);
        if(BinaryPrimitives.ReadUInt32LittleEndian(app[192..])!=board ||
           BinaryPrimitives.ReadUInt32LittleEndian(app[196..])!=address ||
           BinaryPrimitives.ReadUInt32LittleEndian(app[200..])!=version ||
           actual<=208 || actual>length || ((actual+1023)&~1023u)!=length ||
           stack<0x20000000 || stack>=0x40000000 || (stack&7)!=0 || (entry&1)==0 ||
           (entry&~1u)<address+208 || (entry&~1u)>=address+actual)
            throw new InvalidDataException("YMODEM APP 身份、向量或链接长度不正确。");
        return new(board,address,version,crc,(int)length);
    }
}
