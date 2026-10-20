using System.Buffers.Binary;
using BmsTool.Windows;

static class SerialOtaTests
{
    static void Check(bool value, string reason) { if (!value) throw new Exception(reason); }
    public static async Task Main()
    {
        string folder = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "CodexTemp", "serial-ota-test-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(folder);
        try
        {
            byte[] data = new byte[512];
            BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(8), 0x544c4e4b);
            BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(24), (uint)data.Length);
            System.Text.Encoding.ASCII.GetBytes("BMSU8251").CopyTo(data, 128);
            data[136] = 1; data[137] = 14; data[138] = 1;
            BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(140), 0x12345678);
            BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(144), 124 * 1024);
            BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(508), TelinkOtaProtocol.CalculateTelinkCrcTrailer(data.AsSpan(0, 508)));
            string path = Path.Combine(folder, "fixture.img"); File.WriteAllBytes(path, data);
            FirmwareImage image = FirmwareImage.LoadStrictTelink(path);
            TelinkSerialImage info = TelinkSerialImage.Inspect(image);
            Check(info.Product == 14 && info.BuildId == 0x12345678 && info.DescriptorOffset == 128, "descriptor decode");
            var success = new Peer(info) { AddNoise = true };
            using (var client = new TelinkSerialOtaClient(success))
                Check(await client.UpgradeAsync(image, CancellationToken.None), "serial upgrade confirmation");
            Check(success.DataWrites == 4 && success.Commits == 1 && success.Image.SequenceEqual(data), "same canonical BIN and one commit");
            var cross = new Peer(info with { Product = 11 });
            await ExpectStop(cross, image);
            Check(cross.Begins == 0, "cross product began erase");
            var missingAck = new Peer(info) { DropDataAck = true };
            await ExpectStop(missingAck, image);
            Check(missingAck.DataWrites == 1 && missingAck.Commits == 0, "unknown data was resent or committed");
            var commitLost = new Peer(info) { DropCommitAck = true };
            await ExpectStop(commitLost, image);
            Check(commitLost.Commits == 1, "commit automatically retried");
            var badOffset = new Peer(info) { BadOffset = true };
            await ExpectStop(badOffset, image);
            Check(badOffset.DataWrites == 1 && badOffset.Commits == 0, "invalid ACK offset accepted");
            var reject = new Peer(info) { RejectBegin = true };
            await ExpectStop(reject, image);
            Check(reject.DataWrites == 0, "rejected BEGIN sent data");
            var silent = new Peer(info) { Silent = true };
            await ExpectStop(silent, image);
            Check(silent.Begins == 0, "old/unsupported firmware received BEGIN");
            data[137] = 8; File.WriteAllBytes(path, data);
            bool badCrc = false;
            try { FirmwareImage.LoadStrictTelink(path); } catch (InvalidDataException) { badCrc = true; }
            Check(badCrc, "corrupt BIN accepted");
            Console.WriteLine("PASS serial OTA client: canonical image, descriptor/product, fragmented/echo/noisy/stale ACK, offsets, no blind retry, commit uncertainty, unsupported firmware");
        }
        finally { Directory.Delete(folder, true); }
    }
    static async Task ExpectStop(Peer peer, FirmwareImage image)
    {
        using var client = new TelinkSerialOtaClient(peer) { RequestTimeout = TimeSpan.FromMilliseconds(25) };
        bool failed = false;
        try { await client.UpgradeAsync(image, CancellationToken.None); }
        catch (Exception e) when (e is IOException or InvalidDataException or TimeoutException) { failed = true; }
        Check(failed, "failure became success");
    }
    sealed class Peer : IBmsTransport
    {
        readonly TelinkSerialImage _info;
        byte _state;
        uint _session, _offset;
        public Peer(TelinkSerialImage info) { _info = info; }
        public bool IsConnected => true;
        public string DiscoveryDescription => "RAM peer";
        public bool RequiresSerialWakeup => true;
        public bool AddNoise, DropDataAck, DropCommitAck, BadOffset, RejectBegin, Silent;
        public int Begins, DataWrites, Commits;
        public byte[] Image = new byte[512];
        public event Action<ReadOnlyMemory<byte>>? DataReceived;
        public event Action<string>? ConnectionProgress { add { } remove { } }
        public Task ReconnectAsync(CancellationToken ct = default) => Task.CompletedTask;
        public ValueTask DisposeAsync() => default;
        public Task WriteAsync(ReadOnlyMemory<byte> memory, CancellationToken ct = default)
        {
            byte[] req = memory.ToArray();
            Check(ModbusRtu.Crc16(req.AsSpan(0, req.Length - 2)) == BinaryPrimitives.ReadUInt16LittleEndian(req.AsSpan(req.Length - 2)), "request CRC");
            byte cmd = req[2];
            byte error = 0;
            switch (cmd)
            {
                case 0: if (_state == 5) _state = 0; break;
                case 1: Begins++; if (RejectBegin) { error = 2; break; } _session = BinaryPrimitives.ReadUInt32LittleEndian(req.AsSpan(5)); _state = 2; break;
                case 2:
                    DataWrites++; int offset = (int)BinaryPrimitives.ReadUInt32LittleEndian(req.AsSpan(9));
                    int count = req.Length - 15; req.AsSpan(13, count).CopyTo(Image.AsSpan(offset)); _offset = (uint)(offset + count); break;
                case 3: _state = 4; break;
                case 4: _state = 5; Commits++; break;
                case 5: if (_state == 5) error = 1; else _state = 0; break;
            }
            if (Silent || cmd == 2 && DropDataAck || cmd == 4 && DropCommitAck) return Task.CompletedTask;
            byte[] rsp = new byte[30]; req.AsSpan(0, 5).CopyTo(rsp);
            rsp[5] = _state; rsp[6] = error; rsp[7] = _info.Product;
            BinaryPrimitives.WriteUInt32LittleEndian(rsp.AsSpan(8), _session);
            BinaryPrimitives.WriteUInt32LittleEndian(rsp.AsSpan(12), BadOffset && cmd == 2 ? 0 : _offset);
            BinaryPrimitives.WriteUInt32LittleEndian(rsp.AsSpan(16), 512);
            BinaryPrimitives.WriteUInt32LittleEndian(rsp.AsSpan(20), _info.BuildId);
            BinaryPrimitives.WriteUInt32LittleEndian(rsp.AsSpan(24), _info.Crc);
            Seal(rsp);
            if (AddNoise)
            {
                DataReceived?.Invoke(new byte[] { 0x42, 0x43 });
                DataReceived?.Invoke(req); // RS485 local echo
                byte[] stale = rsp.ToArray(); stale[3] ^= 0x80; Seal(stale); DataReceived?.Invoke(stale);
                byte[] corrupt = rsp.ToArray(); corrupt[28] ^= 1; DataReceived?.Invoke(corrupt);
            }
            for (int i = 0; i < rsp.Length; i++) DataReceived?.Invoke(rsp.AsMemory(i, 1));
            return Task.CompletedTask;
        }
        static void Seal(byte[] data) => BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(28), ModbusRtu.Crc16(data.AsSpan(0, 28)));
    }
}
