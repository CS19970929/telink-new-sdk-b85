using System.Buffers.Binary;
using System.IO;
using BmsTool.Windows;

static class Test
{
    static int checks;
    static void Check(bool value, string reason) { checks++; if (!value) throw new Exception(reason); }
    static async Task Main()
    {
        var ordinary = new Peer();
        await using (var client = new BmsClient(ordinary))
        {
            var snapshot = await client.ReadBatteryAsync();
            Check(snapshot.HasSystemStatus && ordinary.FactoryCommands == 0 && ordinary.ExtendedFaultReads == 0,
                "ordinary monitoring must retain its old register contract without C11 commands");
        }
        var qualified = new Peer { Supported = true };
        await using (var client = new BmsClient(qualified))
        {
            await client.C11ExchangeAsync(0x10, Array.Empty<byte>(), default);
            var snapshot = await client.ReadBatteryAsync();
            Check(snapshot.FaultInjection is { DriverValid: true } && qualified.ExtendedFaultReads == 1,
                "validated C11 capabilities enable the C11 diagnostic window");
            Check((snapshot.ProtectionLevel3Raw & 0x4000) != 0 && snapshot.ProtectionLevel3Text.Contains("AFE"),
                "C11 diagnostic faults must reach the shared battery model");
            Check(snapshot.ChargeMosOn && !snapshot.DischargeMosOn,
                "qualified C11 driver evidence overrides only the MOS status bits");
            qualified.BadMagic = true;
            try { await client.TryReadFaultInjectionAsync(forceProbe: true); throw new Exception("bad FINJ accepted"); }
            catch (IOException) { checks++; }
        }
        var unsupported = new Peer();
        await using (var client = new BmsClient(unsupported))
        {
            Check(await client.TryReadFaultInjectionAsync(forceProbe: true) is null,
                "unsupported factory capability remains optional");
            var snapshot = await client.ReadBatteryAsync();
            Check(!snapshot.FaultInjectionStateUnknown && unsupported.ExtendedFaultReads == 0,
                "optional capability absence must not mark a normal device faulty");
        }
        var malformed = new Peer { Supported = true, BadMagic = true };
        await using (var client = new BmsClient(malformed))
        {
            try { await client.C11ExchangeAsync(0x10, Array.Empty<byte>(), default); throw new Exception("bad C11 capability accepted"); }
            catch (IOException) { checks++; }
            await client.ReadBatteryAsync();
            Check(malformed.FactoryCommands == 1 && malformed.ExtendedFaultReads == 0,
                "malformed capabilities must not enable periodic factory reads");
        }
        Console.WriteLine($"PASS C11 client integration: {checks} generic/qualified/corrupt/optional capability and MOS checks; no hardware IO");
    }
    sealed class Peer : IBmsTransport
    {
        public bool Supported, BadMagic;
        public int FactoryCommands, ExtendedFaultReads;
        public bool IsConnected => true;
        public string DiscoveryDescription => "C11 client protocol peer";
        public event Action<ReadOnlyMemory<byte>>? DataReceived;
        public event Action<string>? ConnectionProgress { add {} remove {} }
        public Task ReconnectAsync(CancellationToken ct = default) => Task.CompletedTask;
        public ValueTask DisposeAsync() => ValueTask.CompletedTask;
        public Task WriteAsync(ReadOnlyMemory<byte> bytes, CancellationToken ct)
        {
            byte[] request = bytes.ToArray();
            byte[] body;
            if (request[1] == 0x41)
            {
                FactoryCommands++;
                byte command = request[2];
                if (!Supported) body = new byte[] { 1, 0x41, command, 3 };
                else
                {
                    byte[] payload;
                    if (command == 0x10)
                    {
                        payload = new byte[34];
                        "C11V"u8.CopyTo(payload);
                        BinaryPrimitives.WriteUInt16BigEndian(payload.AsSpan(4), 2);
                        payload[8] = 13; payload[9] = 2; payload[10] = 1;
                        BinaryPrimitives.WriteUInt16BigEndian(payload.AsSpan(30), 3000);
                        BinaryPrimitives.WriteUInt16BigEndian(payload.AsSpan(32), 1500);
                    }
                    else if (command == 0x20)
                    {
                        payload = new byte[24];
                        "FINJ"u8.CopyTo(payload);
                        payload[4] = 1; payload[5] = 1;
                        payload[11] = 1; payload[12] = 1; payload[13] = 1;
                    }
                    else throw new Exception("unexpected factory write");
                    if (BadMagic) payload[0] ^= 1;
                    body = new byte[5 + payload.Length];
                    body[0] = 1; body[1] = 0x41; body[2] = command; body[4] = (byte)payload.Length;
                    payload.CopyTo(body, 5);
                }
            }
            else
            {
                if (request[1] != 3) throw new Exception("unexpected register write");
                ushort address = BinaryPrimitives.ReadUInt16BigEndian(request.AsSpan(2));
                ushort count = BinaryPrimitives.ReadUInt16BigEndian(request.AsSpan(4));
                ushort[] values = new ushort[count];
                if (address == 0xD000)
                {
                    Array.Fill(values, (ushort)61001, 0, 32);
                    values[0] = 3500;
                }
                if (address == 0xD115) values[0] = 12;
                if (address == 0xD109)
                {
                    ExtendedFaultReads++;
                    values[0] = 0x0100; values[12] = 12;
                }
                body = new byte[3 + 2 * count];
                body[0] = 1; body[1] = 3; body[2] = (byte)(2 * count);
                for (int i = 0; i < count; i++) BinaryPrimitives.WriteUInt16BigEndian(body.AsSpan(3 + 2 * i), values[i]);
            }
            byte[] frame = ModbusRtu.Frame(body);
            for (int offset = 0; offset < frame.Length; offset += 3)
                DataReceived?.Invoke(frame.AsMemory(offset, Math.Min(3, frame.Length - offset)));
            return Task.CompletedTask;
        }
    }
}
