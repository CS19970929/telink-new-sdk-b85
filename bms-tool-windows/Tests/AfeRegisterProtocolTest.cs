using BmsTool.Windows;
using System.IO;

static class Test
{
    static void Check(bool condition, string reason) { if (!condition) throw new Exception(reason); }
    static async Task Main()
    {
        var dvc = AfeRegisterCatalog.Load(true);
        var sh = AfeRegisterCatalog.Load(false);
        var old = AfeRegisterCatalog.Load(false, true);
        Check(dvc.Rows.Count == 145 && sh.Rows.Count == 90 && old.Rows.Count == 90, "all reference addresses");
        Check(dvc.Rows.Single(r => r.Address == 0x01).ReadClear && !dvc.Rows.Single(r => r.Address == 0x90).Readable, "DVC RC and unresolved address");
        Check(sh.Rows.Single(r => r.Address == 0x59).ReadClear, "SH RC");
        Check(sh.Rows.All(r => r.Readable), "DVC 0x90 limitation must not affect SH addresses");
        Check(sh.Rows.All(r => r.Fields.Count > 0 && r.Pages.Length > 0), "bit fields have source pages");
        Check(AfeRegisterCatalog.ParseCode("0x0F", 15) == 15, "field encoding parse");
        try { AfeRegisterCatalog.ParseCode("16", 15); throw new Exception("out of mask accepted"); } catch (ArgumentOutOfRangeException) { }
        var bms = new BmsClient();
        bms.Reply = request => Frame(request[2], new byte[] { 1, 0x35, 0x10, 1, 0, 0x12, 0x34, 0x56, 0x78, 7, 0xD0, 3 });
        var info = await bms.ReadAfeRegisterInfoAsync();
        Check(info.Model == 0x3510 && info.ShuntElementMicroOhm == 2000 && info.ParallelCount == 3, "INFO exact ratio and model");
        bms.Reply = request => {
            Check(request.Length == 11 && request[3] == 0x12 && request[4] == 0x34 && request[5] == 0x4D && request[6] == 15 && request[7] == 0x21, "masked CAS request frame");
            return Frame(request[2], new byte[] { 0x4D, 0x23 });
        };
        Check(await bms.WriteAfeRegisterAsync(0x1234, 0x4D, 15, 0x21, 3) == 0x23, "write readback");
        bms.Reply = request => Frame(request[2], new byte[] { 0x4E, 0x23 });
        try { await bms.ReadAfeRegisterAsync(1, 0x4D); throw new Exception("wrong address accepted"); } catch (IOException) { }
        bms.Reply = _ => throw new IOException("ACK lost");
        int previous = bms.Calls;
        try { await bms.WriteAfeRegisterAsync(1, 0x4D, 15, 0x21, 3); throw new Exception("lost ACK accepted"); } catch (IOException) { }
        Check(bms.Calls == previous + 1, "write cannot be automatically retried");
        bms.Reply = request => Frame(request[2], Array.Empty<byte>(), 6);
        try { await bms.BeginAfeRegisterDebugAsync(1); throw new Exception("CAS rejection accepted"); } catch (IOException) { }
        bms.Reply = request => Frame(request[2], Array.Empty<byte>());
        await bms.BeginAfeRegisterDebugAsync(1); await bms.RestoreAfeRegisterDebugAsync(1);
        Console.WriteLine("PASS reference catalogs, exact INFO, masked CAS framing, response address, error status and lost ACK without retry");
    }
    static byte[] Frame(byte command, byte[] payload, byte status = 0) =>
        ModbusRtu.Frame(new byte[] { 1, 0x42, command, status }.Concat(payload).ToArray());
}

namespace BmsTool.Windows
{
    public sealed partial class BmsClient
    {
        public Func<byte[], byte[]> Reply = _ => throw new IOException("unconfigured peer");
        public int Calls;
        private Task<byte[]> TransactAsync(byte[] request, CancellationToken ct) { ct.ThrowIfCancellationRequested(); Calls++; return Task.FromResult(Reply(request)); }
    }
}
