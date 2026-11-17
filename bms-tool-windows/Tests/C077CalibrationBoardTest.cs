using System.IO;
using BmsTool.Windows;
using System.Diagnostics;
using System.Text.Json;

static class Test
{
    static void Check(bool ok,string why){if(!ok)throw new IOException(why);Console.WriteLine("PASS "+why);}
    public static async Task Main(string[] args)
    {
        // This explicit opt-in harness changes offset by only 1mA and restores it.
        // It does not infer a physical zero or replace an external current meter.
        if(args.Length!=4||args[3]!="--test-write-restore")throw new ArgumentException("endpoint, expected-build-id, evidence, --test-write-restore required");
        using var deadline=new CancellationTokenSource(TimeSpan.FromMinutes(3));var ct=deadline.Token;
        Directory.CreateDirectory(args[2]);using var log=File.CreateText(Path.Combine(args[2],"wire.log"));
        IBmsTransport transport;
        var connect=Stopwatch.StartNew();
        if(args[0].StartsWith("COM",StringComparison.OrdinalIgnoreCase)){var serial=new BmsSerialTransport();await serial.ConnectAsync(args[0],19200,ct);transport=serial;}
        else {var ble=new BmsBleTransport();ble.ConnectionProgress+=s=>log.WriteLine(s);await ble.ConnectAsync(Convert.ToUInt64(args[0].Replace(":",""),16),ct);transport=ble;}
        await using var client=new BmsClient(transport);client.Log+=s=>log.WriteLine(s);
        var identity=await client.ReadIdentityAsync(ct);var build=await client.TryReadFirmwareBuildIdAsync(ct);
        Check(client.IsC077&&build==Convert.ToUInt32(args[1],16),"explicit C077 identity/build");
        var hardware=await client.ReadC077HardwareAsync(ct:ct);var eeprom=hardware.Actual.Chips.Select(c=>c.Eeprom.ToArray()).ToArray();
        Check(hardware.Actual.Schema==3&&hardware.Actual.SampleValid&&hardware.Actual.ZeroReady,"dual AFE ready with new startup schema");
        var capture=await client.ReadC060ParametersAsync(ct);
        Check(capture.Schema==2&&!capture.Blocks.ContainsKey("Calibration")&&C060Parameters.FieldsFor(true).Count==14,"generic parameters without retired K/B");
        double connectSeconds=connect.Elapsed.TotalSeconds;
        await Task.Delay(35000,ct);var wake=Stopwatch.StartNew();await client.ReadBatteryAsync(ct);double wakeSeconds=wake.Elapsed.TotalSeconds;
        var session=await client.BeginCurrentCalibrationAsync(ct);var original=session.Original;bool restored=false;
        try {
            Check(session.NativeProduct==0xC077,"native calibration lease");
            // Collect stability/freshness only; the test does not assert the load is zero.
            var samples=await client.CollectCurrentCalibrationAsync(session,true,new CurrentCalibrationPolicy(SampleCount:8),null,ct);
            session.Candidate=original with {OffsetMa=checked(original.OffsetMa+1)};
            await client.SaveCurrentCalibrationAsync(session,session.Candidate,ct);
            Check(session.SaveVerified,"1mA offset transaction, readback and fresh formula");
            File.WriteAllText(Path.Combine(args[2],"samples.json"),JsonSerializer.Serialize(samples,new JsonSerializerOptions{WriteIndented=true}));
        } finally {
            await client.EndCurrentCalibrationAsync(session,verifyConnection:true);
            if(session.WriteAttempted){
                await client.VerifyCurrentCalibrationContinuityAsync(session,ct);
                var current=CurrentCalibrationCoefficients.Decode(await client.ReadRegistersAsync(0x2E24,4,ct));
                if(current==original)restored=true;
                else {
                    Check(current==session.Candidate,"restore only exact test candidate");
                    var restore=await client.BeginCurrentCalibrationAsync(ct);
                    try {
                        Check(restore.Identity==session.Identity&&restore.Original==session.Candidate&&session.Access.Token!=ushort.MaxValue&&restore.Access.Token==session.Access.Token+1,"restore requires same device, coefficients and next lease generation");
                        await client.SaveCurrentCalibrationAsync(restore,original,ct);restored=true;
                    } finally {await client.EndCurrentCalibrationAsync(restore,verifyConnection:true);}
                }
            }
        }
        Check(restored&&CurrentCalibrationCoefficients.Decode(await client.ReadRegistersAsync(0x2E24,4,ct))==original,"original coefficients restored");
        var after=await client.ReadC077HardwareAsync(ct:ct);
        Check(after.Actual.Chips.Select((c,i)=>c.Eeprom.SequenceEqual(eeprom[i])).All(x=>x),"both EEPROM images unchanged");
        File.WriteAllText(Path.Combine(args[2],"report.json"),JsonSerializer.Serialize(new{endpoint=args[0],build,identity,connectSeconds,wakeSeconds,original,restored,after.Actual.SampleValid,after.Actual.Schema,precisionVerified=false},new JsonSerializerOptions{WriteIndented=true}));
        Console.WriteLine("PASS real board protocol/Flash/formula/restore; external-meter precision remains unverified");
    }
}
