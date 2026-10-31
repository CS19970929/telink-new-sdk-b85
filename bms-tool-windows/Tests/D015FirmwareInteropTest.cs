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
        Check(battery.SohPercent==65535 && !battery.SohValid && battery.SohDisplay=="—","unknown SOH is not 65535% or 100%");
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
        Check(parameters.Blocks["D015Basic"].Length==32 && parameters.Blocks["D015Ocv"].Length==42,"actual D015 parameter banks");
        Check(!parameters.Blocks.ContainsKey("Heater") && !parameters.Blocks.ContainsKey("BluetoothName"),"no unrelated product parameters");
        await client.WriteD008VerifiedAsync(0x2318,new ushort[]{350},false,default);
        Check((await client.ReadRegistersAsync(0x2318,1))[0]==350,"35Ah saved and read back");
        await client.WriteD008VerifiedAsync(0x340D,new ushort[]{35},false,default);
        await client.WriteD008VerifiedAsync(0x3411,new ushort[]{4},false,default);
        await client.WriteD008VerifiedAsync(0x2202,new ushort[]{4130,95},false,default);
        Check((await client.ReadRegistersAsync(0x340D,1))[0]==35 && (await client.ReadRegistersAsync(0x3411,1))[0]==4,"D015 balance and RTC saved/read back");
        Check((await client.ReadRegistersAsync(0x2202,2)).SequenceEqual(new ushort[]{4130,95}),"D015 OCV point transaction");
        bool rejected=false;
        try {await client.WriteD008VerifiedAsync(0x2318,new ushort[]{9},false,default);}catch(ArgumentException){rejected=true;}
        Check(rejected,"D015 capacity range rejects before write");
        Check(transport.MaxFrame<=20 && transport.FragmentCount>=16,"MTU23 requests use profile fragments");
        string? expectedBuild=Environment.GetEnvironmentVariable("D015_EXPECT_BUILD_ID");
        if(expectedBuild is not null) {
            var identityBuild=await client.TryReadFirmwareBuildIdentityAsync();
            Check(identityBuild is not null && identityBuild.BuildId==Convert.ToUInt32(expectedBuild,16),"Git Build ID read with existing word order");
            Check(await client.TryReadFirmwareBuildIdAsync()==identityBuild!.BuildId,"legacy Build ID API preserved");
            var diagnostic=await client.ReadDiagnosticsAsync(false,"D015 simulator");
            Check(diagnostic.Supported && diagnostic.Errors.Count==0 && diagnostic.Words is null && diagnostic.Boot.Any(x=>x.Value==expectedBuild),"identity-only block must not fabricate runtime diagnostics");
        }
        await TestStm32IapAsync();
        Console.WriteLine("PASS D015 C firmware interop: identity, realtime, absent slots, software/hardware writes, MTU23 profile commit, enable readback, calibration/fresh sample, atomic SN, parameter capture");
    }
    static async Task TestStm32IapAsync()
    {
        string path=Path.Combine(Path.GetTempPath(),Guid.NewGuid()+".dat");
        try {
            byte[] bytes=new byte[1024];Array.Fill(bytes,(byte)0xFF);
            void Put(int at,uint value)=>System.Buffers.Binary.BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(at,4),value);
            Put(0,0x20002000);Put(4,0x08001D01);Put(192,0xD0150320);Put(196,0x08001C00);Put(204,900);
            File.WriteAllBytes(path,bytes);
            var image=FirmwareImage.LoadForTarget(path,OtaTargetKind.Stm32SerialIap);
            foreach(bool failEnter in new[]{false,true}) {
                await using var link=new IapTransport(failEnter);
                var updater=new Stm32SerialBleOtaClient(link,false){RequireD015Image=true};
                bool failed=false;
                try {Check(await updater.UpgradeAsync(image,default),"IAP completion");}catch(IOException) when(failEnter){failed=true;}
                Check(failed==failEnter,"ENTER failure must stop upgrade");
                Check(link.Reads==2 && link.Writes==(failEnter?1:3) && link.Reconnects==(failEnter?0:1),"ENTER ACK precedes reconnect/page; no retry on exception");
            }
        }finally{File.Delete(path);}
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

sealed class IapTransport(bool failEnter) : IBmsTransport
{
    public int Reads,Writes,Reconnects;
    public bool IsConnected=>true;
    public string DiscoveryDescription=>"offline STM32 IAP peer";
    public event Action<ReadOnlyMemory<byte>>? DataReceived;
    public event Action<string>? ConnectionProgress {add{} remove{}}
    public Task ReconnectAsync(CancellationToken ct=default){Reconnects++;return Task.CompletedTask;}
    public async Task WriteAsync(ReadOnlyMemory<byte> data,CancellationToken ct=default) {
        byte[] frame=data.ToArray();
        ushort reg=(ushort)((frame[2]<<8)|frame[3]);
        if(frame[1]==3) {
            Reads++;
            byte[] reply=reg==0xD170 ? new byte[]{1,0x83,2,0,0} : new byte[]{1,3,8,0xD0,0x15,3,0x20,0,2,4,1,0,0};
            ushort readCrc=ModbusRtu.Crc16(reply.AsSpan(0,reply.Length-2));
            reply[^2]=(byte)readCrc;reply[^1]=(byte)(readCrc>>8);
            DataReceived?.Invoke(reply);return;
        }
        Writes++;
        if(reg==0xFFFD)await Task.Delay(200,ct); // APP prepares before acknowledging.
        if(reg==0xFFFE && (Reconnects!=1 || frame.Length!=1033 || frame[4]!=4 || frame[5]!=0 || frame[6]!=0))
            throw new Exception("IAP page format/order changed");
        byte[] response;
        if(failEnter && reg==0xFFFD)response=new byte[]{1,0x90,4,0,0};
        else{response=new byte[8];Array.Copy(frame,response,6);}
        ushort crc=ModbusRtu.Crc16(response.AsSpan(0,response.Length-2));
        response[^2]=(byte)crc;response[^1]=(byte)(crc>>8);
        DataReceived?.Invoke(response.AsMemory(0,3));DataReceived?.Invoke(response.AsMemory(3));
    }
    public ValueTask DisposeAsync()=>ValueTask.CompletedTask;
}
