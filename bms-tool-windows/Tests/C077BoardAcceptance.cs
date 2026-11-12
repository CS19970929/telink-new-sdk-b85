using BmsTool.Windows;
using System.Text.Json;
using System.Diagnostics;

static class BoardAcceptance
{
    static readonly List<object> Results=new();
    static readonly JsonSerializerOptions Json=new(){WriteIndented=true};
    static string Output="";
    static int Writes,Frames,Failures;
    static StreamWriter Trace=null!;
    static void Check(bool value,string reason){if(!value)throw new Exception(reason);}
    static async Task Case(string name,Func<Task> test)
    {
        var clock=Stopwatch.StartNew();
        try {await test();Results.Add(new{name,status="pass",elapsedMs=clock.ElapsedMilliseconds});Console.WriteLine("PASS "+name);}
        catch(Exception ex){Failures++;Results.Add(new{name,status="fail",error=ex.ToString(),elapsedMs=clock.ElapsedMilliseconds});Console.WriteLine("FAIL "+name+": "+ex.Message);}
        SaveReport();
    }
    static void SaveReport()=>File.WriteAllText(Path.Combine(Output,"report.json"),JsonSerializer.Serialize(new{createdUtc=DateTimeOffset.UtcNow,frames=Frames,writes=Writes,failures=Failures,results=Results},Json));
    static async Task Reject(Func<Task> action)
    {
        int writes=Writes;bool rejected=false;
        try{await action();}catch(ArgumentException){rejected=true;}catch(InvalidOperationException){rejected=true;}catch(InvalidDataException){rejected=true;}catch(IOException ex) when(ex.Message.Contains("Flash配置已由其他入口修改")){rejected=true;}
        Check(rejected,"invalid candidate was accepted");Check(Writes==writes,"invalid candidate transmitted a write");
    }
    public static async Task<int> Main(string[] args)
    {
        if(args.Length!=3||args[0] is not ("read" or "zero" or "write" or "soc" or "runtime" or "idlewrite" or "controls" or "deep"))throw new ArgumentException("phase outputDirectory serialPort are required");
        string phase=args[0];Output=args[1];Directory.CreateDirectory(Output);
        using var timeout=new CancellationTokenSource(TimeSpan.FromMinutes(12));var ct=timeout.Token;
        using var trace=new StreamWriter(Path.Combine(Output,"wire.log")){AutoFlush=true};Trace=trace;
        await using var wire=new BmsSerialTransport();await wire.ConnectAsync(args[2],19200,ct);
        await using var client=new BmsClient(wire);
        client.Log+=line=>{lock(Trace){Trace.WriteLine(DateTimeOffset.UtcNow.ToString("O")+" "+line);if(line.StartsWith("TX ")){Frames++;if(line.Length>=8&&(line.Substring(5,2)=="06"||line.Substring(5,2)=="10"))Writes++;}}};
        await client.ProbeAsync(ct);var identity=await client.ReadIdentityAsync(ct);Check(client.IsC077,"D170 does not identify C077");
        var readyClock=Stopwatch.StartNew();
        while(true) {
            var ready=await client.ReadC077AfeAsync(ct);
            if(ready.Chips[0].ProfileMatches&&ready.Chips[0].Valid&&ready.Chips[0].ZeroState==2&&ready.SampleValid)break;
            if(readyClock.Elapsed>TimeSpan.FromSeconds(20))throw new IOException("AFE1 failed to become ready before testing; no test writes sent");
            await Task.Delay(250,ct);
        }
        var originalControls=await client.ReadRegistersAsync(0xD117,2,ct);
        var capture=await client.ReadC060ParametersAsync(ct);var hardware=await client.ReadC077HardwareAsync(false,ct);var battery=await client.ReadBatteryAsync(ct);
        C060Parameters.Export(Path.Combine(Output,"baseline-parameters.json"),capture);
        File.WriteAllText(Path.Combine(Output,"baseline-hardware.json"),JsonSerializer.Serialize(C077Hardware.File(hardware.Saved),Json));
        File.WriteAllText(Path.Combine(Output,"baseline-state.json"),JsonSerializer.Serialize(new{identity,battery,actual=hardware.Actual},Json));
        await Case("identity, full parameter capture and native schemas",()=>{Check(capture.Errors.Count==0,string.Join(";",capture.Errors));Check(capture.Product=="C077"&&capture.Blocks.Count==12,"unexpected capture blocks");Check(hardware.Actual.Chips[0].ProfileMatches&&hardware.Actual.Chips[0].Valid&&hardware.Actual.SampleValid,"AFE1 not valid");return Task.CompletedTask;});
        await Case("13 cells / 61001 missing slots / zero-current simulator",()=>{Check(battery.ValidCellCount==13&&battery.CellMillivolts.Take(13).All(v=>v is >2500 and <4300)&&battery.CellMillivolts.Skip(13).All(v=>v==61001),"cell map");Check(Math.Abs(battery.CurrentA)<0.1&&!battery.HasProtection,"current/protection baseline");Check(capture.Blocks["231C"].SequenceEqual(new ushort[]{13,2,4,10}),"13S fixed shunt");return Task.CompletedTask;});
        await Case("parameter and hardware JSON export/import roundtrip",()=>{var imported=C060Parameters.Import(Path.Combine(Output,"baseline-parameters.json"),"C077");Check(capture.Blocks.All(p=>p.Value.SequenceEqual(imported.Blocks[p.Key])),"business JSON");var hard=C077Hardware.Import(C077Hardware.ParseFile(File.ReadAllText(Path.Combine(Output,"baseline-hardware.json"))));Check(hard.Zip(hardware.Saved).All(p=>p.First.SequenceEqual(p.Second)),"hardware JSON");return Task.CompletedTask;});
        await Case("identity, event history and current event windows",async()=>{Check((await client.ReadRegistersAsync(0xD170,12,ct))[2]==0xC077,"product capability");Check((await client.ReadRegistersAsync(0xC001,70,ct)).Length==70,"history");Check((await client.ReadRegistersAsync(0xC008,100,ct)).Length==100,"events");});
        if(phase=="read") {
            await Case("continuous shared realtime decoder",async()=>{for(int i=0;i<20;i++){var b=await client.ReadBatteryAsync(ct);Check(b.ValidCellCount==13&&!b.HasProtection,"unstable realtime");await Task.Delay(150,ct);}});
            await Case("serial reopen / RTC idle / serial wake",async()=>{await wire.ReconnectAsync(ct);await client.ReadIdentityAsync(ct);await Task.Delay(36000,ct);var b=await client.ReadBatteryAsync(ct);Check(b.ValidCellCount==13&&!b.HasProtection,"wake decoder");});
        }
        if(phase is "zero" or "write") {
            await client.WriteSingleRegisterAsync(0x1103,3,ct);await client.WriteSingleRegisterAsync(0x1103,1,ct);await Task.Delay(300,ct);
            await Case("output and balance close readback",async()=>{var s=await client.ReadC077AfeAsync(ct);Check(s.OutputRequest==0&&s.BalanceOff,"outputs did not close");});
            await Case("AFE1 RAM current zero via GUI shared service",async()=>{var s=await client.RunC077ServiceAsync(false,ct);Check(s.Chips[0].ZeroState==2&&s.Chips[0].ZeroSamples==8&&s.Chips[0].Valid&&s.SampleValid&&!s.DriverFault,"primary zero incomplete");});
            File.WriteAllText(Path.Combine(Output,"after-zero.json"),JsonSerializer.Serialize(await client.ReadC077AfeAsync(ct),Json));
        }
        if(phase=="soc")await Case("SOC explicit set",async()=>{try{await client.SetSocAndVerifyAsync(61,ct);}finally{await client.SetSocAndVerifyAsync((ushort)battery.SocPercent,ct);}});
        if(phase=="idlewrite")await Case("idle serial command ACK and output-close readback",async()=>{
            await Task.Delay(36000,ct);
            try {await client.WriteSingleRegisterAsync(0x1103,3,ct);await Task.Delay(250,ct);Check((await client.ReadC077AfeAsync(ct)).OutputRequest==0,"command outputs");}
            finally {await client.ReadRegistersAsync(0xD170,12,ct);await client.WriteSingleRegisterAsync(0x1102,3,ct);}
        });
        if(phase=="controls") {
            var flagWords=await client.ReadRegistersAsync(0xD117,2,ct);uint original=(uint)(flagWords[0]|((uint)flagWords[1]<<16));
            foreach(ushort selector in Enumerable.Range(1,32).Where(i=>i is not (10 or 12 or 13)).Select(i=>(ushort)i)) {
                uint mask=1u<<(selector-1);
                await Case($"legacy function {selector}: flag ON/OFF/readback/restore",async()=>{
                    try {
                        await client.WriteSingleRegisterAsync(0x1102,selector,ct);var on=await client.ReadRegistersAsync(0xD117,2,ct);Check((((uint)on[0]|((uint)on[1]<<16))&mask)!=0,"ON flag");
                        await client.WriteSingleRegisterAsync(0x1103,selector,ct);var off=await client.ReadRegistersAsync(0xD117,2,ct);Check((((uint)off[0]|((uint)off[1]<<16))&mask)==0,"OFF flag");
                    } finally {if((original&mask)!=0)await client.WriteSingleRegisterAsync(0x1102,selector,ct);}
                });
            }
            await client.SetSocAndVerifyAsync((ushort)battery.SocPercent,ct);
            await Case("legacy function full flag restore",async()=>{var f=await client.ReadRegistersAsync(0xD117,2,ct);Check(((uint)f[0]|((uint)f[1]<<16))==original,"control flag restore");});
            foreach(ushort invalid in new ushort[]{0,12,13,33})await Case($"invalid legacy selector {invalid} rejected",async()=>{
                bool rejected=false;try{await client.WriteSingleRegisterAsync(0x1102,invalid,ct);}catch(BmsModbusException ex) when(ex.Code==3){rejected=true;}catch(IOException ex) when(ex.Message=="Modbus exception code=0x03."){rejected=true;}Check(rejected,"bad selector accepted");
            });
        }
        if(phase=="deep")await Case("explicit deep sleep command accepted; ST-Link verification follows separately",async()=>{await client.WriteSingleRegisterAsync(0x1102,10,ct);await Task.Delay(1500,ct);});
        if(phase=="runtime") {
            await Case("runtime SOC publication, repeated setting and restore",async()=>{try{for(ushort i=61;i<66;i++)await client.SetSocAndVerifyAsync(i,ct);}finally{await client.SetSocAndVerifyAsync((ushort)battery.SocPercent,ct);}});
            await Case("runtime cycle publication, repeated setting and restore",async()=>{try{for(ushort i=7;i<12;i++)await client.SetCycleCountAndVerifyAsync(i,ct);}finally{await client.SetCycleCountAndVerifyAsync((ushort)battery.CycleCount,ct);}});
        }
        if(phase=="write")await RunWrites(client,capture,hardware,battery,ct);
        if(phase is "zero" or "write") {foreach(ushort selector in new ushort[]{1,3})await client.WriteSingleRegisterAsync((originalControls[0]&(1<<(selector-1)))!=0?(ushort)0x1102:(ushort)0x1103,selector,ct);}
        SaveReport();
        Console.WriteLine($"RESULT frames={Frames} writes={Writes} failures={Failures}");return Failures==0?0:1;
    }
    static async Task RunWrites(BmsClient client,C060ParameterCapture capture,C077HardwareSnapshot hardware,BatterySnapshot battery,CancellationToken ct)
    {
        var protection=capture.Blocks["Protection"];
        for(int g=0;g<13;g++) {
            int offset=g*5;ushort addr=(ushort)(0x2100+offset);var old=protection.Skip(offset).Take(5).ToArray();var candidate=old.Select(v=>(ushort)(v+1)).ToArray();
            await Case($"software protection group {g+1}: all five values, readback and restore",async()=>{
                try{await client.WriteProtectionChangesAsync(candidate.Select((v,i)=>(v,i)).ToDictionary(p=>(ushort)(addr+p.i),p=>p.v),(_,_)=>{},ct);Check((await client.ReadRegistersAsync(addr,5,ct)).SequenceEqual(candidate),"whole group");int writes=Writes;await client.WriteProtectionChangesAsync(candidate.Select((v,i)=>(v,i)).ToDictionary(p=>(ushort)(addr+p.i),p=>p.v),(_,_)=>{},ct);Check(Writes==writes,"same-value writes Flash");}
                finally{await client.WriteProtectionChangesAsync(old.Select((v,i)=>(v,i)).ToDictionary(p=>(ushort)(addr+p.i),p=>p.v),(_,_)=>{},ct);}
            });
            await Case($"software group {g+1}: reject zero filter before write",()=>Reject(()=>client.WriteProtectionChangesAsync(new Dictionary<ushort,ushort>{{(ushort)(addr+4),0}},(_,_)=>{},ct)));
        }
        foreach(var group in C060Parameters.Groups) {
            var old=capture.Blocks[group.Address.ToString("X4")];var fields=C060Parameters.FieldsFor(true).Where(f=>f.WriteAddress==group.Address&&f.Editable&&f.Address!=0x231C).ToArray();if(fields.Length==0)continue;
            var edits=fields.ToDictionary(f=>f.Address,f=>old[f.Address-group.Address]==f.Maximum?(ushort)(old[f.Address-group.Address]-1):(ushort)(old[f.Address-group.Address]+1));
            if(group.Address==0x2308)edits[0x230C]=old[4]==1?(ushort)2:(ushort)1;
            var restore=fields.ToDictionary(f=>f.Address,f=>old[f.Address-group.Address]);
            await Case($"parameter 0x{group.Address:X4}: editable fields, preserve reserved values, restore",async()=>{
                try{await client.WriteC060ParameterChangesAsync(edits,(_,_)=>{},true,ct);var actual=await client.ReadRegistersAsync(group.Address,group.Count,ct);Check(actual.Select((v,i)=>edits.TryGetValue((ushort)(group.Address+i),out ushort expected)?v==expected:v==old[i]).All(v=>v),"group/unchanged bytes");int writes=Writes;await client.WriteC060ParameterChangesAsync(edits,(_,_)=>{},true,ct);Check(Writes==writes,"same-value write");}
                finally{await client.WriteC060ParameterChangesAsync(restore,(_,_)=>{},true,ct);}
            });
        }
        foreach(var f in C060Parameters.FieldsFor(true).Where(f=>f.Address<0x2100&&f.Editable)) {
            C060Parameters.TryValue(capture,f,out ushort old);ushort value=f.Signed?unchecked((ushort)(short)-7):(ushort)(old==1536?1535:old+1);
            await Case($"calibration 0x{f.Address:X4} GUI format, wire sign and restore",async()=>{try{await client.WriteC060ParameterChangesAsync(new Dictionary<ushort,ushort>{{f.Address,value}},(_,_)=>{},false,ct);var actual=await client.ReadRegistersAsync(f.WriteAddress,2,ct);Check(actual[f.Address-f.WriteAddress]==value,"calibration readback");}finally{await client.WriteC060ParameterChangesAsync(new Dictionary<ushort,ushort>{{f.Address,old}},(_,_)=>{},false,ct);}});
        }
        await Case("system cell count 13->12->13 with outputs inhibited",async()=>{try{await client.WriteC060ParameterChangesAsync(new Dictionary<ushort,ushort>{{0x231C,12}},(_,_)=>{},false,ct);Check((await client.ReadRegistersAsync(0x231C,4,ct))[0]==12,"12S save");}finally{await client.WriteC060ParameterChangesAsync(new Dictionary<ushort,ushort>{{0x231C,13}},(_,_)=>{},false,ct);await client.ReadC077HardwareAsync(true,ct);}});
        foreach(var f in C060Parameters.FieldsFor(true).Where(f=>!f.Editable))await Case($"readonly parameter 0x{f.Address:X4} rejected",()=>Reject(()=>client.WriteC060ParameterChangesAsync(new Dictionary<ushort,ushort>{{f.Address,1}},(_,_)=>{},true,ct)));
        await Case("SOC endpoints invalid combination rejected",()=>Reject(()=>client.WriteC060ParameterChangesAsync(new Dictionary<ushort,ushort>{{0x231B,5000}},(_,_)=>{},false,ct)));
        await Case("cell count range rejected",()=>Reject(()=>client.WriteC060ParameterChangesAsync(new Dictionary<ushort,ushort>{{0x231C,4}},(_,_)=>{},false,ct)));
        await Case("SOC set, live capacity verification and restore",async()=>{try{await client.SetSocAndVerifyAsync(61,ct);}finally{await client.SetSocAndVerifyAsync((ushort)battery.SocPercent,ct);}});
        await Case("cycle set, parameter/live consistency and restore",async()=>{try{await client.SetCycleCountAndVerifyAsync((ushort)(battery.CycleCount+1),ct);}finally{await client.SetCycleCountAndVerifyAsync((ushort)battery.CycleCount,ct);}});
        foreach(ushort addr in new ushort[]{0xFFF0,0xFFF1,0xFFF2}) {
            int i=(addr-0xFFF0)*16;ushort[] source=capture.Blocks["Identity"].Skip(i).Take(16).ToArray();byte[] bytes=source.SelectMany(v=>new[]{(byte)(v>>8),(byte)v}).TakeWhile(v=>v!=0).ToArray();System.Text.Encoding.RegisterProvider(System.Text.CodePagesEncodingProvider.Instance);string old=System.Text.Encoding.GetEncoding(936).GetString(bytes);
            await Case($"identity 0x{addr:X4}: GBK write/readback/restore",async()=>{try{await client.WriteC060IdentityAsync(addr,"C077自动测试",ct);}finally{await client.WriteC060IdentityAsync(addr,old,ct);}});
        }
        await Case("identity length and control characters rejected",async()=>{await Reject(()=>client.WriteC060IdentityAsync(0xFFF0,new string('x',33),ct));await Reject(()=>client.WriteC060IdentityAsync(0xFFF0,"bad\nSN",ct));});
        ushort[] baseline=hardware.Saved[0];ushort[] target={16,4255,4000,4145,2620,4000,2820,3920,2020,1020,4980,16,50,2000,400,400,200,192,30,20,500,128,32,610,460,65476,60,760,610,65326,65376,1000};ushort[] invalid={128,999,101,999,999,101,999,5101,5101,5101,5101,9,21,51,31,11,51,1,21,11,201,65,9,1001,1001,65135,65135,1001,1001,65135,65135,101};
        for(int i=0;i<32;i++) {
            int field=i;
            await Case($"AFE1 field {i}: Flash save/readback/restore, EEPROM unchanged",async()=>{var changed=(ushort[])baseline.Clone();changed[field]=target[field];try{await client.SaveC077HardwareAsync(0,changed,baseline,ct);var actual=await client.ReadC077HardwareAsync(false,ct);Check(actual.Saved[0].SequenceEqual(changed),"saved field");Check(actual.Actual.Chips[0].Eeprom.SequenceEqual(hardware.Actual.Chips[0].Eeprom),"Flash unexpectedly programmed EEPROM");}finally{await client.SaveC077HardwareAsync(0,baseline,null,ct);}});
            await Case($"AFE1 field {i}: reject invalid physical code before write",()=>{var bad=(ushort[])baseline.Clone();bad[field]=invalid[field];return Reject(()=>client.SaveC077HardwareAsync(0,bad,null,ct));});
        }
        await Case("AFE1 stale editor observation rejected",()=>{var stale=(ushort[])baseline.Clone();stale[0]^=1;return Reject(()=>client.SaveC077HardwareAsync(0,target,stale,ct));});
        await Case("AFE1 explicit EEPROM selected program, all 32 fields, restore",async()=>{
            try{await client.SaveC077HardwareAsync(0,target,baseline,ct);var actual=await client.ProgramC077HardwareAsync(1,ct);Check(actual.Chips[0].ProfileMatches&&actual.Chips[0].Valid&&actual.Chips[0].ZeroState==2,"EEPROM ready");var chip=actual.Chips[0];int[] decoded=C077Hardware.Decode(chip.Eeprom,chip.Tr);for(int i=0;i<32;i++){if(i is >=23 and <=30)Check(C077Hardware.TemperatureCode(unchecked((short)target[i]),chip.Tr,((i-23)&2)!=0)==chip.Eeprom[17+i-23],$"temperature code {i}");else Check(decoded[i]==target[i],$"EEPROM physical field {i}");}File.WriteAllText(Path.Combine(Output,"changed-afe1.json"),JsonSerializer.Serialize(actual,Json));}
            finally{await client.SaveC077HardwareAsync(0,baseline,null,ct);var restored=await client.ProgramC077HardwareAsync(1,ct);Check(restored.Chips[0].Eeprom.SequenceEqual(hardware.Actual.Chips[0].Eeprom),"EEPROM baseline restore");}
        });
        await Case("final parameters and both saved hardware slots equal baseline",async()=>{var final=await client.ReadC060ParametersAsync(ct);foreach(var block in capture.Blocks.Where(b=>b.Key is not ("Realtime" or "C077Afe")))Check(block.Value.SequenceEqual(final.Blocks[block.Key]),"changed final block "+block.Key);var hard=await client.ReadC077HardwareAsync(true,ct);Check(hard.Saved.Zip(hardware.Saved).All(p=>p.First.SequenceEqual(p.Second)),"saved hardware restore");Check(hard.Actual.Chips[0].Eeprom.SequenceEqual(hardware.Actual.Chips[0].Eeprom),"EEPROM restore");});
    }
}







