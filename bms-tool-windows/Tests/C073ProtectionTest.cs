using System.Buffers.Binary;
using BmsTool.Windows;

// 把真实 BmsClient 和 Comm Tool CAN 封装接到可故障注入的寄存器应答端。
await using var peer = new CanPeer();
await using var client = new BmsClient(peer);
await client.ReadIdentityAsync();
if (!client.IsC073) throw new Exception("C073 identity");
var ct=CancellationToken.None;
var single = (await Sh309Protection.ReadAsync(client, ct)).Chips.Single();
if (single.Index != 0 || single.Saved[1] != 4250 || single.Actual?[1] != 4250 ||
    single.Readback != Sh309Readback.Matches || Sh309Protection.Format("C073", C077Hardware.Fields[12], 30) != "90")
    throw new Exception("common single-chip read or C073 current scale");
peer.RejectVerify = true;
var failed = (await Sh309Protection.ReadAsync(client, ct)).Chips.Single();
if (failed.Saved[1] != 4250 || failed.Actual is not null || failed.Readback != Sh309Readback.Unknown ||
    peer.Groups != 0 || peer.Programs != 0)
    throw new Exception("unconfirmed EEPROM must not hide Flash or display the cache as actual");
peer.RejectVerify = false;
if (Sh309Protection.Encode("C073", C077Hardware.Fields[12], "90") != 30 ||
    Sh309Protection.Encode("C077", C077Hardware.Fields[25], "-5.0") != unchecked((ushort)-50))
    throw new Exception("shared draft must preserve current conversion and signed temperature");
try { Sh309Protection.Encode("C073", C077Hardware.Fields[12], "89"); throw new Exception("rounded current accepted"); }
catch (ArgumentException) { }
var before=await C073Protection.ReadAsync(client,ct);
if (!before.Matches || before.Tr!=64 || before.Actual[1]!=4250 || before.Actual[12]!=30 || before.Actual[14]!=40)
    throw new Exception("actual profile decode");
var candidate=(ushort[])before.Saved.Clone();
candidate[1]=4260;
await C073Protection.ApplyAsync(client,before.Saved,candidate,ct);
if (peer.Groups!=1 || peer.Programs!=1 || peer.Profile[1]!=4260 ||
    (await C073Protection.ReadAsync(client,ct)).Actual[1]!=4260) throw new Exception("apply/readback");
await C073Protection.ApplyAsync(client,candidate,candidate,ct);
if (peer.Groups!=1 || peer.Programs!=1) throw new Exception("duplicate EEPROM write");
try { await C073Protection.ApplyAsync(client,before.Saved,before.Saved,ct); throw new Exception("stale write"); }
catch (IOException) { }
peer.LoseAck=true;
try { await C073Protection.ApplyAsync(client,candidate,before.Saved,ct); throw new Exception("lost ACK accepted"); }
catch (TimeoutException) { }
if (peer.Groups!=2 || peer.Programs!=1) throw new Exception("retried lost write");
ushort[] soc=[245,1,4180,3100];
await client.WriteRegistersAsync(0x2318,soc,ct);
if (!(await client.ReadRegistersAsync(0x2318,4,ct)).SequenceEqual(soc)) throw new Exception("SOC CAN round trip");
peer.Ble=true;
await C073Protection.ApplyAsync(client,before.Saved,candidate,ct);
if (!peer.Fragments.SequenceEqual(new[]{20,20,20,13}) || peer.Groups!=3)
    throw new Exception("MTU 23 must assemble one 73-byte FC10 request");
peer.Compact=true;
await client.ReadIdentityAsync();
if (!client.IsC073 || !client.DeviceDetectionEvidence.Contains("44 KiB")) throw new Exception("44 KiB identity");
if (!(await C073Protection.ReadAsync(client,ct)).Matches) throw new Exception("format-8 profile readback");
if (peer.LegacyProtectionReads != 0) throw new Exception("C073 protection view read the legacy 0x2100 page");
peer.ConfigFormat=9;
try { await C073Protection.ReadAsync(client,ct); throw new Exception("unknown profile accepted"); }
catch (InvalidDataException) { }
peer.ConfigFormat=8;
peer.SingleChip=false;
try { await C073Protection.ReadAsync(client,ct); throw new Exception("wrong hardware accepted"); }
catch (InvalidDataException) { }
Console.WriteLine("PASS C073 CAN: identity, 32-word Flash write, EEPROM readback, stale edits, no duplicate/ACK replay, four SOC words, BLE MTU 23 reassembly, wrong chip count.");

sealed class CanPeer : IBmsTransport,IBmsMtuTransport
{
    public ushort[] Profile=[17,4250,1000,4010,2760,1000,2900,4160,2000,1000,5000,8,30,1000,40,200,50,64,20,1000,200,64,32,700,650,unchecked((ushort)-50),0,700,650,unchecked((ushort)-50),0,2000];
    public bool LoseAck, Ble, SingleChip=true, Compact, RejectVerify;
    public ushort ConfigFormat;
    public int LegacyProtectionReads;
    public int? NegotiatedMtu=>Ble ? 23 : 247;
    public List<int> Fragments {get;}=new();
    private readonly List<byte> partial=new();
    public int Groups,Programs;
    private ushort[] soc=[245,1,4200,2700];
    private int actualOv=4250;
    public bool IsConnected=>true;
    public string DiscoveryDescription=>"CAN register fixture";
    public event Action<ReadOnlyMemory<byte>>? DataReceived;
    public event Action<string>? ConnectionProgress { add {} remove {} }
    public Task ReconnectAsync(CancellationToken ct=default)=>Task.CompletedTask;
    public ValueTask DisposeAsync()=>ValueTask.CompletedTask;
    public Task WriteAsync(ReadOnlyMemory<byte> data,CancellationToken ct=default)
    {
        if (Ble && (partial.Count!=0 || data.Span[1]==16)) {
            if(data.Length>20)throw new IOException("ATT payload limit");
            Fragments.Add(data.Length);partial.AddRange(data.ToArray());
            if(partial.Count<73)return Task.CompletedTask;
            data=partial.ToArray(); partial.Clear();
        }
        DataReceived?.Invoke(CommToolCanTransport.Exchange(data.ToArray(),Command,ct));
        return Task.CompletedTask;
    }
    private byte[] Command(byte opcode, byte[] payload,CancellationToken ct)
    {
        int address=BinaryPrimitives.ReadUInt16LittleEndian(payload), n=BinaryPrimitives.ReadUInt16LittleEndian(payload.AsSpan(2));
        if (opcode==0x16) {
            ushort[] values=new ushort[n];
            for(int i=0;i<n;++i) values[i]=BinaryPrimitives.ReadUInt16LittleEndian(payload.AsSpan(4+2*i));
            if(address==0x2B10 && n==32) { Profile=values; ++Groups; }
            else if(address==0x2318 && n==4) soc=values;
            else throw new Exception("unexpected group");
            if(LoseAck){LoseAck=false;throw new CommToolCommandException(6,"lost ACK");}
            return [];
        }
        if(opcode==0x11 && address==0x2A7F) {
            int command=BinaryPrimitives.ReadUInt16LittleEndian(payload.AsSpan(4));
            if(command==0xC073){actualOv=Profile[1];++Programs;}
            else if(command!=0xC087)throw new Exception("service command");
            else if(RejectVerify)throw new CommToolCommandException(4,"verification unavailable");
            return [];
        }
        if(opcode!=0x10)throw new Exception("opcode");
        if (address==0x2100) ++LegacyProtectionReads;
        ushort[] words=address switch {
            0xD170 => [0x5550,1,0xC073,13,2,2,0x0800,0x3000,0,(ushort)(Compact?0xB000:0xB800),0,0],
            0x2B00 => [0xC073,1,32,(ushort)(SingleChip?1:2),0,0,ConfigFormat != 0 ? ConfigFormat : (ushort)(Compact?8:7)],
            0x2A28 => [1], 0x2B10 => Profile, 0x2318=>soc, _=>new ushort[n]
        };
        if(address==0x2A40){words[0]=0xC073;words[1]=3;words[3]=(ushort)(actualOv==Profile[1]?0:4);words[8]=(ushort)(actualOv==Profile[1]?1:0);}
        if(address is 0x2A60 or 0x2B80) {
            words[1]=(ushort)((6<<12)|((address==0x2A60?actualOv:Profile[1])/5));
            words[6]=0x1616;
            words[12]=(ushort)(address==0x2A60?64:0);
        }
        byte[] result=new byte[2*n];
        for(int i=0;i<n;++i)BinaryPrimitives.WriteUInt16LittleEndian(result.AsSpan(2*i),words[i]);
        return result;
    }
}
