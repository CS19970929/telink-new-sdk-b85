using System.Buffers.Binary;
using BmsTool.Windows;

static class Test
{
    static int checks;
    static void Check(bool ok,string message) { if(!ok)throw new Exception(message);checks++; }
    static void Reject(Action action,string message) {
        bool refused=false;try{action();}catch(InvalidDataException){refused=true;}
        Check(refused,message);
    }
    static byte[] Package(uint board,uint address,int length=2048) {
        byte[] data=new byte[length+32];data.AsSpan(32).Fill(0xFF);
        void Put(int at,uint v)=>BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(at),v);
        Put(0,0x32553044);BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(4),2);
        BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(6),32);
        Put(8,board);Put(12,address);Put(16,(uint)length);Put(20,0x00010002);
        Put(32,0x20002000);Put(36,address+209);
        Put(224,board);Put(228,address);Put(232,0x00010002);Put(236,(uint)length-16);
        Put(24,D015FirmwarePackage.Crc32(data.AsSpan(32)));Put(28,D015FirmwarePackage.Crc32(data.AsSpan(0,28)));
        return data;
    }
    static SerialUpgradeDevice Device(uint board,uint address,int capacity) {
        ushort[] cap={0x5550,1,(ushort)(board>>16),(ushort)board,2,2,(ushort)(address>>16),(ushort)address,
          (ushort)(capacity>>16),(ushort)capacity,0,0};
        return UpgradeDetection.ParseCapabilities(cap) ?? throw new Exception("missing capability");
    }
    public static void Main(string[] args) {
        string work=args[0];Directory.CreateDirectory(work);
        string c060=Path.Combine(work,"synthetic-c060.test-package");
        File.WriteAllBytes(c060,Package(0xC0600020,0x08001C00));
        if(args.Length>1)c060=args[1];
        var inspection=FirmwareImage.Inspect(c060);
        Check(!inspection.D015Package,"C060 cannot be classified as D015");
        Check(inspection.Description.Contains("C060"),"C060 inspection names the actual board");
        var image=FirmwareImage.LoadForTarget(c060,OtaTargetKind.Stm32SerialIap);
        var device=Device(0xC0600020,0x08001C00,51*1024);
        UpgradeDetection.CheckImage(device,image);
        Check(!image.IsD015Package,"C060 image remains separate from D015");
        image.Bytes[240]^=1;
        Reject(()=>UpgradeDetection.CheckImage(device,image),"modified loaded bytes rejected before ENTER");
        image.Bytes[240]^=1;
        Reject(()=>D015FirmwarePackage.Parse(image.Bytes),"strict D015 API does not accept C060");
        Console.WriteLine("PASS: user's valid C060 package accepted without D015 misclassification");
        string d015=Path.Combine(work,"synthetic-d015.test-package");
        File.WriteAllBytes(d015,Package(0xD0150320,0x08002000));
        var d015image=FirmwareImage.LoadForTarget(d015,OtaTargetKind.Stm32SerialIap);
        var d015device=Device(0xD0150320,0x08002000,44*1024);
        UpgradeDetection.CheckImage(d015device,d015image);
        Reject(()=>UpgradeDetection.CheckImage(device,d015image),"D015 package refused on C060");
        Reject(()=>UpgradeDetection.CheckImage(d015device,image),"C060 package refused on D015");
        var other=device with { Address=0x08002000 };
        Reject(()=>UpgradeDetection.CheckImage(other,image),"same board with wrong APP address refused");
        other=device with { Capacity=1024 };
        Reject(()=>UpgradeDetection.CheckImage(other,image),"oversize package refused");
        foreach(int length in new[]{1024,51*1024,128*1024}) {
            // This board number is deliberately absent from product support tables.
            string path=Path.Combine(work,"unknown-board.test-package");
            byte[] bytes=Package(0x1234ABCD,0x08004000,length);File.WriteAllBytes(path,bytes);
            var next=FirmwareImage.LoadForTarget(path,OtaTargetKind.Stm32SerialIap);
            UpgradeDetection.CheckImage(Device(0x1234ABCD,0x08004000,128*1024),next);
            Check(!next.IsD015Package,"new board accepted by contract, without host allow-list");
            foreach(int offset in new[]{8,12,16,20,24,28,240,bytes.Length-1}) {
                byte[] bad=bytes.ToArray();bad[offset]^=1;File.WriteAllBytes(path,bad);
                Reject(()=>FirmwareImage.LoadForTarget(path,OtaTargetKind.Stm32SerialIap),$"bad package @{offset} refused");
            }
        }
        string renamed=Path.Combine(work,"renamed.d015");File.Copy(c060,renamed,true);
        Check(!FirmwareImage.Inspect(renamed).D015Package,"extension does not override package identity");
        Reject(()=>FirmwareImage.LoadForTarget(c060,OtaTargetKind.Telink),"serial package cannot use Telink radio OTA");
        Console.WriteLine($"PASS: {checks} package/capability/cross-project compatibility checks; no device access");
    }
}
