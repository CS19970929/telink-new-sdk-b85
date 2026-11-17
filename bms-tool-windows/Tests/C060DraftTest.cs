using BmsTool.Windows;
static class Test
{
    static int checks;
    static void Check(bool ok,string why){if(!ok)throw new Exception(why);checks++;}
    static void Reject(Action action,string why){bool rejected=false;try{action();}catch(Exception ex)when(ex is ArgumentException or InvalidDataException){rejected=true;}Check(rejected,why);}
    public static async Task Main(){
        var capture=new C060ParameterCapture();
        foreach(var g in C060Parameters.Groups)capture.Blocks[g.Address.ToString("X4")]=new ushort[g.Count];
        capture.Blocks["Calibration"]=new ushort[94];
        foreach(var f in C060Parameters.FieldsFor(false)){
            ushort raw=f.Signed?unchecked((ushort)(short)f.Minimum):(ushort)f.Minimum;
            string key=f.Address<0x2100?"Calibration":f.WriteAddress.ToString("X4");
            capture.Blocks[key][f.Address-(f.Address<0x2100?0x2000:f.WriteAddress)]=raw;
        }
        var saved=capture.Blocks.ToDictionary(p=>p.Key,p=>p.Value.ToArray());
        Check(C060Parameters.Fields.Count==56,"shared definition excludes retired K/B");
        foreach(var f in C060Parameters.FieldsFor(false)){
            Check(C060Parameters.TryValue(capture,f,out ushort old),"field can be read");
            var same=new Dictionary<ushort,string>{{f.Address,f.Format(old)}};
            Check(C060Parameters.CollectChanges(capture,same).Count==0,"unchanged values never require permission or a write");
            int next=f.Minimum<f.Maximum?f.Minimum+1:f.Minimum;
            ushort changed=f.Signed?unchecked((ushort)(short)next):(ushort)next;
            if(changed==old)continue;
            var edits=new Dictionary<ushort,string>{{f.Address,f.Format(changed)}};
            if(!f.Editable)Reject(()=>C060Parameters.CollectChanges(capture,edits,true,true),"reserved field cannot be changed");
            else {
                if(f.FactoryOnly)Reject(()=>C060Parameters.CollectChanges(capture,edits,true,false),"customer cannot edit factory fields");
                if(f.Compatibility)Reject(()=>C060Parameters.CollectChanges(capture,edits,false,true),"compatibility fields require explicit enable");
                Check(C060Parameters.CollectChanges(capture,edits,true,true)[f.Address]==changed,"field scales and signed encoding round trip");
            }
        }
        Reject(()=>C060Parameters.CollectChanges(capture,new Dictionary<ushort,string>{{0xFFFF,"1"}}),"unknown address refused");
        Reject(()=>C060Parameters.CollectChanges(capture,new Dictionary<ushort,string>{{0x2300,"NaN"}}),"invalid number refused");
        var empty=new C060ParameterCapture();Reject(()=>C060Parameters.CollectChanges(empty,new Dictionary<ushort,string>{{0x2300,"3300"}}),"unread fields refused");
        Check(saved.All(p=>p.Value.SequenceEqual(capture.Blocks[p.Key])),"candidate construction never overwrites device readback");
        await using(var peer=new Peer()) {
            await using var client=new BmsClient(peer);
            await client.SelectC060ParametersAsync();peer.Reads.Clear();
            var mobile=await client.ReadC060ParametersAsync(false);
            Check(!mobile.Blocks.ContainsKey("Calibration")&&!mobile.Blocks.ContainsKey("DischargeSummary")&&!mobile.Blocks.ContainsKey("ChargeSummary"),"mobile capture excludes calibration data");
            Check(!peer.Reads.Any(a=>a is 0x2000 or 0x3000 or 0x3050),"mobile sends no calibration read requests");
            var desktop=await client.ReadC060ParametersAsync();
            Check(!desktop.Blocks.ContainsKey("Calibration")&&!desktop.Blocks.ContainsKey("DischargeSummary")&&!desktop.Blocks.ContainsKey("ChargeSummary"),"desktop new C060 capture also excludes retired K/B");
            Check(!peer.Reads.Any(a=>a is 0x2000 or 0x3000 or 0x3050),"neither client sends retired C060 calibration reads");
        }
        Console.WriteLine($"PASS C060 mobile draft: {checks} field/permission/unit/ownership checks");
    }
}

sealed class Peer:IBmsTransport
{
    public readonly List<ushort> Reads=new();
    public bool IsConnected=>true;public string DiscoveryDescription=>"read-only C060 capture fixture";
    public event Action<ReadOnlyMemory<byte>>? DataReceived;
    public event Action<string>? ConnectionProgress {add{}remove{}}
    public Task ReconnectAsync(CancellationToken ct=default)=>Task.CompletedTask;
    public ValueTask DisposeAsync()=>ValueTask.CompletedTask;
    public Task WriteAsync(ReadOnlyMemory<byte> request,CancellationToken ct=default) {
        var b=request.Span;ushort address=(ushort)(b[2]*256+b[3]),count=(ushort)(b[4]*256+b[5]);Reads.Add(address);
        byte[] response=new byte[5+2*count];response[0]=1;response[1]=3;response[2]=(byte)(2*count);
        if(address==0x231C){response[4]=20;response[6]=1;response[8]=1;}
        if(address==0x2318){response[3]=3;response[4]=0xE8;}
        ushort crc=ModbusRtu.Crc16(response.AsSpan(0,response.Length-2));response[^2]=(byte)crc;response[^1]=(byte)(crc>>8);
        DataReceived?.Invoke(response);return Task.CompletedTask;
    }
}
