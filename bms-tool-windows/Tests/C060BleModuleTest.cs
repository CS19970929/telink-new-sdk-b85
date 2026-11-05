using BmsTool.Windows;
using System.Globalization;
using System.IO;
using System.Text.Json;

static class Test
{
    public static async Task Main(string[] args) {
        if(args.Length<3)throw new ArgumentException("MAC, evidence directory and baseline C060 JSON are required");
        ulong mac=ulong.Parse(args[0].Replace(":",""),NumberStyles.HexNumber);
        string output=Path.GetFullPath(args[1]);Directory.CreateDirectory(output);
        bool writeSame=args.Contains("--write-same");
        using var deadline=new CancellationTokenSource(TimeSpan.FromMinutes(5));var ct=deadline.Token;
        using var log=new StreamWriter(Path.Combine(output,"module-transport.log")){AutoFlush=true};
        await using var transport=new BmsBleTransport();transport.ConnectionProgress+=m=>log.WriteLine(m);
        await transport.ConnectAsync(mac,ct);
        await using var client=new BmsClient(transport);client.Log+=m=>log.WriteLine(m);
        await client.ProbeAsync(ct);
        var identity=await client.ReadIdentityAsync(args[0],"BT-1234567890",ct);
        var capability=UpgradeDetection.ParseCapabilities(await client.ReadRegistersAsync(0xD170,12,ct));
        if(!client.IsC060 || capability?.Board!=0xC0600020)throw new Exception("Selected module did not identify the expected C060");
        var before=await client.ReadC060ParametersAsync(ct);
        if(before.Errors.Count!=0)throw new Exception(string.Join("; ",before.Errors));
        var baseline=C060Parameters.Import(args[2]);
        string[] keys=before.Blocks.Keys.Where(k=>k!="Realtime").OrderBy(k=>k).ToArray();
        var changed=keys.Where(k=>!baseline.Blocks.TryGetValue(k,out var v)||!v.SequenceEqual(before.Blocks[k])).ToArray();
        if(changed.Length!=0)throw new Exception("Configuration changed across OTA: "+string.Join(",",changed));
        C060Parameters.Export(Path.Combine(output,"parameters-after-upgrade.json"),before);
        int protectionWrites=0,parameterWrites=0;
        if(writeSame) {
            ProtectionBatch.ValidateC060(before.Blocks["Protection"]);
            // Same-value transactions test the actual ACK/readback path while
            // retaining every existing threshold and group member.
            for(int group=0;group<13;group++) {
                ushort address=(ushort)(0x2100+5*group);ushort[] values=before.Blocks["Protection"].Skip(5*group).Take(5).ToArray();
                await client.WriteRegistersAsync(address,values,ct);
                if(!(await client.ReadRegistersAsync(address,5,ct)).SequenceEqual(values))throw new Exception($"Protection {address:X4} mismatch");
                protectionWrites++;
            }
            foreach(var group in C060Parameters.Groups) {
                ushort[] values=before.Blocks[group.Address.ToString("X4")];
                await client.WriteRegistersAsync(group.Address,values,ct);
                if(!(await client.ReadRegistersAsync(group.Address,group.Count,ct)).SequenceEqual(values))throw new Exception($"Parameters {group.Address:X4} mismatch");
                parameterWrites++;
            }
        }
        var after=await client.ReadC060ParametersAsync(ct);
        if(after.Errors.Count!=0 || keys.Any(k=>!after.Blocks[k].SequenceEqual(before.Blocks[k])))throw new Exception("Configuration changed during unchanged-value write test");
        var events=await client.ReadRegistersAsync(0xC008,100,ct);
        var snapshots=new List<BatterySnapshot>();
        for(int i=0;i<3;i++) { var sample=await client.ReadBatteryAsync(ct);if(sample.ValidCellCount!=20)throw new Exception("C060 cell mapping changed");snapshots.Add(sample);await Task.Delay(500,ct); }
        var imageInfo=await client.ReadRegistersAsync(0xD17C,8,ct);
        C060Parameters.Export(Path.Combine(output,"parameters-after-roundtrip.json"),after);
        var result=new {status="pass",mac=args[0],mtu=transport.NegotiatedMtu,identity,configurationBlocksCompared=keys.Length,
            upgradeConfigurationPreserved=true,protectionWriteGroups=protectionWrites,parameterWriteGroups=parameterWrites,
            sameValueWritesPerformed=writeSame,sameValueWritesPreservedConfiguration=writeSame?(bool?)true:null,eventWords=events.Length,realtimeSamples=snapshots.Count,validCells=20,
            runningImageInfo=imageInfo,communication=client.CommunicationStatistics.Snapshot()};
        string json=JsonSerializer.Serialize(result,new JsonSerializerOptions {WriteIndented=true});
        File.WriteAllText(Path.Combine(output,"result.json"),json);
        Console.WriteLine(json);
    }
}
