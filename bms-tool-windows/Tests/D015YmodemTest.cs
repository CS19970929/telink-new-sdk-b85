using System.Buffers.Binary;
using System.Diagnostics;
using BmsTool.Windows;

static class Test
{
    static void Check(bool result,string message) { if(!result)throw new Exception(message); }
    static byte[] Package(int length=2048)
    {
        byte[] b=new byte[length+32];for(int i=32;i<b.Length;i++)b[i]=(byte)(i*7);
        void Put(int at,uint value)=>BinaryPrimitives.WriteUInt32LittleEndian(b.AsSpan(at,4),value);
        Put(0,D015FirmwarePackage.Magic);BinaryPrimitives.WriteUInt16LittleEndian(b.AsSpan(4),2);BinaryPrimitives.WriteUInt16LittleEndian(b.AsSpan(6),32);
        Put(8,D015FirmwarePackage.BoardId);Put(12,D015FirmwarePackage.AppAddress);Put(16,(uint)length);Put(20,0x00030001);
        Put(32,0x20002000);Put(36,D015FirmwarePackage.AppAddress+209);
        Put(224,D015FirmwarePackage.BoardId);Put(228,D015FirmwarePackage.AppAddress);Put(232,0x00030001);Put(236,(uint)length-16);
        Put(24,D015FirmwarePackage.Crc32(b.AsSpan(32)));Put(28,D015FirmwarePackage.Crc32(b.AsSpan(0,28)));return b;
    }
    public static async Task Main(string[] args)
    {
        var cap=new ushort[]{0x5550,1,0xD015,0x0320,2,2,0x0800,0x2000,0,45056,1,0};
        Check(UpgradeDetection.ParseCapabilities(cap)?.Protocol==SerialUpgradeProtocol.D015Ymodem,"capability identification");
        cap[4]=99;bool rejected=false;try{_=UpgradeDetection.ParseCapabilities(cap);}catch(InvalidDataException){rejected=true;}
        Check(rejected,"unknown advertised protocol must not fall back to a legacy write");
        byte[] good=Package();_=D015FirmwarePackage.Parse(good);
        foreach(int offset in new[]{0,8,12,16,20,24,28,240,good.Length-1}) {
            byte[] bad=good.ToArray();bad[offset]^=1;rejected=false;
            try{_=D015FirmwarePackage.Parse(bad);}catch(InvalidDataException){rejected=true;}
            Check(rejected,$"corrupt package @{offset} rejected before device writes");
        }
        string file=Path.Combine(Path.GetTempPath(),Guid.NewGuid()+".dat");
        try {
            File.WriteAllBytes(file,good);FirmwareImage image=FirmwareImage.LoadForTarget(file,OtaTargetKind.Stm32SerialIap);
            foreach(bool loseAck in new[]{false,true}) {
                await using var transport=new ReceiverProcess(args[0]) {Faults=loseAck};
                var device=await UpgradeDetection.ReadAsync(transport,false,default);
                Check(device.Mode==1,"empty APP still supports read-only Boot identification");
                var sender=new D015YmodemClient(transport);double progress=0;sender.Progress+=p=>progress=p.Percent;sender.Log+=Console.WriteLine;
                Check(await sender.UpgradeAsync(image,device,default),"actual C receiver upgrade");
                Check(transport.Reconnects>0,"APP verification rebuilds transport after reset");
                Check(progress==100,"success only after exact APP CRC/version/length and BMS readback");
                if(loseAck)Check(transport.Corrupted && transport.DroppedAck && transport.DroppedFinalAck,"error injection exercised");
                device=await UpgradeDetection.ReadAsync(transport,false,default);Check(device.Mode==0,"APP advertises same protocol");
                // A second upgrade exercises APP ENTER as well as recovery-only entry.
                int reconnects=transport.Reconnects;
                Check(await new D015YmodemClient(transport).UpgradeAsync(image,device,default),"APP entry and repeated upgrade");
                Check(transport.Reconnects>=reconnects+2,"APP entry and final reset each rebuild the same device link");
            }
            await using(var transport=new ReceiverProcess(args[0])) {
                var device=await UpgradeDetection.ReadAsync(transport,false,default);
                using var cancel=new CancellationTokenSource();var sender=new D015YmodemClient(transport);
                sender.Progress+=p=>{if(p.SentBytes>0)cancel.Cancel();};
                bool canceled=false;try{await sender.UpgradeAsync(image,device,cancel.Token);}catch(OperationCanceledException){canceled=true;}
                Check(canceled,"cancel during transfer");
                await transport.ResetAsync();
                device=await UpgradeDetection.ReadAsync(transport,false,default);Check(device.Mode==1,"partial image cannot boot after MCU reset");
                Check(await new D015YmodemClient(transport).UpgradeAsync(image,device,default),"full restart after interrupted update");
            }
        } finally {File.Delete(file);}
        Console.WriteLine("PASS: host -> production C YMODEM; bad CRC, lost ACK, final ACK ambiguity, cancel/reset/retry, APP entry, read-only detection and exact post-verification");
    }
}
sealed class ReceiverProcess : IBmsTransport
{
    readonly Process _process;readonly SemaphoreSlim _gate=new(1);readonly CancellationTokenSource _stop=new();readonly Task _poll;
    public bool Faults,Corrupted,DroppedAck,DroppedFinalAck;
    public int Reconnects;
    public ReceiverProcess(string path) {
        var start=new ProcessStartInfo(path){UseShellExecute=false,CreateNoWindow=true,RedirectStandardInput=true,RedirectStandardOutput=true,RedirectStandardError=true};
        start.ArgumentList.Add("--server");_process=Process.Start(start)!;_poll=PollAsync();
    }
    public bool IsConnected=>!_process.HasExited;
    public string DiscoveryDescription=>"production D015 C YMODEM receiver with simulated Flash/AFE";
    public event Action<ReadOnlyMemory<byte>>? DataReceived;
    public event Action<string>? ConnectionProgress {add{}remove{}}
    public Task ReconnectAsync(CancellationToken ct=default){Reconnects++;return Task.CompletedTask;}
    public Task ResetAsync()=>CommandAsync("R",false,default);
    async Task PollAsync() {
        try {while(true){await Task.Delay(100,_stop.Token);await CommandAsync("T 250",false,_stop.Token);}}
        catch(OperationCanceledException) { }
    }
    public Task WriteAsync(ReadOnlyMemory<byte> data,CancellationToken ct=default) {
        byte[] bytes=data.ToArray();bool drop=false;
        if(Faults && bytes.Length==1029 && bytes[1]==1) {
            if(!Corrupted){bytes[20]^=1;Corrupted=true;}
            else if(!DroppedAck){drop=true;DroppedAck=true;}
        }
        if(Faults && bytes.Length==133 && bytes[1]==0 && bytes[3]==0 && !DroppedFinalAck){drop=true;DroppedFinalAck=true;}
        return CommandAsync(Convert.ToHexString(bytes),drop,ct);
    }
    async Task CommandAsync(string command,bool drop,CancellationToken ct) {
        await _gate.WaitAsync(ct);
        try {
            await _process.StandardInput.WriteLineAsync(command.AsMemory(),ct);await _process.StandardInput.FlushAsync(ct);
            string? line=await _process.StandardOutput.ReadLineAsync(ct).AsTask().WaitAsync(TimeSpan.FromSeconds(5),ct);
            if(line is null)throw new IOException("C receiver terminated: "+await _process.StandardError.ReadToEndAsync(ct));
            byte[] response=Convert.FromHexString(line);
            if(!drop)for(int at=0;at<response.Length;at+=7)DataReceived?.Invoke(response.AsMemory(at,Math.Min(7,response.Length-at)));
        } finally {_gate.Release();}
    }
    public async ValueTask DisposeAsync() {
        _stop.Cancel();await _poll;_process.StandardInput.Close();
        await _process.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(5));_process.Dispose();_stop.Dispose();_gate.Dispose();
    }
}
