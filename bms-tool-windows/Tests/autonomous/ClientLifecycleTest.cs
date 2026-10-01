using System.Buffers.Binary;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;

namespace BmsTool.Windows;

public static class TestMessageBox
{
    public static MessageBoxResult Answer = MessageBoxResult.Yes;
    public static Action? OnShow;
    public static MessageBoxResult Show(string message, string title, MessageBoxButton buttons, MessageBoxImage image)
    { OnShow?.Invoke(); return Answer; }
}

// Only WPF scaffolding and unrelated window services are replaced. AFE event
// handlers, model, DisposeBmsAsync, Modbus bytes and transaction code are real.
public partial class MainWindow
{
    private enum ConnectionMode { Ble, Serial }
    private ConnectionMode _connectionMode = ConnectionMode.Serial;
    private BmsClient? _bms;
    private IBmsTransport? _bmsTransport;
    private D008ParameterCapture? _parameterCapture;
    private bool _parameterBusy, _diagBusy, _otaRunning, _eventLogReadInProgress, _shBusy, _autoReconnectRunning;
    private readonly TextBlock _parameterStatus = new();
    private bool ShFactoryBusy() => false;
    private void ShowD008ParameterCapture(D008ParameterCapture capture) => _parameterStatus.Text = capture.Status;
    public int RefreshStarts;
    public string ParameterStatus { get => _parameterStatus.Text; set => _parameterStatus.Text = value; }
    public Task RunParameterAsync(Func<BmsClient, CancellationToken, Task> action)
    { _parameterProbedClient = _bms; _parameterCapture = new() { Supported = true }; return ParameterRunAsync(action, false); }
    private BmsClient? _parameterProbedClient;
    private ulong? _connectedAddress;
    private string? _connectedSerialPort;
    private readonly TestLog _sessionLog = new();
    private readonly TextBlock ConnectionText = new();
    private Dispatcher Dispatcher => Dispatcher.CurrentDispatcher;
    public BatterySnapshot? Battery;
    private readonly List<ProtectionParameterRow> _protectionRows = new(ProtectionParameterCatalog.Create());
    private readonly TextBlock ProtectionStatusText = new();
    private readonly TextBlock FactoryParameterSummaryText = new();
    public void ReadProtection() => ReadProtection_Click(this, new RoutedEventArgs());
    public ushort ProtectionMarker => _protectionRows[0].DeviceValue;
    private readonly DispatcherTimer _pollTimer = new();
    private readonly TabControl MainTabs = new();
#if !BMS_FACTORY_APP
    private bool _protectedFeaturesUnlocked = true;
#endif
    public readonly List<Exception> Errors = new();
#if BMS_FACTORY_APP
    private readonly AfeHardwareParameterModel _afeHardwareModel = new();
    private DataGrid? _afeHardwareGrid;
    private TextBlock? _afeHardwareStatus;
#endif
    public MainWindow() { AddAfeHardwareTab(); }
    private Task WaitForCommunicationIdleAsync() => Task.CompletedTask;
    private void StartAutomaticRefresh() { RefreshStarts++; }
    private void OnProtocolLog(string message) { }
    private void AppendLog(string message, string category) { }
    private void ShowError(string title, Exception error, string? details = null) => Errors.Add(error);
    public async Task ReplaceAsync(FakeTransport? transport)
    {
        await DisposeBmsAsync();
        _bmsTransport = transport;
        _bms = transport is null ? null : new BmsClient(transport);
    }
    public void Read() => ReadAfeHardware_Click(this, new RoutedEventArgs());
    public void Apply() => ApplyAfeHardware_Click(this, new RoutedEventArgs());
    public bool ReadComplete => _afeHardwareStatus?.Text.StartsWith("读取完成") == true;
    public bool SaveComplete => _afeHardwareStatus?.Text.StartsWith("保存成功") == true;
    public void Edit() => _afeHardwareModel.Rows.Single(x => x.WireIndex == 23).EditValue = "128";
    public string Status => _afeHardwareStatus?.Text ?? "";
    public ushort SnapshotMarker => _afeHardwareModel.RequestedRaw[24];
    public bool Busy => (bool?)GetType().GetField("_afeOperationRunning", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance)?.GetValue(this) == true;
    public IBmsTransport? CurrentTransport => _bmsTransport;
    public Task<bool> ConnectBleAsync(ulong address) => ConnectBmsInternalAsync(address, allowIapOnly: true);
    public Task ConnectSerialAsync(string port) => ConnectSerialInternalAsync(port, 115200);
    public Task RefreshAsync() => RefreshBatteryAsync();
    private void ApplyBatterySnapshot(BatterySnapshot snapshot) => Battery = snapshot;
}

public sealed class TestLog { public void WriteException(string category, string operation, Exception error) { } }

public class FakeTransport : IBmsTransport
{
    public bool Connected = true;
    public bool IsConnected => Connected;
    public string DiscoveryDescription => "isolated fake; no hardware";
    public event Action<ReadOnlyMemory<byte>>? DataReceived;
    public event Action<string>? ConnectionProgress { add { } remove { } }
    public ushort[] Profile = new ushort[35];
    public int Writes, Requests;
    public bool HoldResponses;
    public bool Disposed;
    public TaskCompletionSource? WriteCompletion;
    public Action? BeforeResponse;
    private readonly Queue<Action> _responses = new();
    public void ReleaseResponses()
    {
        HoldResponses = false;
        while (_responses.TryDequeue(out var respond)) respond();
    }
    public FakeTransport(ushort marker)
    {
        Profile[0] = 1; Profile[1] = 0x3510; Profile[22] = 300;
        Profile[23] = 256; Profile[24] = marker; Profile[34] = 64;
    }
    public Task ReconnectAsync(CancellationToken ct = default) => Task.CompletedTask;
    public ValueTask DisposeAsync() { Disposed = true; return ValueTask.CompletedTask; }
    public Task WriteAsync(ReadOnlyMemory<byte> data, CancellationToken ct = default)
    {
        ct.ThrowIfCancellationRequested(); Requests++;
        var q = data.ToArray(); byte[] response;
        if (q[1] == 3)
        {
            ushort start = BinaryPrimitives.ReadUInt16BigEndian(q.AsSpan(2));
            ushort count = BinaryPrimitives.ReadUInt16BigEndian(q.AsSpan(4));
            var words = start switch {
                0x2523 => new ushort[] { 64, 1, 667, 8, 32, 0, 1, 0, 2 },
                0x2100 => Enumerable.Repeat(Profile[24], 65).ToArray(),
                0xD000 => new ushort[63], 0xD115 => new ushort[2],
                0xD120 => new ushort[] { 0x4253, 1, Profile[24], 0, 50, 650, 650, 650, 3300, 3300, 0 },
                _ => Profile
            };
            response = new byte[3 + 2 * count]; response[0] = 1; response[1] = 3; response[2] = (byte)(2 * count);
            for (int i = 0; i < count; i++) BinaryPrimitives.WriteUInt16BigEndian(response.AsSpan(3 + 2 * i), words[i]);
        }
        else if (q[1] == 0x10)
        {
            Writes++;
            for (int i = 0; i < Profile.Length; i++) Profile[i] = BinaryPrimitives.ReadUInt16BigEndian(q.AsSpan(7 + 2 * i));
            response = q[..6];
        }
        else if (q[1] == 0x42 && q[2] == 1)
            response = new byte[] { 1, 0x42, 1, 0, 0, 7, 0, 60, 1, 0x35, 0x10 };
        else response = new byte[] { 1, 0x42, q[2], 0 };
        var receiver = DataReceived;
        var frame = ModbusRtu.Frame(response);
        BeforeResponse?.Invoke();
        if (HoldResponses) _responses.Enqueue(() => receiver?.Invoke(frame));
        else receiver?.Invoke(frame);
        return WriteCompletion?.Task ?? Task.CompletedTask;
    }
}

// OS-facing connect APIs are fake; production MainWindow connect methods are
// extracted verbatim and still perform their real probe/ownership checks.
public sealed class BmsBleTransport : FakeTransport
{
    public static readonly List<BmsBleTransport> Created = new();
    public static bool HoldNext;
    public readonly TaskCompletionSource ConnectedTask = new(TaskCreationOptions.RunContinuationsAsynchronously);
    public BmsBleTransport() : base(2000) { Created.Add(this); if (!HoldNext) ConnectedTask.SetResult(); HoldNext = false; }
    public Task ConnectAsync(ulong address, CancellationToken ct) => ConnectedTask.Task;
}
public sealed class BmsSerialTransport : FakeTransport
{
    public static readonly List<BmsSerialTransport> Created = new();
    public static bool HoldNext;
    public readonly TaskCompletionSource ConnectedTask = new(TaskCreationOptions.RunContinuationsAsynchronously);
    public BmsSerialTransport() : base(2000) { Created.Add(this); if (!HoldNext) ConnectedTask.SetResult(); HoldNext = false; }
    public Task ConnectAsync(string port, int baud, CancellationToken ct) => ConnectedTask.Task;
}

internal static class Test
{
    private static int _failures;
    [STAThread]
    private static int Main()
    {
        SynchronizationContext.SetSynchronizationContext(new DispatcherSynchronizationContext());
        RunAsync().ContinueWith(task => Dispatcher.CurrentDispatcher.BeginInvokeShutdown(DispatcherPriority.Background),
            CancellationToken.None, TaskContinuationOptions.None, TaskScheduler.FromCurrentSynchronizationContext());
        Dispatcher.Run();
        return _failures == 0 ? 0 : 1;
    }
    private static async Task Until(Func<bool> ready)
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(8));
        while (!ready()) await Task.Delay(1, timeout.Token);
    }
    private static async Task Case(string name, Func<Task> run)
    {
        try { await run(); Console.WriteLine("PASS " + name); }
        catch (Exception ex) { _failures++; Console.WriteLine("FAIL " + name + ": " + ex.Message); }
    }
    private static void Check(bool condition, string message)
    { if (!condition) throw new Exception(message); }
    private static async Task RunAsync()
    {
        await Case("CF01 A-read B-connect must send zero writes", async () =>
        {
            var window = new MainWindow(); var a = new FakeTransport(2000); var b = new FakeTransport(5000);
            await window.ReplaceAsync(a); window.Read(); await Until(() => window.ReadComplete || window.Errors.Count > 0);
            Check(window.Errors.Count == 0, "initial read failed"); window.Edit();
            await window.ReplaceAsync(b); window.Apply();
            await Until(() => window.SaveComplete || window.Errors.Count > 0);
            Check(b.Writes == 0, $"stale A profile written to B: writes={b.Writes}, B.word24={b.Profile[24]}");
            await window.ReplaceAsync(null);
        });
        await Case("repeat read is one operation", async () =>
        {
            var window = new MainWindow(); var transport = new FakeTransport(2000) { HoldResponses = true };
            await window.ReplaceAsync(transport); window.Read(); window.Read();
            transport.ReleaseResponses(); await Until(() => window.ReadComplete || window.Errors.Count > 0);
            await Task.Delay(50);
            Check(transport.Requests == 3, $"expected one 3-read snapshot, requests={transport.Requests}");
            await window.ReplaceAsync(null);
        });
        await Case("dispose cancels pending and queued transactions", async () =>
        {
            var transport = new FakeTransport(2000) { HoldResponses = true }; var client = new BmsClient(transport);
            var pending = client.ReadRegistersAsync(0x2500, 35);
            var queued = client.ReadRegistersAsync(0x2500, 35);
            await client.DisposeAsync();
            await Task.Delay(50);
            Check(pending.IsCompleted && queued.IsCompleted, "dispose left transactions pending");
            foreach (var operation in new[] { pending, queued })
            {
                try { await operation; throw new Exception("disposed read succeeded"); }
                catch (OperationCanceledException) { }
            }
            transport.ReleaseResponses();
            Check(transport.Requests == 1, "queued request reached old transport");
            await client.DisposeAsync();
        });
        await Case("fresh read on B permits one B-owned save", async () =>
        {
            var window = new MainWindow(); var a = new FakeTransport(2000); var b = new FakeTransport(5000);
            await window.ReplaceAsync(a); window.Read(); await Until(() => window.ReadComplete); window.Edit();
            await window.ReplaceAsync(b); window.Read(); await Until(() => window.ReadComplete); window.Edit();
            b.HoldResponses = true; window.Apply(); window.Apply(); b.ReleaseResponses();
            await Until(() => window.SaveComplete || window.Errors.Count > 0); await Task.Delay(20);
            Check(window.Errors.Count == 0 && b.Writes == 1 && b.Profile[24] == 5000 && b.Profile[23] == 128,
                $"reread/repeat save mismatch writes={b.Writes}; errors={window.Errors.Count}");
            await window.ReplaceAsync(null);
        });
        await Case("cancel confirmation sends nothing and permits later save", async () =>
        {
            var window = new MainWindow(); var transport = new FakeTransport(2000);
            await window.ReplaceAsync(transport); window.Read(); await Until(() => window.ReadComplete); window.Edit();
            int before = transport.Requests; TestMessageBox.Answer = MessageBoxResult.No;
            window.Apply(); TestMessageBox.Answer = MessageBoxResult.Yes;
            Check(transport.Requests == before && transport.Writes == 0, "cancel sent protocol bytes");
            window.Apply(); await Until(() => window.SaveComplete || window.Errors.Count > 0);
            Check(window.SaveComplete && transport.Writes == 1, "cancel blocked later deliberate save");
            await window.ReplaceAsync(null);
        });
        await Case("connection changes inside confirmation must cancel candidate", async () =>
        {
            var window = new MainWindow(); var a = new FakeTransport(2000); var b = new FakeTransport(5000);
            await window.ReplaceAsync(a); window.Read(); await Until(() => window.ReadComplete); window.Edit();
            TestMessageBox.OnShow = () => window.ReplaceAsync(b).GetAwaiter().GetResult();
            window.Apply(); TestMessageBox.OnShow = null; await Task.Delay(20);
            Check(a.Writes == 0 && b.Writes == 0, "confirmation crossed connection generation");
            await window.ReplaceAsync(null);
        });
        await Case("late old response cannot replace B snapshot or status", async () =>
        {
            var window = new MainWindow(); var a = new FakeTransport(2000) { HoldResponses = true }; var b = new FakeTransport(5000);
            await window.ReplaceAsync(a); window.Read(); await window.ReplaceAsync(b);
            await Until(() => !window.Busy); window.Read(); await Until(() => window.ReadComplete);
            string status = window.Status; a.ReleaseResponses(); await Task.Delay(30);
            Check(window.Errors.Count == 0 && window.Status == status && window.SnapshotMarker == 5000,
                "old response/cancellation published into B");
            await window.ReplaceAsync(null);
        });
        await Case("physical disconnect rejects stale snapshot", async () =>
        {
            var window = new MainWindow(); var transport = new FakeTransport(2000);
            await window.ReplaceAsync(transport); window.Read(); await Until(() => window.ReadComplete); window.Edit();
            transport.Connected = false; int before = transport.Requests; window.Apply();
            await Until(() => window.Errors.Count > 0 || window.SaveComplete);
            Check(transport.Requests == before, "disconnected snapshot sent traffic");
            await window.ReplaceAsync(null);
        });
        await Case("canceled transaction and disposed write reject further bytes", async () =>
        {
            var transport = new FakeTransport(2000) { HoldResponses = true }; var client = new BmsClient(transport);
            using var cancel = new CancellationTokenSource(); var read = client.ReadRegistersAsync(0x2500, 35, cancel.Token);
            cancel.Cancel();
            try { await read; throw new Exception("cancel ignored"); } catch (OperationCanceledException) { }
            await client.DisposeAsync(); int before = transport.Requests;
            try { await client.WriteSingleRegisterAsync(0x1005, 50); throw new Exception("disposed write accepted"); }
            catch (OperationCanceledException) { }
            transport.ReleaseResponses(); Check(transport.Requests == before, "disposed client wrote bytes");
        });
        await Case("dispose bounds transport write ignoring cancellation", async () =>
        {
            var transport = new FakeTransport(2000) { HoldResponses = true, WriteCompletion = new() };
            var client = new BmsClient(transport); var read = client.ReadRegistersAsync(0x2500, 35);
            await client.DisposeAsync(); await Task.Delay(50);
            Check(read.IsCompleted, "ignored transport cancellation blocked disposal");
            try { await read; throw new Exception("disposed read accepted"); } catch (OperationCanceledException) { }
            transport.WriteCompletion.SetResult(); transport.ReleaseResponses();
        });
        await Case("late BLE connection cannot overwrite new connection", async () =>
        {
            var window = new MainWindow(); BmsBleTransport.HoldNext = true; var old = window.ConnectBleAsync(1);
            var oldTransport = BmsBleTransport.Created.Last(); await window.ConnectBleAsync(2);
            var current = window.CurrentTransport; oldTransport.ConnectedTask.SetResult();
            try { await old; throw new Exception("old connect succeeded"); } catch (OperationCanceledException) { }
            Check(ReferenceEquals(current, window.CurrentTransport) && oldTransport.Disposed, "late BLE replaced or disposed B");
            await window.ReplaceAsync(null);
        });
        await Case("late serial connection cannot overwrite new connection", async () =>
        {
            var window = new MainWindow(); BmsSerialTransport.HoldNext = true; var old = window.ConnectSerialAsync("A");
            var oldTransport = BmsSerialTransport.Created.Last(); await window.ConnectSerialAsync("B");
            var current = window.CurrentTransport; oldTransport.ConnectedTask.SetResult();
            try { await old; throw new Exception("old connect succeeded"); } catch (OperationCanceledException) { }
            Check(ReferenceEquals(current, window.CurrentTransport) && oldTransport.Disposed, "late serial replaced or disposed B");
            await window.ReplaceAsync(null);
        });
        await Case("late parameter failure cannot publish status or restart B polling", async () =>
        {
            var window = new MainWindow(); await window.ReplaceAsync(new FakeTransport(2000));
            var completion = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            var old = window.RunParameterAsync((client, ct) => completion.Task);
            await window.ReplaceAsync(new FakeTransport(5000)); window.ParameterStatus = "B status";
            int starts = window.RefreshStarts; completion.SetException(new InvalidOperationException("old A failure"));
            await old;
            Check(window.ParameterStatus == "B status" && window.RefreshStarts == starts,
                "old parameter catch/finally changed B status or polling");
            await window.RunParameterAsync((client, ct) => Task.CompletedTask);
            Check(window.RefreshStarts == starts + 1, "new parameter operation stayed busy");
            await window.ReplaceAsync(null);
        });
        await Case("late battery response cannot replace B measurement", async () =>
        {
            var window = new MainWindow(); var a = new FakeTransport(2000) { HoldResponses = true };
            await window.ReplaceAsync(a); var old = window.RefreshAsync();
            await window.ReplaceAsync(new FakeTransport(5000)); await window.RefreshAsync();
            var current = window.Battery; a.ReleaseResponses();
            try { await old; throw new Exception("old battery accepted"); } catch (OperationCanceledException) { }
            Check(ReferenceEquals(current, window.Battery), "old battery replaced B measurement");
            await window.ReplaceAsync(null);
        });
        await Case("caller cancellation keeps gate until uncooperative write exits", async () =>
        {
            var transport = new FakeTransport(2000) { HoldResponses = true, WriteCompletion = new() };
            var client = new BmsClient(transport); using var cancel = new CancellationTokenSource();
            var first = client.ReadRegistersAsync(0x2500, 35, cancel.Token); cancel.Cancel();
            var second = client.ReadRegistersAsync(0x2500, 35); await Task.Delay(30);
            bool serialized = transport.Requests == 1 && !first.IsCompleted;
            transport.ReleaseResponses(); transport.WriteCompletion.SetResult();
            try { await first; throw new Exception("caller cancel ignored"); } catch (OperationCanceledException) { }
            await second; await client.DisposeAsync();
            Check(serialized, "caller cancel released gate while previous write was in flight");
        });
        await Case("late protection read entry cannot show old error or restart B polling", async () =>
        {
            var window = new MainWindow(); var a = new FakeTransport(2000) { HoldResponses = true };
            await window.ReplaceAsync(a); window.ReadProtection();
            await window.ReplaceAsync(new FakeTransport(5000)); window.ReadProtection();
            await Task.Delay(50); a.ReleaseResponses(); await Task.Delay(30);
            Check(window.Errors.Count == 0 && window.RefreshStarts == 1 && window.ProtectionMarker == 5000,
                $"old entry published: errors={window.Errors.Count}, polling starts={window.RefreshStarts}");
            await window.ReplaceAsync(null);
        });
        Console.WriteLine($"RESULT failures={_failures}");
    }
}
