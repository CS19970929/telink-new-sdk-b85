using System.Globalization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
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

public sealed class UnifiedMonitorView : UserControl
{
    private static readonly Brush Muted = new SolidColorBrush(Color.FromRgb(113, 132, 155));
    private readonly bool _compact;
    public WrapPanel ConnectionHost { get; } = new() { VerticalAlignment = VerticalAlignment.Center };
    public TextBlock Status { get; } = new() { Foreground = Muted, Margin = new(2, 10, 2, 3), Text = "未连接" };
    public TextBlock ProtocolTag { get; } = new() { Foreground = Muted, FontSize = 12 };
    public TextBlock RecordingStatus { get; } = new() { Foreground = Muted, FontSize = 12 };
    public Button StartRecord { get; } = MakeButton("开始记录", true);
    public Button RecordsButton { get; } = MakeButton("查看记录");
    public Button Disconnect { get; } = MakeButton("断开");
    public Button Advanced { get; } = MakeButton("高级功能");
    public ComboBox Baud { get; } = new() { Width = 88 };
    public MonitorPlot Plot { get; } = new();
    private readonly UniformGrid _cards = new() { Rows = 1, Columns = 4 };
    private readonly WrapPanel _cells = new();
    private readonly StackPanel _details = new();
    private readonly TextBlock _identity = new() { Foreground = Muted, FontSize = 12, Margin = new(0, 3, 0, 0) };
    private readonly Border _cellPanel;
    private readonly DataGrid _measurements = new() { IsReadOnly = true, AutoGenerateColumns = true,
        CanUserAddRows = false, CanUserDeleteRows = false, HeadersVisibility = DataGridHeadersVisibility.Column,
        BorderThickness = new(0), MinHeight = 180, RowHeight = 29, ColumnHeaderHeight = 32,
        ColumnWidth = new DataGridLength(1, DataGridLengthUnitType.Star), GridLinesVisibility = DataGridGridLinesVisibility.Horizontal,
        HorizontalGridLinesBrush = new SolidColorBrush(Color.FromRgb(231,237,245)),
        Background = Brushes.White, AlternatingRowBackground = new SolidColorBrush(Color.FromRgb(248,250,253)) };
    private readonly TextBlock _fault = new() { TextWrapping = TextWrapping.Wrap, FontSize = 14 };
    public StackPanel Operations { get; } = new();
    public Expander OperationsContainer { get; } = new() { Header = "设备操作" };
    public TextBox Raw { get; } = new() { IsReadOnly = true, AcceptsReturn = true, MinHeight = 70,
        FontFamily = new("Consolas"), FontSize = 12, MaxHeight = 140, VerticalScrollBarVisibility = ScrollBarVisibility.Auto };
    public TextBlock CellAge { get; } = new() { Foreground = Muted, FontSize = 11 };
    public UnifiedMonitorView(bool compact)
    {
        _compact = compact;
        Foreground = new SolidColorBrush(Color.FromRgb(25,44,68)); FontWeight = FontWeights.Normal;
        var root = new StackPanel { Margin = new(22, 18, 22, 18) };
        root.Children.Add(Panel(ConnectionHost));
        root.Children.Add(Status);
        var heading = new DockPanel { Margin = new(2, 19, 2, 15) };
        var actions = new StackPanel { Orientation = Orientation.Horizontal };
        actions.Children.Add(StartRecord); actions.Children.Add(RecordsButton);
        DockPanel.SetDock(actions, Dock.Right); heading.Children.Add(actions);
        var names = new StackPanel();
        names.Children.Add(new TextBlock { Text = compact ? "实时数据" : "电池总览", FontSize = 24, FontWeight = FontWeights.SemiBold });
        names.Children.Add(_identity); heading.Children.Add(names); root.Children.Add(heading);
        root.Children.Add(_cards);
        var chartPanel = new StackPanel();
        var chartTools = new DockPanel();
        var range = new ComboBox { Width = 96, ItemsSource = new[] { "30分钟", "2小时", "24小时" }, SelectedIndex = 0 };
        range.SelectionChanged += (_, _) => { Plot.Range = range.SelectedIndex switch { 1 => TimeSpan.FromHours(2), 2 => TimeSpan.FromHours(24), _ => TimeSpan.FromMinutes(30) }; Plot.InvalidateVisual(); };
        DockPanel.SetDock(range, Dock.Right); chartTools.Children.Add(range);
        var metric = new ComboBox { Width = 105, ItemsSource = new[] { "总压", "电流", "温度", "SOC" }, SelectedIndex = 0 };
        metric.SelectionChanged += (_, _) => { Plot.Metric = metric.SelectedItem?.ToString() ?? "总压"; Plot.InvalidateVisual(); };
        chartTools.Children.Add(metric); chartPanel.Children.Add(chartTools); chartPanel.Children.Add(Plot);
        var main = new Grid { Margin = new(0, 14, 0, 0) };
        main.ColumnDefinitions.Add(new() { Width = new(1, GridUnitType.Star) });
        main.ColumnDefinitions.Add(new() { Width = new(compact ? 1 : .38, GridUnitType.Star) });
        var side = compact ? Panel(_measurements) : Panel(_details);
        side.Margin = new(0, 0, 12, 0); main.Children.Add(side);
        var chart = Panel(chartPanel); Grid.SetColumn(chart, 1); main.Children.Add(chart);
        if (!compact) { main.ColumnDefinitions[0].Width = new(1, GridUnitType.Star); main.ColumnDefinitions[1].Width = new(2.4, GridUnitType.Star); }
        root.Children.Add(main);
        var cellBody = new StackPanel(); cellBody.Children.Add(new TextBlock { Text = "单体电压 · mV", FontWeight = FontWeights.SemiBold });
        cellBody.Children.Add(CellAge); cellBody.Children.Add(_cells);
        _cellPanel = Panel(cellBody); _cellPanel.Margin = new(0, 14, 0, 0);
        if (!compact) { _cellPanel.Child = null; _cellPanel.Child = new Expander { Header = "单体电压", Content = cellBody, IsExpanded = false }; }
        root.Children.Add(_cellPanel);
        OperationsContainer.Content = Operations; OperationsContainer.IsExpanded = compact;
        OperationsContainer.Margin = new(0, 14, 0, 0); root.Children.Add(Panel(OperationsContainer));
        var stateBody = new StackPanel();
        stateBody.Children.Add(new TextBlock { Text = "状态与记录", FontWeight = FontWeights.SemiBold, Margin = new(0, 0, 0, 10) });
        stateBody.Children.Add(_fault); stateBody.Children.Add(RecordingStatus);
        var state = Panel(stateBody); state.Margin = new(0, 14, 0, 0); root.Children.Add(state);
        if (compact)
        {
            var debug = new StackPanel(); debug.Children.Add(new TextBlock { Text = "通信诊断", FontWeight = FontWeights.SemiBold, Margin = new(0, 0, 0, 8) });
            debug.Children.Add(Raw); var debugPanel = Panel(debug); debugPanel.Margin = new(0, 14, 0, 0); root.Children.Add(debugPanel);
        }
        Content = new ScrollViewer { Content = root, VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
            HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled, Background = new SolidColorBrush(Color.FromRgb(243, 246, 250)) };
        SetReading(null);
    }

    public static Button MakeButton(string text, bool primary = false) => new()
    {
        Content = text, Padding = new(13, 7, 13, 7), Margin = new(4, 2, 0, 2),
        Background = primary ? new SolidColorBrush(Color.FromRgb(37, 99, 235)) : Brushes.White,
        Foreground = primary ? Brushes.White : new SolidColorBrush(Color.FromRgb(43, 72, 107)),
        BorderBrush = new SolidColorBrush(Color.FromRgb(218, 227, 239)), BorderThickness = new(1)
    };
    public static Border Panel(UIElement child) => new() { Child = child, Background = Brushes.White,
        CornerRadius = new(8), Padding = new(16), BorderThickness = new(1), BorderBrush = new SolidColorBrush(Color.FromRgb(224, 231, 240)) };
    private static TextBlock Small(string text) => new() { Text = text, Foreground = Muted, FontSize = 12 };
    public void SetReading(MonitorReading? reading)
    {
        _cards.Children.Clear(); _details.Children.Clear(); _cells.Children.Clear();
        bool hasCurrent = reading?.HasCurrent ?? true;
        var values = new List<(string Name, double? Value, string Unit)>() {
            ("剩余电量", reading?.Soc, "%"), ("电池总压", reading?.Voltage, "V") };
        if (hasCurrent) values.Add((_compact ? "电流" : reading?.Current is < -0.05 ? "充电电流" :
            reading?.Current is > 0.05 ? "放电电流" : "电流",
            !_compact && reading?.Current is double amps ? Math.Abs(amps) : reading?.Current, "A"));
        values.Add(("电池温度", reading?.Temperature, "℃"));
        if (_compact && reading?.HasCells == true)
        {
            values.Add(("单体压差", reading.Cells.Count == 0 ? null : reading.Cells.Values.Max() - reading.Cells.Values.Min(), "mV"));
            values.Add(("循环次数", reading.Cycles, "次"));
        }
        _cards.Columns = values.Count;
        foreach (var value in values)
        {
            var body = new StackPanel();
            body.Children.Add(Small(value.Name));
            string number = value.Value?.ToString(value.Unit == "A" && reading?.CurrentResolutionA == 1 ? "0" : "0.##", CultureInfo.CurrentCulture) ?? "—";
            body.Children.Add(new TextBlock { Text = number + " " + value.Unit, FontSize = _compact ? 23 : 31,
                FontWeight = FontWeights.SemiBold, Margin = new(0, _compact ? 8 : 14, 0, _compact ? 2 : 8) });
            var card = Panel(body); card.Margin = new(0, 0, 10, 0); _cards.Children.Add(card);
        }
        _identity.Text = reading is null ? "连接设备后自动接收数据" :
            $"{reading.Source} · {reading.Protocol}" + (string.IsNullOrEmpty(reading.Identity) ? "" : " · " + reading.Identity);
        ProtocolTag.Text = reading?.Protocol ?? "";
        _details.Children.Add(Small("当前连接"));
        _details.Children.Add(new TextBlock { Text = reading?.Source ?? "—", Margin = new(0, 9, 0, 12), FontSize = 16 });
        _details.Children.Add(Small("协议"));
        _details.Children.Add(new TextBlock { Text = reading?.Protocol ?? "—", Margin = new(0, 9, 0, 12), TextWrapping = TextWrapping.Wrap });
        _details.Children.Add(Small(reading?.CurrentResolutionA == 1 ? "扩展电流分辨率 1 A" : ""));
        _measurements.ItemsSource = values.Select(v => new { 数据项 = v.Name,
            数值 = v.Value?.ToString("0.##", CultureInfo.CurrentCulture) ?? "—", 单位 = v.Unit }).ToArray();
        _cellPanel.Visibility = reading?.HasCells == false ? Visibility.Collapsed : Visibility.Visible;
        CellAge.Text = reading?.CellsUtc is DateTimeOffset utc ? $"单体独立更新时间：{utc.ToLocalTime():HH:mm:ss}" : "等待有效单体数据";
        if (reading is not null) foreach (var cell in reading.Cells.OrderBy(x => x.Key))
        {
            var body = new StackPanel();
            body.Children.Add(Small(cell.Key.ToString("D2")));
            body.Children.Add(new TextBlock { Text = cell.Value.ToString("0"), FontWeight = FontWeights.SemiBold, Margin = new(0, 4, 0, 0) });
            _cells.Children.Add(new Border { Child = body, Width = _compact ? 82 : 78, Padding = new(10, 7, 10, 7),
                Margin = new(0, 7, 7, 0), Background = new SolidColorBrush(Color.FromRgb(241, 246, 253)), CornerRadius = new(5) });
        }
        _fault.Text = reading is null ? "等待有效数据" : !reading.HasFault ? "当前协议提供电量、总压和电池温度" :
            reading.FaultCode == 0 ? "无上报告警" : reading.Fault ?? "状态未提供";
        _fault.Foreground = reading?.FaultCode is > 0 ? Brushes.DarkOrange : new SolidColorBrush(Color.FromRgb(24, 125, 85));
    }
}
