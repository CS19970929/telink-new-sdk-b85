using BmsTool.Windows;
using System.Text;

internal static class Program
{
    static void Check(bool value,string why) { if(!value)throw new Exception(why); }
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
        Console.WriteLine("PASS C060 protection: 13 groups/65 words, MTU23 19-byte atomic writes, preflight, same-value skip, partial failure and bad readback");
        Console.WriteLine("PASS C060 connection: old/new capabilities, GBK, 20 cells/61001, units, optional rejection, unknown/generic fallback, no reconnect, session cache");
    }
}
internal sealed class Peer : IBmsTransport, IBmsMtuTransport
{
    public bool Advertised,Unknown,SeparateIdentity,NoStatus;
    public int Writes,Reconnects;public int FailWriteAddress;public bool CorruptReadback;public List<int> WriteLengths=new();public int? NegotiatedMtu {get;set;}=247;
    public bool IsConnected=>true;public bool RequiresSerialWakeup=>false;public string DiscoveryDescription=>"C060 fixture";
    public event Action<ReadOnlyMemory<byte>>? DataReceived; public event Action<string>? ConnectionProgress;
    public List<(int,int)> Reads=new();
    public ushort[] Protection={4200,4200,4250,4100,100,3200,3200,2700,3000,100,8400,8400,8500,8300,100,6000,6000,5400,6000,100,100,100,120,100,100,600,600,800,700,500,850,850,950,850,100,450,450,400,450,100,1000,1000,1100,1000,100,250,250,200,250,100,1200,1200,1200,1150,100,1000,1000,1000,900,100,3,2,1,2,100};
    public ushort[] Other={3800,30,20,9,8,7,6,5,4000,4000,4000,4000,1,0,1000,30,4600,120,2700,1,2,2,240,0,240,1,4200,2700,20,2,4,10,400,450,10,1100,1000,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    public Task ReconnectAsync(CancellationToken ct=default){Reconnects++;return Task.CompletedTask;}
    public ValueTask DisposeAsync()=>ValueTask.CompletedTask;
    public Task WriteAsync(ReadOnlyMemory<byte> value,CancellationToken ct=default)
    {
        ct.ThrowIfCancellationRequested();var q=value.ToArray();int address=q[2]*256+q[3],n=q[4]*256+q[5];byte[] reply;
        if(q[1]==3) {
            Reads.Add((address,n));ushort[]? w=Read(address,n);
            reply=w is null?Frame(new byte[]{1,0x83,1}):Frame(new byte[]{1,3,(byte)(w.Length*2)}.Concat(w.SelectMany(x=>new[]{(byte)(x>>8),(byte)x})).ToArray());
        } else {
            Writes++;WriteLengths.Add(q.Length);
            if(address==FailWriteAddress)reply=Frame(new byte[]{1,(byte)(q[1]|128),4});
            else {
                if(q[1]==16&&address>=0x2100&&address+n<=0x2141) {
                    for(int i=0;i<n;i++)Protection[address-0x2100+i]=(ushort)(q[7+2*i]*256+q[8+2*i]);
                    if(CorruptReadback)Protection[address-0x2100]++;
                }
                reply=Frame(q[..6]);
            }
        }
        for(int at=0;at<reply.Length;at+=7)DataReceived?.Invoke(reply.AsMemory(at,Math.Min(7,reply.Length-at)));
        return Task.CompletedTask;
    }
    ushort[]? Read(int a,int n)
    {
        if(a==0x2000&&n==2)return new ushort[]{1024,0};
        if(Unknown)return null;
        if(a==0xD170&&n==12&&Advertised)return new ushort[]{0x5550,1,0xC060,0x20,2,2,0x800,0x1C00,0,0xCC00,0,0};
        if(a==0xC002&&n==48&&!SeparateIdentity)return Text("SN").Concat(Text("测试硬件")).Concat(Text("c060V3-20250304-20s")).ToArray();
        if(a==0xC002&&n==16)return Text("SN");
        if(a==0xC012&&n==16&&SeparateIdentity)return Text("generic");
        if(a==0xD120&&n==2&&!SeparateIdentity)return new ushort[2];
        if(a==0xD000) {
            ushort[] data=new ushort[63];for(int i=0;i<32;i++)data[i]=(ushort)(i<20?3300:61001);
            data[32]=data[33]=3300;data[34]=1;data[35]=2;data[37]=6600;data[47]=650;data[48]=700;data[49]=600;data[50]=20;data[52]=55;data[53]=100;data[54]=1320;data[55]=data[56]=2400;data[57]=Other[25];
            return data.Take(n).ToArray();
        }
        if(a==0xD115&&n==2&&!NoStatus)return new ushort[]{4,0};
        if(a>=0x2100&&a+n<=0x2141)return Protection.Skip(a-0x2100).Take(n).ToArray();
        if(a>=0x2300&&a+n<=0x2338)return Other.Skip(a-0x2300).Take(n).ToArray();
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
