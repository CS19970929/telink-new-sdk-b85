using System.Buffers.Binary;
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
        window.Close(); main.Close();
        Console.WriteLine("PASS OWC WPF: entry, v1 data rendering, offline clearing; no serial access");
        return 0;
    }
}
