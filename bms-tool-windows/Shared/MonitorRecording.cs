using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Text;
using System.Text.Json;
using System.Threading.Channels;

namespace BmsTool.Windows;

public sealed record MonitorLogEntry(string Kind, DateTimeOffset Utc, int Segment,
    MonitorReading? Reading, string? Note);

// 一个写盘消费者；队列有界，写盘失败和积压均显式停止记录。
// 文件按UTC日期或100000条轮换；UI只保留有界趋势，不保留完整历史于内存。
public sealed class MonitorRecording
{
    private readonly Channel<MonitorLogEntry> _queue = Channel.CreateBounded<MonitorLogEntry>(
        new BoundedChannelOptions(256) { SingleReader = true, SingleWriter = true, FullMode = BoundedChannelFullMode.Wait });
    private readonly Task _writer;
    private readonly long _started = Stopwatch.GetTimestamp();
    private long _lastCapture;
    private bool _captured, _wasGap, _ended;
    private string? _key, _sequence;
    private int _segment;
    private long _written;
    public string DirectoryPath { get; }
    public int IntervalSeconds { get; }
    public long Written => Interlocked.Read(ref _written);
    public TimeSpan Elapsed => Stopwatch.GetElapsedTime(_started);
    public bool IsRunning => !_ended && !_writer.IsCompleted;
    public Exception? Error => _writer.Exception?.GetBaseException();
    private static readonly JsonSerializerOptions Json = new() { PropertyNameCaseInsensitive = true };

    public MonitorRecording(string parent, int intervalSeconds)
    {
        if (intervalSeconds is < 1 or > 3600) throw new ArgumentOutOfRangeException(nameof(intervalSeconds));
        IntervalSeconds = intervalSeconds;
        DirectoryPath = Path.Combine(Path.GetFullPath(parent),
            $"BMS-{DateTime.Now:yyyyMMdd-HHmmss}-{Guid.NewGuid().ToString("N")[..8]}");
        Directory.CreateDirectory(DirectoryPath);
        File.WriteAllText(Path.Combine(DirectoryPath, "session.json"), JsonSerializer.Serialize(new
        {
            schema = 1, appCommit = BuildIdentity.Display, startedUtc = DateTimeOffset.UtcNow, intervalSeconds,
            currentSign = "positive discharge; negative charge", currentUnit = "A",
            cellUnit = "mV", timestamp = "PC observation time; SourceAgeMs/CellsUtc preserve frame age",
            atomicSnapshot = false, identityAssurance = "endpoint/session only; unknown identity starts a new segment",
            recovery = "complete JSON lines retained; unflushed final line may be incomplete"
        }), new UTF8Encoding(false));
        _writer = Task.Run(WriteAsync);
        Enqueue(new("start", DateTimeOffset.UtcNow, 0, null, null));
    }

    private void Enqueue(MonitorLogEntry item)
    {
        if (_ended) throw new InvalidOperationException("记录已结束");
        if (Error is Exception error) throw new IOException("记录写盘失败", error);
        if (!_queue.Writer.TryWrite(item))
        {
            _ended = true; _queue.Writer.TryComplete();
            throw new IOException("记录写盘队列已满，任务已停止；已有文件保留");
        }
    }

    public void Capture(MonitorReading? reading)
    {
        if (_ended) return;
        if (Error is Exception error) throw new IOException("记录写盘失败", error);
        bool valid = reading?.IsFresh == true;
        if (!valid)
        {
            if (!_wasGap) Enqueue(new("gap", DateTimeOffset.UtcNow, _segment, null, "数据中断或等待有效数据"));
            _wasGap = true; _sequence = null;
            return;
        }
        bool changed = _key != reading!.SegmentKey || _wasGap;
        if (changed)
        {
            ++_segment; _key = reading.SegmentKey; _captured = false;
            Enqueue(new("segment", DateTimeOffset.UtcNow, _segment, null,
                "新连接/协议/身份或缺口后的记录段；不推断为同一电池"));
        }
        _wasGap = false;
        long now = Stopwatch.GetTimestamp();
        if (_sequence == reading.Sequence ||
            (_captured && Stopwatch.GetElapsedTime(_lastCapture, now).TotalSeconds < IntervalSeconds)) return;
        Enqueue(new("sample", DateTimeOffset.UtcNow, _segment, reading, null));
        _sequence = reading.Sequence; _lastCapture = now; _captured = true;
    }

    public void Note(string text)
    {
        if (string.IsNullOrWhiteSpace(text)) return;
        if (text.Length > 2000) throw new ArgumentException("备注最多2000字");
        Enqueue(new("note", DateTimeOffset.UtcNow, _segment, null, text.Trim()));
    }

    private async Task WriteAsync()
    {
        StreamWriter? writer = null;
        DateOnly date = default;
        int fileIndex = 0, rows = 0;
        try
        {
            await foreach (var item in _queue.Reader.ReadAllAsync())
            {
                DateOnly next = DateOnly.FromDateTime(item.Utc.UtcDateTime);
                if (writer is null || next != date || rows >= 100000)
                {
                    if (writer is not null) await writer.DisposeAsync();
                    var stream = new FileStream(Path.Combine(DirectoryPath, $"records-{++fileIndex:D4}.jsonl"),
                        FileMode.CreateNew, FileAccess.Write, FileShare.Read, 65536,
                        FileOptions.Asynchronous | FileOptions.WriteThrough);
                    writer = new StreamWriter(stream, new UTF8Encoding(false)) { AutoFlush = true };
                    date = next; rows = 0;
                }
                await writer.WriteLineAsync(JsonSerializer.Serialize(item, Json));
                ++rows; Interlocked.Increment(ref _written);
            }
        }
        finally { if (writer is not null) await writer.DisposeAsync(); }
    }

    public async Task StopAsync()
    {
        Exception? enqueueError = null;
        if (!_ended)
        {
            try { Enqueue(new("end", DateTimeOffset.UtcNow, _segment, null, "用户结束")); }
            catch (Exception ex) { enqueueError = ex; }
            finally { _ended = true; _queue.Writer.TryComplete(); }
        }
        await _writer;
        if (enqueueError is not null) throw enqueueError;
    }

    public static IEnumerable<MonitorLogEntry> Read(string directory)
    {
        var files = Directory.GetFiles(directory, "records-*.jsonl").OrderBy(x => x, StringComparer.Ordinal).ToArray();
        DateTimeOffset lastUtc = default; int lastSegment = 0;
        foreach (string file in files)
        {
            using var reader = new StreamReader(file, Encoding.UTF8);
            string? line; int number = 0;
            while ((line = reader.ReadLine()) is not null)
            {
                ++number;
                if (line.Length == 0) continue;
                MonitorLogEntry entry;
                try
                {
                    entry = JsonSerializer.Deserialize<MonitorLogEntry>(line, Json) ?? throw new JsonException("空记录");
                    if (entry.Kind is not ("start" or "end" or "sample" or "gap" or "segment" or "note" or "recovery_gap") ||
                        entry.Utc == default || entry.Segment < 0 || (entry.Kind == "sample" && entry.Reading is null))
                        throw new JsonException("记录格式非法");
                }
                catch (JsonException ex)
                {
                    if (!reader.EndOfStream || file != files[^1])
                        throw new InvalidDataException($"记录损坏：{Path.GetFileName(file)}:{number}", ex);
                    // 只恢复最后文件的最后一行，明确保留恢复缺口；中间损坏不静默跳过。
                    entry = new("recovery_gap", lastUtc == default ? DateTimeOffset.UtcNow : lastUtc, lastSegment, null,
                        $"末行时间未知，按最后完整记录标记异常中断缺口：{Path.GetFileName(file)}:{number}");
                }
                lastUtc = entry.Utc; lastSegment = entry.Segment;
                yield return entry;
            }
        }
    }

    public static string CsvCell(object? value)
    {
        if (value is null) return "";
        string text = value is IFormattable formattable
            ? formattable.ToString(null, CultureInfo.InvariantCulture) ?? "" : value.ToString() ?? "";
        if (value is string && text.Length > 0 && "=+-@\t\r".Contains(text[0])) text = "'" + text;
        return "\"" + text.Replace("\"", "\"\"") + "\"";
    }

    public static void ExportCsv(string directory, string destination)
    {
        if (Path.GetFullPath(destination).StartsWith(Path.GetFullPath(directory) + Path.DirectorySeparatorChar,
                StringComparison.OrdinalIgnoreCase)) throw new IOException("导出文件请选择记录目录之外的位置");
        using var writer = new StreamWriter(destination, false, new UTF8Encoding(true));
        writer.WriteLine("UTC,Kind,Segment,Source,Endpoint,Protocol,Identity,SOC_pct,Voltage_V,Current_A,CurrentResolution_A,Temp_C,MinTemp_C,MosTemp_C,Cycles,SourceAge_ms,FaultCode,Fault,CellsUTC,Cells_mV,Note");
        foreach (var e in Read(directory))
        {
            var r = e.Reading;
            object?[] cells = [e.Utc.ToString("O"), e.Kind, e.Segment, r?.Source, r?.Endpoint, r?.Protocol,
                r?.Identity, r?.Soc, r?.Voltage, r?.Current, r?.CurrentResolutionA, r?.Temperature,
                r?.MinTemperature, r?.MosTemperature, r?.Cycles, r?.SourceAgeMs, r?.FaultCode, r?.Fault, r?.CellsUtc?.ToString("O"),
                r is null ? null : JsonSerializer.Serialize(r.Cells), e.Note];
            writer.WriteLine(string.Join(",", cells.Select(CsvCell)));
        }
    }
}
