using System.Diagnostics;
namespace BmsTool.Windows;

public enum BleDiscoverySource { Advertisement, History, Connected }

public sealed record DiscoveredDevice(ulong Address, string Name, short Rssi)
{
    public BleDiscoverySource Source { get; init; } = BleDiscoverySource.Advertisement;
    public DateTimeOffset? LastSeenUtc { get; init; } = DateTimeOffset.UtcNow;
    public long? LastSeenTimestamp { get; init; } = Stopwatch.GetTimestamp();
    public string Category => Source switch
    {
        BleDiscoverySource.Advertisement => "附近设备（最近12秒广播）",
        BleDiscoverySource.Connected => "已连接设备",
        _ => "历史设备（未确认在线）"
    };
    public bool HasRecentAdvertisement => Source == BleDiscoverySource.Advertisement &&
        LastSeenTimestamp is long stamp && Stopwatch.GetElapsedTime(stamp) < BleDiscoveryCatalog.Freshness;
    public override string ToString()
    {
        string mac = Address.ToString("X12");
        mac = string.Join(":", Enumerable.Range(0, 6).Select(i => mac.Substring(i * 2, 2)));
        string state = Source switch
        {
            BleDiscoverySource.Connected => "已连接",
            BleDiscoverySource.History => LastSeenUtc is null ? "历史 · 未发现" : "历史 · 已过期",
            _ => LastSeenUtc is { } seen ? $"最近广播 · {Math.Max(0, (int)(DateTimeOffset.UtcNow - seen).TotalSeconds)}秒前" : "最近广播"
        };
        string signal = HasRecentAdvertisement ? $"RSSI {Rssi} dBm" : "RSSI —";
        return $"【{state}】 {Name}    MAC {mac}    {signal}";
    }
}

// 列表只以真实广播刷新新鲜度；系统枚举只能补充历史名称。
public sealed class BleDiscoveryCatalog
{
    public static readonly TimeSpan Freshness = TimeSpan.FromSeconds(12);
    private readonly object _gate = new();
    private readonly Dictionary<ulong, DiscoveredDevice> _devices = new();
    private readonly TimeProvider _clock;
    public BleDiscoveryCatalog(TimeProvider? clock = null) => _clock = clock ?? TimeProvider.System;
    public bool KnowsBms(ulong address) { lock (_gate) return _devices.ContainsKey(address); }
    public void ObserveSystem(ulong address, string name)
    {
        lock (_gate)
        {
            if (_devices.TryGetValue(address, out var previous) && previous.Source == BleDiscoverySource.Advertisement) return;
            _devices[address] = new(address, name, 0)
            {
                Source = BleDiscoverySource.History,
                LastSeenUtc = previous?.LastSeenUtc,
                LastSeenTimestamp = previous?.LastSeenTimestamp
            };
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
            if (_devices.TryGetValue(address, out var item) && item.Source != BleDiscoverySource.Advertisement)
                _devices.Remove(address);
        }
    }
    private void Expire()
    {
        foreach (var pair in _devices.ToArray())
        {
            var item = pair.Value;
            if (item.Source == BleDiscoverySource.Advertisement && item.LastSeenTimestamp is long stamp &&
                _clock.GetElapsedTime(stamp, _clock.GetTimestamp()) >= Freshness)
                _devices[pair.Key] = item with { Source = BleDiscoverySource.History, Rssi = 0 };
        }
    }
    public IReadOnlyList<DiscoveredDevice> Snapshot(bool includeHistory = false)
    {
        lock (_gate)
        {
            Expire();
            return _devices.Values.Where(d => includeHistory || d.Source == BleDiscoverySource.Advertisement)
                .OrderBy(d => d.Source).ThenByDescending(d => d.Rssi).ThenBy(d => d.Address).ToArray();
        }
    }
}
