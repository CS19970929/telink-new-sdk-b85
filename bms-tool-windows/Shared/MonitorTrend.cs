namespace BmsTool.Windows;

// 近30分钟保留逐样本趋势，较早数据每30秒抽样；完整记录只在磁盘。
// 两个队列各限3000，长时间运行与历史加载不会无限增长内存。
public sealed class MonitorTrend
{
    private readonly Queue<MonitorLogEntry> _recent = new(), _archive = new();
    private DateTimeOffset _latest, _archiveSample;
    private int _archiveSegment = -1;
    public void Add(MonitorLogEntry entry)
    {
        if (entry.Utc > _latest) _latest = entry.Utc;
        _recent.Enqueue(entry);
        while (_recent.Count > 3000 || (_recent.TryPeek(out var first) && first.Utc < _latest.AddMinutes(-30)))
        {
            var old = _recent.Dequeue();
            if (old.Kind != "sample" || old.Segment != _archiveSegment || (old.Utc - _archiveSample).TotalSeconds >= 30)
            {
                _archive.Enqueue(old);
                if (old.Kind == "sample") { _archiveSample = old.Utc; _archiveSegment = old.Segment; }
            }
        }
        while (_archive.Count > 3000 || (_archive.TryPeek(out var first) && first.Utc < _latest.AddHours(-24)))
            _archive.Dequeue();
    }
    public MonitorLogEntry[] ToArray() => _archive.Concat(_recent).ToArray();
}
