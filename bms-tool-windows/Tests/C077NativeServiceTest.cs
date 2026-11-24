using BmsTool.Windows;
using System.Diagnostics;
using System.IO;

static class Test
{
    static void Check(bool ok,string why){if(!ok)throw new Exception(why);}
    public static async Task Main(string[] args)
    {
        await using var wire=new NativeTransport(args[0]);
        await using var client=new BmsClient(wire);
        await client.ReadIdentityAsync();Check(client.IsC077,"production C capability selects C077");
        var initial=await client.ReadC077AfeAsync();Check(initial.Schema==3&&initial.UsesPersistentCalibration,"new startup-check diagnostics");
        Check(!initial.Chips[1].ProfileMatches&&!initial.SampleValid,"actual C fixture begins with AFE2 mismatch");
        var dual = (await Sh309Protection.ReadAsync(client)).Chips;
        Check(dual.Count == 2 && dual[0].Saved[1] == 4260 && dual[1].Saved[1] == 4300 &&
            dual[0].Readback == Sh309Readback.Matches && dual[1].Readback == Sh309Readback.Different &&
            dual[1].Actual?[1] != 4300 && wire.ConfigurationWrites == 0,
            "common dual-chip read keeps each Flash target and live difference");
        wire.RejectVerify = true;
        var failed = (await Sh309Protection.ReadAsync(client)).Chips;
        Check(failed.All(c => c.Actual is null && c.Readback == Sh309Readback.Unknown) &&
            failed[0].Saved[1] == 4260 && failed[1].Saved[1] == 4300 && wire.ConfigurationWrites == 0,
            "failed EEPROM verification keeps both Flash targets and hides cached hardware values");
        wire.RejectVerify = false;
        var programmed=await client.RunC077ServiceAsync(true);
        Check(programmed.ZeroReady&&programmed.SampleValid&&programmed.Chips.All(c=>c.Valid),"C driver differential programming, reset, zero and recovery complete");
        Check(programmed.Chips[1].ZeroStateText=="未启用","secondary never requests a CADC zero");
        var zero=await client.RunC077ServiceAsync(false);
        Check(zero.Chips[0].ZeroSamples==8&&zero.ZeroReady,"production primary-only zero collection");
        await wire.Configure("@fail");int commands=wire.ServiceWrites;
        try{await client.RunC077ServiceAsync(true);throw new Exception("interrupted EEPROM accepted");}
        catch(IOException e){Check(e.Message.Contains("0x03"),"failure reports actual chip/register");}
        Check(wire.ServiceWrites==commands+1,"failed production service never automatically replays");
        await wire.Configure("@resume");
        var resumed=await client.RunC077ServiceAsync(true);
        Check(resumed.ZeroReady&&resumed.SampleValid&&!resumed.ServiceFailed,"explicit retry recovers native C state");
        var hardware=await client.ReadC077HardwareAsync(true);
        Check(hardware.EepromReadbackConfirmed && hardware.Saved[0][1]==4260&&hardware.Saved[1][1]==4300,
            "explicit matching EEPROM readback and independent defaults");
        Check(!(await client.ReadC077HardwareAsync(false)).EepromReadbackConfirmed,
            "cached diagnostic never counts as a new EEPROM readback");
        var file=C077Hardware.File(hardware.Saved);
        Check(C077Hardware.Import(file)[0].SequenceEqual(hardware.Saved[0]),"named JSON physical roundtrip");
        string webJson=System.Text.Json.JsonSerializer.Serialize(file,new System.Text.Json.JsonSerializerOptions(System.Text.Json.JsonSerializerDefaults.Web));
        Check(C077Hardware.Import(C077Hardware.ParseFile(webJson))[0].SequenceEqual(hardware.Saved[0]),"actual CLI camelCase JSON imports into editor");
        try {C077Hardware.ParseFile(webJson.Replace("\"product\":\"C077\"","\"product\":\"C060\""));throw new Exception("foreign hardware JSON accepted");}catch(System.IO.InvalidDataException){}
        Check(C077Hardware.Decode(hardware.Actual.Chips[0].Eeprom,hardware.Actual.Chips[0].Tr)[14]==500,"OCD2 distinct official 500mV table");
        var a=(ushort[])hardware.Saved[0].Clone();var b=(ushort[])hardware.Saved[1].Clone();a[1]=4240;b[1]=4320;
        wire.NegotiatedMtu = 23;
        int savedWrites = wire.ConfigurationWrites;
        try { await client.SaveC077HardwareAsync(0,a,hardware.Saved[0]); throw new Exception("insufficient MTU accepted"); }
        catch (IOException e) { Check(e.Message.Contains("MTU"), "insufficient MTU reported"); }
        Check(wire.ConfigurationWrites == savedWrites, "insufficient MTU never sends a configuration write");
        wire.NegotiatedMtu = 76;
        await client.SaveC077HardwareAsync(0,a,hardware.Saved[0]);await client.SaveC077HardwareAsync(1,b,hardware.Saved[1]);
        var pending=await client.ReadC077HardwareAsync(true);
        Check(!pending.EepromReadbackConfirmed && pending.Actual.Chips[0].Eeprom[3]==0x54&&pending.Actual.Chips[1].Eeprom[3]==0x5C,
            "saving Flash leaves both EEPROMs pending");
        commands = wire.ServiceWrites;
        try { await client.ProgramC077HardwareAsync(3, observed:hardware.Saved); throw new Exception("stale Flash target accepted"); }
        catch (IOException e) { Check(e.Message.Contains("Flash"), "stale Flash target reported"); }
        Check(wire.ServiceWrites == commands, "stale draft never sends EEPROM service command");
        var primary=await client.ProgramC077HardwareAsync(1, observed:pending.Saved);
        Check(primary.Chips[0].ProfileMatches&&!primary.Chips[1].ProfileMatches&&primary.Chips[0].ZeroState==2,"selected primary completes zero while secondary awaits explicit program");
        await client.ProgramC077HardwareAsync(2);
        var complete=await client.ReadC077HardwareAsync(true);
        Check(complete.Actual.Chips.All(c=>c.ProfileMatches&&c.Valid)&&complete.Actual.SampleValid,"second independent program restores pack eligibility");
        Check(complete.Actual.Chips[0].Eeprom[3]==0x50&&complete.Actual.Chips[1].Eeprom[3]==0x60,"two distinct EEPROM values read back");
        int writes=wire.ServiceWrites;
        try {var bad=(ushort[])a.Clone();bad[13]=30;await client.SaveC077HardwareAsync(0,bad);throw new Exception("bad hardware delay accepted");}catch(ArgumentException){}
        Check(writes==wire.ServiceWrites,"invalid physical code does not program");
        await wire.Configure("@unready");int serviceBefore=wire.ServiceWrites;
        try {await client.ReadC077HardwareAsync();throw new Exception("unready RAM advertised as saved Flash");}catch(System.IO.IOException){}
        Check(wire.ServiceWrites==serviceBefore,"unready parameters reject before maintenance write");await wire.Configure("@ready");
        Console.WriteLine("PASS shared C# BmsClient / production C309 service: schema3 + hardware schema1, independent Flash/selected program/readback, actual profile/zero completion, disabled AFE2 CADC, interrupted EEPROM, no blind replay and explicit recovery");
    }
}
sealed class NativeTransport : IBmsTransport, IBmsMtuTransport
{
    readonly Process process;public int ServiceWrites;
    public bool RejectVerify { get; set; }
    public NativeTransport(string path,string mode="--host-service") {
        process=Process.Start(new ProcessStartInfo(path,mode) {UseShellExecute=false,CreateNoWindow=true,RedirectStandardInput=true,RedirectStandardOutput=true,RedirectStandardError=true})??throw new IOException("native fixture start failed");
    }
    public int? NegotiatedMtu { get; set; } = 247;
    public int ConfigurationWrites { get; private set; }
    public bool IsConnected=>!process.HasExited;
    public bool RequiresSerialWakeup=>false;
    public string DiscoveryDescription=>"C077 production C service / modeled GPIO, EEPROM, clock and wire";
    public event Action<ReadOnlyMemory<byte>>? DataReceived;
    public event Action<string>? ConnectionProgress {add{}remove{}}
    public Task ReconnectAsync(CancellationToken ct=default)=>Task.CompletedTask;
    public async Task Configure(string command) {
        await process.StandardInput.WriteLineAsync(command);await process.StandardInput.FlushAsync();
        if(await process.StandardOutput.ReadLineAsync()!= "OK")throw new IOException("native fixture configuration failed");
    }
    public async Task WriteAsync(ReadOnlyMemory<byte> data,CancellationToken ct=default) {
        byte[] bytes=data.ToArray();if(bytes.Length>=6&&bytes[1]==6&&bytes[2]==0x2A&&bytes[3]==0x7F)ServiceWrites++;
        if (bytes.Length >= 6 && bytes[1] == 16 && bytes[2] == 0x2B) ConfigurationWrites++;
        if (RejectVerify && bytes.Length == 8 && bytes.AsSpan(1, 5).SequenceEqual(new byte[] { 6, 0x2A, 0x7F, 0xC0, 0x87 }))
        {
            DataReceived?.Invoke(ModbusRtu.Frame(new byte[] { 1, 0x86, 4 }));
            return;
        }
        await process.StandardInput.WriteLineAsync(Convert.ToHexString(bytes));await process.StandardInput.FlushAsync();
        string? reply=await process.StandardOutput.ReadLineAsync(ct);
        if(reply is null)throw new IOException("native fixture exited: "+await process.StandardError.ReadToEndAsync(ct));
        byte[] frame=Convert.FromHexString(reply);
        for(int at=0;at<frame.Length;at+=7)DataReceived?.Invoke(frame.AsMemory(at,Math.Min(7,frame.Length-at)));
    }
    public async ValueTask DisposeAsync() {
        if(!process.HasExited){process.StandardInput.Close();await process.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(3));}
        process.Dispose();
    }
}
