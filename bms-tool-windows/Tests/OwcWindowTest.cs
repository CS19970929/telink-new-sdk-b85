using System.Buffers.Binary;
using System.Reflection;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using BmsTool.Windows;

static class OwcWindowTest
{
    [STAThread]
    static int Main(string[] args)
    {
        var app = new Application();
        var main = new MainWindow();
        if (main.FindName("OwcMonitorButton") is not Button entry || (string)entry.Content != "一线通监控")
            throw new Exception("主窗口缺少一线通入口");
        var window = new OwcMonitorWindow();
        var content = (Grid)window.Content;
        content.Background = Brushes.White;
        var table = content.Children.OfType<DataGrid>().Single();
        byte[] raw = new byte[33];
        raw[0] = 0x3A; raw[1] = 0x13; raw[2] = 29; raw[3] = 160;
        BinaryPrimitives.WriteUInt16LittleEndian(raw.AsSpan(4), 520);
        BinaryPrimitives.WriteUInt16LittleEndian(raw.AsSpan(6), 4877);
        raw[8] = 65; raw[9] = 60; raw[10] = 70; raw[12] = 1;
        raw[^1] = (byte)(raw.Take(raw.Length - 1).Sum(b => (int)b) & 255);
        var live = new OwcSnapshot(true, true, 100, 10, 0, 0, 0, 10, raw, 0, 1);
        var snapshots = new Dictionary<byte, OwcSnapshot> { [0] = live, [0x3A] = live };
        window.ShowSnapshots(snapshots);
        var rows = table.Items.OfType<OwcField>().ToArray();
        if (!rows.Any(r => r.Name == "总电压" && r.Value == "52") ||
            !rows.Any(r => r.Name == "电流" && r.Value == "-12.3"))
            throw new Exception("v1实时测量未显示");
        content.Measure(new Size(980, 700));
        content.Arrange(new Rect(0, 0, 980, 700));
        content.UpdateLayout();
        if (args.Length > 0)
        {
            Directory.CreateDirectory(args[0]);
            var bitmap = new RenderTargetBitmap(980, 700, 96, 96, PixelFormats.Pbgra32);
            bitmap.Render(content);
            var png = new PngBitmapEncoder();
            png.Frames.Add(BitmapFrame.Create(bitmap));
            using var stream = File.Create(Path.Combine(args[0], "owc-fixture.png"));
            png.Save(stream);
        }
        snapshots[0] = snapshots[0x3A] = live with { AgeMs = 6000 };
        window.ShowSnapshots(snapshots);
        if (table.Items.OfType<OwcField>().Any(r => r.Name == "总电压"))
            throw new Exception("离线数据仍显示为测量");
        var modes = content.Children.OfType<WrapPanel>().SelectMany(panel => panel.Children.OfType<ComboBox>())
            .Single(combo => combo.Items.Count == 3 && Equals(combo.Items[0], "自动识别"));
        if (modes.SelectedIndex != 0) throw new Exception("接收模式默认不是自动");
        byte[] lishenRaw = [0, 80, 76, 236, 0, 136];
        var lishen = new OwcSnapshot(true, true, 100, 2, 0, 0, 0, 2, lishenRaw, 9, 3)
            { Mode = OwcReceiveMode.Auto, Protocol = OwcWireProtocol.LishenV111, FrameProtocol = OwcWireProtocol.LishenV111 };
        var modern = new Dictionary<byte, OwcSnapshot> { [0] = lishen, [0x80] = lishen, [0x3A] = live };
        window.ShowSnapshots(modern);
        rows = table.Items.OfType<OwcField>().ToArray();
        if (!rows.Any(r => r.Name == "电池温度" && r.Value == "-20") ||
            !rows.Any(r => r.Name == "总电压" && r.Value == "76") ||
            rows.Any(r => r.Name == "电流"))
            throw new Exception("锂神页面量纲、温度或跨协议隔离错误");
        if (args.Length > 0)
        {
            content.Measure(new Size(980, 700)); content.Arrange(new Rect(0, 0, 980, 700)); content.UpdateLayout();
            var bitmap = new RenderTargetBitmap(980, 700, 96, 96, PixelFormats.Pbgra32); bitmap.Render(content);
            var png = new PngBitmapEncoder(); png.Frames.Add(BitmapFrame.Create(bitmap));
            using var stream = File.Create(Path.Combine(args[0], "owc-lishen-fixture.png")); png.Save(stream);
        }
        modes.SelectedIndex = 2;
        if (table.Items.Count != 0) throw new Exception("切换模式未清数据");
        modern[0] = modern[0x80] = lishen with { MismatchProtocol = OwcWireProtocol.A133 };
        window.ShowSnapshots(modern);
        if (table.Items.OfType<OwcField>().Any(r => r.Name == "总电压" || r.Name == "电池温度"))
            throw new Exception("不匹配时旧测量仍可见");
        modern[0] = modern[0x80] = lishen with { AgeMs = 5001 };
        window.ShowSnapshots(modern);
        if (table.Items.OfType<OwcField>().Any(r => r.Name == "总电压"))
            throw new Exception("锂神离线仍显示旧测量");
        var waiting = lishen with { Protocol = OwcWireProtocol.Unknown, FrameProtocol = OwcWireProtocol.Unknown,
            Raw = [], ChecksumOk = false };
        window.ShowSnapshots(new Dictionary<byte, OwcSnapshot> { [0] = waiting });
        if (table.Items.Count != 0) throw new Exception("自动确认前有测量");
        const BindingFlags flags = BindingFlags.NonPublic | BindingFlags.Instance;
        using var cancel = new CancellationTokenSource();
        var running = new TaskCompletionSource();
        typeof(OwcMonitorWindow).GetField("_running", flags)!.SetValue(window, running.Task);
        typeof(OwcMonitorWindow).GetField("_stopSource", flags)!.SetValue(window, cancel);
        modes.SelectedIndex = 1;
        if (!cancel.IsCancellationRequested || !(bool)typeof(OwcMonitorWindow).GetField("_restartAfterStop", flags)!.GetValue(window)!)
            throw new Exception("运行中换模式未取消旧任务并排队重新接收");
        typeof(OwcMonitorWindow).GetMethod("Stop", flags)!.Invoke(window, [false]);
        if ((bool)typeof(OwcMonitorWindow).GetField("_restartAfterStop", flags)!.GetValue(window)!)
            throw new Exception("用户停止不能撤销自动重新接收");
        typeof(OwcMonitorWindow).GetField("_running", flags)!.SetValue(window, null);
        typeof(OwcMonitorWindow).GetField("_stopSource", flags)!.SetValue(window, null);
        window.Close(); main.Close();
        Console.WriteLine("PASS OWC WPF: legacy and Lishen fields, defaults, mode changes, cancellation/restart, expiry and protocol isolation; no serial access");
        return 0;
    }
}
