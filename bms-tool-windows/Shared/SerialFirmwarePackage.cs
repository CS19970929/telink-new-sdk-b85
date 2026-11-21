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
        0xD0150320=>"D015", 0xC0600020=>"C060", 0xC0770016=>"C077", 0xC073000D=>"C073", _=>$"板号 0x{BoardId:X8}"
    };
    public static bool HasHeader(ReadOnlySpan<byte> bytes) => bytes.Length>=4 && BinaryPrimitives.ReadUInt32LittleEndian(bytes)==Magic;
    public static byte[]? WrapC060Bin(ReadOnlySpan<byte> app)=>WrapAppBin(app,0xC0600020u);
    public static byte[]? WrapC077Bin(ReadOnlySpan<byte> app)=>WrapAppBin(app,0xC0770016u);
    public static byte[]? WrapC073Bin(ReadOnlySpan<byte> app)=>WrapAppBin(app,0xC073000Du);
    private static byte[]? WrapAppBin(ReadOnlySpan<byte> app,uint board)
    {
        int id = board == 0xC073000Du ? 0x104 : 192;
        if (app.Length < id + 16 || BinaryPrimitives.ReadUInt32LittleEndian(app[id..]) != board) return null;
        uint address = BinaryPrimitives.ReadUInt32LittleEndian(app[(id+4)..]);
        bool valid = board == 0xC073000Du ? C073Layout(address, (uint)app.Length)
            : address == 0x08001C00u && app.Length <= 51 * PageBytes;
        if (!valid) throw new InvalidDataException("APP BIN 地址或长度超出本板分区。");
        byte[] result=new byte[HeaderBytes+app.Length];
        BinaryPrimitives.WriteUInt32LittleEndian(result,Magic);
        BinaryPrimitives.WriteUInt16LittleEndian(result.AsSpan(4),2);
        BinaryPrimitives.WriteUInt16LittleEndian(result.AsSpan(6),HeaderBytes);
        app.Slice(id,8).CopyTo(result.AsSpan(8));
        BinaryPrimitives.WriteUInt32LittleEndian(result.AsSpan(16),(uint)app.Length);
        BinaryPrimitives.WriteUInt32LittleEndian(result.AsSpan(20),BinaryPrimitives.ReadUInt32LittleEndian(app[(id+8)..]));
        BinaryPrimitives.WriteUInt32LittleEndian(result.AsSpan(24),Crc32(app));
        BinaryPrimitives.WriteUInt32LittleEndian(result.AsSpan(28),Crc32(result.AsSpan(0,28)));
        app.CopyTo(result.AsSpan(HeaderBytes));
        _=Parse(result); // Validate embedded identity, vectors and padded length.
        return result;
    }
    private static bool C073Layout(uint address,uint size) =>
        address is 0x08003000u or 0x08001C00u && size <= 0x0800E800u - address;
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
        int id = board == 0xC073000Du ? 0x104 : 192;
        if (board == 0xC073000Du && (!C073Layout(address,length) || stack < 0x200000D0 || stack > 0x20005000))
            throw new InvalidDataException("C073 APP 分区或栈地址错误。");
        uint actual=BinaryPrimitives.ReadUInt32LittleEndian(app[(id+12)..]);
        if(BinaryPrimitives.ReadUInt32LittleEndian(app[id..])!=board ||
           BinaryPrimitives.ReadUInt32LittleEndian(app[(id+4)..])!=address ||
           BinaryPrimitives.ReadUInt32LittleEndian(app[(id+8)..])!=version ||
           actual<=id+16 || actual>length || ((actual+1023)&~1023u)!=length ||
           stack<0x20000000 || stack>=0x40000000 || (stack&7)!=0 || (entry&1)==0 ||
           (entry&~1u)<address+id+16 || (entry&~1u)>=address+actual)
            throw new InvalidDataException("YMODEM APP 身份、向量或链接长度不正确。");
        return new(board,address,version,crc,(int)length);
    }
}
