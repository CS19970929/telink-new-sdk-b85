using BmsTool.Windows;
static class Test
{
    static void Check(bool value,string why){if(!value)throw new Exception(why);}
    public static async Task Main(string[] args)
    {
        await using var wire=new NativeTransport(args[0],"--host-parameters");
        await using var c=new BmsClient(wire);
        await c.ReadIdentityAsync();Check(c.IsC077,"native C077 identity");
        var before=await c.ReadC060ParametersAsync();
        ushort[] expected={4000,4100,4250,4150,100,3000,2800,2700,2800,100,6400,6400,6800,6560,100,4800,4800,4400,4480,100,120,120,120,10,100,600,750,750,600,100,850,850,950,850,100,450,450,400,450,100,1000,1000,1100,1000,100,300,250,200,250,100,1200,1250,1350,1200,100,1000,1000,1000,900,100,20,15,10,11,100};
        Check(before.Blocks["Protection"].SequenceEqual(expected),"all 65 legacy defaults from production C Flash initialization");
        int verified=0;
        var changes=Enumerable.Range(0,13).ToDictionary(i=>(ushort)(0x2104+i*5),i=>(ushort)(101+i));
        await c.WriteProtectionChangesAsync(changes,(_,_)=>verified++);Check(verified==13,"13 atomic production RTU and Flash commits");
        foreach(var entry in changes)expected[entry.Key-0x2100]=entry.Value;
        await c.WriteC060ParameterChangesAsync(new Dictionary<ushort,ushort>{{0x2318,245},{0x2047,unchecked((ushort)(short)-123)}},(_,_)=>{});
        await c.WriteC060IdentityAsync(0xFFF0,"C077联测SN");
        var hard=await c.ReadC077HardwareAsync();var a=(ushort[])hard.Saved[0].Clone();var b=(ushort[])hard.Saved[1].Clone();
        a[1]=4240;b[1]=4320;a[17]=256;b[19]=20;
        await c.SaveC077HardwareAsync(0,a,hard.Saved[0]);await c.SaveC077HardwareAsync(1,b,hard.Saved[1]);
        await c.WriteC060ParameterChangesAsync(new Dictionary<ushort,ushort>{{0x231C,13}},(_,_)=>{});
        await wire.Configure("@reload");
        var after=await c.ReadC060ParametersAsync();
        Check(after.Blocks["Protection"].SequenceEqual(expected),"every parameter persists after production reload");
        Check(after.Blocks["231C"].SequenceEqual(new ushort[]{13,2,4,10}),"13 series saves and reloads through actual C parameter/Flash engine");
        Check(after.Blocks["2318"][0]==245&&after.Blocks["2308"][2]==1280&&after.Blocks["2308"][3]==3400,"capacity and legacy compatibility fields preserved");
        Check(after.Blocks["Calibration"][0x47]==unchecked((ushort)(short)-123),"signed current calibration survives Flash reload");
        var reloaded=await c.ReadC077HardwareAsync();Check(reloaded.Saved[0].SequenceEqual(a)&&reloaded.Saved[1].SequenceEqual(b),"both full independent hard protection profiles persist through actual Flash reload");
        var identity=await c.ReadIdentityAsync();Check(identity.Serial=="C077联测SN","GBK identity persisted");
        string output=Path.Combine(Path.GetTempPath(),"CodexTemp","bms-tool-windows","native-c077-"+Guid.NewGuid()+".json");
        C060Parameters.Export(output,after);Check(C060Parameters.Import(output,"C077").Blocks["Protection"].SequenceEqual(expected),"host parameter export/import roundtrip");
        Console.WriteLine("PASS shared C# BmsClient / production C RTU+parameters+Flash A/B: independent 65 defaults, all 13 group saves, capacity/short/current K-B/GBK identity and production reload");
    }
}
