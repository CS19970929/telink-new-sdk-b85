using BmsTool.Windows;
using System.Buffers.Binary;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;

static class Program
{
    const BindingFlags Private = BindingFlags.Instance | BindingFlags.NonPublic;
    static void Check(bool ok, string message) { if (!ok) throw new Exception(message); }

    [STAThread]
    static int Main(string[] args)
    {
        var app = new Application();
        var watchdog = new DispatcherTimer { Interval = TimeSpan.FromSeconds(45) };
        watchdog.Tick += (_, _) => { Console.Error.WriteLine("Calibration recovery test timed out (possible unexpected dialog)"); Environment.Exit(2); };
        watchdog.Start();
        SynchronizationContext.SetSynchronizationContext(new DispatcherSynchronizationContext());
        var frame = new DispatcherFrame();
        var task = RunAsync(args[0]);
        var dispatcher = Dispatcher.CurrentDispatcher;
        _ = task.ContinueWith(_ => dispatcher.BeginInvoke(() => frame.Continue = false), TaskScheduler.Default);
        Dispatcher.PushFrame(frame);
        watchdog.Stop();
        try { task.GetAwaiter().GetResult(); return 0; }
        catch (Exception ex) { Console.Error.WriteLine(ex); return 1; }
    }

    static async Task RunAsync(string firmware)
    {
        await using var transport = new CalibrationFirmwareTransport(firmware);
        await using var client = new BmsClient(transport);
        var session = await client.BeginCurrentCalibrationAsync(default);
        transport.DropHeartbeats = 1;
        await client.CheckCurrentCalibrationAsync(session, default);
        Check(transport.Dropped == 1 && !session.ConnectionLost && transport.Opens == 1,
            "丢失首个心跳后只重试同一token，不重新OPEN");
        Console.WriteLine("PASS lost heartbeat recovered; same firmware session retained");

        var zero = await client.CollectCurrentCalibrationAsync(session, true,
            new CurrentCalibrationPolicy(SampleCount: 4, TimeoutSeconds: 5), null, default);
        session.ZeroOnly = true;
        var candidate = CurrentCalibrationPolicy.Default.CalculateZeroOnly(zero, session.Original);
        await client.SaveCurrentCalibrationAsync(session, candidate, default);
        Check(session.SaveVerified && transport.CoefficientWrites == 1, "真实C固件仅写一次并核对新样本");
        bool busyExplained = false;
        try { await client.OpenAfeHardwareAccessAsync(); }
        catch (IOException ex) when (ex.Message.Contains("0x06") && ex.Message.Contains("授权")) { busyExplained = true; }
        Check(busyExplained, "已有授权占用时提供可读中文提示");
        await client.EndCurrentCalibrationAsync(session);

        var window = new MainWindow();
        void Set(string name, object? value) => typeof(MainWindow).GetField(name, Private)!.SetValue(window, value);
        object? Get(string name) => typeof(MainWindow).GetField(name, Private)!.GetValue(window);
        Task Finish() => (Task)typeof(MainWindow).GetMethod("FinishCurrentCalibrationAsync", Private)!.Invoke(window, null)!;
        var button = (Button)Get("_currentCalibrationZero")!;
        if (button.Parent is null) typeof(MainWindow).GetMethod("AddParameterTab", Private)!.Invoke(window, null);
        var messageMethod = typeof(MainWindow).GetMethod("CurrentCalibrationCancellationMessage", Private)!;
        Check(((string)messageMethod.Invoke(window, new object[] { true })!).Contains("45秒") &&
            ((string)messageMethod.Invoke(window, new object[] { false })!).Contains("10分钟"), "分别解释单步与总流程超时");
        void Attach(CurrentCalibrationSession value, CancellationTokenSource cancel)
        {
            Set("_bms", client); Set("_bmsTransport", transport); Set("_currentCalibrationOwner", client);
            Set("_currentCalibrationSession", value); Set("_currentCalibrationRecord", value);
            Set("_currentCalibrationCts", cancel); Set("_parameterBusy", true); Set("_diagBusy", true);
            Set("_deviceDisplaysCleared", false); Set("_currentCalibrationWindowClosed", false);
            var reason = typeof(MainWindow).GetField("_currentCalibrationStopReason", Private)!;
            reason.SetValue(window, Enum.Parse(reason.FieldType, "None"));
        }
        try
        {
            session = await client.BeginCurrentCalibrationAsync(default);
            using var cancel = new CancellationTokenSource();
            Attach(session, cancel);
            Func<CancellationToken, Task> noWork = _ => Task.CompletedTask;
            await (Task)typeof(MainWindow).GetMethod("RunCurrentCalibrationStepAsync", Private)!.Invoke(window, new object[] { noWork })!;
            var freshness = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(100) };
            freshness.Tick += (_, _) => typeof(MainWindow).GetMethod("CheckDeviceDisplayFreshness", Private)!.Invoke(window, null);
            int heartbeats = transport.Heartbeats;
            freshness.Start();
            await Task.Delay(9300);
            freshness.Stop();
            Check(ReferenceEquals(Get("_currentCalibrationOwner"), client) && !cancel.IsCancellationRequested &&
                !session.ConnectionLost && transport.Heartbeats >= heartbeats + 3,
                "等候接负载超过8秒仍维持校准，不误判断线");
            Console.WriteLine("PASS real UI timer holds calibration for >8s without ordinary polling");

            int closes = transport.Closes;
            typeof(BmsClient).GetField("_lastSuccessfulResponseTick", Private)!.SetValue(client,
                Stopwatch.GetTimestamp() - 9L * Stopwatch.Frequency);
            typeof(MainWindow).GetMethod("CheckDeviceDisplayFreshness", Private)!.Invoke(window, null);
            Check(cancel.IsCancellationRequested, "真实通信超时仍须停止校准并清空显示");
            Set("_currentCalibrationWindowClosed", true);
            await Finish();
            Check(transport.Closes == closes + 1 && session.ConnectionLost,
                "超时后先验证再释放原会话，旧候选不可恢复使用");
            session = await client.BeginCurrentCalibrationAsync(default);
            Console.WriteLine("PASS stale display cleanup releases authorization; next OPEN succeeds immediately");
            await client.EndCurrentCalibrationAsync(session);

            session = await client.BeginCurrentCalibrationAsync(default);
            using var userCancel = new CancellationTokenSource();
            Attach(session, userCancel);
            var started = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            Func<CancellationToken, Task> waiting = async ct => { started.SetResult(); await Task.Delay(Timeout.Infinite, ct); };
            var running = (Task)typeof(MainWindow).GetMethod("RunCurrentCalibrationStepAsync", Private)!.Invoke(window, new object[] { waiting })!;
            await started.Task;
            closes = transport.Closes;
            await (Task)typeof(MainWindow).GetMethod("CancelCurrentCalibrationAsync", Private)!.Invoke(window, new object[] { "用户取消校准" })!;
            await running;
            ((DispatcherTimer)Get("_pollTimer")!).Stop();
            Check(transport.Closes == closes + 1 && Get("_currentCalibrationOwner") is null &&
                ((TextBlock)Get("_currentCalibrationStatus")!).Text.Contains("用户"), "主动取消正常释放且不弹错误对话框");
            Console.WriteLine("PASS user cancellation completes without an error dialog and releases the session");

            session = await client.BeginCurrentCalibrationAsync(default);
            using var resetCancel = new CancellationTokenSource();
            Attach(session, resetCancel);
            typeof(BmsClient).GetField("_lastSuccessfulResponseTick", Private)!.SetValue(client,
                Stopwatch.GetTimestamp() - 9L * Stopwatch.Frequency);
            typeof(MainWindow).GetMethod("CheckDeviceDisplayFreshness", Private)!.Invoke(window, null);
            Set("_currentCalibrationWindowClosed", true);
            transport.ResetSampleClock = true;
            closes = transport.Closes;
            await Finish();
            Check(session.ConnectionLost && transport.Closes == closes, "重启/采样时钟不连续时不发送旧token CLOSE");
            Check(transport.CoefficientWrites == 1, "失败、清理和等待均不追加系数写入");
            Console.WriteLine("PASS reset evidence suppresses old-token CLOSE; no calibration write retried");
        }
        finally
        {
            Set("_bms", null); Set("_bmsTransport", null); Set("_currentCalibrationOwner", null);
            Set("_currentCalibrationSession", null); Set("_currentCalibrationCts", null);
            window.Close();
        }
    }
}

sealed class CalibrationFirmwareTransport : IBmsTransport
{
    readonly Process process;
    readonly Stopwatch sinceCommand = Stopwatch.StartNew();
    public int DropHeartbeats, Dropped, Heartbeats, Closes, CoefficientWrites, Opens;
    public bool ResetSampleClock;
    public bool IsConnected => !process.HasExited;
    public string DiscoveryDescription => "actual C modules; simulated lost wake frame and reset evidence";
    public event Action<ReadOnlyMemory<byte>>? DataReceived;
    public event Action<string>? ConnectionProgress { add { } remove { } }
    public Task ReconnectAsync(CancellationToken ct = default) => Task.CompletedTask;
    public CalibrationFirmwareTransport(string executable)
    {
        var start = new ProcessStartInfo(executable) { RedirectStandardInput = true, RedirectStandardOutput = true,
            RedirectStandardError = true, UseShellExecute = false, CreateNoWindow = true };
        start.ArgumentList.Add("--pc-server"); process = Process.Start(start)!;
    }
    async Task<byte[]> Exchange(byte[] request, CancellationToken ct)
    {
        await process.StandardInput.WriteLineAsync(Convert.ToHexString(request));
        await process.StandardInput.FlushAsync(ct);
        string? line = await process.StandardOutput.ReadLineAsync(ct).AsTask().WaitAsync(TimeSpan.FromSeconds(3), ct);
        if (string.IsNullOrEmpty(line)) throw new IOException("C firmware server ended");
        return Convert.FromHexString(line);
    }
    public async Task WriteAsync(ReadOnlyMemory<byte> memory, CancellationToken ct = default)
    {
        byte[] frame = memory.ToArray();
        if (frame[1] == 0x42 && frame[2] == 1) Opens++;
        if (frame[1] == 0x42 && frame[2] == 2)
        {
            Heartbeats++;
            if (DropHeartbeats > 0) { DropHeartbeats--; Dropped++; return; }
        }
        if (frame[1] == 0x42 && frame[2] == 3) Closes++;
        if (frame[1] == 0x10 && BinaryPrimitives.ReadUInt16BigEndian(frame.AsSpan(2)) == 0x2E24) CoefficientWrites++;
        // C harness每个请求推进100ms；等待期间以无副作用读推进模型时间，避免把主机等待误判为MCU复位。
        int steps = Math.Min(100, (int)(sinceCommand.ElapsedMilliseconds / 100));
        for (int i = 0; i < steps; i++) await Exchange(ModbusRtu.ReadHolding(0xD130, 2), ct);
        byte[] reply = await Exchange(frame, ct);
        sinceCommand.Restart();
        if (ResetSampleClock && frame[1] == 3 && BinaryPrimitives.ReadUInt16BigEndian(frame.AsSpan(2)) == 0x2E28)
        {
            Array.Clear(reply, 13, 4);
            reply = ModbusRtu.Frame(reply.AsSpan(0, reply.Length - 2));
        }
        for (int i = 0; i < reply.Length; i += 7) DataReceived?.Invoke(reply.AsMemory(i, Math.Min(7, reply.Length - i)));
    }
    public async ValueTask DisposeAsync()
    {
        process.StandardInput.Close();
        await process.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(3));
        process.Dispose();
    }
}
