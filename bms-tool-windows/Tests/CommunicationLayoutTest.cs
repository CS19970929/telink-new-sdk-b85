using System.IO;
using System.Text.Json;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;

// 只创建和离屏布局编译后的窗口，不 Show、不连接设备，不访问当前用户窗口。
static class CommunicationLayoutTest
{
    [STAThread]
    static int Main(string[] args)
    {
        var app = new Application();
        var window = new BmsTool.Windows.MainWindow();
        var content = (FrameworkElement)window.Content;
        var counter = window.FindName("CommunicationCountersText") as TextBlock;
        var build = (TextBlock)window.FindName("CustomerBuildText");
        build.Text = "固件 Build ID：1234abcd · dirty · production";
        ((TextBlock)window.FindName("CustomerClockText")).Text = "板载时间：2026-11-03 18:00:00 +08:00";
        bool passed = true;
        foreach (var (width, height) in new[] { (1360, 900), (1180, 760) }) {
            foreach (double scale in new[] { 1.0, 1.25, 1.5 }) {
                foreach (bool large in new[] { false, true }) {
                    if (counter is not null) counter.Text = large ?
                        "通信统计：TX 9223372036854775807帧 / RX 9223372036854775807帧 · 超时 999999999 · CRC 999999999 · 异常 999999999 · 发送失败 999999999 · 重连 999999999" :
                        "通信统计：TX 498帧 / RX 498帧 · 超时 0 · CRC 0 · 异常 0 · 发送失败 0 · 重连 0";
                    content.LayoutTransform = new ScaleTransform(scale, scale);
                    content.Measure(new Size(width, height));
                    content.Arrange(new Rect(0, 0, width, height));
                    content.UpdateLayout();
                    var footer = (Border)((Grid)build.Parent).Parent;
                    var page = (Grid)footer.Parent;
                    var anchor = counter ?? build;
                    var bounds = anchor.TransformToAncestor(page).TransformBounds(new Rect(anchor.RenderSize));
                    bool visible = build.Visibility == Visibility.Visible && bounds.Height > 0 &&
                        bounds.Top >= 0 && bounds.Bottom <= page.ActualHeight + 0.1;
                    var clock = (TextBlock)window.FindName("CustomerClockText");
                    var clockBounds = clock.TransformToAncestor(page).TransformBounds(new Rect(clock.RenderSize));
                    var buildBounds = build.TransformToAncestor(page).TransformBounds(new Rect(build.RenderSize));
                    visible &= clockBounds.Height > 0 && clockBounds.Top >= buildBounds.Bottom &&
                        clockBounds.Bottom <= page.ActualHeight + 0.1;
                    if (counter is not null) visible &= bounds.Top >= clockBounds.Bottom;
                    Console.WriteLine(JsonSerializer.Serialize(new {
                        width, height, scale, large, visible, counterTop = bounds.Top,
                        counterBottom = bounds.Bottom, pageHeight = page.ActualHeight,
                        footerRowHeight = page.RowDefinitions[Grid.GetRow(footer)].ActualHeight
                    }));
                    passed &= visible;
                    if (args.Length > 0 && scale == 1 && !large) {
                        Directory.CreateDirectory(args[0]);
                        var bitmap = new RenderTargetBitmap(width, height, 96, 96, PixelFormats.Pbgra32);
                        bitmap.Render(content);
                        SavePng(bitmap, Path.Combine(args[0], $"layout-{width}-{height}.png"));
                        var footerBitmap = new RenderTargetBitmap((int)Math.Ceiling(footer.ActualWidth),
                            (int)Math.Ceiling(footer.ActualHeight), 96, 96, PixelFormats.Pbgra32);
                        var footerVisual = new DrawingVisual();
                        using (var drawing = footerVisual.RenderOpen())
                            drawing.DrawRectangle(new VisualBrush(footer), null, new Rect(0, 0, footer.ActualWidth, footer.ActualHeight));
                        footerBitmap.Render(footerVisual);
                        SavePng(footerBitmap, Path.Combine(args[0], $"footer-{width}-{height}.png"));
                    }
                }
            }
        }
        window.Close();
        Console.WriteLine(passed ? "PASS communication footer layout: both window sizes, scaling and wrapped counters" :
            "FAIL communication footer layout: counters clipped by page");
        return passed ? 0 : 1;
    }

    private static void SavePng(BitmapSource bitmap, string path)
    {
        var encoder = new PngBitmapEncoder();
        encoder.Frames.Add(BitmapFrame.Create(bitmap));
        using var file = File.Create(path);
        encoder.Save(file);
    }
}

