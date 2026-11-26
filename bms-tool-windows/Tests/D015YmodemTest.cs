using System.Buffers.Binary;
using System.Diagnostics;
using BmsTool.Windows;

static class Test
{
    static string project = "D015";
    static uint Board => project switch { "C060" => 0xC0600020, "C077" => 0xC0770016, _ => D015FirmwarePackage.BoardId };
    static uint App => project == "D015" ? D015FirmwarePackage.AppAddress : 0x08001C00;
    static ushort Capacity => project == "D015" ? (ushort)(44 * 1024) : (ushort)(51 * 1024);
    static void Check(bool result,string message) { if(!result)throw new Exception(message); }
    static byte[] Package(int length=2048)
    {
        byte[] b=new byte[length+32];for(int i=32;i<b.Length;i++)b[i]=(byte)(i*7);
        void Put(int at,uint value)=>BinaryPrimitives.WriteUInt32LittleEndian(b.AsSpan(at,4),value);
        Put(0,D015FirmwarePackage.Magic);BinaryPrimitives.WriteUInt16LittleEndian(b.AsSpan(4),2);BinaryPrimitives.WriteUInt16LittleEndian(b.AsSpan(6),32);
        Put(8,Board);Put(12,App);Put(16,(uint)length);Put(20,0x00030001);
        Put(32,0x20002000);Put(36,App+209);
        Put(224,Board);Put(228,App);Put(232,0x00030001);Put(236,(uint)length-16);
        Put(24,D015FirmwarePackage.Crc32(b.AsSpan(32)));Put(28,D015FirmwarePackage.Crc32(b.AsSpan(0,28)));return b;
    }
    static byte[] Capability(ushort mode=1,ushort protocol=2)
    {
        ushort[] words={0x5550,1,(ushort)(Board>>16),(ushort)Board,protocol,2,(ushort)(App>>16),(ushort)App,0,Capacity,mode,0};
        byte[] b=new byte[29];b[0]=1;b[1]=3;b[2]=24;
        for(int i=0;i<12;i++)BinaryPrimitives.WriteUInt16BigEndian(b.AsSpan(3+i*2,2),words[i]);
        ushort crc=0xFFFF;foreach(byte v in b.AsSpan(0,27)){crc^=v;for(int i=0;i<8;i++)crc=(ushort)((crc>>1)^((crc&1)!=0?0xA001:0));}
        BinaryPrimitives.WriteUInt16LittleEndian(b.AsSpan(27),crc);return b;
    }
    static async Task DiscoveryEdges()
    {
        foreach(bool noise in new[]{false,true}) {
            await using var t=new ProbeTransport(_=>Capability()) { Noise=noise };
            var d=await UpgradeDetection.ReadAsync(t,false,default);
            Check(d.Protocol==SerialUpgradeProtocol.YmodemV2,"C prefix and byte-fragmented capability response");
        }
        await using(var t=new ProbeTransport(_=>throw new IOException("cable gone"))) {
            bool failed=false;try{await UpgradeDetection.ReadAsync(t,true,default);}catch(IOException){failed=true;}
            Check(failed && t.Writes.Count==1,"I/O failure cannot authorize legacy fallback");
        }
        await using(var t=new ProbeTransport(_=>Capability(1,99))) {
            bool failed=false;try{await UpgradeDetection.ReadAsync(t,true,default);}catch(InvalidDataException){failed=true;}
            Check(failed && t.Writes.Count==1,"unknown capability cannot authorize legacy fallback");
        }
        int queries=0;
        await using(var t=new ProbeTransport(_=>++queries<3 ? null : Capability(3))) {
            var d=await UpgradeDetection.WaitForYmodemAsync(t,default);
            Check(d.Mode==3 && queries==3 && t.Writes.All(b=>b[1]==3),"power-on recovery probes only read capabilities");
        }
        await using(var t=new ProbeTransport(_=>Capability(2))) {
            string path=Path.Combine(Path.GetTempPath(),Guid.NewGuid()+".dat");
            try {
                File.WriteAllBytes(path,Package());var image=FirmwareImage.LoadForTarget(path,OtaTargetKind.Stm32SerialIap);
                var d=await UpgradeDetection.ReadAsync(t,false,default);bool failed=false;
                try{await new SerialYmodemClient(t).UpgradeAsync(image,d,default);}catch(IOException){failed=true;}
                Check(failed && t.Writes.All(b=>b[1]==3),"unsafe Boot never receives ENTER or data");
            }finally{File.Delete(path);}
        }
    }
    public static async Task Main(string[] args)
    {
        if (args.Length > 1) project = args[1].ToUpperInvariant();
        await DiscoveryEdges();
        var cap=new ushort[]{0x5550,1,(ushort)(Board>>16),(ushort)Board,2,2,(ushort)(App>>16),(ushort)App,0,Capacity,1,0};
        Check(UpgradeDetection.ParseCapabilities(cap)?.Protocol==SerialUpgradeProtocol.YmodemV2,"capability identification");
        cap[4]=99;bool rejected=false;try{_=UpgradeDetection.ParseCapabilities(cap);}catch(InvalidDataException){rejected=true;}
        Check(rejected,"unknown advertised protocol must not fall back to a legacy write");
        byte[] good=Package();_=SerialFirmwarePackage.Parse(good);
        foreach(int offset in new[]{0,8,12,16,20,24,28,240,good.Length-1}) {
            byte[] bad=good.ToArray();bad[offset]^=1;rejected=false;
            try{_=SerialFirmwarePackage.Parse(bad);}catch(InvalidDataException){rejected=true;}
            Check(rejected,$"corrupt package @{offset} rejected before device writes");
        }
        string file=Path.Combine(Path.GetTempPath(),Guid.NewGuid()+".dat");
        try {
            File.WriteAllBytes(file,good);FirmwareImage image=FirmwareImage.LoadForTarget(file,OtaTargetKind.Stm32SerialIap);
            if(args.Length>2&&args[2]=="ble") {
              foreach(var profile in new[]{(Mtu:23,Limit:20,Max:20),(Mtu:247,Limit:120,Max:120),(Mtu:23,Limit:120,Max:20)}) {
                await using var transport=new ReceiverProcess(args[0]){NegotiatedMtu=profile.Mtu,YmodemBlePayloadLimit=profile.Limit};
                var device=await UpgradeDetection.ReadAsync(transport,false,default);
                var sender=new SerialYmodemClient(transport,chunkForBle:true);
                Check(await sender.UpgradeAsync(image,device,default),"BLE stream -> production receiver");
                Check(transport.MaxWriteBytes==profile.Max&&transport.WriteCalls>10,"BLE fragments respect negotiated MTU and explicit module limit");
                Check(sender.Verified?.PayloadCrc==SerialFirmwarePackage.Parse(good).PayloadCrc,"BLE final metadata matches payload");
                if (project == "C077")
                    Check(sender.Verified?.RuntimeDataAvailable == false,
                        "unsupported C077 monitoring data does not invalidate the verified APP");
                Console.WriteLine($"PASS {project} production receiver MTU={profile.Mtu}, limit={profile.Limit}: {transport.WriteCalls} writes, max {transport.MaxWriteBytes} bytes");
              }
              foreach (var fault in new[] { FastBleFault.Corrupt, FastBleFault.LoseAck }) {
                await using var transport = new ReceiverProcess(args[0]) {
                    NegotiatedMtu = 247, YmodemBlePayloadLimit = 120, FirstFastBlockFault = fault
                };
                var device = await UpgradeDetection.WaitForYmodemAsync(transport, default, image.Package);
                var sender = new SerialYmodemClient(transport, chunkForBle: true);
                var log = new List<string>();
                sender.Log += log.Add;
                Check(device.Mode == 1 && await sender.UpgradeAsync(image, device, default),
                    "upgrade from open Boot recovers a failed first BLE data block");
                Check(transport.FastBlockFaultInjected && transport.CompatibleRetrySeen && transport.EnterRequests == 0,
                    "retry uses 20-byte chunks on the same Boot without sending ENTER");
                Check(log.Any(s => s.Contains("block=1") && s.Contains(fault == FastBleFault.Corrupt ? "reply=NAK" : "响应超时")),
                    "log identifies the first data block failure");
                Check(sender.Verified?.PayloadCrc == SerialFirmwarePackage.Parse(good).PayloadCrc,
                    "BLE compatibility retry verifies the exact running image");
                Console.WriteLine($"PASS {project} BLE automatic fallback from block 1 after {fault}");
              }
              return;
            }
            foreach(bool loseAck in new[]{false,true}) {
                await using var transport=new ReceiverProcess(args[0]) {Faults=loseAck};
                var device=await UpgradeDetection.ReadAsync(transport,false,default);
                Check(device.Mode==1,"empty APP still supports read-only Boot identification");
                var sender=new SerialYmodemClient(transport);double progress=0;sender.Progress+=p=>progress=p.Percent;sender.Log+=Console.WriteLine;
                Check(await sender.UpgradeAsync(image,device,default),"actual C receiver upgrade");
                Check(transport.Reconnects == 0, "a working link survives APP reset without a forced reconnect");
                Check(transport.FinalPackets >= 1, "the final commit was attempted before APP verification");
                Check(progress==100 && sender.Verified is not null,"one shared verified readback for UI and CLI");
                if(loseAck)Check(transport.Corrupted && transport.DroppedAck && transport.DroppedFinalAck,"error injection exercised");
                device=await UpgradeDetection.ReadAsync(transport,false,default);Check(device.Mode==0,"APP advertises same protocol");
                // A second upgrade exercises APP ENTER as well as recovery-only entry.
                int reconnects=transport.Reconnects;
                transport.TurnaroundMs=40;
                transport.DropEnterOnce=!loseAck;
                Check(await new SerialYmodemClient(transport).UpgradeAsync(image,device,default),"APP entry and repeated upgrade");
                Check(transport.TurnaroundDrops==0,"50 ms response handoff respected across all client lifetimes");
                if(!loseAck)Check(transport.DroppedEnterRequests==1 && transport.EnterRequests==2,"lost ENTER request permits only fresh-identity checked retry");
                Check(transport.Reconnects>reconnects,"APP entry rebuilds the same link before YMODEM");
                if(!loseAck) {
                    await transport.WindowAsync();
                    device=await UpgradeDetection.WaitForYmodemAsync(transport,default);
                    Check(device.Mode==3,"actual C Boot distinguishes power-on entry window");
                    Check(await new SerialYmodemClient(transport).UpgradeAsync(image,device,default),"power-on entry then reidentify safe Boot");
                }
            }
            await using(var transport=new ReceiverProcess(args[0])) {
                var device=await UpgradeDetection.ReadAsync(transport,false,default);
                using var cancel=new CancellationTokenSource();var sender=new SerialYmodemClient(transport);
                sender.Progress+=p=>{if(p.SentBytes>0)cancel.Cancel();};
                bool canceled=false;try{await sender.UpgradeAsync(image,device,cancel.Token);}catch(OperationCanceledException){canceled=true;}
                Check(canceled,"cancel during transfer");
                await transport.ResetAsync();
                device=await UpgradeDetection.ReadAsync(transport,false,default);Check(device.Mode==1,"partial image cannot boot after MCU reset");
                Check(await new SerialYmodemClient(transport).UpgradeAsync(image,device,default),"full restart after interrupted update");
            }

            using(var cancelAtEnd=new CancellationTokenSource())
            await using(var transport=new ReceiverProcess(args[0]) { CancelAtEnd=cancelAtEnd }) {
                var device=await UpgradeDetection.ReadAsync(transport,false,default);bool canceled=false;
                try {await new SerialYmodemClient(transport).UpgradeAsync(image,device,cancelAtEnd.Token);}
                catch(OperationCanceledException){canceled=true;}
                Check(canceled && transport.CancelSent,"cancel after EOT but before final block sends CAN CAN");
            }
            // A cable failure after the final block may mean the commit succeeded.
            await using(var transport=new ReceiverProcess(args[0]) { DisconnectAtFinal=true }) {
                var device=await UpgradeDetection.ReadAsync(transport,false,default);
                var sender=new SerialYmodemClient(transport);
                Check(await sender.UpgradeAsync(image,device,default),"final write IOException resolves through exact APP readback");
                Check(transport.FinalDisconnected && transport.Reconnects == 1 && sender.Verified is not null,
                    "post-commit disconnect reconnects the same link and verifies the exact image");
            }
            await using (var transport = new ReceiverProcess(args[0]) { DropAppProbes = 2, StallRuntime = true })
            {
                var device = await UpgradeDetection.ReadAsync(transport, false, default);
                var sender = new SerialYmodemClient(transport);
                var stages = new List<double>();
                sender.Progress += p => stages.Add(p.Percent);
                using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(22));
                Check(await sender.UpgradeAsync(image, device, deadline.Token), "slow runtime does not hold confirmed APP at 98%");
                Check(transport.Reconnects == 1 && transport.StalledRuntime && sender.Verified is { RuntimeDataAvailable: false },
                    "two unanswered probes trigger one recovery; optional runtime is time-limited");
                Check(stages.Contains(98) && stages.Contains(99) && stages[^1] == 100 &&
                      stages.SequenceEqual(stages.Order()), "verification progress is monotonic without waiting for telemetry");
            }
            await using(var transport=new ReceiverProcess(args[0]) { WrongBootOnReconnect=true }) {
                var device=(await UpgradeDetection.ReadAsync(transport,false,default)) with { Mode=0 };
                bool refused=false;try {await new SerialYmodemClient(transport).UpgradeAsync(image,device,default);}
                catch(Exception ex) when(ex is IOException or InvalidDataException){refused=true;}
                Check(refused && transport.DataBlocks==0,"reconnected identity mismatch stops before file transfer");
            }
            await using (var transport = new ReceiverProcess(args[0]) { WrongCrcAfterCommit = true })
            {
                var device = await UpgradeDetection.ReadAsync(transport, false, default);
                var sender = new SerialYmodemClient(transport);
                bool refused = false;
                try { await sender.UpgradeAsync(image, device, default); }
                catch (InvalidDataException ex) { refused = ex.Message.Contains("CRC"); }
                Check(refused && transport.CommitObserved && sender.Verified is null,
                    "a committed final block cannot turn a mismatching running APP CRC into success");
            }
            await using(var transport=new ReceiverProcess(args[0]) {WrongCapacityAfterCommit=true}) {
                var device=await UpgradeDetection.ReadAsync(transport,false,default);
                bool refused=false;
                try{await new SerialYmodemClient(transport).UpgradeAsync(image,device,default);}
                catch(InvalidDataException){refused=true;}
                Check(refused && transport.CommitObserved,"post-APP layout mismatch is not reported as upgrade success");
            }
        } finally {File.Delete(file);}
        Console.WriteLine($"PASS: {project} host -> production C YMODEM; bad CRC, lost ACK, final ACK ambiguity, cancel/reset/retry, APP entry, read-only detection and exact post-verification");
    }
}
enum FastBleFault { None, Corrupt, LoseAck }

sealed class ReceiverProcess : IBmsTransport, IBmsMtuTransport
{
    public int? NegotiatedMtu {get;init;}=23;
    public int YmodemBlePayloadLimit {get;init;}=20;
    readonly Process _process;readonly SemaphoreSlim _gate=new(1);readonly CancellationTokenSource _stop=new();readonly Task _poll;
    public bool Faults,Corrupted,DroppedAck,DroppedFinalAck,DisconnectAtFinal,FinalDisconnected,WrongBootOnReconnect;
    public int DataBlocks,MaxWriteBytes,WriteCalls,FinalPackets,DropAppProbes;
    public bool StallRuntime,StalledRuntime;
    private bool disconnected;
    public FastBleFault FirstFastBlockFault { get; init; }
    public bool FastBlockFaultInjected { get; private set; }
    public bool CompatibleRetrySeen { get; private set; }
    private int _firstFastBlockRemaining;
    public bool DropEnterOnce,WrongCapacityAfterCommit,WrongCrcAfterCommit,CommitObserved;
    public int EnterRequests,DroppedEnterRequests,TurnaroundMs,TurnaroundDrops;
    private long lastReplyTick;
    public CancellationTokenSource? CancelAtEnd;
    public bool CancelSent;
    int eots;
    public int Reconnects;
    public ReceiverProcess(string path) {
        var start=new ProcessStartInfo(path){UseShellExecute=false,CreateNoWindow=true,RedirectStandardInput=true,RedirectStandardOutput=true,RedirectStandardError=true};
        start.ArgumentList.Add("--server");_process=Process.Start(start)!;_poll=PollAsync();
    }
    public bool IsConnected=>!_process.HasExited && !disconnected;
    public string DiscoveryDescription=>"production C YMODEM receiver with simulated Flash/AFE";
    public event Action<ReadOnlyMemory<byte>>? DataReceived;
    public event Action<string>? ConnectionProgress {add{}remove{}}
    public Task ReconnectAsync(CancellationToken ct=default){Reconnects++;disconnected=false;return Task.CompletedTask;}
    public Task WindowAsync()=>CommandAsync("W",false,default);
    public Task ResetAsync()=>CommandAsync("R",false,default);
    async Task PollAsync() {
        try {while(true){await Task.Delay(100,_stop.Token);await CommandAsync("T 250",false,_stop.Token);}}
        catch(OperationCanceledException) { }
    }
    public async Task WriteAsync(ReadOnlyMemory<byte> data,CancellationToken ct=default) {
        byte[] bytes=data.ToArray();bool drop=false;
        WriteCalls++;MaxWriteBytes=Math.Max(MaxWriteBytes,bytes.Length);
        bool modbus=bytes.Length==8 && bytes[0]==1 && bytes[1] is 3 or 6;
        if(modbus && lastReplyTick!=0 && Stopwatch.GetElapsedTime(lastReplyTick).TotalMilliseconds<TurnaroundMs) { TurnaroundDrops++;return; }
        if (modbus && bytes[1] == 3 && CommitObserved)
        {
            ushort address = BinaryPrimitives.ReadUInt16BigEndian(bytes.AsSpan(2));
            if (address == 0xD170 && DropAppProbes > 0)
            {
                DropAppProbes--;
                return;
            }
            if (address == 0xC002 && StallRuntime)
            {
                StalledRuntime = true;
                await Task.Delay(Timeout.InfiniteTimeSpan, ct);
            }
        }
        if(modbus && bytes[1]==6) { EnterRequests++;if(DropEnterOnce) { DropEnterOnce=false;DroppedEnterRequests++;return; } }
        if(bytes.Length==1029)DataBlocks++;
        bool firstDataFragment = bytes.Length >= 3 && bytes[0] == 2 && bytes[1] == 1 && bytes[2] == 0xFE;
        if (FirstFastBlockFault != FastBleFault.None && !FastBlockFaultInjected && firstDataFragment && bytes.Length > 20)
        {
            FastBlockFaultInjected = true;
            _firstFastBlockRemaining = 1029;
            if (FirstFastBlockFault == FastBleFault.Corrupt) bytes[20] ^= 1;
        }
        if (_firstFastBlockRemaining > 0)
        {
            _firstFastBlockRemaining -= bytes.Length;
            drop = _firstFastBlockRemaining == 0 && FirstFastBlockFault == FastBleFault.LoseAck;
        }
        else if (FastBlockFaultInjected && firstDataFragment && bytes.Length == 20)
            CompatibleRetrySeen = true;
        if (CompatibleRetrySeen && bytes.Length > 20) throw new IOException("BLE upgrade resumed large writes after fallback");
        if(bytes.SequenceEqual(new byte[]{0x18,0x18}))CancelSent=true;
        if(Faults && bytes.Length==1029 && bytes[1]==1) {
            if(!Corrupted){bytes[20]^=1;Corrupted=true;}
            else if(!DroppedAck){drop=true;DroppedAck=true;}
        }
        if(Faults && bytes.Length==133 && bytes[1]==0 && bytes[3]==0 && !DroppedFinalAck){drop=true;DroppedFinalAck=true;}
        if(bytes.Length==133 && bytes[1]==0 && bytes[3]==0) { CommitObserved=true; FinalPackets++; }
        bool disconnect=DisconnectAtFinal && bytes.Length==133 && bytes[1]==0 && bytes[3]==0 && !FinalDisconnected;
        await CommandAsync(Convert.ToHexString(bytes),drop || disconnect,ct);
        if(bytes.Length==1 && bytes[0]==4 && ++eots==2)CancelAtEnd?.Cancel();
        if(disconnect){FinalDisconnected=true;disconnected=true;throw new IOException("cable disconnected after final block");}
    }
    async Task CommandAsync(string command,bool drop,CancellationToken ct) {
        await _gate.WaitAsync(ct);
        try {
            await _process.StandardInput.WriteLineAsync(command.AsMemory(),ct);await _process.StandardInput.FlushAsync(ct);
            string? line=await _process.StandardOutput.ReadLineAsync(ct).AsTask().WaitAsync(TimeSpan.FromSeconds(5),ct);
            if(line is null)throw new IOException("C receiver terminated: "+await _process.StandardError.ReadToEndAsync(ct));
            byte[] response=Convert.FromHexString(line);
            if(WrongBootOnReconnect && Reconnects>0 && response.Length==29 && response[1]==3) {
                response[8]^=1;
                BinaryPrimitives.WriteUInt16LittleEndian(response.AsSpan(27),ModbusRtu.Crc16(response.AsSpan(0,27)));
            }
            if(WrongCapacityAfterCommit && CommitObserved && response.Length==29 && response[1]==3) {
                ushort capacity=BinaryPrimitives.ReadUInt16BigEndian(response.AsSpan(21,2));
                BinaryPrimitives.WriteUInt16BigEndian(response.AsSpan(21,2),(ushort)(capacity-1024));
                BinaryPrimitives.WriteUInt16LittleEndian(response.AsSpan(27),ModbusRtu.Crc16(response.AsSpan(0,27)));
            }
            if (WrongCrcAfterCommit && CommitObserved && command.StartsWith("0103D17C") && response.Length == 21)
            {
                response[12] ^= 1;
                BinaryPrimitives.WriteUInt16LittleEndian(response.AsSpan(19), ModbusRtu.Crc16(response.AsSpan(0, 19)));
            }
            if(command.Length==16 && response.Length>=5 && response[0]==1 && response[1] is 3 or 6)lastReplyTick=Stopwatch.GetTimestamp();
            if(!drop)for(int at=0;at<response.Length;at+=7)DataReceived?.Invoke(response.AsMemory(at,Math.Min(7,response.Length-at)));
        } finally {_gate.Release();}
    }
    public async ValueTask DisposeAsync() {
        _stop.Cancel();await _poll;_process.StandardInput.Close();
        await _process.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(5));_process.Dispose();_stop.Dispose();_gate.Dispose();
    }
}

sealed class ProbeTransport(Func<byte[],byte[]?> respond):IBmsTransport
{
    public bool Noise;
    public List<byte[]> Writes=new();
    public bool IsConnected=>true;
    public string DiscoveryDescription=>"fragmented capability probe fixture";
    public event Action<ReadOnlyMemory<byte>>? DataReceived;
    public event Action<string>? ConnectionProgress {add{}remove{}}
    public Task ReconnectAsync(CancellationToken ct=default)=>Task.CompletedTask;
    public ValueTask DisposeAsync()=>ValueTask.CompletedTask;
    public Task WriteAsync(ReadOnlyMemory<byte> data,CancellationToken ct=default)
    {
        byte[] request=data.ToArray();Writes.Add(request);byte[]? b=respond(request);
        if(b is not null) {
            if(Noise)DataReceived?.Invoke(new byte[]{0x43,0x43});
            foreach(byte value in b)DataReceived?.Invoke(new byte[]{value});
        }
        return Task.CompletedTask;
    }
}
