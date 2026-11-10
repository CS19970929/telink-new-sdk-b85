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
        var initial=await client.ReadC077AfeAsync();
        Check(!initial.Chips[1].ProfileMatches&&!initial.SampleValid,"actual C fixture begins with AFE2 mismatch");
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
        Console.WriteLine("PASS shared C# BmsClient / production C309 service: schema2, actual profile/zero completion, disabled AFE2 CADC, interrupted EEPROM, no blind replay and explicit recovery");
    }
}
sealed class NativeTransport : IBmsTransport
{
    readonly Process process;public int ServiceWrites;
    public NativeTransport(string path,string mode="--host-service") {
        process=Process.Start(new ProcessStartInfo(path,mode) {UseShellExecute=false,CreateNoWindow=true,RedirectStandardInput=true,RedirectStandardOutput=true,RedirectStandardError=true})??throw new IOException("native fixture start failed");
    }
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
