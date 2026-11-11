using System.Diagnostics;
namespace BmsTool.Windows;

public enum BleDiscoverySource { Advertisement, History, Connected, SystemRecent }

public sealed record DiscoveredDevice(ulong Address, string Name, short Rssi)
{
    public BleDiscoverySource Source { get; init; } = BleDiscoverySource.Advertisement;
    public DateTimeOffset? LastSeenUtc { get; init; } = DateTimeOffset.UtcNow;
    public long? LastSeenTimestamp { get; init; } = Stopwatch.GetTimestamp();
    public string Category => Source switch
    {
        BleDiscoverySource.Advertisement => "附近设备（直接广播）",
        BleDiscoverySource.SystemRecent => "附近设备（系统广播更新）",
        BleDiscoverySource.Connected => "已连接设备",
        _ => "历史设备（未确认在线）"
    };
    public bool IsNearby => Source is BleDiscoverySource.Advertisement or BleDiscoverySource.SystemRecent;
    public bool HasRecentAdvertisement => IsNearby &&
        LastSeenTimestamp is long stamp && Stopwatch.GetElapsedTime(stamp) < BleDiscoveryCatalog.Freshness;
    public override string ToString()
    {
        string mac = Address.ToString("X12");
        mac = string.Join(":", Enumerable.Range(0, 6).Select(i => mac.Substring(i * 2, 2)));
        string state = Source switch
        {
            BleDiscoverySource.Connected => "已连接",
            BleDiscoverySource.History => LastSeenUtc is null ? "历史 · 未发现" : "历史 · 已过期",
            BleDiscoverySource.SystemRecent => LastSeenUtc is { } systemSeen ? $"系统发现 · {Math.Max(0, (int)(DateTimeOffset.UtcNow - systemSeen).TotalSeconds)}秒前" : "系统发现",
            _ => LastSeenUtc is { } seen ? $"最近广播 · {Math.Max(0, (int)(DateTimeOffset.UtcNow - seen).TotalSeconds)}秒前" : "最近广播"
        };
        string signal = HasRecentAdvertisement && Rssi is >= -127 and < 0 ? $"RSSI {Rssi} dBm" : "RSSI —";
        return $"【{state}】 {Name}    MAC {mac}    {signal}";
    }
}

// 直接广播，或本次扫描中持续推进的系统 LastSeenTime，才能刷新附近状态。
// 不用系统属性的绝对时刻计算年龄：现场系统曾报告偏移8小时的时间。
public sealed class BleDiscoveryCatalog
{
    public static readonly TimeSpan Freshness = TimeSpan.FromSeconds(12);
    private readonly object _gate = new();
    private readonly Dictionary<ulong, DiscoveredDevice> _devices = new();
    private readonly Dictionary<ulong, DateTimeOffset> _systemLastSeen = new();
    private readonly TimeProvider _clock;
    public BleDiscoveryCatalog(TimeProvider? clock = null) => _clock = clock ?? TimeProvider.System;
    public bool KnowsBms(ulong address) { lock (_gate) return _devices.ContainsKey(address); }
    public bool ObserveSystem(ulong address, string name, DateTimeOffset? lastSeen = null, bool isUpdate = false, short? rssi = null)
    {
        lock (_gate)
        {
            Expire();
            bool advanced = lastSeen is { } seen && isUpdate &&
                _systemLastSeen.TryGetValue(address, out var previousSeen) && seen > previousSeen;
            if (lastSeen is { } baseline && (!_systemLastSeen.TryGetValue(address, out var highestSeen) || baseline > highestSeen))
                _systemLastSeen[address] = baseline;
            _devices.TryGetValue(address, out var previous);
            // 直接广播仍然新鲜时保留它的名称、信号和来源。
            if (previous?.Source == BleDiscoverySource.Advertisement) return false;
            if (advanced)
            {
                _devices[address] = new(address, name, rssi ?? 0)
                {
                    Source = BleDiscoverySource.SystemRecent,
                    LastSeenUtc = _clock.GetUtcNow(),
                    LastSeenTimestamp = _clock.GetTimestamp()
                };
            }
            else if (previous?.Source != BleDiscoverySource.SystemRecent)
            {
                _devices[address] = new(address, name, 0)
                {
                    Source = BleDiscoverySource.History,
                    LastSeenUtc = previous?.LastSeenUtc,
                    LastSeenTimestamp = previous?.LastSeenTimestamp
                };
            }
            return advanced;
        }
    }
    public void ObserveAdvertisement(ulong address, string name, short rssi)
    {
        lock (_gate)
        {
            if (string.IsNullOrWhiteSpace(name) && _devices.TryGetValue(address, out var previous)) name = previous.Name;
            _devices[address] = new(address, string.IsNullOrWhiteSpace(name) ? "未命名BMS（广播服务）" : name, rssi)
            {
                LastSeenUtc = _clock.GetUtcNow(),
                LastSeenTimestamp = _clock.GetTimestamp()
            };
        }
    }
    public void RemoveSystem(ulong address)
    {
        lock (_gate)
        {
            Expire();
            _systemLastSeen.Remove(address);
            if (_devices.TryGetValue(address, out var item) && item.Source != BleDiscoverySource.Advertisement)
                _devices.Remove(address);
        }
    }
    private void Expire()
    {
        foreach (var pair in _devices.ToArray())
        {
            var item = pair.Value;
            if (item.IsNearby && item.LastSeenTimestamp is long stamp &&
                _clock.GetElapsedTime(stamp, _clock.GetTimestamp()) >= Freshness)
                _devices[pair.Key] = item with { Source = BleDiscoverySource.History, Rssi = 0 };
        }
    }
    public IReadOnlyList<DiscoveredDevice> Snapshot(bool includeHistory = false)
    {
        lock (_gate)
        {
            Expire();
            return _devices.Values.Where(d => includeHistory || d.IsNearby)
                .OrderBy(d => d.Source).ThenByDescending(d => d.Rssi).ThenBy(d => d.Address).ToArray();
        }
    }
}
