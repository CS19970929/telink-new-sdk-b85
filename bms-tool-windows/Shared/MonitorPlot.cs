using System.Globalization;
using System.Windows;
using System.Windows.Media;

namespace BmsTool.Windows;

public sealed class MonitorPlot : FrameworkElement
{
    public IReadOnlyList<MonitorLogEntry> Entries { get; set; } = [];
    public string Metric { get; set; } = "总压";
    public TimeSpan Range { get; set; } = TimeSpan.FromMinutes(30);
    public MonitorPlot() { MinHeight = 190; Height = 220; }
    protected override void OnRender(DrawingContext dc)
    {
        base.OnRender(dc);
        double width = ActualWidth, height = ActualHeight;
        if (width < 120 || height < 80) return;
        var selected = Entries.Where(x => x.Utc >= (Entries.LastOrDefault()?.Utc ?? DateTimeOffset.UtcNow) - Range).ToList();
        double? Value(MonitorReading? r) => Metric switch
        { "电流" => r?.Current, "温度" => r?.Temperature, "SOC" => r?.Soc, _ => r?.Voltage };
        var numbers = selected.Where(e => e.Kind == "sample").Select(e => Value(e.Reading))
            .Where(x => x.HasValue && double.IsFinite(x.Value)).Select(x => x!.Value).ToList();
        double min = numbers.Count == 0 ? 0 : numbers.Min(), max = numbers.Count == 0 ? 1 : numbers.Max();
        if (max - min < .1) { min -= .5; max += .5; }
        double pad = (max - min) * .1; min -= pad; max += pad;
        var typeface = new Typeface("Segoe UI");
        void Text(string text, double x, double y) => dc.DrawText(new FormattedText(text,
            CultureInfo.CurrentCulture, FlowDirection.LeftToRight, typeface, 11,
            new SolidColorBrush(Color.FromRgb(125, 145, 168)), VisualTreeHelper.GetDpi(this).PixelsPerDip), new Point(x, y));
        for (int i = 0; i < 4; i++)
        {
            double y = 18 + (height - 54) * i / 3;
            dc.DrawLine(new Pen(new SolidColorBrush(Color.FromRgb(231, 237, 245)), 1), new(48, y), new(width - 12, y));
            Text((max - (max - min) * i / 3).ToString("0.##"), 0, y - 7);
        }
        if (selected.Count == 0) { Text("连接设备后显示趋势", 64, height / 2); return; }
        var first = selected[0].Utc; var last = selected[^1].Utc;
        double seconds = Math.Max(1, (last - first).TotalSeconds);
        Point? previous = null; int segment = -1;
        foreach (var e in selected)
        {
            double? value = e.Kind == "sample" ? Value(e.Reading) : null;
            if (value is null || !double.IsFinite(value.Value) || e.Segment != segment)
                previous = null;
            segment = e.Segment;
            if (value is null || !double.IsFinite(value.Value)) continue;
            var point = new Point(48 + (width - 60) * (e.Utc - first).TotalSeconds / seconds,
                18 + (height - 54) * (max - value.Value) / (max - min));
            if (previous is Point old) dc.DrawLine(new Pen(new SolidColorBrush(Color.FromRgb(43, 112, 235)), 2), old, point);
            previous = point;
        }
        if (previous is Point lastPoint) dc.DrawEllipse(new SolidColorBrush(Color.FromRgb(43,112,235)), null, lastPoint, 3, 3);
        Text(first.ToLocalTime().ToString("HH:mm:ss"), 48, height - 21);
        Text(last.ToLocalTime().ToString("HH:mm:ss"), Math.Max(48, width - 73), height - 21);
    }
}
