using BmsTool.Windows;
using System.Text;
using System.Diagnostics;
using System.IO;

internal static class Program
{
    static void Check(bool value,string why) { if(!value)throw new Exception(why); }
    private static async Task CheckNativeCalibrationAsync(bool c077=false)
    {
        var peer=new Peer{C077=c077,Advertised=true,NativeCalibration=true,RawCurrentMa=230};
        await using var client=new BmsClient(peer);
        await client.ReadIdentityAsync();
        int identityWrites=peer.Writes;
        try{await client.WriteC060IdentityAsync(0xFFF2,"wrong",default);throw new Exception("C060 software version write accepted");}catch(NotSupportedException){}
        Check(peer.Writes==identityWrites,"C060 software version is read-only without a write");
        BmsBleTransport.ValidateDeviceName("BT_测试");
        foreach(string bad in new[]{"other","BT_"+new string('x',14),"BT_\n"}) {
            try{BmsBleTransport.ValidateDeviceName(bad);throw new Exception("invalid BLE name accepted");}catch(ArgumentException){}
        }
        var policy=new CurrentCalibrationPolicy(SampleCount:4,TimeoutSeconds:4);
        var session=await client.BeginCurrentCalibrationAsync(default);
        Check(session.NativeProduct==(c077?0xC077:0xC060) && session.Original==new CurrentCalibrationCoefficients(0,1000000),"native C060 lease and independent mA/ppm defaults");
        session.Zero=await client.CollectCurrentCalibrationAsync(session,true,policy,null,default);
        Check(session.Zero.MeanMa==230,"fresh CC zero samples");
        peer.RawCurrentMa=8230;
        session.Loaded=await client.CollectCurrentCalibrationAsync(session,false,policy,null,default);
        session.Candidate=policy.Calculate(session.Zero,session.Loaded,10000);
        Check(session.Candidate==new CurrentCalibrationCoefficients(230,1250000),"two point gain from external 10 A reference");
        int value=await client.SaveCurrentCalibrationAsync(session,session.Candidate,default);
        Check(value==10000 && session.SaveVerified && peer.NativeCoefficientWrites==1,"single atomic native save and fresh formula verification");
        Check(peer.SentRequests.Where(q=>q[1]==16&&q[2]==0x2e).All(q=>q.Length==19),"native grouped write works at MTU23");
        Check(peer.SentRequests.All(q=>q[1]!=0x42)&&peer.Reads.All(q=>q.Item1<0x2000||q.Item1>=0x205e),"native calibration never opens old AFE authorization or K/B registers");
        await client.EndCurrentCalibrationAsync(session);
        Check(peer.NativeToken==0,"native lease released");
        var restore=await client.BeginCurrentCalibrationAsync(default);
        Check(restore.Access.Token==session.Access.Token+1,"restore keeps generation continuity");
        Check(await client.SaveCurrentCalibrationAsync(restore,session.Original,default)==8230,"original parameters restore without calibration tables");
        await client.EndCurrentCalibrationAsync(restore);
        var failed=await client.BeginCurrentCalibrationAsync(default);
        peer.NativeToken=0;int before=peer.NativeCoefficientWrites;
        try{await client.SaveCurrentCalibrationAsync(failed,new(0,1000000),default);throw new Exception("stale token accepted");}catch(IOException){}
        Check(peer.NativeCoefficientWrites==before,"expired/restarted lease cannot write");
        await client.EndCurrentCalibrationAsync(failed);
        var wake=new Peer{Advertised=true};await using var awake=new BmsClient(wake);
        await awake.ReadIdentityAsync();await Task.Delay(3100);wake.SilentReads=1;
        await awake.WriteSingleRegisterAsync(0x1005,67);
        Check(wake.Soc==67&&wake.Writes==1&&wake.Reconnects==0,"idle C060 read wake retries without rebuilding GATT or repeating write");
        int writes=wake.Writes;wake.SilentWrites=1;
        try{await awake.WriteSingleRegisterAsync(0x1005,68,default,TimeSpan.FromMilliseconds(200));throw new Exception("missing write ACK accepted");}catch(TimeoutException){}
        Check(wake.Writes==writes+1,"lost write response never causes blind retry");
        Console.WriteLine($"PASS native {(c077?"C077":"C060")} calibration: real freshness intervals, offset/gain, one 19-byte save, readback/formula, restore, expired-token refusal; idle wake read retry and one-shot writes");
    }

    static async Task Main()
    {
        foreach(bool advertised in new[]{false,true}) {
            var t=new Peer{Advertised=advertised};await using var c=new BmsClient(t);
            await c.ProbeAsync();var id=await c.ReadIdentityAsync("endpoint","广播名");
            Check(c.IsC060 && id.Software.StartsWith("c060"),"automatic C060 selection");
            Check(id.Hardware=="测试硬件","GBK identity");
            for(int i=0;i<3;i++) {
                var data=await c.ReadBatteryAsync();
                Check(data.ValidCellCount==20 && data.SocPercent==55 && data.CurrentA==-2,"legacy units/direction/cells");
                Check(data.CellMillivolts[20]==61001 && !data.UsesRealtimeWindow,"missing cell and legacy realtime");
            }
            Check(!t.Reads.Any(r=>r.Item1==0xC012 || r.Item1==0xC022 || r==(0xD120,11)),"never send incompatible C060 identity/realtime addresses");
            Check(t.Writes==0 && t.Reconnects==0,"discovery is read-only without reconnect");
        }
        var silent=new Peer{SilentRealtime=true};await using(var c=new BmsClient(silent)) {
            await c.ProbeAsync();Check((await c.ReadBatteryAsync()).ValidCellCount==20&&silent.Reconnects==0,"old firmware silently ignores D120; D000 fallback without reopen");
        }
        var noIdentity=new Peer{NoIdentity=true};await using(var c=new BmsClient(noIdentity)) {
            await c.ProbeAsync();await c.ReadIdentityAsync();Check(!c.IsC060,"missing identity does not guess product");
            await c.SelectC060ParametersAsync();Check(c.IsC060,"explicit legacy parameter map without firmware identity changes");
            Check((await c.ReadC060ParametersAsync()).Blocks.ContainsKey("2318"),"manual selection retains available groups");
        }
        var unknown=new Peer{Unknown=true};await using(var c=new BmsClient(unknown)) {
            await c.ProbeAsync();await c.ReadIdentityAsync();
            Check(!c.IsC060,"do not infer C060 just from 20 cells");
            try {await c.ReadBatteryAsync();throw new Exception("missing realtime accepted");}catch(NotSupportedException){}
            Check((await c.ReadRegistersAsync(0x2000,2)).Length==2 && unknown.Reconnects==0,"unknown slave stays connected and communicates");
        }
        var generic=new Peer{SeparateIdentity=true};await using(var c=new BmsClient(generic)) {
            await c.ProbeAsync();var id=await c.ReadIdentityAsync();
            Check(!c.IsC060 && id.Hardware=="generic","separate identity fallback");
            Check((await c.ReadBatteryAsync()).ValidCellCount==20,"D120 exception cannot block legacy data");
            Check((await c.ReadBatteryAsync()).HasSystemStatus,"normal status remains available");
        }
        var noStatus=new Peer{NoStatus=true};await using(var c=new BmsClient(noStatus)) {
            await c.ReadIdentityAsync();Check(!(await c.ReadBatteryAsync()).HasSystemStatus,"missing status is unknown, not all-off evidence");
            await c.ReadBatteryAsync();Check(noStatus.Reads.Count(r=>r==(0xD115,2))==1,"unsupported optional window cached for session");
        }
        var protectionPeer=new Peer{NegotiatedMtu=23};await using(var c=new BmsClient(protectionPeer)) {
            await c.ReadIdentityAsync();int verified=0;
            var edits=Enumerable.Range(0,13).ToDictionary(i=>(ushort)(0x2104+i*5),i=>(ushort)110);
            await c.WriteProtectionChangesAsync(edits,(_,_)=>verified++);
            Check(verified==13&&protectionPeer.Writes==13&&protectionPeer.WriteLengths.All(n=>n==19),"all 13 groups atomic at MTU23");
            await c.WriteProtectionChangesAsync(edits,(_,_)=>throw new Exception("same-value write"));
            Check(protectionPeer.Writes==13,"same values do not write Flash");
            int before=protectionPeer.Writes;
            try {await c.WriteProtectionChangesAsync(new Dictionary<ushort,ushort>{{0x213E,60}},(_,_)=>{});throw new Exception("invalid SOC accepted");}catch(ArgumentException){}
            try {await c.WriteProtectionChangesAsync(new Dictionary<ushort,ushort>{{0x2102,0}},(_,_)=>{});throw new Exception("zero disables protection");}catch(ArgumentException){}
            Check(protectionPeer.Writes==before,"all preflight before writing");
            protectionPeer.FailWriteAddress=0x2105;
            try {await c.WriteProtectionChangesAsync(new Dictionary<ushort,ushort>{{0x2104,120},{0x2109,120}},(_,_)=>{});throw new Exception("partial failure ignored");}
            catch(System.IO.IOException ex){Check(ex.Message.Contains("此前1组")&&ex.Message.Contains("0x2105"),"partial progress reported");}
            Check(protectionPeer.Protection[4]==120&&protectionPeer.Protection[9]==110,"prior success retained and no automatic retry");
        }
        var corrupted=new Peer{CorruptReadback=true};await using(var c=new BmsClient(corrupted)) {
            await c.ReadIdentityAsync();
            try{await c.WriteProtectionChangesAsync(new Dictionary<ushort,ushort>{{0x2104,120}},(_,_)=>{});throw new Exception("bad readback accepted");}
            catch(System.IO.IOException e){Check(e.Message.Contains("回读校验"),"readback mismatch stage");}
        }
        var parameters=new Peer();await using(var c=new BmsClient(parameters)) {
            await c.ReadIdentityAsync();var capture=await c.ReadC060ParametersAsync();
            Check(capture.Errors.Count==0&&!capture.Blocks.ContainsKey("Calibration")&&C060Parameters.FieldsFor(false).All(f=>f.Address>=0x2300),"new C060 map excludes old K/B and segmented calibration reads");
            var capacity=C060Parameters.Fields.Single(f=>f.Address==0x2318);
            Check(capacity.Format(240)=="24"&&capacity.Parse("24.5")==245,"capacity parameter unit");
            try{capacity.Parse("24.05");throw new Exception("imprecise capacity accepted");}catch(ArgumentException){}
            parameters.Other[3]=44;
            await c.WriteC060ParameterChangesAsync(new Dictionary<ushort,ushort>{{0x2300,3900},{0x2318,245},{0x2319,12}},(_,_)=>{});
            Check(parameters.Other[0]==3900&&parameters.Other[3]==44&&parameters.Other[24]==245&&parameters.Other[26]==4200,"fresh read preserves untouched words");
            int count=parameters.Writes;await c.WriteC060ParameterChangesAsync(new Dictionary<ushort,ushort>{{0x2318,245}},(_,_)=>{});
            Check(parameters.Writes==count,"same conventional values skip writes");
            int oldWrites=parameters.Writes;
            try{await c.WriteC060ParameterChangesAsync(new Dictionary<ushort,ushort>{{0x2001,123}},(_,_)=>{});throw new Exception("old K/B write accepted");}catch(ArgumentException){}
            Check(parameters.Writes==oldWrites,"C060 rejects removed K/B before sending any write");
            await c.SetCycleCountAndVerifyAsync(77);
            Check(parameters.Other[25]==77&&parameters.SentRequests.Last(r=>r[1]==16&&r[2]==0x23)[5]==4,"quick cycle button uses whole four-word group");
            await c.SetSocAndVerifyAsync(66);Check(parameters.Soc==66,"SOC read from data, not write-only command register");
            int before=parameters.Writes;
            try{await c.WriteC060ParameterChangesAsync(new Dictionary<ushort,ushort>{{0x2311,3}},(_,_)=>{});throw new Exception("compatibility not gated");}catch(ArgumentException){}
            Check(parameters.Writes==before,"compatibility edits opt in");
            try{await c.WriteC060ParameterChangesAsync(new Dictionary<ushort,ushort>{{0x2311,3}},(_,_)=>{},true);throw new Exception("retired C060 field accepted");}catch(ArgumentException){}
            Check(parameters.Other[17]==120&&parameters.Other[16]==4600,"retired C060 fields remain untouched");
            try{await c.WriteC060ParameterChangesAsync(new Dictionary<ushort,ushort>{{0x231C,19}},(_,_)=>{});throw new Exception("physical map changed");}catch(ArgumentException){}
            await c.WriteC060IdentityAsync(0xFFF0,"中文SN");Check(BmsClient.DecodeIdentityText(parameters.Identity[..16])=="中文SN","GBK identity roundtrip");
            before=parameters.Writes;
            await c.WriteC060IdentityAsync(0xFFF0,"中文SN");Check(parameters.Writes==before,"identical identity skips Flash write");
            parameters.Calibration[1]=0x8000;
            try{await c.WriteC060ParameterChangesAsync(new Dictionary<ushort,ushort>{{0x2000,1025}},(_,_)=>{});throw new Exception("unencodable untouched B accepted");}catch(ArgumentException){}
            Check(parameters.Writes==before,"whole K/B pair preflight protects untouched negative offset");parameters.Calibration[1]=0;
            try{await c.WriteC060IdentityAsync(0xFFF0,new string('汉',17));throw new Exception("long identity accepted");}catch(ArgumentException){}
            Check(parameters.Writes==before,"GBK byte limit checked before writing");
            string folder=System.IO.Path.Combine(System.IO.Path.GetTempPath(),"CodexTemp","bms-tool-windows","package-"+Guid.NewGuid().ToString("N"));System.IO.Directory.CreateDirectory(folder);
            string file=System.IO.Path.Combine(folder,"parameters.json");C060Parameters.Export(file,capture);var imported=C060Parameters.Import(file);
            Check(imported.Blocks["2318"].SequenceEqual(capture.Blocks["2318"]),"parameter package roundtrip");
            System.IO.File.WriteAllText(file,System.IO.File.ReadAllText(file).Replace("\"Product\": \"C060\"","\"Product\": \"D015\""));
            try{C060Parameters.Import(file);throw new Exception("cross-product import accepted");}catch(System.IO.InvalidDataException){}
        }
        var mtuPeer=new Peer{NegotiatedMtu=23};await using(var c=new BmsClient(mtuPeer)) {
            await c.ReadIdentityAsync();
            try{await c.WriteC060ParameterChangesAsync(new Dictionary<ushort,ushort>{{0x2319,7},{0x2300,3900}},(_,_)=>{});throw new Exception("oversize group sent");}catch(System.IO.IOException){}
            Check(mtuPeer.Writes==0,"all groups MTU-preflight before any save");
            await c.WriteC060ParameterChangesAsync(new Dictionary<ushort,ushort>{{0x2319,7}},(_,_)=>{});Check(mtuPeer.Writes==1,"short group works at MTU23");
        }
        var normalized=C060Parameters.NormalizeCurrentGroup(new ushort[]{4000,4000,750,600,1,0,1000,30},2,4);
        Check(normalized[2]==1000&&normalized[3]==660,"production short-circuit quantization golden");
        await CheckNativeCalibrationAsync();await CheckNativeCalibrationAsync(true);
        Console.WriteLine("PASS C060 parameters: new field map without old K/B, fresh grouped saves, reserved fields, exact units, cycle/SOC, compatibility gating, GBK identity, package product checks, MTU preflight");
        Console.WriteLine("PASS C060 protection: 13 groups/65 words, MTU23 19-byte atomic writes, preflight, same-value skip, partial failure and bad readback");
        Console.WriteLine("PASS C060 connection: old/new capabilities, GBK, 20 cells/61001, units, optional rejection, unknown/generic fallback, no reconnect, session cache");
    }
}
internal sealed class Peer : IBmsTransport, IBmsMtuTransport
{
    public bool NativeCalibration=false;
    public int RawCurrentMa,NativeOffsetMa,NativeCoefficientWrites;
    public uint NativeGainPpm=1000000;
    public ushort NativeToken;private ushort nativeGeneration;
    private readonly long nativeClockStarted=Stopwatch.GetTimestamp();
    private byte[]? NativeWrite(byte[] q,int a,int n)
    {
        ushort Word(int at)=>(ushort)(q[at]*256+q[at+1]);
        byte[] Denied()=>Frame(new byte[]{1,(byte)(q[1]|128),7});
        if(q[1]==6&&a==0x2e30&&n==0xca01){if(NativeToken!=0)return Denied();NativeToken=++nativeGeneration;return Frame(q[..6]);}
        if(q[1]==6&&a is 0x2e31 or 0x2e32){if(NativeToken==0||n!=NativeToken)return Denied();if(a==0x2e32)NativeToken=0;return Frame(q[..6]);}
        if(q[1]==16&&a==0x2e24){if(n!=5||NativeToken==0||Word(7)!=NativeToken)return Denied();NativeOffsetMa=unchecked((int)((uint)Word(9)|((uint)Word(11)<<16)));NativeGainPpm=(uint)Word(13)|((uint)Word(15)<<16);NativeCoefficientWrites++;return Frame(q[..6]);}
        return null;
    }
    public bool C077=false,ServiceFailure=false,Bad309Schema=false,SecondaryUnavailable=false,PrimaryUnavailable=false,PrimaryZeroFailed=false; public bool SerialWakeup=false; public int SilentReads,SilentWrites; public int ServiceCommands,ServicePolls; public ushort OutputRequest=0; private int pendingService;
    public bool Advertised=false,Unknown=false,SeparateIdentity=false,NoStatus=false,NoIdentity=false,SilentRealtime=false;
    public int Writes,Reconnects;public int Soc=55;public int DelayedSocReads,DelayedCycleReads,StartupSampleReads;private int? pendingSoc;private ushort reportedCycle;public List<byte[]> SentRequests=new();public int FailWriteAddress=0;public bool CorruptReadback=false;public List<int> WriteLengths=new();public int? NegotiatedMtu {get;set;}=247;
    public bool IsConnected=>true;public bool RequiresSerialWakeup=>SerialWakeup;public string DiscoveryDescription=>"C060 fixture";
    public event Action<ReadOnlyMemory<byte>>? DataReceived; public event Action<string>? ConnectionProgress {add{}remove{}}
    public List<(int,int)> Reads=new();
    public ushort[] Calibration=Enumerable.Range(0,94).Select(i=>(ushort)(i%2==0?1024:0)).ToArray();
    public ushort[] Identity=Text("SN").Concat(Text("测试硬件")).Concat(Text("c060V3-20250304-20s")).ToArray();
    public ushort[] Protection={4200,4200,4250,4100,100,3200,3200,2700,3000,100,8400,8400,8500,8300,100,6000,6000,5400,6000,100,100,100,120,100,100,600,600,800,700,500,850,850,950,850,100,450,450,400,450,100,1000,1000,1100,1000,100,250,250,200,250,100,1200,1200,1200,1150,100,1000,1000,1000,900,100,3,2,1,2,100};
    public ushort[] Other={3800,30,20,9,8,7,6,5,4000,4000,4000,4000,1,0,1000,30,4600,120,2700,1,2,2,240,0,240,1,4200,2700,20,2,4,10,400,450,10,1100,1000,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    public Task ReconnectAsync(CancellationToken ct=default){Reconnects++;return Task.CompletedTask;}
    public ValueTask DisposeAsync()=>ValueTask.CompletedTask;
    public Task WriteAsync(ReadOnlyMemory<byte> value,CancellationToken ct=default)
    {
        ct.ThrowIfCancellationRequested();var q=value.ToArray();SentRequests.Add(q);int address=q[2]*256+q[3],n=q[4]*256+q[5];byte[] reply;
        if(q[1]==3) {
            Reads.Add((address,n));if(SilentReads>0){SilentReads--;return Task.CompletedTask;}if(SilentRealtime&&address==0xD120)return Task.CompletedTask;ushort[]? w=Read(address,n);
            reply=w is null?Frame(new byte[]{1,0x83,1}):Frame(new byte[]{1,3,(byte)(w.Length*2)}.Concat(w.SelectMany(x=>new[]{(byte)(x>>8),(byte)x})).ToArray());
        } else {
            Writes++;WriteLengths.Add(q.Length);if(SilentWrites>0){SilentWrites--;return Task.CompletedTask;}
            var native=NativeCalibration?NativeWrite(q,address,n):null;
            if(native is not null)reply=native;
            else if(address==FailWriteAddress)reply=Frame(new byte[]{1,(byte)(q[1]|128),4});
            else {
                if(q[1]==16&&address>=0x2100&&address+n<=0x2141) {
                    for(int i=0;i<n;i++)Protection[address-0x2100+i]=(ushort)(q[7+2*i]*256+q[8+2*i]);
                    if(CorruptReadback)Protection[address-0x2100]++;
                }
                if(q[1]==16&&address==0x2318&&DelayedCycleReads>0)reportedCycle=Other[25];
                if(q[1]==16&&address>=0x2300&&address+n<=0x2338)
                    for(int i=0;i<n;i++)Other[address-0x2300+i]=(ushort)(q[7+2*i]*256+q[8+2*i]);
                if(q[1]==16&&address>=0x2000&&address+n<=0x205E) {
                    for(int i=0;i<n;i++) {
                        ushort raw=(ushort)(q[7+2*i]*256+q[8+2*i]);
                        Calibration[address-0x2000+i]=i%2==1&&(raw&0x8000)!=0?unchecked((ushort)-(raw&0x7fff)):raw;
                    }
                }
                if(q[1]==16&&address>=0xFFF0&&address<=0xFFF2)
                    for(int i=0;i<n;i++)Identity[(address-0xFFF0)*16+i]=(ushort)(q[7+2*i]*256+q[8+2*i]);
                if(q[1]==6&&address==0x1005){if(DelayedSocReads>0)pendingSoc=n;else Soc=n;}
                if(C077&&q[1]==6&&address==0x2A7F) {ServiceCommands++;pendingService=1;ServicePolls=0;}
                reply=Frame(q[..6]);
            }
        }
        for(int at=0;at<reply.Length;at+=7)DataReceived?.Invoke(reply.AsMemory(at,Math.Min(7,reply.Length-at)));
        return Task.CompletedTask;
    }
    public ushort[]? MeasurementSnapshot { get; set; }

    ushort[]? Read(int a,int n)
    {
        if (MeasurementSnapshot is { } snapshot && a >= 0x2C00 && a+n <= 0x2C00+snapshot.Length)
            return snapshot.Skip(a-0x2C00).Take(n).ToArray();
        if(NativeCalibration) {
            if(a==0x2e00&&n==3)return new ushort[]{(ushort)(C077?0xc077:0xc060),1,1};
            if(a==0x2e24&&n==4)return D008Parameters.Calibration(NativeOffsetMa,NativeGainPpm);
            if(a==0x2e30&&n==4)return new ushort[]{NativeToken,30,1,(ushort)(C077?0x0309:0x7693)};
            if(a==0x2e28&&n==7){uint tick=unchecked((uint)(Stopwatch.GetElapsedTime(nativeClockStarted).TotalSeconds*32000));tick=tick/8000*8000;int final=new CurrentCalibrationCoefficients(NativeOffsetMa,NativeGainPpm).ApplyMilliampere(RawCurrentMa);return D008Parameters.Calibration(RawCurrentMa,unchecked((uint)final)).Concat(new ushort[]{1,(ushort)tick,(ushort)(tick>>16)}).ToArray();}
        }
        if(a>=0x2000&&a+n<=0x205E)return C077||Unknown||SeparateIdentity?Calibration.Skip(a-0x2000).Take(n).ToArray():null;
        if(Unknown)return null;
        if(NoIdentity&&a>=0xC000&&a<0xC100)return null;
        if(a==0xD170&&n==12&&Advertised)return new ushort[]{0x5550,1,(ushort)(C077?0xC077:0xC060),(ushort)(C077?0x16:0x20),2,2,0x800,0x1C00,0,0xCC00,0,0};
        if(C077&&a>=0x2A40&&a+n<=0x2A80) {
            if(a==0x2A40&&pendingService!=0 && ++ServicePolls>=2)pendingService=0;
            var w=new ushort[64];w[0]=0xC077;w[1]=(ushort)(Bad309Schema?1:NativeCalibration?3:2);w[2]=(ushort)pendingService;w[3]=(ushort)(ServiceFailure?5:0);w[5]=OutputRequest;w[7]=1;
            w[58]=(ushort)(pendingService==0?1:0);w[59]=(ushort)(pendingService==0?2:1);w[60]=NativeCalibration?(ushort)0:unchecked((ushort)(short)-12);w[61]=(ushort)(pendingService==0?8:3);w[63]=0x010E;
            for(int i=0;i<2;i++) {
                int at=8+i*12;w[at]=1;w[at+1]=(ushort)(i==0?8:0);w[at+6]=64;w[at+8]=(ushort)(pendingService==0?1:0);w[at+11]=(ushort)(i==0?1:0);
                byte[] profile=Convert.FromHexString(i==0?"501C8F54833E828CC36432FA26F6420004000000000000000040":"501C8F5C833E828CC36432FA26F6530004000000000000000040");
                for(int j=0;j<13;j++)w[32+i*13+j]=(ushort)(profile[j*2]*256+profile[j*2+1]);
            }
            if(StartupSampleReads>0){StartupSampleReads--;w[58]=0;}
            if(SecondaryUnavailable){w[20]=0;w[28]=0;w[58]=0;}
            if(PrimaryUnavailable){w[8]=0;w[16]=0;w[58]=0;}
            if(PrimaryZeroFailed)w[59]=3;
            return w.Skip(a-0x2A40).Take(n).ToArray();
        }
        if(a==0xC002&&n==48&&!SeparateIdentity)return Identity.ToArray();
        if(a==0xC002&&n==16)return Text("SN");
        if(a==0xC012&&n==16&&SeparateIdentity)return Text("generic");
        if(a==0xD120&&n==2&&!SeparateIdentity)return new ushort[2];
        if(a==0xD000) {
            if(pendingSoc.HasValue&&DelayedSocReads--<=0){Soc=pendingSoc.Value;pendingSoc=null;}
            ushort[] data=new ushort[63];for(int i=0;i<32;i++)data[i]=(ushort)(i<(C077?Other[28]:20)?3300:61001);
            data[32]=data[33]=3300;data[34]=1;data[35]=2;data[37]=6600;data[47]=650;data[48]=700;data[49]=600;data[50]=20;data[52]=(ushort)Soc;data[53]=100;data[54]=1320;data[55]=data[56]=(ushort)(Other[24]*10);data[57]=DelayedCycleReads-->0?reportedCycle:Other[25];
            return data.Take(n).ToArray();
        }
        if(a==0xD115&&n==2&&!NoStatus)return new ushort[]{4,0};
        if(a>=0x2100&&a+n<=0x2141)return Protection.Skip(a-0x2100).Take(n).ToArray();
        if(a>=0x2300&&a+n<=0x2338)return Other.Skip(a-0x2300).Take(n).ToArray();
        if(a==0x2200&&n==86)return new ushort[86];
        if(a is 0x3000 or 0x3050 && n==21)return new ushort[21];
        return null;
    }
    static ushort[] Text(string s) {
        Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);byte[] b=new byte[32];Encoding.GetEncoding(936).GetBytes(s).CopyTo(b,0);
        return Enumerable.Range(0,16).Select(i=>(ushort)(b[2*i]*256+b[2*i+1])).ToArray();
    }
    static byte[] Frame(byte[] body) {
        ushort crc=65535;foreach(byte b in body){crc^=b;for(int j=0;j<8;j++)crc=(ushort)((crc&1)!=0?(crc>>1)^0xA001:crc>>1);}
        return body.Concat(new[]{(byte)crc,(byte)(crc>>8)}).ToArray();
    }
}
