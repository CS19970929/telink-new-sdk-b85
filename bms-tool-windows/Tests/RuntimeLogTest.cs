using BmsTool.Windows;
using System.Buffers.Binary;
using System.IO;

static class RuntimeLogTest
{
    static void Check(bool ok, string why) { if (!ok) throw new Exception(why); }
    public static async Task Main()
    {
        var t = new LogTransport(); await using var client = new BmsClient(t);
        var b = await client.ReadRuntimeLogsAsync(null);
        Check(b.Supported && b.Enabled && b.Entries.Count == 64 && b.Entries[0].Sequence == 6, "initial ring");
        Check(b.Entries[0].Arg1 == unchecked((uint)-1500) && b.Entries[0].Detail.Contains("-1500"), "signed current");
        var cursor = b.Cursor;
        b = await client.ReadRuntimeLogsAsync(cursor); Check(b.Entries.Count == 0 && b.Missed == 0, "duplicates");
        t.Next = 140; b = await client.ReadRuntimeLogsAsync(cursor);
        Check(b.Entries.Count == 64 && b.Missed == 6, "lost records");
        t.Next = 4; t.Boot++; b = await client.ReadRuntimeLogsAsync(b.Cursor);
        Check(b.Reset && b.Entries.Count == 4 && b.Missed == 0, "reset");
        t.Next = 2; t.CountOverride = 64;
        b = await client.ReadRuntimeLogsAsync(new(uint.MaxValue - 1, t.Boot, t.Build));
        Check(!b.Reset && b.Entries.Select(x => x.Sequence).SequenceEqual(new[] { uint.MaxValue-1, uint.MaxValue, 0u, 1u }), "sequence wrap");
        t.CountOverride = null; t.Next = 70; t.CorruptSlot = true;
        b = await client.ReadRuntimeLogsAsync(null); Check(b.Missed > 0 && b.Entries.Count < 64, "overwrite during capture");
        t.CorruptSlot = false; t.Enabled = false;
        b = await client.ReadRuntimeLogsAsync(null); Check(b.Supported && !b.Enabled && b.Entries.Count == 0, "production disabled");
        t.Enabled = true; t.Legacy = true;
        b = await client.ReadRuntimeLogsAsync(null); Check(!b.Supported, "legacy zero window");
        t.Legacy = false; t.IllegalAddress = true;
        b = await client.ReadRuntimeLogsAsync(null); Check(!b.Supported, "legacy exception");
        t.IllegalAddress = false; t.BadHeader = true;
        try { await client.ReadRuntimeLogsAsync(null); throw new Exception("invalid header accepted"); }
        catch (InvalidDataException) { }
        t.BadHeader = false; t.ResetDuringRead = true; t.HeaderReads = 0;
        try { await client.ReadRuntimeLogsAsync(null); throw new Exception("mixed reset snapshot accepted"); }
        catch (IOException) { }
        t.ResetDuringRead = false;
        using var cts = new CancellationTokenSource(); cts.Cancel();
        try { await client.ReadRuntimeLogsAsync(null, cts.Token); throw new Exception("cancellation ignored"); }
        catch (OperationCanceledException) { }
        Check(t.MaxWords <= 40 && t.AllReads, "bounded read-only transactions");
        Console.WriteLine("PASS runtime logs: real BmsClient/CRC/fragments, signed decode, cursor, loss, overwrite, wrap, reset, legacy, disabled, cancellation");
    }
}

sealed class LogTransport : IBmsTransport
{
    public bool IsConnected => true;
    public string DiscoveryDescription => "runtime-log-test";
    public event Action<ReadOnlyMemory<byte>>? DataReceived;
    public event Action<string>? ConnectionProgress { add { } remove { } }
    public Task ReconnectAsync(CancellationToken ct = default) => Task.CompletedTask;
    public ValueTask DisposeAsync() => ValueTask.CompletedTask;
    public uint Next = 70, Boot = 7, Build = 0x12345678;
    public ushort? CountOverride;
    public bool Enabled = true, Legacy, IllegalAddress, CorruptSlot, BadHeader, ResetDuringRead;
    public bool AllReads = true;
    public int HeaderReads, MaxWords;
    public Task WriteAsync(ReadOnlyMemory<byte> data, CancellationToken ct = default)
    {
        ct.ThrowIfCancellationRequested();
        byte[] q = data.ToArray(); ModbusRtu.ValidateFrame(q);
        AllReads &= q[1] == 3;
        ushort address = BinaryPrimitives.ReadUInt16BigEndian(q.AsSpan(2,2));
        ushort count = BinaryPrimitives.ReadUInt16BigEndian(q.AsSpan(4,2));
        MaxWords = Math.Max(MaxWords, count);
        if (IllegalAddress) { Emit(ModbusRtu.Frame(new byte[] { 1, 0x83, 2 })); return Task.CompletedTask; }
        var words = new ushort[656];
        static void Put(ushort[] w, int i, uint value) { w[i] = (ushort)(value >> 16); w[i+1] = (ushort)value; }
        if (address == 0x3000 && ++HeaderReads == 2 && ResetDuringRead) Boot++;
        if (!Legacy) {
            words[0]=0x4C47; words[1]=1; words[2]=(ushort)(Enabled?1:0); words[3]=(ushort)(BadHeader?65:64);
            words[4]=10; words[5]=CountOverride ?? (ushort)Math.Min(Next,64u); words[14]=4; words[15]=0x1ff;
            Put(words,6,Next); Put(words,8,Next>64?Next-64:0); Put(words,10,Boot); Put(words,12,Build);
            for(uint i=0;i<words[5];i++) {
                uint seq=unchecked(Next-(uint)words[5]+i); int at=16+(int)(seq%64)*10;
                Put(words,at,CorruptSlot&&i==0?seq+64:seq); Put(words,at+2,unchecked(seq*32000));
                words[at+4]=0x0401; words[at+5]=32; Put(words,at+6,1); Put(words,at+8,unchecked((uint)-1500));
            }
        }
        var body=new byte[3+2*count];body[0]=1;body[1]=3;body[2]=(byte)(count*2);
        for(int i=0;i<count;i++) BinaryPrimitives.WriteUInt16BigEndian(body.AsSpan(3+i*2,2),words[address-0x3000+i]);
        Emit(ModbusRtu.Frame(body)); return Task.CompletedTask;
    }
    private void Emit(byte[] frame)
    {
        // Arbitrary boundaries cover BLE fragments and serial partial receives.
        for(int i=0;i<frame.Length;i+=7) DataReceived?.Invoke(frame.AsMemory(i,Math.Min(7,frame.Length-i)));
    }
}
