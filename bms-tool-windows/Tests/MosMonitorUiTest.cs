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
        if (typeof(MainWindow).GetField("_protectedFeaturesUnlocked", Private) is not null) {
            Check(!tabs.Items.OfType<TabItem>().Any(t => t.Header?.ToString() == "MOS 监控"), "customer edition has no MOS monitor page");
            Check(((CheckBox)Get(window, "_mosAuto")!).IsChecked == false, "customer edition disables background MOS recording");
            var customerClient = new BmsClient(new NoIoTransport());
            Set(window, "_bms", customerClient);
            Call(window, "ApplyBatterySnapshot", new BatterySnapshot { SystemStatus = 12, MinCellMv = 3300, MaxCellMv = 3301 });
            Check(Get(window, "_mosJournal") is null, "customer snapshots do not open a MOS journal");
            Check(((TextBlock)window.FindName("CustomerOpenWireText")).Visibility == Visibility.Collapsed, "home page hides open-wire explanation");
            window.Close();
            Console.WriteLine("PASS actual WPF customer: MOS page removed, background journal disabled, home explanation hidden");
            return 0;
        }
        var page = tabs.Items.OfType<TabItem>().Single(t => t.Header?.ToString() == "MOS 监控");
        tabs.SelectedItem = page;
        var grid = ((DockPanel)page.Content).Children.OfType<DataGrid>().Single();
        Check(((DataGridTextColumn)grid.Columns.Last()).ElementStyle.Setters.OfType<Setter>().Any(s =>
            s.Property == TextBlock.TextWrappingProperty && (TextWrapping)s.Value == TextWrapping.Wrap),
            "reason text wraps and remains readable in narrow windows");
        Check(((CheckBox)Get(window, "_mosAuto")!).IsChecked == true, "monitor defaults enabled in actual window");
        var client = new BmsClient(new NoIoTransport());
        Set(window, "_bms", client);
        Call(window, "SetMosIdentity", client, new DeviceIdentity("mock", "UI-test", "D011", "test", "mock"));
        // 使用同一实际 writer，把模拟记录限制在测试证据目录，不混入用户监控记录。
        string testParent = args.Length > 0 ? Path.Combine(args[0], "journals") :
            Path.Combine(Path.GetTempPath(), "BmsMonitorUi", Guid.NewGuid().ToString("N"));
        var testJournal = new BmsMosJournal(testParent);
        Set(window, "_mosJournal", testJournal); Set(window, "_mosDirectory", testJournal.DirectoryPath);
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
        // 同启动连续固件记录可补回：5 秒显示过期不能丢掉关命令起点。
        MosHistoryCapture Capture(uint seq, ushort command, uint tick) {
            var header = new ushort[16]; header[0]=0x4D48;header[1]=header[2]=1;header[3]=8;header[4]=32;header[5]=1;header[6]=(ushort)seq;
            header[10]=123;header[12]=456;header[14]=0x1124;
            var w = new ushort[32];w[0]=(ushort)seq;w[2]=(ushort)tick;w[3]=(ushort)(tick>>16);
            w[4]=3;w[6]=command;w[7]=w[9]=w[19]=1;w[8]=3;w[27]=0x1124;
            var entry=new MosHistoryEntry(w);var capture=new MosHistoryCapture {Header=header,Current=entry};capture.Entries.Add(entry);return capture;
        }
        Call(window,"ApplyMosCapture",client,Capture(1,15,100));
        Call(window,"NoteMosDataStale");
        Call(window,"ApplyMosCapture",client,Capture(2,0,1000));
        var tracker=(MosCommandTracker)Get(window,"_mosCommandTracker")!;
        Check(tracker.OffEvents==1,"stale UI preserves complete firmware command edge");
        Call(window,"NoteMosDataStale");
        Call(window,"ApplyMosCapture",client,Capture(3,11,20520));
        Call(window,"ApplyMosCapture",client,Capture(4,15,26720));
        using(var fs= new FileStream(Path.Combine(directory,"mos_0001.jsonl"),FileMode.Open,FileAccess.Read,FileShare.ReadWrite)) {
            using var reader=new StreamReader(fs);string journal=reader.ReadToEnd();
            Check(journal.Contains("610.00 ms") && journal.Contains("803.75 ms"),"complete firmware history preserves duration across stale battery display");
        }
        Call(window,"NoteMosGap","设备断开或重连");
        Call(window,"ApplyMosCapture",client,Capture(5,0,30000));
        Check(tracker.OffEvents==1,"real disconnect cannot fabricate an edge from old baseline");
        // 只对有协议日志 TextBox 的内部版执行；客户版原本不在 UI 追加分片。
        if (typeof(MainWindow).GetMethod("QueueUiLog",Private) is not null) {
            var logBox=(TextBox)window.FindName("LogBox");
            Check(!logBox.IsUndoEnabled,"read-only protocol log retains no undo history");
            for(int block=0;block<40;block++) {
                for(int i=0;i<500;i++) Call(window,"QueueUiLog",$"flood-{block}-{i} " + new string('x',512),"TEST");
                for(int i=0;i<5;i++) Call(window,"FlushUiLog");
                Check(logBox.Text.Length<=262144,"20,000 protocol lines cannot grow visible log without bound");
            }
            Check(logBox.Text.Contains("flood-39-499"),"bounded log retains latest entries");
            for(int i=0;i<3000;i++) Call(window,"QueueUiLog",new string('z',512),"TEST");
            Check((int)Get(window,"_uiLogPendingChars")!<=262144,"pending display work is bounded during dispatcher starvation");
            Call(window,"AppendLog","full-file-marker-"+new string('q',20000),"TEST");
            var session=(SessionLogger)Get(window,"_sessionLog")!;
            using var fs=new FileStream(session.FilePath,FileMode.Open,FileAccess.Read,FileShare.ReadWrite);
            using var reader=new StreamReader(fs);
            Check(reader.ReadToEnd().Contains("full-file-marker-"+new string('q',20000)),"UI truncation never truncates original file record");
            Console.WriteLine("PASS bounded log: 20,000 lines, pending queue cap, recent tail, full file retained");
        }
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
        var openWirePage = tabs.Items.OfType<TabItem>().Single(t => t.Header?.ToString() == "断线检测");
        tabs.SelectedItem = openWirePage;
        var openWirePanel = (DockPanel)openWirePage.Content;
        var openWireAuto = openWirePanel.Children.OfType<CheckBox>().Single();
        Check(openWireAuto.IsChecked == true, "open-wire monitoring visible and enabled by default");
        openWireAuto.IsChecked = false;
        Check(((CheckBox)Get(window, "_mosAuto")!).IsChecked == false, "open-wire shares actual monitor lifecycle and OTA cancellation");
        openWireAuto.IsChecked = true;
        var owWords = new ushort[48]; owWords[0] = 0x4F57; owWords[1] = 1; owWords[2] = 2;
        owWords[3] = 2; owWords[4] = 48; owWords[16] = owWords[18] = 2; owWords[30] = 3; owWords[31] = 20;
        owWords[32] = (ushort)(300000u & 0xFFFFu); owWords[33] = (ushort)(300000u >> 16); owWords[46] = 800;
        var demonstration = BmsOpenWireStatus.Decode(owWords);
        ((TextBlock)Get(window,"_openWireStatus")!).Text = "离屏布局测试：模拟数据，无实板通信\n" + demonstration.Summary;
        ((TextBlock)Get(window,"_openWirePath")!).Text = "测试记录：" + directory;
        var rowType = typeof(MainWindow).GetNestedType("OpenWireRow", BindingFlags.NonPublic)!;
        ((IList)Get(window,"_openWireRows")!).Add(Activator.CreateInstance(rowType,
            new object[] { "22:00:00.000", demonstration.StateText, demonstration.Summary })!);
        foreach (var size in new[] { (1360,900), (1180,760) }) foreach (double scale in new[] { 1d,1.25,1.5 }) {
            content.LayoutTransform = new ScaleTransform(scale,scale);
            content.Measure(new Size(size.Item1,size.Item2)); content.Arrange(new Rect(0,0,size.Item1,size.Item2)); content.UpdateLayout();
            var state = (TextBlock)Get(window,"_openWireStatus")!;
            Check(openWirePage.IsSelected && state.ActualHeight > 0 && openWirePanel.Children.OfType<DataGrid>().Single().ActualHeight > 100,
                "open-wire status and history visible at supported sizes/scales");
            Console.WriteLine($"PASS OpenWire UI layout {size.Item1}x{size.Item2} scale={scale}");
            if (args.Length > 0 && scale == 1) {
                var bitmap = new RenderTargetBitmap(size.Item1,size.Item2,96,96,PixelFormats.Pbgra32); bitmap.Render(content);
                var encoder = new PngBitmapEncoder(); encoder.Frames.Add(BitmapFrame.Create(bitmap));
                using var file = File.Create(Path.Combine(args[0],$"openwire-{size.Item1}-{size.Item2}.png")); encoder.Save(file);
            }
        }
        tabs.SelectedIndex = 0;
        var mainStatus = (TextBlock)window.FindName("CustomerOpenWireText");
        owWords[3] = 3; owWords[4] = 52; owWords[22] = 5;
        Call(window, "ApplyMainOpenWireStatus", BmsOpenWireStatus.Decode(owWords));
        Check(mainStatus.Text.Contains("C1, C3") && mainStatus.Foreground == Brushes.Firebrick,
            "hidden compatibility text retains decoded latched channels with SH mapping qualification");
        foreach (var size in new[] { (1360,900), (1180,760) }) foreach (double scale in new[] { 1d,1.25,1.5 }) {
            content.LayoutTransform = new ScaleTransform(scale,scale);
            content.Measure(new Size(size.Item1,size.Item2)); content.Arrange(new Rect(0,0,size.Item1,size.Item2)); content.UpdateLayout();
            Check(tabs.SelectedIndex == 0 && mainStatus.Visibility == Visibility.Collapsed,
                "home page keeps open-wire explanation hidden at supported sizes/scales");
            Console.WriteLine($"PASS hidden Main OpenWire UI {size.Item1}x{size.Item2} scale={scale}");
            if (args.Length > 0 && scale == 1) {
                var bitmap = new RenderTargetBitmap(size.Item1,size.Item2,96,96,PixelFormats.Pbgra32); bitmap.Render(content);
                var encoder = new PngBitmapEncoder(); encoder.Frames.Add(BitmapFrame.Create(bitmap));
                using var file = File.Create(Path.Combine(args[0],$"main-openwire-{size.Item1}-{size.Item2}.png")); encoder.Save(file);
            }
        }
        Set(window, "_openWireClient", client); Set(window, "_openWireReceived", DateTimeOffset.UtcNow.AddSeconds(-6));
        Call(window, "RefreshMainOpenWireFreshness");
        Check(mainStatus.Text.Contains("未知") && mainStatus.Foreground == Brushes.DimGray, "stale main page never shows current confirmed channel");
        Set(window, "_openWireReceived", DateTimeOffset.UtcNow); openWireAuto.IsChecked = false;
        Call(window, "RefreshMainOpenWireFreshness"); Check(mainStatus.Text.Contains("暂停"), "paused monitor shows unknown on main page");
        openWireAuto.IsChecked = true; Set(window, "_bms", null!);
        Call(window, "RefreshMainOpenWireFreshness"); Check(mainStatus.Text.Contains("未连接"), "disconnect removes current channel claim");
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
