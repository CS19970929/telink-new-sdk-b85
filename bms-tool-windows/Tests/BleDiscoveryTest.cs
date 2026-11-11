using BmsTool.Windows;
static class BleDiscoveryTest
{
    private static int checks;
    private static void Check(bool value, string name) { if (!value) throw new Exception(name); checks++; Console.WriteLine("PASS " + name); }
    private static async Task Canceled(Task task) { try { await task; throw new Exception("expected cancellation"); } catch (OperationCanceledException) { } }
    public static async Task Main()
    {
        const ulong mac = 0xA4C138A0EB83;
        var clock = new ManualClock();
        var list = new BleDiscoveryCatalog(clock);
        list.ObserveSystem(mac, "BT_历史");
        Check(list.Snapshot().Count == 0, "系统缓存不会成为附近设备");
        var history = list.Snapshot(true).Single();
        Check(history.Source == BleDiscoverySource.History && history.LastSeenUtc is null && history.Rssi == 0, "历史记录无新鲜时间和伪造RSSI");
        list.ObserveAdvertisement(mac, "BT_D008", -50);
        Check(list.Snapshot().Single().Name == "BT_D008", "真实广播进入附近列表并覆盖历史名称");
        clock.Advance(TimeSpan.FromSeconds(11));
        list.ObserveSystem(mac, "BT_旧名称");
        Check(list.Snapshot().Single().Name == "BT_D008", "系统更新不会覆盖真实广播名称");
        clock.Advance(TimeSpan.FromSeconds(1));
        Check(list.Snapshot().Count == 0, "恰好12秒未收到广播即过期");
        Check(list.Snapshot(true).Single().LastSeenUtc == clock.Start, "系统更新不刷新广播发现时间");
        list.ObserveSystem(mac, "BT_历史");
        Check(list.Snapshot().Count == 0, "过期后系统缓存不能重新授权附近状态");
        list.ObserveAdvertisement(mac, "", -70);
        Check(list.Snapshot().Single().Name == "BT_历史", "无名称广播可使用同MAC已知名称");
        list.RemoveSystem(mac);
        Check(list.Snapshot().Count == 1, "系统移除不掩盖刚收到的真实广播");
        clock.Advance(TimeSpan.FromSeconds(12));
        list.RemoveSystem(mac);
        Check(list.Snapshot(true).Count == 0, "系统移除能删除过期历史记录");
        Check(!list.KnowsBms(mac), "删除后不残留名称映射");
        var unknown = new BleDiscoveryCatalog(clock);
        unknown.ObserveAdvertisement(mac, "", -80);
        Check(unknown.Snapshot().Single().Name == "未命名BMS（广播服务）", "已确认服务的无名称广播有清楚标记");
        var gate = new LatestConnectionOperation();
        var started = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var cleanup = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        int active = 0, maxActive = 0;
        var first = gate.RunAsync(async ct =>
        {
            active++; maxActive = Math.Max(maxActive, active); started.SetResult();
            try { await Task.Delay(Timeout.Infinite, ct); }
            finally { await cleanup.Task; active--; }
            return 1;
        });
        await started.Task;
        var second = gate.RunAsync(ct => { active++; maxActive = Math.Max(maxActive, active); active--; return Task.FromResult(2); });
        Check(!second.IsCompleted, "新连接等待旧连接完成清理");
        cleanup.SetResult();
        await Canceled(first);
        Check(await second == 2 && maxActive == 1, "手动连接和自动重连不会并行");
        Check(!gate.IsBusy, "连接结束后释放所有权");
        var nativeStarted = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var nativeFinish = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var native = gate.RunAsync(async ct => { nativeStarted.SetResult(); await nativeFinish.Task; return 3; });
        await nativeStarted.Task;
        bool queuedRan = false;
        var queued = gate.RunAsync(ct => { queuedRan = true; return Task.FromResult(4); });
        var latest = gate.RunAsync(ct => Task.FromResult(5));
        nativeFinish.SetResult();
        await Canceled(native); await Canceled(queued);
        Check(await latest == 5 && !queuedRan, "不能即时取消的系统查询完成后只执行最新连接");
        var cancelStarted = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var canceled = gate.RunAsync(async ct => { cancelStarted.SetResult(); await Task.Delay(Timeout.Infinite, ct); return 0; });
        await cancelStarted.Task; gate.Cancel(); await Canceled(canceled);
        Check(!gate.IsBusy, "取消按钮能取消当前连接并释放入口");
        try { await gate.RunAsync<int>(_ => throw new InvalidOperationException("injected")); } catch (InvalidOperationException) { }
        Check(await gate.RunAsync(_ => Task.FromResult(7)) == 7, "连接异常后仍能发起新连接");
        Console.WriteLine($"BLE_DISCOVERY_PASS checks={checks}");
    }
    private sealed class ManualClock : TimeProvider
    {
        public DateTimeOffset Start { get; } = new(2026, 11, 11, 0, 0, 0, TimeSpan.Zero);
        private long ticks;
        public override long TimestampFrequency => TimeSpan.TicksPerSecond;
        public override long GetTimestamp() => ticks;
        public override DateTimeOffset GetUtcNow() => Start + TimeSpan.FromTicks(ticks);
        public void Advance(TimeSpan duration) => ticks += duration.Ticks;
    }
}
