using System.Buffers.Binary;
using System.IO.Compression;
using BmsTool.Windows;

static class Test
{
    static void Check(bool ok,string why) {if(!ok)throw new Exception(why);}
    static async Task Main()
    {
        var t=new FakeTransport();await using var b=new BmsClient(t);
        var capture=await b.ReadDiagnosticsAsync(true,"mock");
        Check(capture.Supported&&capture.SnapshotConsistent&&capture.TraceConsistent,"capability/snapshot");
        Check(capture.Boot.Any(f=>f.Value.Contains("旧固件未提供")),"legacy upgrade detail unavailable");
        var upgradeWords=capture.Words!.ToArray();upgradeWords[2]|=16;
        upgradeWords[27]=3;upgradeWords[28]=3;upgradeWords[98]=2200;upgradeWords[102]=3200;
        upgradeWords[106]=1;upgradeWords[108]=2;
        var upgrade=new DiagnosticCapture();BmsDiagnostics.Decode(upgrade,upgradeWords);
        Check(upgrade.Boot.Any(f=>f.Value=="候选参数校验失败"),"upgrade stage");
        Check(upgrade.Boot.Any(f=>f.Value=="软件保护参数；AFE 硬件参数"),"multiple validation failures");
        Check(upgrade.Boot.Any(f=>f.Field.StartsWith("升级候选 CUV")&&f.Value.Contains("3200")),"candidate snapshot");
        Check(capture.Errors.Count==0,"unexpected errors: "+string.Join(";",capture.Errors));
        Check(capture.Storage.Any(f=>f.Value.Contains("布局拒绝")),"storage failure explanation");
        Check(capture.Mos.Any(f=>f.Value.Contains("启动存储升级未完成")),"MOS reason");
        Check(capture.Mos.Any(f=>f.Value=="不可用 / unknown"),"physical feedback");
        Check(capture.Trace.Count==1&&capture.Trace[0].Arg0==0x12345678,"trace/endian");
        Check(capture.SoftwareProtectionWords?.Length==65 && capture.SoftwareProtectionWords[0]==3750 && capture.SoftwareProtectionWords[64]==100,"software parameter read/endian");
        Check(t.Writes==0,"diagnostic must be read-only");
        t.Unstable=true;var moving=await b.ReadDiagnosticsAsync(true,"mock");
        Check(!moving.TraceConsistent&&moving.Errors.Any(e=>e.Contains("分页")),"moving trace must be flagged");
        t.Unstable=false;t.Legacy=true;var old=await b.ReadDiagnosticsAsync(false,"old");
        Check(!old.Supported&&old.Status.Contains("不支持"),"legacy zero response");
        t.Legacy=false;t.ExceptionCode=2;var unsupported=await b.ReadDiagnosticsAsync(false,"old");
        Check(!unsupported.Supported&&unsupported.Status.Contains("不支持"),"illegal address");
        t.ExceptionCode=0;t.FailEvents=true;var partial=await b.ReadDiagnosticsAsync(true,"partial");
        Check(partial.Supported&&partial.Errors.Any(e=>e.StartsWith("Events:")),"optional error preserves snapshot");
        t.FailEvents=false;t.FailProtection=true;
        var missingParams=await b.ReadDiagnosticsAsync(true,"missing parameters");
        Check(missingParams.Supported && missingParams.SoftwareProtectionWords is null && missingParams.Errors.Any(e=>e.StartsWith("SoftwareProtection:")),"parameter failure preserves diagnostics");
        t.FailProtection=false;t.Timeout=true;
        var timedOut=await b.ReadDiagnosticsAsync(false,"timeout");
        Check(timedOut.Status=="诊断通信/解码失败"&&timedOut.Errors.Any(e=>e.Contains("TimeoutException")),"real timeout");
        using var cancel=new CancellationTokenSource(100);
        var failed=await b.ReadDiagnosticsAsync(false,"timeout",cancel.Token);
        Check(!failed.Status.Contains("不支持")&&failed.Errors.Count>0,"timeout is not unsupported");
        var dir=Path.Combine(Path.GetTempPath(),"diag-export-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(dir);
        try {
            var path=Path.Combine(dir,"fault.zip");BmsDiagnostics.Export(path,partial);
            using var zip=ZipFile.OpenRead(path);
            Check(zip.GetEntry("manifest.json")!=null&&zip.GetEntry("raw_frames.json")!=null&&zip.GetEntry("storage.json")!=null,"bundle members");
            using(var reader=new StreamReader(zip.GetEntry("software_protection.json")!.Open())) {
                var text=reader.ReadToEnd();Check(text.Contains("3750") && text.Contains("0x2100"),"parameter ZIP content");
            }
            Check(partial.Frames.All(f=>f.Direction!="TX" || f.Hex.StartsWith("0103")),"no privileged frames");
        } finally {Directory.Delete(dir,true);}
        var malformed=new DiagnosticCapture();bool rejected=false;
        try {BmsDiagnostics.Decode(malformed,new ushort[2]);}catch(InvalidDataException){rejected=true;}
        Check(rejected,"short snapshot");
        Console.WriteLine("PASS Windows diagnostics: actual client+fragmented transport, decode, legacy/exceptions, timeout/cancel, bounded trace retry, partial ZIP and read-only frame capture");
    }
}
sealed class FakeTransport:IBmsTransport
{
    public bool IsConnected=>true;
    public string DiscoveryDescription=>"test";
    public event Action<ReadOnlyMemory<byte>>? DataReceived;
    public event Action<string>? ConnectionProgress {add{}remove{}}
    public bool Legacy,Unstable,FailEvents,FailProtection,Timeout;
    public byte ExceptionCode;
    public int Writes;
    private uint traceSeq=1;
    public Task ReconnectAsync(CancellationToken ct=default)=>Task.CompletedTask;
    public ValueTask DisposeAsync()=>ValueTask.CompletedTask;
    public Task WriteAsync(ReadOnlyMemory<byte> data,CancellationToken ct=default)
    {
        var q=data.Span;if(q[1]!=3)Writes++;
        if(Timeout)return Task.CompletedTask;
        ushort start=BinaryPrimitives.ReadUInt16BigEndian(q[2..4]);ushort count=BinaryPrimitives.ReadUInt16BigEndian(q[4..6]);
        if(ExceptionCode!=0 || (FailEvents&&start==0xC008) || (FailProtection&&start==0x2100)) {Emit(ModbusRtu.Frame(new byte[]{1,0x83,ExceptionCode==0?(byte)2:ExceptionCode}));return Task.CompletedTask;}
        var w=new ushort[1024];
        if(!Legacy) {
            w[0]=0x4447;w[1]=1;w[2]=15;w[3]=1;w[6]=100;w[8]=(ushort)traceSeq;w[12]=1;
            w[18]=0x5678;w[19]=0x1234;w[36]=2;w[37]=3;w[38]=3;w[128]=3;w[136]=3;w[138]=2;
            w[256]=(ushort)traceSeq;w[260]=3;w[262]=0x5678;w[263]=0x1234;
        }
        if(Unstable&&start==0x2A00)traceSeq++;
        var body=new byte[3+count*2];body[0]=1;body[1]=3;body[2]=(byte)(count*2);
        for(int i=0;i<count;i++) {int at=start-0x2A00+i;ushort value=at>=0&&at<w.Length?w[at]:(ushort)0;if(start==0x2100)value=i==0?(ushort)3750:(ushort)100;BinaryPrimitives.WriteUInt16BigEndian(body.AsSpan(3+i*2,2),value);}
        Emit(ModbusRtu.Frame(body));return Task.CompletedTask;
    }
    private void Emit(byte[] frame) {for(int i=0;i<frame.Length;i+=7)DataReceived?.Invoke(frame.AsMemory(i,Math.Min(7,frame.Length-i)));}
}

// Host-only type identity for the unrelated MTU guard in parameter writes.
namespace BmsTool.Windows {
 abstract class BmsBleTransport:IBmsTransport {
    public int? NegotiatedMtu=>null;
    public abstract bool IsConnected{get;}
    public abstract string DiscoveryDescription{get;}
    public abstract event Action<ReadOnlyMemory<byte>>? DataReceived;
    public abstract event Action<string>? ConnectionProgress;
    public abstract Task ReconnectAsync(CancellationToken ct=default);
    public abstract Task WriteAsync(ReadOnlyMemory<byte> data,CancellationToken ct=default);
    public abstract ValueTask DisposeAsync();
 }
}
