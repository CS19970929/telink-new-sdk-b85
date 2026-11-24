using System.Text;
using Android.App;
using Android.Content;
using Android.Content.PM;
using Android.OS;
using Android.Provider;
using BmsTool.Windows;
using OperationCanceledException = System.OperationCanceledException;
using Uri = Android.Net.Uri;

namespace BmsTool.Android;

internal sealed record BatteryRecordingStatus(bool Active, bool Stopping, string Text, string? DocumentUri = null);

internal sealed class BatteryRecordingSession(
    BmsClient client, AndroidBmsBleTransport transport, SemaphoreSlim operationGate,
    string mac, string folderUri, int intervalSeconds, Func<bool> isCalibrating)
{
    private readonly CancellationTokenSource _stop = new();

    internal BmsClient Client { get; } = client;
    internal AndroidBmsBleTransport Transport { get; } = transport;
    internal SemaphoreSlim OperationGate { get; } = operationGate;
    internal string Mac { get; } = mac;
    internal string FolderUri { get; } = folderUri;
    internal string FileName { get; } = $"BMS_{mac.Replace(":", "")}_{DateTime.Now:yyyyMMdd_HHmmss_fff}.csv";
    internal int IntervalSeconds { get; } = intervalSeconds;
    internal Func<bool> IsCalibrating { get; set; } = isCalibrating;
    internal bool ActivityAttached { get; set; } = true;
    internal CancellationToken Token => _stop.Token;
    internal bool IsStopping => _stop.IsCancellationRequested;
    internal string StopReason { get; private set; } = "已停止";

    internal void Cancel(string reason)
    {
        if (IsStopping) return;
        StopReason = reason;
        _stop.Cancel();
    }

    internal void Dispose() => _stop.Dispose();
}

[Service(Exported = false, ForegroundServiceType = ForegroundService.TypeConnectedDevice)]
public sealed class BatteryRecordingService : Service
{
    internal const string CsvMime = "text/csv";
    private const string ChannelId = "battery_recording";
    private const string StopAction = "BmsTool.BatteryRecording.Stop";
    private const int NotificationId = 101;
    private const int ReconnectIntervalMs = 10000;
    private const int ReconnectTimeoutMs = 20000;
    private const int WakeLockTimeoutMs = 600000;

    private static BatteryRecordingSession? _current;
    private static BatteryRecordingStatus _status = new(false, false, "未开始");
    internal static BatteryRecordingStatus Status => _status;
    internal static event Action? StatusChanged;

    private BatteryRecordingSession? _running;
    private PowerManager.WakeLock? _wakeLock;
    private readonly object _wakeGate = new();
    private Handler? _mainHandler;

    internal static void Start(Context context, BatteryRecordingSession session)
    {
        if (_current is not null) throw new InvalidOperationException("已有记录正在运行");
        _current = session;
        Publish(new(true, false, $"正在创建 {session.FileName}"));
        try
        {
            using var intent = new Intent(context, typeof(BatteryRecordingService));
            if (OperatingSystem.IsAndroidVersionAtLeast(26)) context.StartForegroundService(intent);
            else context.StartService(intent);
        }
        catch
        {
            _current = null;
            session.Dispose();
            Publish(new(false, false, "无法启动后台记录"));
            throw;
        }
    }

    internal static void Stop(string reason, BmsClient? owner = null)
    {
        if (_current is not { IsStopping: false } session ||
            (owner is not null && !ReferenceEquals(owner, session.Client))) return;
        session.Cancel(reason);
        Publish(new(true, true, $"正在完成最后的写入…\n{session.FileName}"));
    }

    internal static bool DetachActivity(BmsClient? client)
    {
        if (_current is not BatteryRecordingSession session || !ReferenceEquals(session.Client, client))
            return false;
        session.ActivityAttached = false;
        return true;
    }

    internal static BatteryRecordingSession? AttachActivity(Func<bool> isCalibrating)
    {
        if (_current is not { ActivityAttached: false, IsStopping: false } session) return null;
        session.ActivityAttached = true;
        session.IsCalibrating = isCalibrating;
        return session;
    }

    private static void Publish(BatteryRecordingStatus status)
    {
        _status = status;
        StatusChanged?.Invoke();
    }

    public override IBinder? OnBind(Intent? intent) => null;

    public override StartCommandResult OnStartCommand(Intent? intent, StartCommandFlags flags, int startId)
    {
        if (intent?.Action == StopAction)
        {
            if (intent.GetStringExtra("recording") == _current?.FileName) Stop("已从通知停止");
            if (_current is null) StopSelf(startId);
            return StartCommandResult.NotSticky;
        }

        if (_running is not null) return StartCommandResult.NotSticky;
        if (_current is not BatteryRecordingSession session)
        {
            StopSelf(startId);
            return StartCommandResult.NotSticky;
        }

        try
        {
            using var notification = CreateRecordingNotification(session);
            if (OperatingSystem.IsAndroidVersionAtLeast(29))
                StartForeground(NotificationId, notification, ForegroundService.TypeConnectedDevice);
            else
                StartForeground(NotificationId, notification);
            var power = (PowerManager?)GetSystemService(PowerService)
                ?? throw new IOException("无法取得 Android 电源服务");
            _wakeLock = power.NewWakeLock(WakeLockFlags.Partial, "BmsTool:BatteryRecording")
                ?? throw new IOException("无法取得记录唤醒锁");
            _wakeLock.SetReferenceCounted(false);
            _wakeLock.Acquire(WakeLockTimeoutMs);
            _mainHandler = new Handler(Looper.MainLooper!);
        }
        catch (Exception ex)
        {
            _ = FinishAsync(session, $"记录未开始 · {ex.Message}", null);
            return StartCommandResult.NotSticky;
        }

        _running = session;
        _ = Task.Run(() => RecordAsync(session));
        return StartCommandResult.NotSticky;
    }

    private Notification CreateRecordingNotification(BatteryRecordingSession session)
    {
        if (OperatingSystem.IsAndroidVersionAtLeast(26))
        {
            var manager = (NotificationManager)GetSystemService(NotificationService)!;
            using var channel = new NotificationChannel(ChannelId, "电池数据记录", NotificationImportance.Low);
            manager.CreateNotificationChannel(channel);
        }

        using var openIntent = new Intent(this, typeof(MainActivity));
        openIntent.AddFlags(ActivityFlags.SingleTop | ActivityFlags.ClearTop);
        using var open = PendingIntent.GetActivity(this, 0, openIntent, PendingIntentFlags.UpdateCurrent | PendingIntentFlags.Immutable);
        using var stopIntent = new Intent(this, typeof(BatteryRecordingService));
        stopIntent.SetAction(StopAction);
        stopIntent.PutExtra("recording", session.FileName);
        using var stop = PendingIntent.GetService(this, 0, stopIntent, PendingIntentFlags.UpdateCurrent | PendingIntentFlags.Immutable);

        using var builder = OperatingSystem.IsAndroidVersionAtLeast(26)
            ? new Notification.Builder(this, ChannelId) : new Notification.Builder(this);
        using var stopIcon = global::Android.Graphics.Drawables.Icon.CreateWithResource(
            this, global::Android.Resource.Drawable.IcMenuCloseClearCancel);
        using var actionBuilder = new Notification.Action.Builder(stopIcon, "停止记录", stop);
        using var stopAction = actionBuilder.Build();
        builder.SetContentTitle("正在记录电池数据");
        builder.SetContentText($"{session.Mac} · 每 {session.IntervalSeconds} 秒");
        builder.SetSmallIcon(global::Android.Resource.Drawable.IcDialogInfo);
        builder.SetContentIntent(open);
        builder.SetOngoing(true);
        builder.AddAction(stopAction);
        return builder.Build()!;
    }

    private async Task RecordAsync(BatteryRecordingSession session)
    {
        CancellationToken ct = session.Token;
        long samples = 0, gaps = 0, lastRowMs = 0, retryAfterMs = 0;
        long renewByMs = SystemClock.ElapsedRealtime() + WakeLockTimeoutMs / 2;
        bool needsProbe = false;
        string? error = null, documentUri = null;
        try
        {
            ct.ThrowIfCancellationRequested();
            using var folder = Uri.Parse(session.FolderUri) ?? throw new IOException("文件夹地址无效");
            using var parent = DocumentsContract.BuildDocumentUriUsingTree(folder, DocumentsContract.GetTreeDocumentId(folder));
            using var document = DocumentsContract.CreateDocument(ContentResolver!, parent!, CsvMime, session.FileName)
                ?? throw new IOException("无法在所选文件夹创建 CSV");
            var stream = ContentResolver!.OpenOutputStream(document) ?? throw new IOException("无法打开 CSV");
            await using var writer = new StreamWriter(stream, new UTF8Encoding(true));
            await writer.WriteLineAsync(BatteryLogCsv.Header);
            await writer.FlushAsync();
            documentUri = document.ToString();

            using var timer = new PeriodicTimer(TimeSpan.FromSeconds(session.IntervalSeconds));
            do
            {
                long now = SystemClock.ElapsedRealtime();
                if (now >= renewByMs)
                {
                    lock (_wakeGate) _wakeLock?.Acquire(WakeLockTimeoutMs);
                    renewByMs = now + WakeLockTimeoutMs / 2;
                }

                BatterySnapshot? sample = null;
                string code = "ok";
                await session.OperationGate.WaitAsync(ct);
                try
                {
                    bool reconnect = needsProbe || !session.Transport.IsConnected;
                    if (session.IsCalibrating())
                        code = "calibration";
                    else if (reconnect && now < retryAfterMs)
                        code = "link_down";
                    else
                    {
                        try
                        {
                            if (reconnect)
                            {
                                needsProbe = true;
                                using var retry = CancellationTokenSource.CreateLinkedTokenSource(ct);
                                retry.CancelAfter(ReconnectTimeoutMs);
                                await session.Transport.ReconnectAsync(retry.Token);
                                await session.Client.ProbeAsync(retry.Token);
                                needsProbe = false;
                            }
                            sample = await session.Client.ReadBatteryAsync(ct);
                            if (reconnect) code = "reconnected";
                        }
                        catch (Exception ex) when (!ct.IsCancellationRequested &&
                            ex is IOException or TimeoutException or InvalidDataException or OperationCanceledException)
                        {
                            code = reconnect ? "reconnect_error" : "read_error";
                            if (reconnect || !session.Transport.IsConnected)
                                retryAfterMs = SystemClock.ElapsedRealtime() + ReconnectIntervalMs;
                            global::Android.Util.Log.Warn("BmsBatteryRecord", ex.Message);
                        }
                    }
                }
                finally { session.OperationGate.Release(); }

                long stamp = SystemClock.ElapsedRealtime();
                var time = DateTimeOffset.Now;
                long gapLimitMs = session.IntervalSeconds * 1000L + Math.Max(1000L, session.IntervalSeconds * 500L);
                if (lastRowMs != 0 && stamp - lastRowMs > gapLimitMs)
                {
                    await writer.WriteLineAsync(BatteryLogCsv.Row(time, session.Mac, "time_gap", null));
                    gaps++;
                }
                await writer.WriteLineAsync(BatteryLogCsv.Row(time, session.Mac, code, sample));
                await writer.FlushAsync();
                if (sample is null) gaps++; else samples++;
                lastRowMs = stamp;
                var progress = new BatteryRecordingStatus(true, false,
                    $"记录中 · {samples} 条 · 缺口 {gaps} · 每 {session.IntervalSeconds} 秒\n{session.FileName}");
                _mainHandler?.Post(() =>
                {
                    if (ReferenceEquals(_current, session) && !session.IsStopping) Publish(progress);
                });
            }
            while (await timer.WaitForNextTickAsync(ct));
        }
        catch (OperationCanceledException) when (ct.IsCancellationRequested) { }
        catch (Exception ex)
        {
            error = ex.Message;
            global::Android.Util.Log.Warn("BmsBatteryRecord", ex.Message);
        }

        string reason = error is null ? session.StopReason : $"记录中断 · {error}";
        string text = $"{reason} · {samples} 条 · 缺口 {gaps}\n{session.FileName}";
        _mainHandler?.Post(() => _ = FinishAsync(session, text, error is null && samples + gaps > 0 ? documentUri : null));
    }

    private async Task FinishAsync(BatteryRecordingSession session, string text, string? documentUri)
    {
        ReleaseWakeLock();
        session.Cancel("后台记录已结束");
        if (!session.ActivityAttached)
        {
            await session.OperationGate.WaitAsync();
            try
            {
                await session.Client.DisposeAsync();
                await session.Transport.DisposeAsync();
            }
            catch (Exception ex) { global::Android.Util.Log.Warn("BmsBatteryRecord", ex.Message); }
            finally { session.OperationGate.Release(); }
        }

        _running = null;
        if (ReferenceEquals(_current, session))
        {
            _current = null;
            session.Dispose();
            Publish(new(false, false, text, documentUri));
        }
        if (OperatingSystem.IsAndroidVersionAtLeast(24)) StopForeground(StopForegroundFlags.Remove);
        else StopForeground(true);
        StopSelf();
    }

    private void ReleaseWakeLock()
    {
        lock (_wakeGate)
        {
            if (_wakeLock is not PowerManager.WakeLock wake) return;
            _wakeLock = null;
            if (wake.IsHeld) wake.Release();
            wake.Dispose();
        }
    }

    public override void OnDestroy()
    {
        if (_running is { IsStopping: false } session) session.Cancel("系统停止后台记录");
        ReleaseWakeLock();
        base.OnDestroy();
    }
}
