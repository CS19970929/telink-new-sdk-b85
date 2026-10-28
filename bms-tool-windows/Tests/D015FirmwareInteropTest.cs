using BmsTool.Windows;
using System.Diagnostics;
using System.IO;

static class Program
{
    static void Check(bool ok,string why) { if(!ok)throw new Exception(why); }
    static async Task Main(string[] args)
    {
        await using var transport=new FirmwareTransport(args[0]);
        await using var client=new BmsClient(transport);
        var identity=await client.ReadIdentityAsync("", "D015 host simulation");
        Check(client.IsD015 && identity.Hardware.StartsWith("HS-D015"),"D015 product identity");
        var battery=await client.ReadBatteryAsync();
        Check(battery.CellMillivolts.Take(13).All(v=>v>3000) && battery.CellMillivolts.Skip(13).All(v=>v==61001),"13 cells and absent slots");
        Check(battery.UsesRealtimeWindow && battery.ProtocolVersion==2,"D120 realtime");
        int verified=0;
        await client.WriteProtectionChangesAsync(new Dictionary<ushort,ushort>{{0x2104,101}},(_,_)=>verified++);
        Check(verified==1 && (await client.ReadProtectionAllAsync())[4]==101,"real firmware software protection write/readback");
        var afeClient=new AfeHardwareClient(client);
        var before=await afeClient.ReadAllAsync();
        Check(before.Info.BackendModel==0x3520 && before.Info.CellCount==13,"3520 capability");
        var candidate=before.Requested.ToArray();candidate[34]&=0xFFBE;
        var after=await afeClient.WriteAllAsync(candidate);
        Check(after.Requested.SequenceEqual(candidate) && after.Effective[34]==candidate[34] && after.Info.ApplyState==1,"MTU23 staged commit and enables");
        await afeClient.WriteAllAsync(before.Requested);
        var calibration=await client.BeginCurrentCalibrationAsync(default);
        int ma=await client.SaveCurrentCalibrationAsync(calibration,new CurrentCalibrationCoefficients(100,1100000),default);
        Check(ma==-110 && calibration.SaveVerified,"persisted current calibration and fresh formula");
        await client.EndCurrentCalibrationAsync(calibration);
        await client.WriteD008SerialAsync("D015-INTEROP-001");
        identity=await client.ReadIdentityAsync("", "D015 host simulation");
        Check(identity.Serial=="D015-INTEROP-001","staged SN atomic save");
        var parameters=await client.ReadD008ParametersAsync();
        Check(parameters.Supported && parameters.Errors.Count==0,"D015 parameter capture: "+string.Join(";",parameters.Errors));
        Check(transport.MaxFrame<=20 && transport.FragmentCount>=16,"MTU23 requests use profile fragments");
        Console.WriteLine("PASS D015 C firmware interop: identity, realtime, absent slots, software/hardware writes, MTU23 profile commit, enable readback, calibration/fresh sample, atomic SN, parameter capture");
    }
}
sealed class FirmwareTransport : IBmsTransport, IBmsMtuTransport
{
    readonly Process process;
    public FirmwareTransport(string exe) {
        var start=new ProcessStartInfo(exe) {RedirectStandardInput=true,RedirectStandardOutput=true,RedirectStandardError=true,UseShellExecute=false,CreateNoWindow=true};
        start.ArgumentList.Add("--pc-server");process=Process.Start(start)!;
    }
    public bool IsConnected=>!process.HasExited;
    public int? NegotiatedMtu=>23;
    public string DiscoveryDescription=>"real D015 C modules / simulated AFE and Flash";
    public int MaxFrame,FragmentCount;
    public event Action<ReadOnlyMemory<byte>>? DataReceived;
    public event Action<string>? ConnectionProgress {add{} remove{}}
    public Task ReconnectAsync(CancellationToken ct=default)=>Task.CompletedTask;
    public async Task WriteAsync(ReadOnlyMemory<byte> frame,CancellationToken ct=default) {
        byte[] bytes=frame.ToArray();MaxFrame=Math.Max(MaxFrame,bytes.Length);
        if(bytes[1]==0x42 && bytes[2]==5)FragmentCount++;
        await process.StandardInput.WriteLineAsync(Convert.ToHexString(bytes));
        await process.StandardInput.FlushAsync(ct);
        string? response=await process.StandardOutput.ReadLineAsync(ct).AsTask().WaitAsync(TimeSpan.FromSeconds(5),ct);
        if(string.IsNullOrEmpty(response))throw new IOException("Firmware server stopped: "+await process.StandardError.ReadToEndAsync(ct));
        byte[] data=Convert.FromHexString(response);
        // BLE可能拆分通知，真实客户端负责重组，不能依赖一次事件一整帧。
        for(int at=0;at<data.Length;at+=7)DataReceived?.Invoke(data.AsMemory(at,Math.Min(7,data.Length-at)));
    }
    public async ValueTask DisposeAsync() {
        process.StandardInput.Close();
        await process.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(5));
        process.Dispose();
    }
}
