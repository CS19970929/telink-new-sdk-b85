using System.Buffers.Binary;
using BmsTool.Windows;

static class MosControlTest
{
    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    public static async Task Main()
    {
        var transport = new MosTransport();
        await using (var client = new BmsClient(transport))
        {
            MosControlState? initial = await client.TryReadMosControlAsync();
            Check(initial is { ChargeAllowed: true, DischargeAllowed: true, DriverStateValid: true }, "initial MOS state");
            MosControlState chargeOff = await client.SetMosChannelAllowedAsync(true, false);
            Check(!chargeOff.ChargeAllowed && chargeOff.DischargeAllowed, "charge permission changes independently");
            Check(transport.LastWriteRegister == 0x2F12 && transport.WriteCount == 1, "only charge permission register written");

            transport.IgnoreWrite = true;
            await Expect<IOException>(() => client.SetMosChannelAllowedAsync(false, false));
            Check(transport.LastWriteRegister == 0x2F13, "discharge permission register selected");
        }

        var legacy = new MosTransport { Unsupported = true };
        await using (var client = new BmsClient(legacy))
            Check(await client.TryReadMosControlAsync() is null && legacy.WriteCount == 0, "legacy firmware is read-only fallback");

        Console.WriteLine("PASS MOS control: capability probe, independent single-register write, readback mismatch and legacy fallback");
    }

    private static async Task Expect<T>(Func<Task> action) where T : Exception
    {
        try { await action(); }
        catch (T) { return; }
        throw new Exception("Expected " + typeof(T).Name);
    }

    private sealed class MosTransport : IBmsTransport
    {
        private bool _chargeAllowed = true;
        private bool _dischargeAllowed = true;
        public bool Unsupported;
        public bool IgnoreWrite;
        public ushort LastWriteRegister;
        public int WriteCount;
        public bool IsConnected => true;
        public bool RequiresSerialWakeup => false;
        public string DiscoveryDescription => "MOS control fixture";
        public event Action<ReadOnlyMemory<byte>>? DataReceived;
        public event Action<string>? ConnectionProgress { add { } remove { } }
        public Task ReconnectAsync(CancellationToken ct = default) => Task.CompletedTask;
        public ValueTask DisposeAsync() => ValueTask.CompletedTask;

        public Task WriteAsync(ReadOnlyMemory<byte> data, CancellationToken ct = default)
        {
            ReadOnlySpan<byte> request = data.Span;
            if (request[1] == 0x03)
            {
                if (Unsupported)
                {
                    DataReceived?.Invoke(ModbusRtu.Frame(new byte[] { 1, 0x83, 2 }));
                    return Task.CompletedTask;
                }
                ushort[] words = { 0x4D43, 1, (ushort)(_chargeAllowed ? 1 : 0), (ushort)(_dischargeAllowed ? 1 : 0), 3, 1, 0x11, 3 };
                byte[] body = new byte[3 + words.Length * 2];
                body[0] = 1; body[1] = 3; body[2] = (byte)(words.Length * 2);
                for (int i = 0; i < words.Length; i++) BinaryPrimitives.WriteUInt16BigEndian(body.AsSpan(3 + i * 2, 2), words[i]);
                DataReceived?.Invoke(ModbusRtu.Frame(body));
                return Task.CompletedTask;
            }
            if (request[1] != 0x06) throw new IOException("unexpected function");
            LastWriteRegister = BinaryPrimitives.ReadUInt16BigEndian(request[2..4]);
            ushort value = BinaryPrimitives.ReadUInt16BigEndian(request[4..6]);
            WriteCount++;
            if (!IgnoreWrite)
            {
                if (LastWriteRegister == 0x2F12) _chargeAllowed = value == 1;
                else if (LastWriteRegister == 0x2F13) _dischargeAllowed = value == 1;
            }
            DataReceived?.Invoke(ModbusRtu.Frame(request[..6]));
            return Task.CompletedTask;
        }
    }
}
