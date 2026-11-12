using BmsTool.Windows;
using System.Text;
static class Test
{
    static int checks;
    static void Check(bool ok,string why){if(!ok)throw new Exception(why);checks++;}
    static async Task Reject(Func<Task> action,string why){bool rejected=false;try{await action();}catch(Exception e)when(e is ArgumentException or InvalidDataException or NotSupportedException or IOException or InvalidOperationException){rejected=true;}Check(rejected,why);}
    public static async Task Main()
    {
        var peer=new Peer{C077=true,Advertised=true};peer.Other[28]=16;peer.Other[10]=1280;peer.Other[11]=3400;
        peer.Protection=new ushort[]{4000,4100,4250,4150,100,3000,2800,2700,2800,100,6400,6400,6800,6560,100,4800,4800,4400,4480,100,120,120,120,10,100,600,750,750,600,100,850,850,950,850,100,450,450,400,450,100,1000,1000,1100,1000,100,300,250,200,250,100,1200,1250,1350,1200,100,1000,1000,1000,900,100,20,15,10,11,100};
        await using var c=new BmsClient(peer);await c.ReadIdentityAsync();
        Check(c.IsC077&&!c.IsC060&&c.UsesC060Protocol&&c.ParameterProduct=="C077","explicit C077 identity stays distinct");
        var battery=await c.ReadBatteryAsync();Check(battery.ValidCellCount==16&&battery.CellMillivolts[16]==61001&&battery.CurrentA==-2,"16 cells, absent61001, normalized current");
        Check(!peer.Reads.Any(r=>r.Item1==0xD120),"no unsupported realtime probing");
        var capture=await c.ReadC060ParametersAsync();Check(capture.Product=="C077"&&capture.Blocks.ContainsKey("231C")&&capture.Blocks.ContainsKey("C077Afe"),"shared parameter wire contract, separate product tag");
        Check(!peer.Reads.Any(r=>r.Item1 is 0x3000 or 0x3050),"C077 does not resurrect retired segmented current calibration");
        foreach(ushort address in new ushort[]{0x231D,0x231E}) {
            Check(!C060Parameters.FieldsFor(true).Single(f=>f.Address==address).Editable,"fixed board/hardware fields read only");
            int writes=peer.Writes;await Reject(()=>c.WriteC060ParameterChangesAsync(new Dictionary<ushort,ushort>{{address,99}},(_,_)=>{}),"fixed profile edit rejected");Check(peer.Writes==writes,"no invalid write");
        }
        var seriesField=C060Parameters.FieldsFor(true).Single(f=>f.Address==0x231C);
        Check(seriesField.Editable&&seriesField.Minimum==5&&seriesField.Maximum==16,"C077 existing series parameter supports5..16");
        int seriesWrites=peer.Writes;
        foreach(ushort bad in new ushort[]{4,17,65535})await Reject(()=>c.WriteC060ParameterChangesAsync(new Dictionary<ushort,ushort>{{0x231C,bad}},(_,_)=>{}),"out-of-range series preflight");
        peer.OutputRequest=2;
        await Reject(()=>c.WriteC060ParameterChangesAsync(new Dictionary<ushort,ushort>{{0x231C,13}},(_,_)=>{}),"active outputs block series change");
        Check(peer.Writes==seriesWrites,"invalid/live series changes write zero");
        peer.OutputRequest=0;
        await c.WriteC060ParameterChangesAsync(new Dictionary<ushort,ushort>{{0x231C,13}},(_,_)=>{});
        Check(peer.Other[28]==13&&peer.Other[29]==2&&peer.Other[30]==4,"13-series group commits and reads back with fixed shunt");
        var thirteen=await c.ReadBatteryAsync();Check(thirteen.ValidCellCount==13&&thirteen.CellMillivolts[13]==61001,"13 real cells and missing slots survive client decoding");
        await c.WriteC060ParameterChangesAsync(new Dictionary<ushort,ushort>{{0x231C,16}},(_,_)=>{});
        Check(new ushort[]{0x2020,0x203F,0x204E,0x2054}.All(a=>!C060Parameters.FieldsFor(true).Single(f=>f.Address==a).Editable),"unused cells/current/temp channels read only");
        Check(new ushort[]{0x2000,0x2040,0x2042,0x2044,0x2046,0x2048,0x204A,0x2050,0x2052,0x205C}.All(a=>C060Parameters.FieldsFor(true).Single(f=>f.Address==a).Editable),"real16-cell/total/temperature/MOS coefficients retained");
        await c.WriteC060ParameterChangesAsync(new Dictionary<ushort,ushort>{{0x2318,245}},(_,_)=>{});Check(peer.Other[24]==245,"shared capacity group on C077");
        int before=peer.Writes;
        await Reject(()=>c.WriteProtectionChangesAsync(new Dictionary<ushort,ushort>{{0x2102,4000}},(_,_)=>{}),"invalid voltage order rejected");
        await Reject(()=>c.WriteProtectionChangesAsync(new Dictionary<ushort,ushort>{{0x2116,4001}},(_,_)=>{}),"hardware overcurrent range checked");
        Check(peer.Writes==before,"protection preflight sends no partial write");
        await c.WriteProtectionChangesAsync(new Dictionary<ushort,ushort>{{0x2104,120}},(_,_)=>{});
        var diag=await c.ReadC077AfeAsync();Check(diag.ZeroReady&&diag.Chips[0].OffsetCounts==-12&&diag.Chips[1].ZeroStateText=="未启用"&&!diag.Chips[1].CurrentEnabled&&diag.Chips[0].Eeprom.Length==26,"primary signed zero, secondary CADC disabled and packed EEPROM");
        Check(diag.Chips[0].HardwareCurrentText.Contains("OCD1 80A")&&diag.Chips[1].HardwareCurrentText.Contains("SC 400A"),"actual chip code display");
        peer.OutputRequest=2;
        await c.WriteProtectionChangesAsync(new Dictionary<ushort,ushort>{{0x2116,420},{0x213C,15},{0x2104,65535}},(_,_)=>{});
        Check(peer.Protection[22]==420&&peer.Protection[4]==65535,"software protection independently editable, equality and full uint16 filter");
        await Reject(()=>c.WriteC060ParameterChangesAsync(new Dictionary<ushort,ushort>{{0x230B,4600}},(_,_)=>{}),"legacy short compatibility field is read-only; use explicit chip editor");
        peer.OutputRequest=0;
        var zero=await c.RunC077ServiceAsync(false);Check(zero.ZeroReady&&peer.ServiceCommands==1&&peer.ServicePolls>=2,"ACK alone is not completion; poll zero readiness");
        Check(peer.SentRequests.Last(r=>r[1]==6)[4]==0x5A,"explicit zero command");
        await c.RunC077ServiceAsync(true);Check(peer.ServiceCommands==2&&peer.SentRequests.Last(r=>r[1]==6)[4]==0xC0,"program only through explicit309 command");
        peer.ServiceFailure=true;await Reject(()=>c.RunC077ServiceAsync(true),"partial service failure surfaced");Check(peer.ServiceCommands==3,"no automatic write retry");peer.ServiceFailure=false;
        peer.SecondaryUnavailable=true;
        var primaryOnly=await c.RunC077ServiceAsync(false);
        Check(primaryOnly.Chips[0].ZeroState==2&&!primaryOnly.Chips[1].Valid&&!primaryOnly.ZeroReady&&!primaryOnly.SampleValid,"AFE1 zero succeeds without claiming pack or AFE2 ready");
        int serviceCount=peer.ServiceCommands;
        using(var deadline=new CancellationTokenSource(750)) {
            bool cancelled=false;try{await c.RunC077ServiceAsync(true,deadline.Token);}catch(OperationCanceledException){cancelled=true;}
            Check(cancelled&&peer.ServiceCommands==serviceCount+1,"dual program still waits for both chips and never replays");
        }
        peer.PrimaryUnavailable=true;
        using(var deadline=new CancellationTokenSource(750)) {
            bool cancelled=false;try{await c.RunC077ServiceAsync(false,deadline.Token);}catch(OperationCanceledException){cancelled=true;}
            Check(cancelled,"invalid AFE1 cannot complete zero");
        }
        peer.PrimaryUnavailable=false;peer.PrimaryZeroFailed=true;
        await Reject(()=>c.RunC077ServiceAsync(false),"AFE1 failed zero is explicit failure");peer.PrimaryZeroFailed=false;peer.SecondaryUnavailable=false;
        peer.SerialWakeup=true;
        await c.ReadRegistersAsync(0x231C,4);peer.SilentReads=1;
        int readCount=peer.Reads.Count;
        bool freshTimedOut=false;try{await c.ReadRegistersAsync(0x231C,4,responseTimeout:TimeSpan.FromMilliseconds(20));}catch(TimeoutException){freshTimedOut=true;}
        Check(freshTimedOut&&peer.Reads.Count==readCount+1,"active reads retain timeout without RTC retry");
        await Task.Delay(3100);peer.SilentReads=1;readCount=peer.Reads.Count;
        await c.ReadRegistersAsync(0x231C,4,responseTimeout:TimeSpan.FromMilliseconds(20));
        Check(peer.Reads.Count==readCount+2,"idle C077 serial read retries exactly once");
        peer.SilentReads=2;await Task.Delay(3100);readCount=peer.Reads.Count;
        bool bounded=false;try{await c.ReadRegistersAsync(0x231C,4,responseTimeout:TimeSpan.FromMilliseconds(20));}catch(TimeoutException){bounded=true;}
        Check(bounded&&peer.Reads.Count==readCount+2,"failed wake read has bounded retry");
        int writeCount=peer.Writes;peer.SilentWrites=1;
        bool writeTimeout=false;try{await c.WriteSingleRegisterAsync(0x1005,55,responseTimeout:TimeSpan.FromMilliseconds(20));}catch(TimeoutException){writeTimeout=true;}
        Check(writeTimeout&&peer.Writes==writeCount+1,"idle write is never automatically replayed");
        await Task.Delay(3100);peer.SilentReads=1;readCount=peer.Reads.Count;writeCount=peer.Writes;
        await c.WriteSingleRegisterAsync(0x1005,56,responseTimeout:TimeSpan.FromMilliseconds(20));
        Check(peer.Reads.Count==readCount+2&&peer.Writes==writeCount+1,"idle write wakes with reads then sends command once");
        await Task.Delay(3100);peer.SilentReads=2;writeCount=peer.Writes;
        bool unawake=false;try{await c.WriteSingleRegisterAsync(0x1005,57,responseTimeout:TimeSpan.FromMilliseconds(20));}catch(TimeoutException){unawake=true;}
        Check(unawake&&peer.Writes==writeCount,"unconfirmed wake sends no write");
        peer.SerialWakeup=false;
        peer.DelayedSocReads=2;writeCount=peer.Writes;
        Check((await c.SetSocAndVerifyAsync(61)).SocPercent==61&&peer.Writes==writeCount+1,"SOC waits for actual report publication and writes only once");
        peer.DelayedSocReads=100;writeCount=peer.Writes;
        await Reject(()=>c.SetSocAndVerifyAsync(62),"missing SOC report publication fails bounded verification");
        Check(peer.Writes==writeCount+1,"SOC verification timeout never repeats setting command");
        peer.DelayedSocReads=0;
        peer.DelayedCycleReads=2;writeCount=peer.Writes;
        Check((await c.SetCycleCountAndVerifyAsync(7)).CycleCount==7&&peer.Writes==writeCount+1,"cycle waits for report publication without repeating Flash write");
        peer.DelayedCycleReads=100;writeCount=peer.Writes;
        await Reject(()=>c.SetCycleCountAndVerifyAsync(8),"missing cycle publication fails bounded verification");
        Check(peer.Writes==writeCount+1,"cycle verification timeout never repeats capacity group");peer.DelayedCycleReads=0;


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
