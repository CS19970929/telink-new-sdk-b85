using System.Buffers.Binary;
using BmsTool.Windows;

static class CommunicationCountersTest
{
    static void Check(bool ok, string why) { if (!ok) throw new Exception(why); }
    public static async Task RunAsync()
    {
        var stats = new BmsCommunicationStatistics();
        var transport = new CounterTransport();
        await using (var client = new BmsClient(transport, stats)) {
            var words = await client.ReadRegistersAsync(0xD120, 2);
            var s = stats.Snapshot();
            Check(words.Length == 2 && s.TxFrames == 1 && s.TxBytes == 8 && s.RxFrames == 1 && s.RxBytes == 9,
                "fragmented response counts once, raw bytes accumulate");
            Check(s.LastResponseAgeMs is >= 0 && s.LastResponseMs is >= 0, "monotonic response timing");
            transport.Mode = "crc";
            await Expect<ModbusCrcException>(() => client.ReadRegistersAsync(0xD120, 2));
            transport.Mode = "address";
            await Expect<IOException>(() => client.ReadRegistersAsync(0xD120, 2));
            transport.Mode = "quantity";
            await Expect<IOException>(() => client.ReadRegistersAsync(0xD120, 2));
            transport.Mode = "exception";
            await Expect<BmsModbusException>(() => client.ReadRegistersAsync(0xD120, 2));
            s = stats.Snapshot();
            Check(s.TxFrames == 5 && s.RxFrames == 2 && s.CrcErrors == 1 && s.InvalidResponses == 2 && s.ExceptionResponses == 1,
                "bad CRC/header excluded, Modbus exceptions prove live response");
            transport.Mode = "write-fail";
            await Expect<IOException>(() => client.ReadRegistersAsync(0xD120, 2));
            transport.EmitUnsolicited();
            s = stats.Snapshot();
            Check(s.TxFrames == 5 && s.SendFailures == 1 && s.RxFrames == 2, "failed write and unsolicited bytes cannot count as response");
            transport.Mode = "silent";
            using (var cts = new CancellationTokenSource(40))
                await Expect<OperationCanceledException>(() => client.ReadRegistersAsync(0xD120, 2, cts.Token));
            await Expect<TimeoutException>(() => client.ReadRegistersAsync(0xD120, 2));
            s = stats.Snapshot();
            Check(s.Cancellations == 1 && s.Timeouts == 1 && s.SendFailures == 1 && s.InvalidResponses == 2,
                "cancellation/timeout/write failure are distinct");
            transport.Mode = "ok";
            await Task.WhenAll(client.ReadRegistersAsync(0xD120, 2), client.ReadRegistersAsync(0xD120, 2));
        }
        transport.Mode = "ok";
        await using (var reconnected = new BmsClient(transport, stats))
            await reconnected.ReadRegistersAsync(0xD120, 2);
        var final = stats.Snapshot();
        Check(final.TxFrames == 10 && final.RxFrames == 5 && final.Timeouts == 1 && final.CrcErrors == 1,
            "counters survive new client, error paths release receive ownership and request gate");
        var serial = new CounterTransport { Serial = true };
        await using (var serialClient = new BmsClient(serial)) {
            await serialClient.ReadRegistersAsync(0xD120, 2);
            long responseTick = serial.LastEmitTick;
            await serialClient.ReadRegistersAsync(0xD120, 2);
            Check(System.Diagnostics.Stopwatch.GetElapsedTime(responseTick, serial.LastWriteTick).TotalMilliseconds >= 20,
                "serial next request must leave verified 20ms response turnaround");
        }
        Console.WriteLine("PASS communication counters: fragmentation/CRC/header/exception/write failure/cancel/timeout/reconnect");
    }
    static async Task Expect<T>(Func<Task> action) where T : Exception
    {
        try { await action(); } catch (T) { return; }
        throw new Exception("Expected " + typeof(T).Name);
    }
    sealed class CounterTransport : IBmsTransport
    {
        public string Mode = "ok";
        public bool Serial;
        public bool RequiresSerialWakeup => Serial;
        public long LastWriteTick, LastEmitTick;
        public bool IsConnected => true;
        public string DiscoveryDescription => "counter test";
        public event Action<ReadOnlyMemory<byte>>? DataReceived;
        public event Action<string>? ConnectionProgress { add { } remove { } }
        public Task ReconnectAsync(CancellationToken ct = default) => Task.CompletedTask;
        public ValueTask DisposeAsync() => ValueTask.CompletedTask;
        public void EmitUnsolicited() => DataReceived?.Invoke(new byte[]{1,2,3});
        public Task WriteAsync(ReadOnlyMemory<byte> data, CancellationToken ct = default)
        {
            LastWriteTick = System.Diagnostics.Stopwatch.GetTimestamp();
            if (Mode == "write-fail") throw new IOException("injected write failure");
            if (Mode == "silent") return Task.CompletedTask;
            byte[] body = Mode == "exception" ? new byte[]{1,0x83,2} :
                Mode == "quantity" ? new byte[]{1,3,2,0,1} : new byte[]{1,3,4,0x42,0x53,0,1};
            if (Mode == "address") body[0] = 2;
            byte[] frame = ModbusRtu.Frame(body);
            if (Mode == "crc") frame[^1] ^= 1;
            for (int i=0; i<frame.Length; i+=3) {
                LastEmitTick = System.Diagnostics.Stopwatch.GetTimestamp();
                DataReceived?.Invoke(frame.AsMemory(i,Math.Min(3,frame.Length-i)));
            }
            return Task.CompletedTask;
        }
    }
}
