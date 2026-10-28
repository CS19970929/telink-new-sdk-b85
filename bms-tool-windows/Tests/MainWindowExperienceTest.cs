using System.Reflection;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using BmsTool.Windows;

// 离屏验证主窗口交互与小屏布局，不显示窗口、不扫描或连接设备。
static class MainWindowExperienceTest
{
    private const BindingFlags PrivateInstance = BindingFlags.Instance | BindingFlags.NonPublic;
    private static bool _passed = true;

    [STAThread]
    static int Main()
    {
        var app = new Application();
        var window = new MainWindow();
        var type = typeof(MainWindow);

        void Check(bool ok, string message)
        {
            Console.WriteLine($"{(ok ? "PASS" : "FAIL")} {message}");
            _passed &= ok;
        }

        var deviceList = (ComboBox)window.FindName("DeviceList");
        MethodInfo upsert = type.GetMethod("UpsertDevice", PrivateInstance)!;
        const ulong address = 0xAABBCCDDEEFF;
        upsert.Invoke(window, new object[] { new DiscoveredDevice(address, "BT_D0148s", -62) });
        deviceList.SelectedIndex = 0;
        upsert.Invoke(window, new object[] { new DiscoveredDevice(address, "BT_D0148s", -48) });
        Check(deviceList.SelectedItem is DiscoveredDevice selected && selected.Address == address,
            "同一 MAC 的重复广播不会清除已选设备");

        GroupBox system = Descendants(window).OfType<GroupBox>()
            .Single(box => Equals(box.Header, "系统状态"));
        string systemText = string.Join("|", VisibleText(system));
        Check(!systemText.Contains("加热") && !systemText.Contains("制冷") && !systemText.Contains("均衡"),
            "系统状态只显示充放电 MOS");
        Check(!systemText.Contains("用户许可") && !systemText.Contains("连接后读取"),
            "系统状态不显示说明性状态文字");
        Check(VisibleButtons(system).Count(button => button.Contains("充电 MOS")) == 1 &&
              VisibleButtons(system).Count(button => button.Contains("放电 MOS")) == 1,
            "系统状态保留两个 MOS 控制按钮");

        Check(window.MinWidth <= 900 && window.MinHeight <= 600,
            "窗口最小尺寸适配 900x600 可用区域");
        var realtimeScroller = window.FindName("RealtimeScrollViewer") as ScrollViewer;
        Check(realtimeScroller is not null &&
              realtimeScroller.HorizontalScrollBarVisibility == ScrollBarVisibility.Auto &&
              realtimeScroller.VerticalScrollBarVisibility == ScrollBarVisibility.Auto,
            "实时监控页在小窗口下可滚动访问完整内容");
        var realtimeContent = realtimeScroller?.Content as FrameworkElement;
        Binding? widthBinding = realtimeContent is null
            ? null
            : BindingOperations.GetBinding(realtimeContent, FrameworkElement.WidthProperty);
        Check(realtimeContent is not null &&
              widthBinding?.ElementName == "RealtimeScrollViewer" &&
              widthBinding.Path?.Path == "ViewportWidth" &&
              realtimeContent.Margin == new Thickness(0),
            "实时监控页按可视区宽度排版，宽屏不会被电芯列表横向撑开");
        var root = (FrameworkElement)window.Content;
        foreach (var size in new[] { new Size(1180, 760), new Size(1920, 1000) })
        {
            Arrange(root, size);
            Check(realtimeScroller!.ScrollableWidth < 1,
                $"实时监控页在 {size.Width:0}x{size.Height:0} 下无横向溢出");
        }
        Arrange(root, new Size(900, 600));
        Check(realtimeScroller!.ScrollableWidth > 1,
            "实时监控页仅在小于设计宽度时提供横向滚动");
        foreach (string name in new[] { "CustomerAlarm1Text", "CustomerAlarm2Text", "CustomerAlarm3Text" })
        {
            var alarm = (TextBlock)window.FindName(name);
            Check(Ancestors(alarm).OfType<ScrollViewer>().Any(), $"{name} 长文本不会被固定高度裁剪");
        }

        type.GetMethod("AddParameterTab", PrivateInstance)!.Invoke(window, null);
        var tabs = (TabControl)window.FindName("MainTabs");
        var businessTab = tabs.Items.OfType<TabItem>().Single(item => Equals(item.Header, "业务参数"));
        string businessText = string.Join("|", VisibleText((DependencyObject)businessTab.Content));
        string[] verbosePhrases = {
            "读取的是设备当前保存值", "容量单位为", "SN 为工厂身份字段",
            "禁止后掉电", "压差相对有效最低单体计算", "请先断开充电器和负载",
            "读取设备当前完整 profile", "多个条件取最先到期项", "过放未恢复：固定"
        };
        Check(verbosePhrases.All(phrase => !businessText.Contains(phrase)),
            "业务参数页移除冗长说明文字");

        type.GetMethod("AddSh3520ParameterTab", PrivateInstance)!.Invoke(window, null);
        var hsTab = tabs.Items.OfType<TabItem>().Single(item => Equals(item.Header, "3520 参数"));
        var hsGrid = Descendants((DependencyObject)hsTab.Content).OfType<DataGrid>().Single();
        Check(hsGrid.Columns.Count == 5 && !hsGrid.AutoGenerateColumns && !hsGrid.CanUserAddRows,
            "3520 参数使用明确列定义，禁止插入或隐式生成寄存器行");
        string hsText = string.Join("|", VisibleText((DependencyObject)hsTab.Content));
        Check(hsText.Contains("只读测量值"), "3520 页面区分测量和可写配置");
        var hsButtons = Descendants((DependencyObject)hsTab.Content).OfType<Button>().ToArray();
        Check(hsButtons.Any(b => Equals(b.Content, "保存整组硬件保护并读回")) &&
              hsButtons.Any(b => Equals(b.Content, "保存 SN")), "两版均提供3520硬件保护和SN读写入口");
        hsButtons.Single(b => Equals(b.Content, "读取 3520 参数及基本信息")).RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        var hsStatus = (TextBlock)type.GetField("_hs3520Status", PrivateInstance)!.GetValue(window)!;
        Check(hsStatus.Text.Contains("请先连接"), "3520页面未连接时不执行事务");

        window.Close();
        Console.WriteLine(_passed ? "MAIN_WINDOW_EXPERIENCE_PASS" : "MAIN_WINDOW_EXPERIENCE_FAIL");
        return _passed ? 0 : 1;
    }

    private static IEnumerable<DependencyObject> Descendants(DependencyObject root)
    {
        foreach (object child in LogicalTreeHelper.GetChildren(root))
        {
            if (child is not DependencyObject node) continue;
            yield return node;
            foreach (DependencyObject descendant in Descendants(node)) yield return descendant;
        }
    }

    private static IEnumerable<DependencyObject> Ancestors(DependencyObject child)
    {
        for (DependencyObject? current = LogicalTreeHelper.GetParent(child);
             current is not null;
             current = LogicalTreeHelper.GetParent(current))
            yield return current;
    }

    private static void Arrange(FrameworkElement element, Size size)
    {
        element.InvalidateMeasure();
        element.Measure(size);
        element.Arrange(new Rect(size));
        element.UpdateLayout();
    }

    private static IEnumerable<string> VisibleText(DependencyObject root)
    {
        foreach (DependencyObject node in Descendants(root).Prepend(root))
        {
            if (node is UIElement element && element.Visibility != Visibility.Visible) continue;
            if (node is TextBlock text && !string.IsNullOrWhiteSpace(text.Text)) yield return text.Text;
        }
    }

    private static IEnumerable<string> VisibleButtons(DependencyObject root) =>
        Descendants(root).OfType<Button>()
            .Where(button => button.Visibility == Visibility.Visible)
            .Select(button => button.Content?.ToString() ?? string.Empty);
}
