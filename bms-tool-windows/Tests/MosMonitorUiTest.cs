using System.Collections;
using System.IO;
using System.Reflection;
using System.Text.Json;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using BmsTool.Windows;

// 使用编译后的真实 MainWindow 离屏执行。无 Show、无串口/BLE 或用户窗口操作。
static class MosMonitorUiTest
{
    const BindingFlags Private = BindingFlags.Instance | BindingFlags.NonPublic;
    static void Check(bool condition, string message) { if (!condition) throw new Exception(message); }
    static void Call(MainWindow w, string name, params object[] values) => typeof(MainWindow).GetMethod(name, Private)!.Invoke(w, values);
    static object? Get(MainWindow w, string name) => typeof(MainWindow).GetField(name, Private)!.GetValue(w);
    static void Set(MainWindow w, string name, object value) => typeof(MainWindow).GetField(name, Private)!.SetValue(w, value);
    [STAThread]
    static int Main(string[] args)
    {
        var app = new Application(); var window = new MainWindow();
        Call(window, "AddDiagnosticTab");
        var tabs = (TabControl)window.FindName("MainTabs");
        var page = tabs.Items.OfType<TabItem>().Single(t => t.Header?.ToString() == "MOS 监控");
        tabs.SelectedItem = page;
        Check(((CheckBox)Get(window, "_mosAuto")!).IsChecked == true, "monitor defaults enabled in actual window");
        var client = new BmsClient(new NoIoTransport());
        Set(window, "_bms", client);
        Call(window, "SetMosIdentity", client, new DeviceIdentity("mock", "UI-test", "D011", "test", "mock"));
        Call(window, "ApplyBatterySnapshot", new BatterySnapshot { SystemStatus = 12, MinCellMv = 3300, MaxCellMv = 3301 });
        Call(window, "ApplyBatterySnapshot", new BatterySnapshot { SystemStatus = 8, MinCellMv = 3300, MaxCellMv = 3301 });
        string directory = (string)Get(window, "_mosDirectory")!;
        using (var fs = new FileStream(Path.Combine(directory, "mos_0001.jsonl"), FileMode.Open, FileAccess.Read, FileShare.ReadWrite)) {
            using var reader = new StreamReader(fs);
            var entries = reader.ReadToEnd().Split('\n', StringSplitOptions.RemoveEmptyEntries).Select(line => JsonDocument.Parse(line)).ToArray();
            try {
                Check(entries.Count(e => e.RootElement.GetProperty("kind").GetString() == "sample") == 2, "actual ApplyBatterySnapshot auto writes both samples");
                Check(entries.Last().RootElement.GetProperty("data").GetProperty("indicationChanged").GetBoolean(), "actual GUI edge persisted before diagnostics");
            }
            finally { foreach (var e in entries) e.Dispose(); }
        }
        Check(((ICollection)Get(window, "_mosRows")!).Count == 1, "edge appears in visible list");
        Set(window, "_mosDriverInvalid", true); Call(window, "UpdateCustomerSystemLamps");
        Check(((TextBlock)window.FindName("CustomerChargeMosText")).Text == "--", "invalid AFE evidence shows UNKNOWN lamp");
        Set(window, "_mosDriverInvalid", false); Call(window, "UpdateCustomerSystemLamps");
        Check(((TextBlock)window.FindName("CustomerChargeMosText")).Text == "off", "valid fresh reported OFF remains visible");
        Set(window, "_customerLastDataUtc", DateTime.UtcNow.AddSeconds(-6)); Call(window, "UpdateCustomerSystemLamps");
        Check(((TextBlock)window.FindName("CustomerDischargeMosText")).Text == "--", "stale data never masquerades as valid MOS state");
        var content = (FrameworkElement)window.Content;
        foreach (var size in new[] { (1360,900), (1180,760) }) foreach (double scale in new[] { 1d,1.25,1.5 }) {
            content.LayoutTransform = new ScaleTransform(scale,scale);
            content.Measure(new Size(size.Item1,size.Item2)); content.Arrange(new Rect(0,0,size.Item1,size.Item2)); content.UpdateLayout();
            var state = (TextBlock)Get(window,"_mosStatus")!;
            Check(page.IsSelected && state.Visibility == Visibility.Visible && state.ActualHeight > 0 && ((FrameworkElement)page.Content).ActualHeight > 200,
                "MOS page, status and table have visible layout");
            Console.WriteLine($"PASS MOS UI layout {size.Item1}x{size.Item2} scale={scale}");
            if (args.Length > 0 && scale == 1) {
                Directory.CreateDirectory(args[0]);
                var bitmap = new RenderTargetBitmap(size.Item1,size.Item2,96,96,PixelFormats.Pbgra32); bitmap.Render(content);
                var encoder = new PngBitmapEncoder(); encoder.Frames.Add(BitmapFrame.Create(bitmap));
                using var file = File.Create(Path.Combine(args[0],$"mos-{size.Item1}-{size.Item2}.png")); encoder.Save(file);
            }
        }
        Console.WriteLine("MOS UI journal: " + directory);
        window.Close();
        Console.WriteLine("PASS actual WPF MOS monitor: auto journal, indication edge, invalid/stale UNKNOWN, both sizes and scaling");
        return 0;
    }
    sealed class NoIoTransport : IBmsTransport
    {
        public bool IsConnected => true;
        public bool RequiresSerialWakeup => false;
        public string DiscoveryDescription => "offline UI test";
        public event Action<ReadOnlyMemory<byte>>? DataReceived { add { } remove { } }
        public event Action<string>? ConnectionProgress { add { } remove { } }
        public Task WriteAsync(ReadOnlyMemory<byte> data, CancellationToken ct = default) => throw new Exception("offline UI test must never send hardware I/O");
        public Task ReconnectAsync(CancellationToken ct = default) => Task.CompletedTask;
        public ValueTask DisposeAsync() => ValueTask.CompletedTask;
    }
}
