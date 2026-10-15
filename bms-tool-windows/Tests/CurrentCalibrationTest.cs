using System.Buffers.Binary;
using System.IO;
using System.Reflection;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using BmsTool.Windows;

internal static class CurrentCalibrationTest
{
    private static void Check(bool value, string message)
    { if (!value) throw new Exception(message); }
    private static async Task Reject<T>(Func<Task> action) where T : Exception
    { try { await action(); } catch (T) { return; } throw new Exception("Expected " + typeof(T).Name); }

    [STAThread]
    private static void Main()
    {
        CheckProtocolAsync().GetAwaiter().GetResult();
        CheckWindow();
        Console.WriteLine("CURRENT_CALIBRATION_PASS: four product identities, original gain preserved including legacy range, exact grouped write/readback, refusal paths, both UI save modes; no hardware access");
    }

    private static async Task CheckProtocolAsync()
    {
        var policy = new CurrentCalibrationPolicy(SampleCount: 4, TimeoutSeconds: 1);
        string[] products = { "D008", "D011", "D013", "D014" };
        uint[] gains = { 100000, 1234567, 3000000, 10000000 };
        for (int i = 0; i < products.Length; i++)
        {
            var transport = new CalibrationTransport(products[i], gains[i]);
            await using var client = new BmsClient(transport);
            var session = await client.BeginCurrentCalibrationAsync(CancellationToken.None);
            Check(session.Identity.Hardware == products[i], "Product identity");
            session.ZeroOnly = true;
            session.Zero = await client.CollectCurrentCalibrationAsync(session, true, policy, null, CancellationToken.None);
            var candidate = policy.CalculateZeroOnly(session.Zero, session.Original);
            session.Candidate = candidate;
            Check(candidate.OffsetMa == 230 && candidate.GainPpm == gains[i], "Zero must use raw mA and preserve gain");
            int current = await client.SaveCurrentCalibrationAsync(session, candidate, CancellationToken.None);
            Check(current == 0 && transport.Writes == 1, "One grouped write and fresh formula validation");
            Check(transport.LastWritten!.Skip(2).SequenceEqual(session.Original.Words.Skip(2)), "Gain words must remain identical");
            Check(session.Report.Contains("仅校准零点") && session.Report.Contains("无加载点"), "Zero-only record");
            await client.EndCurrentCalibrationAsync(session);
        }
        var original = new CurrentCalibrationCoefficients(999, 1234567);
        var zero = new CurrentCalibrationSamples(-230.5, -231, -230, 0, 1, DateTimeOffset.UtcNow);
        Check(policy.CalculateZeroOnly(zero, original).OffsetMa == -231, "Signed zero and rounding");
        var loaded = new CurrentCalibrationSamples(-10000, -10000, -10000, 0, 2, DateTimeOffset.UtcNow);
        var twoPoint = policy.Calculate(new CurrentCalibrationSamples(0, 0, 0, 0, 1, DateTimeOffset.UtcNow), loaded, -11000);
        Check(twoPoint.GainPpm == 1100000, "Existing two-point gain calculation");
        await Reject<ArgumentException>(() => Task.FromResult(policy.CalculateZeroOnly(zero with { MeanMa = double.NaN }, original)));

        var blocked = new CalibrationTransport("D008", 1234567);
        await using var blockedClient = new BmsClient(blocked);
        var active = await blockedClient.BeginCurrentCalibrationAsync(CancellationToken.None);
        active.ZeroOnly = true;
        var valid = new CurrentCalibrationCoefficients(230, 1234567);
        await Reject<ArgumentException>(() => blockedClient.SaveCurrentCalibrationAsync(active, valid with { GainPpm = 1000000 }, CancellationToken.None));
        Check(blocked.Writes == 0, "Zero-only must refuse a changed gain before write");
        blocked.RawMa = 3000;
        await Reject<IOException>(() => blockedClient.CollectCurrentCalibrationAsync(active, true, policy, null, CancellationToken.None));
        blocked.RawMa = 230;
        blocked.InvalidSample = true;
        await Reject<IOException>(() => blockedClient.CollectCurrentCalibrationAsync(active, true, policy, null, CancellationToken.None));
        blocked.InvalidSample = false;
        blocked.Parameters = blocked.Parameters with { GainPpm = 1234568 };
        await Reject<IOException>(() => blockedClient.SaveCurrentCalibrationAsync(active, valid, CancellationToken.None));
        Check(blocked.Writes == 0, "Invalid sample, nonzero current or external parameter change must not write");
        blocked.Parameters = active.Original;
        using (var cancel = new CancellationTokenSource())
        { cancel.Cancel(); await Reject<OperationCanceledException>(() => blockedClient.SaveCurrentCalibrationAsync(active, valid, cancel.Token)); }
        Check(blocked.Writes == 0, "Cancellation before write");
        blocked.MismatchReadback = true;
        await Reject<IOException>(() => blockedClient.SaveCurrentCalibrationAsync(active, valid, CancellationToken.None));
        Check(blocked.Writes == 1 && active.WriteAttempted, "Unknown write result must not retry");
        await blockedClient.EndCurrentCalibrationAsync(active);
        var old = new CalibrationTransport("D008", 1000000) { Capability = false };
        await using var oldClient = new BmsClient(old);
        await Reject<NotSupportedException>(() => oldClient.BeginCurrentCalibrationAsync(CancellationToken.None));
        Check(old.Writes == 0, "Legacy capability refusal");
    }

    private static void CheckWindow()
    {
        var app = new Application();
        var window = new MainWindow();
        const BindingFlags flags = BindingFlags.Instance | BindingFlags.NonPublic;
        object Field(string name) => typeof(MainWindow).GetField(name, flags)!.GetValue(window)!;
        void Set(string name, object? value) => typeof(MainWindow).GetField(name, flags)!.SetValue(window, value);
        void Update() => typeof(MainWindow).GetMethod("UpdateCurrentCalibrationControls", flags)!.Invoke(window, null);
        var save = (Button)Field("_currentCalibrationZeroSave");
        if (save.Parent is null) typeof(MainWindow).GetMethod("AddParameterTab", flags)!.Invoke(window, null);
        Check(save.Parent is not null && !save.IsEnabled, "Zero-only entry must be discoverable and initially disabled");
        var transport = new CalibrationTransport("D014", 3000000);
        var client = new BmsClient(transport);
        var session = client.BeginCurrentCalibrationAsync(CancellationToken.None).GetAwaiter().GetResult();
        Set("_bms", client); Set("_currentCalibrationOwner", client); Set("_currentCalibrationSession", session);
        session.Zero = new CurrentCalibrationSamples(230, 230, 230, 0, 1, DateTimeOffset.UtcNow);
        Update();
        Check(save.IsEnabled && !((Button)Field("_currentCalibrationSave")).IsEnabled, "Zero-only save needs no loaded point");
        ((TextBox)Field("_currentCalibrationReference")).Text = "invalid";
        Check(save.IsEnabled, "Zero-only save must not require reference input");
        session.Loaded = session.Zero; session.Candidate = new CurrentCalibrationCoefficients(230, 1100000); Update();
        Check(!save.IsEnabled && ((Button)Field("_currentCalibrationSave")).IsEnabled, "Loaded candidate uses the existing two-point save");
        Set("_currentCalibrationBusy", true); Update();
        Check(!save.IsEnabled && !((Button)Field("_currentCalibrationSave")).IsEnabled, "No concurrent save");
        Set("_currentCalibrationSession", null); Set("_currentCalibrationOwner", null); Set("_bms", null);
        client.EndCurrentCalibrationAsync(session).GetAwaiter().GetResult();
        client.DisposeAsync().AsTask().GetAwaiter().GetResult();
        window.Close(); app.Shutdown();
    }
}

// Only an in-memory Modbus peer. No serial/BLE object or hardware connection is created.
internal sealed class CalibrationTransport : IBmsTransport
{
    public CalibrationTransport(string product, uint gain) { Product = product; Parameters = new(17, gain); }
    private string Product { get; }
    public CurrentCalibrationCoefficients Parameters { get; set; }
    public bool IsConnected => true;
    public bool RequiresSerialWakeup => false;
    public string DiscoveryDescription => "in-memory calibration test";
    public event Action<ReadOnlyMemory<byte>>? DataReceived;
    public event Action<string>? ConnectionProgress { add { } remove { } }
    public bool Capability = true, InvalidSample, MismatchReadback;
    public int RawMa = 230, Writes;
    public ushort[]? LastWritten;
    private uint _tick;
    public Task ReconnectAsync(CancellationToken ct = default) => Task.CompletedTask;
    public ValueTask DisposeAsync() => default;
    public Task WriteAsync(ReadOnlyMemory<byte> memory, CancellationToken ct = default)
    {
        ct.ThrowIfCancellationRequested();
        var request = memory.ToArray();
        ModbusRtu.ValidateFrame(request);
        byte[] body;
        if (request[1] == 3)
        {
            ushort address = BinaryPrimitives.ReadUInt16BigEndian(request.AsSpan(2, 2));
            int count = BinaryPrimitives.ReadUInt16BigEndian(request.AsSpan(4, 2));
            ushort[] words = new ushort[count];
            if (address == 0x2e00) words = new ushort[] { 0xd008, 2, Capability ? (ushort)0x0400 : (ushort)0 };
            else if (address == 0x2e24)
                words = (MismatchReadback && Writes != 0 ? Parameters with { GainPpm = Parameters.GainPpm + 1 } : Parameters).Words;
            else if (address == 0x2e28)
            {
                _tick += 256;
                int final = Parameters.ApplyMilliampere(RawMa);
                words = new ushort[] { (ushort)RawMa, (ushort)((uint)RawMa >> 16), (ushort)final, (ushort)((uint)final >> 16), InvalidSample ? (ushort)0 : (ushort)1, (ushort)_tick, (ushort)(_tick >> 16) };
            }
            else if (address is BmsRegisters.Hardware or BmsRegisters.Serial or BmsRegisters.Software)
            {
                byte[] ascii = Encoding.ASCII.GetBytes(Product);
                for (int i = 0; i < ascii.Length; i++) words[i / 2] |= (ushort)(ascii[i] << (i % 2 == 0 ? 8 : 0));
            }
            body = new byte[3 + count * 2]; body[0] = 1; body[1] = 3; body[2] = (byte)(count * 2);
            for (int i = 0; i < count; i++) BinaryPrimitives.WriteUInt16BigEndian(body.AsSpan(3 + 2 * i, 2), words[i]);
        }
        else if (request[1] == 0x10)
        {
            if (request.Length != 17 || request[2] != 0x2e || request[3] != 0x24 || request[5] != 4 || request[6] != 8)
                throw new Exception("Unexpected calibration write layout");
            LastWritten = Enumerable.Range(0, 4).Select(i => BinaryPrimitives.ReadUInt16BigEndian(request.AsSpan(7 + 2 * i, 2))).ToArray();
            Parameters = CurrentCalibrationCoefficients.Decode(LastWritten); Writes++;
            body = request.Take(6).ToArray();
        }
        else if (request[1] == 0x42)
        {
            byte command = request[2];
            body = command == 1 ? new byte[] { 1, 0x42, 1, 0, 0x12, 0x34, 0, 60, 2, 0x35, 0x10 } :
                command == 2 ? new byte[] { 1, 0x42, 2, 0, 0x12, 0x34, 0, 60 } : new byte[] { 1, 0x42, command, 0 };
        }
        else throw new Exception("Unexpected request");
        DataReceived?.Invoke(ModbusRtu.Frame(body));
        return Task.CompletedTask;
    }
}
