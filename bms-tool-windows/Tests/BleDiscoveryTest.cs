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
        var system = new BleDiscoveryCatalog(clock);
        var reported = clock.GetUtcNow().AddHours(8); // 现场 LastSeenTime 比本机UTC偏移8小时。
        system.ObserveSystem(mac, "BT_DEFAULT", reported, false, -93);
        Check(system.Snapshot().Count == 0, "初始枚举即使带未来时间和RSSI也只是历史");
        system.ObserveSystem(mac, "BT_DEFAULT", reported, true, -93);
        Check(system.Snapshot().Count == 0, "重复LastSeenTime不能授权在线");
        system.ObserveSystem(mac, "BT_DEFAULT", reported.AddSeconds(1), true, -94);
        var recent = system.Snapshot().Single();
        Check(recent.Source == BleDiscoverySource.SystemRecent && recent.Rssi == -94, "持续推进的系统广播时间进入附近列表");
        Check(recent.LastSeenUtc == clock.GetUtcNow(), "新鲜度采用本机接收时刻而非错误的系统绝对时间");
        clock.Advance(TimeSpan.FromSeconds(11));
        system.ObserveSystem(mac, "BT_DEFAULT", null, true, -80);
        system.ObserveSystem(mac, "BT_DEFAULT", reported.AddSeconds(1), true, -79);
        clock.Advance(TimeSpan.FromSeconds(1));
        Check(system.Snapshot().Count == 0, "名称信号及重复时间更新不能延长12秒期限");
        Check(system.Snapshot(true).Single().Rssi == 0, "过期系统发现隐藏旧RSSI");
        system.ObserveSystem(mac, "BT_DEFAULT", reported.AddSeconds(-1), true, -95);
        Check(system.Snapshot().Count == 0, "回退的系统时间不能授权在线");
        system.ObserveSystem(mac, "BT_DEFAULT", reported, true, -92);
        Check(system.Snapshot().Count == 0, "乱序回放旧时间不能重复授权在线");
        system.ObserveSystem(mac, "BT_DEFAULT", reported.AddSeconds(2), true, -92);
        Check(system.Snapshot().Count == 1, "只有超过已见最高时间的新广播才能重新发现");
        system.RemoveSystem(mac);
        Check(system.Snapshot(true).Count == 0, "系统移除立即撤销系统广播发现和基线");
        system.ObserveSystem(mac, "BT_DEFAULT", reported.AddSeconds(2), true, -91);
        Check(system.Snapshot().Count == 0, "移除后首条更新不能复用旧时间基线");
        system.ObserveAdvertisement(mac, "BT_直接广播", -50);
        system.ObserveSystem(mac, "BT_DEFAULT", reported.AddSeconds(3), true, -90);
        Check(system.Snapshot().Single().Source == BleDiscoverySource.Advertisement &&
            system.Snapshot().Single().Name == "BT_直接广播", "系统更新不覆盖仍新鲜的直接广播");
        clock.Advance(TimeSpan.FromSeconds(12));
        system.ObserveSystem(mac, "BT_DEFAULT", reported.AddSeconds(4), true, -89);
        Check(system.Snapshot().Single().Source == BleDiscoverySource.SystemRecent, "直接广播过期后可由系统新广播续接");
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
