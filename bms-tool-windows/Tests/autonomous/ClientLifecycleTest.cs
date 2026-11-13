using System.Buffers.Binary;
using System.Diagnostics;
using System.Reflection;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
using BmsTool.Windows;

static class ClientLifecycleTest
{
    const BindingFlags Private = BindingFlags.Instance | BindingFlags.NonPublic;
    static int checks;
    static void Check(bool value, string reason) { checks++; if (!value) throw new Exception(reason); }
    static void PumpUntil(Func<bool> done)
    {
        var limit = Stopwatch.StartNew();
        while (!done())
        {
            if (limit.Elapsed > TimeSpan.FromSeconds(6)) throw new TimeoutException("test operation did not finish");
            var frame = new DispatcherFrame();
            var timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(10) };
            timer.Tick += (_, _) => frame.Continue = false;
            timer.Start(); Dispatcher.PushFrame(frame); timer.Stop();
        }
    }
    static void Wait(Task task) { PumpUntil(() => task.IsCompleted); task.GetAwaiter().GetResult(); }
    static void Cancelled(Task task)
    {
        PumpUntil(() => task.IsCompleted);
        try { task.GetAwaiter().GetResult(); throw new Exception("cancelled client request succeeded"); }
        catch (OperationCanceledException) { checks++; }
    }
    [STAThread] static int Main()
    {
        var app = new Application();
        var blocked = new Peer { HoldWrite = true };
        var disposedClient = new BmsClient(blocked);
        Task pending = disposedClient.ReadRegistersAsync(0xD115, 2);
        PumpUntil(() => blocked.Writes == 1);
        Task queued = disposedClient.ReadRegistersAsync(0xD115, 2);
        Wait(disposedClient.DisposeAsync().AsTask());
        Cancelled(pending); Cancelled(queued);
        Check(blocked.Writes == 1, "disposing a client must prevent a queued physical write");
        blocked.Release();
        Cancelled(disposedClient.ReadRegistersAsync(0xD115, 2));
        Wait(disposedClient.DisposeAsync().AsTask());

        var serialised = new Peer { HoldWrite = true };
        var serialClient = new BmsClient(serialised);
        using var caller = new CancellationTokenSource();
        Task first = serialClient.ReadRegistersAsync(0xD115, 2, caller.Token);
        PumpUntil(() => serialised.Writes == 1);
        caller.Cancel();
        Task<ushort[]> second = serialClient.ReadRegistersAsync(0xD115, 2);
        var pause = Stopwatch.StartNew(); PumpUntil(() => pause.ElapsedMilliseconds >= 60);
        Check(serialised.Writes == 1 && !first.IsCompleted,
            "caller cancellation must not overlap an uncancelable transport write");
        serialised.Release(); Cancelled(first); Wait(second);
        Check(serialised.Writes == 2 && second.Result[0] == 12, "next request resumes only after physical write completion");
        Wait(serialClient.DisposeAsync().AsTask());

        var window = new MainWindow();
        var type = typeof(MainWindow);
        void Set(string name, object? value) => type.GetField(name, Private)!.SetValue(window, value);
        object? Get(string name) => type.GetField(name, Private)!.GetValue(window);
        object? Call(string name, params object[] arguments) => type.GetMethod(name, Private)!.Invoke(window, arguments);
        void Reject(string name, BmsClient client, long generation)
        {
            try { Call(name, client, generation); throw new Exception("stale snapshot accepted"); }
            catch (TargetInvocationException ex) when (ex.InnerException is OperationCanceledException or InvalidOperationException) { checks++; }
        }
        var originalTransport = new Peer();
        var original = new BmsClient(originalTransport);
        Set("_bms", original); Set("_bmsTransport", originalTransport);
        long generation = (long)Get("_connectionGeneration")!;
        Reject("EnsureProtectionSnapshot", original, generation);
        Reject("EnsureAfeSnapshot", original, generation);
        Set("_protectionSnapshotClient", original); Set("_protectionSnapshotGeneration", generation);
        Set("_afeHardwareSource", original); Set("_afeSnapshotGeneration", generation);
        Call("EnsureProtectionSnapshot", original, generation);
        Call("EnsureAfeSnapshot", original, generation);
        Call("InvalidateConnectionState");
        Reject("EnsureProtectionSnapshot", original, generation);
        Reject("EnsureAfeSnapshot", original, generation);
        Check(Get("_parameterCapture") is null && Get("_protectionSnapshotClient") is null && Get("_afeHardwareSource") is null,
            "connection replacement must invalidate all editable source snapshots");

        var batteryTransport = new Peer { HoldReply = true };
        var batteryClient = new BmsClient(batteryTransport);
        Set("_bms", batteryClient); Set("_bmsTransport", batteryTransport);
        Task batteryRead = (Task)Call("RefreshBatteryAsync", CancellationToken.None)!;
        PumpUntil(() => batteryTransport.Writes == 1);
        Call("InvalidateConnectionState");
        var replacementTransport = new Peer();
        var replacement = new BmsClient(replacementTransport);
        Set("_bms", replacement); Set("_bmsTransport", replacementTransport);
        ((TextBlock)window.FindName("PackVoltageText")).Text = "NEW SESSION";
        batteryTransport.Release(); Wait(batteryRead);
        Check(((TextBlock)window.FindName("PackVoltageText")).Text == "NEW SESSION",
            "an old battery read must not publish into a new connection");

        var protectionTransport = new Peer { HoldReply = true };
        var protectionClient = new BmsClient(protectionTransport);
        Set("_bms", protectionClient); Set("_bmsTransport", protectionTransport);
        Task protectionRead = (Task)Call("ReadProtectionAsync")!;
        PumpUntil(() => protectionTransport.Writes == 1);
        Call("InvalidateConnectionState");
        Set("_bms", replacement); Set("_bmsTransport", replacementTransport);
        ((TextBlock)window.FindName("ProtectionStatusText")).Text = "NEW PARAMETERS";
        protectionTransport.Release(); Cancelled(protectionRead);
        Check(((TextBlock)window.FindName("ProtectionStatusText")).Text == "NEW PARAMETERS",
            "an old protection read must not replace new status or snapshot ownership");
        Set("_bms", null); Set("_bmsTransport", null);
        Call("ClearDeviceDisplays");
        window.Close(); app.Shutdown();
        Wait(original.DisposeAsync().AsTask()); Wait(batteryClient.DisposeAsync().AsTask());
        Wait(protectionClient.DisposeAsync().AsTask()); Wait(replacement.DisposeAsync().AsTask());
        Console.WriteLine($"PASS real client lifecycle: {checks} transport cancellation, stale-read and snapshot-ownership checks; no hardware IO");
        return 0;
    }
    sealed class Peer : IBmsTransport
    {
        public bool HoldWrite, HoldReply;
        public int Writes;
        Action? reply;
        readonly TaskCompletionSource completion = new(TaskCreationOptions.RunContinuationsAsynchronously);
        public bool IsConnected => true;
        public string DiscoveryDescription => "client lifecycle peer";
        public event Action<ReadOnlyMemory<byte>>? DataReceived;
        public event Action<string>? ConnectionProgress { add {} remove {} }
        public Task ReconnectAsync(CancellationToken ct = default) => Task.CompletedTask;
        public ValueTask DisposeAsync() => ValueTask.CompletedTask;
        public void Release() { var action = reply; reply = null; action?.Invoke(); completion.TrySetResult(); }
        public Task WriteAsync(ReadOnlyMemory<byte> data, CancellationToken ct)
        {
            Interlocked.Increment(ref Writes);
            byte[] request = data.ToArray();
            if (request[1] != 3) throw new Exception("lifecycle test must not issue device writes");
            ushort address = BinaryPrimitives.ReadUInt16BigEndian(request.AsSpan(2));
            ushort count = BinaryPrimitives.ReadUInt16BigEndian(request.AsSpan(4));
            ushort[] words = new ushort[count];
            if (address == 0xD000) { Array.Fill(words, (ushort)61001, 0, 32); words[0] = 3500; }
            if (address == 0xD115) words[0] = 12;
            if (address == 0x2100) words[0] = 4250;
            byte[] body = new byte[3 + count * 2]; body[0] = 1; body[1] = 3; body[2] = (byte)(count * 2);
            for (int i = 0; i < count; i++) BinaryPrimitives.WriteUInt16BigEndian(body.AsSpan(3 + i * 2), words[i]);
            byte[] frame = ModbusRtu.Frame(body);
            void Reply() => DataReceived?.Invoke(frame);
            if (HoldWrite) { HoldWrite = false; reply = Reply; return completion.Task; }
            if (HoldReply) { HoldReply = false; reply = Reply; return Task.CompletedTask; }
            Reply(); return Task.CompletedTask;
        }
    }
}
