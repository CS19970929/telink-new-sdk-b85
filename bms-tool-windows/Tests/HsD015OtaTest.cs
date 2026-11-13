using System.Buffers.Binary;
using System.Diagnostics;
using BmsTool.Windows;

static class Program
{
    static int checks;
    static void Check(bool value, string why) { checks++; if (!value) throw new Exception(why); }
    static async Task Main()
    {
        string root = Path.Combine(Environment.GetEnvironmentVariable("LOCALAPPDATA")!, "CodexTemp", "hs-d015-ota-test-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try
        {
            byte[] payload = Enumerable.Range(0, 2049).Select(i => (byte)i).ToArray();
            BinaryPrimitives.WriteUInt32LittleEndian(payload, 0x20002000);
            BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(4), 0x08002881);
            string path = Path.Combine(root, "image.dat");
            File.WriteAllBytes(path, payload);
            var image = FirmwareImage.LoadForTarget(path, OtaTargetKind.Stm32SerialIap);
            var ok = new FakeTransport();
            Check(await new Stm32SerialBleOtaClient(ok, false).UpgradeAsync(image, default), "upgrade confirmation");
            Check(ok.Pages == 3 && ok.Entered == 1 && ok.Completed == 1 && ok.Reconnects == 2, "single ENTER/pages/COMPLETE and verify reconnect");
            Check(ok.Received.SequenceEqual(payload.Concat(Enumerable.Repeat((byte)0xFF, 3072 - payload.Length))), "exact payload and FF padding");
            Check(ok.CrcReads == 1, "installed application CRC readback required");
            var corrupt = new FakeTransport { BadCrc = true };
            try { await new Stm32SerialBleOtaClient(corrupt, false).UpgradeAsync(image, default); throw new Exception("bad readback accepted"); }
            catch (IOException) { checks++; }
            Check(corrupt.Completed == 1 && corrupt.CrcReads == 1, "readback failure never repeats COMPLETE");
            var unknown = new FakeTransport { UnknownBoard = true };
            try { await new Stm32SerialBleOtaClient(unknown, false).UpgradeAsync(image, default); throw new Exception("unknown 3520 accepted"); }
            catch (InvalidDataException) { checks++; }
            Check(unknown.Entered == 0 && unknown.Pages == 0, "preflight refusal occurs before ENTER");
            var lost = new FakeTransport { DropPageAck = true };
            try { await new Stm32SerialBleOtaClient(lost, false).UpgradeAsync(image, default); throw new Exception("lost ACK accepted"); }
            catch (IOException) { checks++; }
            Check(lost.Pages == 1 && lost.Completed == 0, "implicit page protocol never retries a page");
            var unconfirmed = new FakeTransport { Legacy = true };
            try { await new Stm32SerialBleOtaClient(unconfirmed, false).UpgradeAsync(image, default); throw new Exception("unconfirmed legacy accepted"); }
            catch (IOException) { checks++; }
            Check(unconfirmed.Entered == 0, "unknown legacy requires explicit selection before ENTER");
            var legacy = new FakeTransport { Legacy = true };
            Check(await new Stm32SerialBleOtaClient(legacy, false) { AllowUnidentifiedLegacy = true }.UpgradeAsync(image, default), "explicit legacy capability absence still supported");
            Check(legacy.CrcReads == 0 && legacy.Completed == 1, "legacy verification contract unchanged");
            Console.WriteLine($"PASS HS-D015 actual OTA client: {checks} checks; preflight, exact wire frames, padding, reconnect CRC, unknown result, no page retry, legacy");
        }
        finally { Directory.Delete(root, true); }
    }

    sealed class FakeTransport : IBmsTransport
    {
        public bool BadCrc, UnknownBoard, DropPageAck, Legacy;
        public int Entered, Pages, Completed, Reconnects, CrcReads;
        public long CompletedAt;
        public List<byte> Received = new();
        public bool IsConnected => true;
        public string DiscoveryDescription => "fake";
        public event Action<ReadOnlyMemory<byte>>? DataReceived;
        public event Action<string>? ConnectionProgress { add { } remove { } }
        public Task ReconnectAsync(CancellationToken ct = default) {
            if (Completed > 0 && !Legacy) Check(Stopwatch.GetElapsedTime(CompletedAt).TotalMilliseconds >= 2200, "HS-D015 reset plus boot recovery window must elapse before APP verify");
            Reconnects++; return Task.CompletedTask;
        }
        public ValueTask DisposeAsync() => ValueTask.CompletedTask;
        public Task WriteAsync(ReadOnlyMemory<byte> data, CancellationToken ct = default)
        {
            byte[] r = data.ToArray();
            ModbusRtu.ValidateFrame(r);
            ushort reg = BinaryPrimitives.ReadUInt16BigEndian(r.AsSpan(2));
            ushort count = BinaryPrimitives.ReadUInt16BigEndian(r.AsSpan(4));
            byte[] body;
            if (r[1] == 3)
            {
                if (reg == 0xD170 || (Legacy && reg is 0x2500 or 0xD130)) body = new byte[] { 1, 0x83, 2 };
                else
                {
                    var words = new ushort[count];
                    if (reg == 0x2500)
                    {
                        words[0] = 0x3520; words[1] = 1; words[24] = UnknownBoard ? (ushort)0 : (ushort)0xD015; words[25] = 13;
                        words[28] = 23; words[29] = 0x2800; words[30] = 0x0800; words[31] = 24;
                    }
                    else if (reg == 0xD120) { words[0] = 0x4253; words[1] = 2; }
                    else if (reg == 0xD130) { words[0] = 0xD015; words[1] = 0x0320; }
                    else if (reg == 0xD13D)
                    {
                        CrcReads++;
                        uint crc = FirmwareImage.HsD015PaddedCrc32(Received.ToArray()) ^ (BadCrc ? 1u : 0u);
                        words[0] = (ushort)(crc >> 16); words[1] = (ushort)crc; words[2] = (ushort)Received.Count;
                    }
                    else throw new Exception($"unexpected read {reg:X4}");
                    body = new byte[3 + count * 2]; body[0] = 1; body[1] = 3; body[2] = (byte)(count * 2);
                    for (int i = 0; i < count; i++) BinaryPrimitives.WriteUInt16BigEndian(body.AsSpan(3 + i * 2), words[i]);
                }
            }
            else
            {
                Check(r[1] == 0x10, "write multiple only");
                if (reg == 0xFFFD) { Entered++; Check(count == 1 && r[6] == 2 && r[7] == 0 && r[8] == 1, "CONNECT wire format"); }
                else if (reg == 0xFFFE)
                {
                    Pages++; Check(count == 1024 && r[6] == 0 && r.Length == 1033, "PAGE quantity is byte count");
                    Received.AddRange(r.Skip(7).Take(1024));
                    if (DropPageAck) return Task.CompletedTask;
                }
                else if (reg == 0xFFFF) { CompletedAt = Stopwatch.GetTimestamp(); Completed++; Check(count == 1 && r[6] == 2, "COMPLETE wire format"); }
                else throw new Exception($"unexpected write {reg:X4}");
                body = r.Take(6).ToArray();
            }
            DataReceived?.Invoke(ModbusRtu.Frame(body));
            return Task.CompletedTask;
        }
    }
}

// Hardware transports are not exercised by this offline protocol test.
namespace BmsTool.Windows
{
    public abstract class BmsBleTransport : IBmsTransport
    {
        public int? NegotiatedMtu => 23;
        public abstract bool IsConnected { get; }
        public abstract string DiscoveryDescription { get; }
        public abstract event Action<ReadOnlyMemory<byte>>? DataReceived;
        public abstract event Action<string>? ConnectionProgress;
        public abstract Task ReconnectAsync(CancellationToken ct = default);
        public abstract Task WriteAsync(ReadOnlyMemory<byte> data, CancellationToken ct = default);
        public abstract ValueTask DisposeAsync();
    }
    public abstract class BmsSerialTransport : BmsBleTransport { public int BaudRate => 19200; }
}
