using System.Reflection;
using System.Windows;
using System.Windows.Controls;
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
