using System.Buffers.Binary;
using BmsTool.Windows;

static class OpenWireTest
{
    static void Check(bool ok, string reason) { if (!ok) throw new Exception(reason); }
    public static async Task RunAsync()
    {
        var w = new ushort[48]; w[0] = 0x4F57; w[1] = 1; w[2] = 2; w[3] = 1; w[4] = 1;
        w[8] = 0xFFF0; w[9] = 0xFFFF; w[44] = 32; w[31] = 20;
        var active = BmsOpenWireStatus.Decode(w);
        Check(active.Fresh && active.Active && !active.Fault && active.StateText.Contains("不主动关"), "active detection is separate from fault, tick wrap supported");
        w[3] = 4; w[4] = 4; w[5] = 4; w[22] = 0x8001; w[23] = 8; w[30] = 3;
        var failed = BmsOpenWireStatus.Decode(w);
        Check(failed.FaultCells == "C1, C16, C20" && failed.Summary.Contains("映射不确定"), "indeterminate result retains previous fault mask, high bits decoded");
        Check(active.Active && !active.Fault, "decoded snapshot does not change after source buffer mutation");
        w[44] = 0; w[45] = 3; Check(!BmsOpenWireStatus.Decode(w).Fresh, "stale snapshot is unknown");
        w[31] = 25;
        try { BmsOpenWireStatus.Decode(w); throw new Exception("invalid channel count accepted"); }
        catch (System.IO.InvalidDataException) { }
        var transport = new Peer(); await using var client = new BmsClient(transport);
        Check((await client.ReadOpenWireStatusAsync()) is { Backend: "SH3673510" }, "client reads independent window through shared protocol");
        await client.ReadOpenWireStatusAsync(); Check(transport.Probes == 1 && transport.Reads == 2, "capability cached per connection");
        var old = new Peer { Supported = false }; await using var oldClient = new BmsClient(old);
        Check(await oldClient.ReadOpenWireStatusAsync() is null, "old firmware reports unsupported instead of healthy");
        await oldClient.ReadOpenWireStatusAsync(); Check(old.Probes == 1 && old.Reads == 0, "unsupported window is never polled");
        Check(transport.Writes == 0 && old.Writes == 0, "monitor never writes configuration or outputs");
        Console.WriteLine("PASS OpenWire: active/fault separation, raw decode, clone, tick wrap, stale, bounds, shared read-only transport, old firmware");
    }
    sealed class Peer : IBmsTransport
    {
        public bool Supported = true;
        public int Probes, Reads, Writes;
        public bool IsConnected => true;
        public bool RequiresSerialWakeup => false;
        public string DiscoveryDescription => "openwire test";
        public event Action<ReadOnlyMemory<byte>>? DataReceived;
        public event Action<string>? ConnectionProgress { add { } remove { } }
        public Task ReconnectAsync(CancellationToken ct = default) => Task.CompletedTask;
        public ValueTask DisposeAsync() => ValueTask.CompletedTask;
        public Task WriteAsync(ReadOnlyMemory<byte> data, CancellationToken ct = default)
        {
            ct.ThrowIfCancellationRequested();
            if (data.Span[1] != 3) { ++Writes; throw new Exception("unexpected write"); }
            ushort address = BinaryPrimitives.ReadUInt16BigEndian(data.Span[2..4]);
            ushort count = BinaryPrimitives.ReadUInt16BigEndian(data.Span[4..6]);
            var words = new ushort[count];
            if (address == 0x2A00 && count == 3) {
                ++Probes; words[0] = 0x4447; words[1] = 1; words[2] = Supported ? (ushort)0x100 : (ushort)0;
            } else if (address == 0x3600 && count == 48) {
                ++Reads; words[0] = 0x4F57; words[1] = 1; words[2] = 2; words[31] = 20;
            } else throw new Exception("unexpected register request");
            var response = new byte[3 + count * 2]; response[0] = 1; response[1] = 3; response[2] = (byte)(count * 2);
            for (int i = 0; i < count; i++) BinaryPrimitives.WriteUInt16BigEndian(response.AsSpan(3 + 2 * i), words[i]);
            DataReceived?.Invoke(ModbusRtu.Frame(response)); return Task.CompletedTask;
        }
    }
}
