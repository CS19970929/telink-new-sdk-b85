using BmsTool.Windows;
using BmsTool.Android;
using System.Buffers.Binary;
using System.Text.RegularExpressions;

static class Test
{
    static int checks;
    static void Check(bool ok,string why){if(!ok)throw new Exception(why);checks++;}
    static void Reject(Action a,string why){bool bad=false;try{a();}catch(Exception e)when(e is ArgumentException or IOException or InvalidDataException){bad=true;}Check(bad,why);}
    public static void Main(string[] args){
        string work=args[0];Directory.CreateDirectory(work);
        byte[] original=File.ReadAllBytes(args[1]);
        Check(FirmwareImage.LoadForBluetoothUpgrade(args[1]).Package?.BoardId==0xC0600020,"actual C060 file uses common YMODEM route");
        byte[] future=original.ToArray();uint oldAddress=BinaryPrimitives.ReadUInt32LittleEndian(future.AsSpan(12));
        uint entry=BinaryPrimitives.ReadUInt32LittleEndian(future.AsSpan(36));
        void Put(int at,uint value)=>BinaryPrimitives.WriteUInt32LittleEndian(future.AsSpan(at),value);
        Put(8,0x1234ABCD);Put(224,0x1234ABCD);Put(12,0x08004000);Put(228,0x08004000);Put(36,entry-oldAddress+0x08004000);
        Put(24,SerialFirmwarePackage.Crc32(future.AsSpan(32)));Put(28,SerialFirmwarePackage.Crc32(future.AsSpan(0,28)));
        string path=Path.Combine(work,"unknown-project.d016");File.WriteAllBytes(path,future);
        Check(FirmwareImage.LoadForBluetoothUpgrade(path).Package?.BoardId==0x1234ABCD,"new project and extension need no table");
        future[240]^=1;File.WriteAllBytes(path,future);Reject(()=>FirmwareImage.LoadForBluetoothUpgrade(path),"corrupt generic package refused");
        byte[] telink=Enumerable.Range(0,36).Select(i=>(byte)i).ToArray();telink[8]=0x4B;telink[9]=0x4E;telink[10]=0x4C;telink[11]=0x54;
        BitConverter.GetBytes((uint)36).CopyTo(telink,24);BitConverter.GetBytes(TelinkOtaProtocol.CalculateTelinkCrcTrailer(telink.AsSpan(0,32))).CopyTo(telink,32);
        path=Path.Combine(work,"telink.sample");File.WriteAllBytes(path,telink);
        Check(FirmwareImage.LoadForBluetoothUpgrade(path).TargetKind==OtaTargetKind.Telink,"strict Telink route retained");
        telink[16]^=1;File.WriteAllBytes(path,telink);Reject(()=>FirmwareImage.LoadForBluetoothUpgrade(path),"bad Telink CRC refused");
        foreach(string name in new[]{"C060_APP.c060","app.d015","app.d016","new-project.update","firmware.bin"})Check(AndroidFirmwareInbox.IsValidFileName(name),"known/future import filename");
        foreach(string name in new[]{"../app.c060","x/y.bin","x\\y.bin",".incoming.part","",new string('x',100)})Check(!AndroidFirmwareInbox.IsValidFileName(name),"import cannot escape its directory");
        var state=new EndpointState();state.Check("EC:F6:4C:E9:F1:3D",false);checks++;
        Reject(()=>state.Check("",false),"empty target never chooses first device");
        Reject(()=>state.Check("ECF64CE9F13D",false),"malformed target refused");
        Reject(()=>state.Check("EC:F6:4C:E9:F1:3D",true),"automation requires a live existing session");
        state._connectedMac="EC:F6:4C:E9:F1:3D";state._client=new();state._transport=new();state.Check(state._connectedMac,true);checks++;
        Reject(()=>state.Check("00:11:22:33:44:55",false),"connected device cannot silently change");
        Console.WriteLine($"PASS Android firmware routing and selected-endpoint policy: {checks} checks; no device access");
    }
}
partial class EndpointState
{
    public string? _connectedMac;public object? _client,_transport;
    public void Check(string mac,bool requireExisting)=>CheckUpgradeEndpoint(mac,requireExisting);
}
namespace Android.App {internal sealed class Placeholder {}}
namespace Android.Util {internal sealed class Placeholder {}}
namespace Android.Content {internal sealed class Context {public FileDirectory? FilesDir {get;set;}} internal sealed class FileDirectory {public string AbsolutePath {get;set;}="";}}
