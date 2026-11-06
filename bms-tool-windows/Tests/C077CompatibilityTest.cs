using BmsTool.Windows;
using System.Text;
static class Test
{
    static int checks;
    static void Check(bool ok,string why){if(!ok)throw new Exception(why);checks++;}
    static async Task Reject(Func<Task> action,string why){bool rejected=false;try{await action();}catch(Exception e)when(e is ArgumentException or InvalidDataException or NotSupportedException or IOException){rejected=true;}Check(rejected,why);}
    public static async Task Main()
    {
        var peer=new Peer{C077=true,Advertised=true};peer.Other[28]=16;peer.Other[10]=4480;
        await using var c=new BmsClient(peer);await c.ReadIdentityAsync();
        Check(c.IsC077&&!c.IsC060&&c.UsesC060Protocol&&c.ParameterProduct=="C077","explicit C077 identity stays distinct");
        var battery=await c.ReadBatteryAsync();Check(battery.ValidCellCount==16&&battery.CellMillivolts[16]==61001&&battery.CurrentA==-2,"16 cells, absent61001, normalized current");
        Check(!peer.Reads.Any(r=>r.Item1==0xD120),"no unsupported realtime probing");
        var capture=await c.ReadC060ParametersAsync();Check(capture.Product=="C077"&&capture.Blocks.ContainsKey("231C")&&capture.Blocks.ContainsKey("C077Afe"),"shared parameter wire contract, separate product tag");
        Check(!peer.Reads.Any(r=>r.Item1 is 0x3000 or 0x3050),"C077 does not resurrect retired segmented current calibration");
        foreach(ushort address in new ushort[]{0x230A,0x230B,0x231C,0x231D,0x231E}) {
            Check(!C060Parameters.FieldsFor(true).Single(f=>f.Address==address).Editable,"fixed board/hardware fields read only");
            int writes=peer.Writes;await Reject(()=>c.WriteC060ParameterChangesAsync(new Dictionary<ushort,ushort>{{address,99}},(_,_)=>{}),"fixed profile edit rejected");Check(peer.Writes==writes,"no invalid write");
        }
        Check(new ushort[]{0x2020,0x203F,0x2042,0x2046,0x204E,0x2054}.All(a=>!C060Parameters.FieldsFor(true).Single(f=>f.Address==a).Editable),"unused cells/current/temp channels read only");
        Check(new ushort[]{0x2000,0x2040,0x2044,0x204A,0x205C}.All(a=>C060Parameters.FieldsFor(true).Single(f=>f.Address==a).Editable),"real16-cell/total/temperature/MOS coefficients retained");
        await c.WriteC060ParameterChangesAsync(new Dictionary<ushort,ushort>{{0x2318,245}},(_,_)=>{});Check(peer.Other[24]==245,"shared capacity group on C077");
        int before=peer.Writes;
        await Reject(()=>c.WriteProtectionChangesAsync(new Dictionary<ushort,ushort>{{0x2102,4300}},(_,_)=>{}),"OV must be inside hardware trip");
        await Reject(()=>c.WriteProtectionChangesAsync(new Dictionary<ushort,ushort>{{0x2116,400}},(_,_)=>{}),"charge hardware minimum40A enforced");
        Check(peer.Writes==before,"protection preflight sends no partial write");
        await c.WriteProtectionChangesAsync(new Dictionary<ushort,ushort>{{0x2104,120}},(_,_)=>{});
        var diag=await c.ReadC077AfeAsync();Check(diag.ZeroReady&&diag.Chips[0].OffsetCounts==-12&&diag.Chips[1].OffsetCounts==7&&diag.Chips[0].Eeprom.Length==26,"signed dual zero and packed EEPROM");
        var zero=await c.RunC077ServiceAsync(false);Check(zero.ZeroReady&&peer.ServiceCommands==1&&peer.ServicePolls>=2,"ACK alone is not completion; poll zero readiness");
        Check(peer.SentRequests.Last(r=>r[1]==6)[4]==0x5A,"explicit zero command");
        await c.RunC077ServiceAsync(true);Check(peer.ServiceCommands==2&&peer.SentRequests.Last(r=>r[1]==6)[4]==0xC0,"program only through explicit309 command");
        peer.ServiceFailure=true;await Reject(()=>c.RunC077ServiceAsync(true),"partial service failure surfaced");Check(peer.ServiceCommands==3,"no automatic write retry");peer.ServiceFailure=false;
        peer.Bad309Schema=true;await Reject(()=>c.ReadC077AfeAsync(),"unknown diagnostic schema rejected");peer.Bad309Schema=false;
        string path=Path.Combine(Path.GetTempPath(),"CodexTemp","bms-tool-windows","c077-params-"+Guid.NewGuid().ToString("N")+".json");Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        C060Parameters.Export(path,capture);Check(C060Parameters.Import(path,"C077").Product=="C077","C077 parameter roundtrip");
        await Reject(()=>Task.Run(()=>C060Parameters.Import(path)),"C077 cannot import as C060");
        var old=new Peer{Advertised=true};await using var c060=new BmsClient(old);await c060.ReadIdentityAsync();int oldWrites=old.Writes;
        await Reject(()=>c060.RunC077ServiceAsync(true),"C060 cannot receive a309 maintenance command");Check(old.Writes==oldWrites,"C060 unaffected");
        Check(!peer.Reads.Any(r=>r.Item1 is 0x2500 or 0x2540),"309 never uses 3510/3520 hardware model");
        Console.WriteLine($"PASS C077 shared host: {checks} identity/cell/parameter/zero/service/cross-product checks");
    }
}
