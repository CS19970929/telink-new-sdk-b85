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
        Check(capture.Boot.Any(f=>f.Field=="Git 工作区"&&f.Value=="CLEAN"),"clean firmware provenance");
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
        Check(capture.Current.Any(f=>f.Field=="AFE 原始电流"&&f.Value.Contains("-123")),"runtime raw current");
        Check(capture.Current.Any(f=>f.Field=="持久化 gain"&&f.Value.Contains("1000000")),"parameter calibration evidence");
        Check(capture.Soc.Any(f=>f.Field=="SOC estimate"&&f.Value.Contains("73")),"runtime SOC");
        Check(capture.Soc.Any(f=>f.Field=="Time To Empty"&&f.Value.Contains("180")),"runtime TTE");
        Check(capture.Soc.Any(f=>f.Field=="Last Learning Reject Reason"&&f.Value.Contains("CANDIDATE_INCONSISTENT")),"learning rejection decode");
        var soc=BmsDiagnostics.DecodeSocSnapshot(capture.Words!);
        Check(soc.SocEstimate==73&&soc.SocDisplay==72&&soc.TimeToEmptyMinutes==180,"typed SOC diagnostics");
        Check(soc.EtaValid&&soc.EtaDirection=="DISCHARGE"&&soc.FilteredCurrentMa==10000,"typed ETA diagnostics");
        Check(soc.TimeToFullMinutes is null,"unavailable ETA sentinel");
        Check(soc.CandidateCapacityAh==91.0&&soc.LastLearningRejectReason=="CANDIDATE_INCONSISTENT","typed learning diagnostics");
        Check(soc.RuntimeVersion==3&&soc.LastSampleState=="ACCEPTED"&&soc.LastIntegralDirection=="DISCHARGE"&&
            soc.LastSocAction=="INTEGRATE"&&soc.LastSocBefore==74&&soc.LastSocAfter==73&&soc.LastIntegralDeltaAs10==42,"typed SOC decision diagnostics");
        var socTest=await BmsTestEngine.RunSocAsync(b,"mock",2,TimeSpan.Zero);
        Check(socTest.Passed&&socTest.Endpoint=="mock"&&socTest.RequestedSamples==2&&socTest.SuccessfulSamples==2&&
            socTest.FirmwareBuildId=="12345678"&&socTest.Checks.Any(x=>x.Id=="soc.capacity"&&x.Status==BmsTestStatus.Pass),"shared SOC test engine");
        var diagTest=await BmsTestEngine.RunDiagnosticsAsync(b,"mock",2,true,TimeSpan.Zero);
        Check(diagTest.Passed&&diagTest.SuccessfulSamples==2&&diagTest.Checks.Any(x=>x.Id=="diag.trace"&&x.Status==BmsTestStatus.Pass),"shared diagnostics test engine");
        var runtimeV1=capture.Words!.ToArray();runtimeV1[192]=1;bool oldSocRejected=false;
        try {BmsDiagnostics.DecodeSocSnapshot(runtimeV1);}catch(InvalidDataException){oldSocRejected=true;}
        Check(oldSocRejected,"runtime v1 must not be decoded as SOC v2");
        Check(capture.Power.Any(f=>f.Field=="阻断原因"&&f.Value.Contains("电流达到suspend门槛")),"PM reason decode");
        Check(capture.Power.Any(f=>f.Field=="设备运行模式"&&f.Value.Contains("FACTORY")),"factory runtime mode");
        Check(capture.Protection.Any(f=>f.Value.Contains("放电过流")),"runtime protection decode");
        Check(capture.Trace.Count==1&&capture.Trace[0].Arg0==0x12345678,"trace/endian");
        Check(capture.SoftwareProtectionWords?.Length==65 && capture.SoftwareProtectionWords[0]==3750 && capture.SoftwareProtectionWords[64]==100,"software parameter read/endian");
        Check(capture.EvidenceBlocks.ContainsKey("AfeRequested")&&capture.EvidenceBlocks["AfeRequested"].Length==35,"AFE evidence capture");
        Check(capture.EvidenceBlocks.ContainsKey("D008CapabilityTail")&&capture.EvidenceBlocks.ContainsKey("Balance"),"D008 protocol v2 evidence capture");
        var shWords=capture.Words!.ToArray();shWords[14]=0x3510;shWords[29]=BmsDiagnostics.GenericFetBitsInfo;
        shWords[2]=(ushort)(BmsDiagnostics.BootCapability|BmsDiagnostics.TraceCapability|BmsDiagnostics.StorageCapability|BmsDiagnostics.MosCapability|BmsDiagnostics.RuntimeCapability);
        shWords[130]=3;shWords[131]=1;shWords[132]=2;shWords[133]=1;
        var shCapture=new DiagnosticCapture();BmsDiagnostics.Decode(shCapture,shWords);
        Check(shCapture.Boot.Any(f=>f.Field=="MCU / AFE"&&f.Value.Contains("SH3673510")),"SH AFE identity");
        Check(shCapture.Mos.Any(f=>f.Field=="AFE Command bits"&&f.Value.Contains("SH command"))&&!shCapture.Mos.Any(f=>f.Field.Contains("R81")),"SH FET command semantics");
        Check(shCapture.Mos.Any(f=>f.Field=="AFE Status bits"&&f.Value.Contains("BSTATUS1"))&&!shCapture.Mos.Any(f=>f.Field.Contains("R6")),"SH FET status semantics");
        Check(shCapture.Current.Any(f=>f.Field=="AFE 换算电流（无独立 raw）"),"SH current semantics");
        Check(shCapture.Power.Any(f=>f.Value.Contains("未声明 D008 PM")),"SH reserved PM fields");
        t.NonD008=true;var nonD008=await b.ReadDiagnosticsAsync(true,"sh-product");t.NonD008=false;
        Check(!nonD008.EvidenceBlocks.ContainsKey("D008Capability")&&nonD008.EvidenceBlocks.ContainsKey("AfeRequested"),"AFE evidence must not depend on D008 capability");
        var health=BmsHealth.Evaluate(capture,new DeviceIdentity("AA","SN","D008","V1","BT_D008"),new BatterySnapshot{
            MinCellMv=3000,MaxCellMv=3200,CellDeltaMv=200,CellMillivolts=new ushort[]{3000,3200}});
        Check(health.Overall==BmsHealthStatus.Critical&&health.Checks.Any(x=>x.Id=="d008.protocol"&&x.Status==BmsHealthStatus.Pass),"health assessment and D008 capability");
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
            Check(zip.GetEntry("manifest.json")!=null&&zip.GetEntry("raw_frames.json")!=null&&zip.GetEntry("storage.json")!=null&&zip.GetEntry("health.json")!=null&&
                zip.GetEntry("summary.md")!=null&&zip.GetEntry("current.json")!=null&&zip.GetEntry("power.json")!=null&&
                zip.GetEntry("parameters.json")!=null&&zip.GetEntry("afe.json")!=null&&zip.GetEntry("evidence.json")!=null,"bundle members");
            using(var reader=new StreamReader(zip.GetEntry("software_protection.json")!.Open())) {
                var text=reader.ReadToEnd();Check(text.Contains("3750") && text.Contains("0x2100"),"parameter ZIP content");
            }
            Check(partial.Frames.All(f=>f.Direction!="TX" || f.Hex.StartsWith("0103")),"no privileged frames");
            var before=Path.Combine(dir,"before.zip");var after=Path.Combine(dir,"after.zip");
            CreateComparisonBundle(before,"8af89d22",73);CreateComparisonBundle(after,"12345678",74);
            var comparison=DiagnosticBundleComparer.Compare(before,after);
            Check(comparison.Differences.Any(x=>x.Category=="identity"&&x.Entry=="manifest.json"&&x.Path=="$.firmware_git_commit")&&
                comparison.Differences.Any(x=>x.Category=="runtime"&&x.Entry=="soc.json"&&x.Path=="$[field=SOC estimate].value"),"offline diagnostic bundle comparison");
            var runtimeComparison=DiagnosticBundleComparer.Filter(comparison,"runtime");
            Check(runtimeComparison.DifferenceCount==1&&runtimeComparison.Differences[0].Entry=="soc.json","diagnostic comparison category filter");
        } finally {Directory.Delete(dir,true);}
        Check(ProductSupportMatrix.Products.Count==4&&ProductSupportMatrix.Products.Any(x=>x.Product=="D013"&&x.DiagnosticsSchema==1),"product support matrix");
        var malformed=new DiagnosticCapture();bool rejected=false;
        try {BmsDiagnostics.Decode(malformed,new ushort[2]);}catch(InvalidDataException){rejected=true;}
        Check(rejected,"short snapshot");
        Console.WriteLine("PASS Windows diagnostics: runtime current/SOC/PM/protection decode, parameter+AFE evidence, legacy/exceptions, timeout/cancel, bounded trace retry, AI ZIP and read-only frames");
    }

    static void CreateComparisonBundle(string path,string build,int soc)
    {
        using var zip=ZipFile.Open(path,ZipArchiveMode.Create);
        foreach(string name in new[]{"manifest.json","boot.json","storage.json","current.json","soc.json","power.json","protection_runtime.json","parameters.json","afe.json","health.json"}) {
            using var writer=new StreamWriter(zip.CreateEntry(name).Open());
            writer.Write(name=="manifest.json"?$"{{\"firmware_git_commit\":\"{build}\"}}":
                name=="soc.json"?$"[{{\"field\":\"SOC estimate\",\"value\":{soc}}}]":"{}");
        }
    }
}
sealed class FakeTransport:IBmsTransport
{
    public bool IsConnected=>true;
    public string DiscoveryDescription=>"test";
    public event Action<ReadOnlyMemory<byte>>? DataReceived;
    public event Action<string>? ConnectionProgress {add{}remove{}}
    public bool Legacy,Unstable,FailEvents,FailProtection,Timeout,NonD008;
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
        if(NonD008&&start==0x2E00) {Emit(ModbusRtu.Frame(new byte[]{1,0x83,2}));return Task.CompletedTask;}
        if(ExceptionCode!=0 || (FailEvents&&start==0xC008) || (FailProtection&&start==0x2100)) {Emit(ModbusRtu.Frame(new byte[]{1,0x83,ExceptionCode==0?(byte)2:ExceptionCode}));return Task.CompletedTask;}
        var w=new ushort[1024];
        if(!Legacy) {
            w[0]=0x4447;w[1]=1;w[2]=0x002F;w[3]=1;w[6]=100;w[8]=(ushort)traceSeq;w[12]=1;w[14]=0x1124;w[15]=0x8251;
            w[18]=0x5678;w[19]=0x1234;w[22]=0x5678;w[23]=0x1234;w[36]=2;w[37]=3;w[38]=3;w[128]=3;w[136]=3;w[138]=2;
            w[192]=3;w[193]=3;
            w[194]=unchecked((ushort)-123);w[195]=0xFFFF;w[196]=456;w[197]=0;
            w[198]=90;w[199]=0;w[200]=200;w[202]=73;w[203]=72;w[204]=2;w[205]=74;w[206]=69;w[207]=79;
            w[208]=90;w[209]=600;w[210]=1;w[211]=1;w[212]=580;
            w[213]=16;w[214]=0;w[215]=0;w[216]=3;w[217]=120;w[219]=1;w[220]=0;w[221]=500;
            w[222]=0;w[223]=0;w[224]=0x20;w[225]=1;
            w[226]=1;w[227]=1;w[228]=2;w[229]=1;w[230]=0x0307;
            w[231]=1000;w[232]=950;w[233]=700;w[234]=10000;w[235]=0;w[236]=100;
            w[237]=180;w[238]=0xFFFF;w[239]=(ushort)(2|(2<<4)|(90<<8));
            w[240]=95;w[241]=2;w[242]=100;w[243]=910;w[244]=4;w[245]=2;w[246]=12;w[247]=75;w[248]=3400;
            w[249]=(ushort)(5|(2<<4)|(1<<8));w[250]=6400;w[252]=42;w[254]=(ushort)(74|(73<<8));w[255]=73;
            w[256]=(ushort)traceSeq;w[260]=3;w[262]=0x5678;w[263]=0x1234;
        }
        if(Unstable&&start==0x2A00)traceSeq++;
        var body=new byte[3+count*2];body[0]=1;body[1]=3;body[2]=(byte)(count*2);
        for(int i=0;i<count;i++) {
            int at=start-0x2A00+i;ushort address=(ushort)(start+i);
            ushort value=at>=0&&at<w.Length?w[at]:(ushort)0;
            if(start==0x2100)value=i==0?(ushort)3750:(ushort)100;
            if(address==0x2E00)value=0xD008;
            else if(address==0x2E01)value=2;
            else if(address==0x2E02)value=0x007F;
            else if(address==0x2E05)value=4;
            else if(address==0x2E0C)value=1;
            else if(address==0x2E0D)value=3400;
            else if(address==0x2E0E)value=50;
            else if(address==0x2E0F)value=30;
            else if(address==0x2E24)value=unchecked((ushort)-12);
            else if(address==0x2E25)value=0xFFFF;
            else if(address==0x2E26)value=0x4240;
            else if(address==0x2E27)value=0x000F;
            else if(address==0x2E28)value=unchecked((ushort)-123);
            else if(address==0x2E29)value=0xFFFF;
            else if(address==0x2E2A)value=456;
            else if(address==0x2E2B)value=0;
            else if(address==0x2E2C)value=1;
            else if(address==0x2E2D)value=90;
            else if(address==0x2E2E)value=0;
            else if(address==0x2E70)value=1;
            else if(address==0x2E71)value=3400;
            else if(address==0x2E72)value=50;
            else if(address==0x2E73)value=30;
            else if(address>=0x2500&&address<0x2523)value=(ushort)(address-0x2500+1);
            else if(address>=0x2523&&address<0x252C)value=(ushort)(address-0x2523+100);
            else if(address>=0x2540&&address<0x2563)value=(ushort)(address-0x2540+200);
            BinaryPrimitives.WriteUInt16BigEndian(body.AsSpan(3+i*2,2),value);
        }
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
