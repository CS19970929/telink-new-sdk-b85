using System.Buffers.Binary;
using System.Text.Json;
using BmsTool.Windows;

static class MosMonitorTest
{
    static void Check(bool condition, string message) { if (!condition) throw new Exception(message); }
    public static async Task RunAsync()
    {
        var t = new MosTransport();
        await using var client = new BmsClient(t);
        var first = await client.ReadMosHistoryAsync();
        Check(first.Enabled && first.Entries.Count == 3 && first.Entries[1].Detail.Contains("开启→关闭") &&
            first.Entries[2].Detail.Contains("关闭→开启"), "off/on between GUI polls survives firmware ring");
        Check(first.Entries[1].Reasons.Contains("原因未确认") && first.Entries[1].Reasons.Contains("物理 Gate 未测"), "command ON with driver OFF is unexplained, not physical proof");
        var words = first.Entries[1].Words.ToArray(); words[9] = 0;
        Check(new MosHistoryEntry(words).State.Contains("未知"), "invalid driver is UNKNOWN, never OFF");
        words[9] = 1; words[19] = 0;
        Check(new MosHistoryEntry(words).State.Contains("未知"), "invalid sample cannot confirm driver state");
        words[19] = 1; words[10] = 32;
        Check(new MosHistoryEntry(words).Reasons.Contains("AFE 硬件保护"), "explicit reason survives decoder");
        words[10] = 0; words[14] = 16; words[27] = 0x3510; words[31] = 0x22;
        Check(new MosHistoryEntry(words).Reasons.Contains("短路") && new MosHistoryEntry(words).Reasons.Contains("高侧报告"),
            "SH raw flags and high-side report retained without claiming physical Gate proof");
        var unchanged = await client.ReadMosHistoryAsync(3, first.BootTick, first.BuildId);
        Check(unchanged.Entries.Count == 0 && unchanged.Missing == 0, "unchanged firmware adds no duplicate events");
        t.Latest = 20; t.Count = 8;
        var overwritten = await client.ReadMosHistoryAsync(3, first.BootTick, first.BuildId);
        Check(overwritten.Missing == 9 && overwritten.Entries.Count == 8, "ring overwrite reports exact client gap");
        t.BadSlot = true;
        var torn = await client.ReadMosHistoryAsync(19, first.BootTick, first.BuildId);
        Check(torn.Missing == 1 && torn.Entries.Count == 0, "slot sequence mismatch discarded, never misattributed");
        t.BadSlot = false; t.Latest = 1; t.Count = 1; t.Boot = 456;
        var reset = await client.ReadMosHistoryAsync(20, first.BootTick, first.BuildId);
        Check(reset.Reset && reset.Entries.Count == 1, "reset establishes new segment");
        t.Latest = 1; t.Count = 3;
        var wrap = await client.ReadMosHistoryAsync(uint.MaxValue, t.Boot, first.BuildId);
        Check(!wrap.Reset && wrap.Entries.Select(e => e.Sequence).SequenceEqual(new uint[] { 0, 1 }), "sequence wraps through zero with correct physical slots");
        t.Count = 1; t.ChangeBootOnTail = true;
        try { await client.ReadMosHistoryAsync(); throw new Exception("mid-read reset accepted"); }
        catch (System.IO.InvalidDataException) { }
        var old = new MosTransport { Old = true };
        await using var oldClient = new BmsClient(old);
        var legacy = await oldClient.ReadMosHistoryAsync();
        Check(!legacy.Enabled && legacy.LegacyMosWords is { Length: 32 }, "old firmware falls back to current MOS diagnostic");
        await oldClient.ReadMosHistoryAsync();
        Check(old.HistoryRequests == 1 && old.ProbeRequests == 1, "unsupported windows probed once per connection");
        var production = new MosTransport { Enabled = false };
        await using var productionClient = new BmsClient(production);
        var disabled = await productionClient.ReadMosHistoryAsync();
        Check(!disabled.Enabled && disabled.Header is not null && disabled.LegacyMosWords is { Length: 32 },
            "history-disabled firmware still supplies current MOS reasons and validity");
        string parent = Path.Combine(Path.GetTempPath(), "BmsMosMonitorTest", Guid.NewGuid().ToString("N"));
        using (var journal = new BmsMosJournal(parent)) {
            journal.Write("sample", new { indicationChanged = true, snapshot = new BatterySnapshot { SystemStatus = 4 } });
            journal.Write("gap", new { reason = "timeout" });
            journal.Write("firmware", new { Words = first.Entries[1].Words });
            using var liveStream = new FileStream(Path.Combine(journal.DirectoryPath, "mos_0001.jsonl"), FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
            using var liveReader = new StreamReader(liveStream);
            var lines = liveReader.ReadToEnd().Split('\n', StringSplitOptions.RemoveEmptyEntries);
            Check(lines.Length == 3, "events flushed while journal remains open");
            using var document = JsonDocument.Parse(lines[1]);
            Check(document.RootElement.GetProperty("kind").GetString() == "gap" && document.RootElement.TryGetProperty("utc", out _), "durable JSONL preserves gap and timestamp");
            Console.WriteLine("MOS journal evidence: " + journal.DirectoryPath);
        }
        Console.WriteLine("PASS MOS monitor: short off/on, invalid samples, unknown cause, overwrite, torn slot, reset, sequence wrap, old firmware, durable journal");
    }

    sealed class MosTransport : IBmsTransport
    {
        public uint Latest = 3, Boot = 123;
        public ushort Count = 3;
        public bool Old, BadSlot, ChangeBootOnTail;
        public bool Enabled = true;
        public int HistoryRequests, ProbeRequests;
        private bool _sawSlot;
        public bool IsConnected => true;
        public bool RequiresSerialWakeup => false;
        public string DiscoveryDescription => "MOS test";
        public event Action<ReadOnlyMemory<byte>>? DataReceived;
        public event Action<string>? ConnectionProgress { add { } remove { } }
        public Task ReconnectAsync(CancellationToken ct = default) => Task.CompletedTask;
        public ValueTask DisposeAsync() => ValueTask.CompletedTask;
        private ushort[] Header()
        {
            var w = new ushort[16]; w[0] = 0x4D48; w[1] = 1; w[2] = Enabled ? (ushort)1 : (ushort)0; w[3] = 8; w[4] = 32; w[5] = Count;
            Put(w, 6, Latest); Put(w, 10, Boot); Put(w, 12, 0x12345678); w[14] = 0x3510; return w;
        }
        private static void Put(ushort[] w, int at, uint value) { w[at] = (ushort)value; w[at+1] = (ushort)(value >> 16); }
        private static ushort[] Entry(uint sequence)
        {
            var w = new ushort[32]; Put(w, 0, sequence); Put(w, 2, unchecked(0xFFFFF000u + sequence * 6554));
            w[4] = w[5] = w[6] = 3; w[7] = w[9] = w[19] = 1;
            w[8] = sequence == 2 ? (ushort)2 : (ushort)3;
            w[28] = sequence == 3 ? (ushort)2 : (ushort)3; w[29] = 1; w[30] = sequence != 1 ? (ushort)1 : (ushort)0;
            return w;
        }
        public Task WriteAsync(ReadOnlyMemory<byte> data, CancellationToken ct = default)
        {
            ct.ThrowIfCancellationRequested();
            ushort address = BinaryPrimitives.ReadUInt16BigEndian(data.Span[2..4]);
            ushort count = BinaryPrimitives.ReadUInt16BigEndian(data.Span[4..6]);
            if (address == 0x3400) {
                ++HistoryRequests;
                if (Old) { DataReceived?.Invoke(ModbusRtu.Frame(new byte[] { 1, 0x83, 2 })); return Task.CompletedTask; }
                if (ChangeBootOnTail && _sawSlot && count == 16) ++Boot;
            }
            ushort[] words;
            if (address == 0x3400) words = count == 48 ? Header().Concat(Entry(Latest)).ToArray() : Header();
            else if (address >= 0x3430 && address < 0x3530) {
                _sawSlot = true;
                uint slot = (uint)(address - 0x3430) / 32;
                uint seq = Latest;
                while (unchecked(seq-1) % 8 != slot) --seq;
                words = Entry(BadSlot ? unchecked(seq+1) : seq);
            }
            else if (address == 0x2A00) { ++ProbeRequests; words = new ushort[] { 0x4447, 1, 8 }; }
            else words = new ushort[count];
            byte[] body = new byte[3 + count * 2]; body[0] = 1; body[1] = 3; body[2] = (byte)(count * 2);
            for (int i = 0; i < count; ++i) BinaryPrimitives.WriteUInt16BigEndian(body.AsSpan(3+i*2, 2), words[i]);
            byte[] response = ModbusRtu.Frame(body);
            // 实际客户端接收拼帧路径。
            DataReceived?.Invoke(response.AsMemory(0, 4)); DataReceived?.Invoke(response.AsMemory(4));
            return Task.CompletedTask;
        }
    }
}
