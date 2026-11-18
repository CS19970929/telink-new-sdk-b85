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
        bool factory = type.Assembly.GetName().Name!.Contains("Factory");

        void Check(bool ok, string message)
        {
            Console.WriteLine($"{(ok ? "PASS" : "FAIL")} {message}");
            _passed &= ok;
        }

        using var scanner = new BmsBleScanner(_ => { });
        type.GetField("_watcher", PrivateInstance)!.SetValue(window, scanner);
        var catalog = (BleDiscoveryCatalog)typeof(BmsBleScanner).GetField("_catalog", PrivateInstance)!.GetValue(scanner)!;
        var refresh = type.GetMethod("RefreshBleDeviceList", PrivateInstance)!;
        var historyBox = (CheckBox)window.FindName("ShowHistoryDevicesBox");
        var discoveryList = (ComboBox)window.FindName("DeviceList");
        const ulong historical = 0x102030405060;
        catalog.ObserveSystem(historical, "BT_历史测试");
        refresh.Invoke(window, null);
        Check(historyBox?.IsChecked != true && discoveryList.Items.Count == 0, "默认列表不显示系统历史缓存");
        if (factory)
        {
            historyBox.IsChecked = true;
            refresh.Invoke(window, null);
            Check(discoveryList.Items.Count == 1 && ((DiscoveredDevice)discoveryList.Items[0]).Source == BleDiscoverySource.History,
                "内部版可以显式查看历史设备");
            historyBox.IsChecked = false;
        }
        else Check(historyBox is null, "客户版没有历史设备入口");
        catalog.ObserveAdvertisement(historical, "BT_附近测试", -55);
        refresh.Invoke(window, null);
        Check(discoveryList.Items.Count == 1 && ((DiscoveredDevice)discoveryList.Items[0]).HasRecentAdvertisement,
            "真实广播进入两版附近设备列表");
        catalog.RemoveSystem(historical);
        refresh.Invoke(window, null);
        Check(discoveryList.Items.Count == 1, "系统移除不会删除仍有新鲜广播的设备");
        const ulong systemAddress = 0x102030405061;
        var lastSeen = DateTimeOffset.UtcNow.AddHours(8);
        catalog.ObserveSystem(systemAddress, "BT_系统发现测试", lastSeen, false, -60);
        catalog.ObserveSystem(systemAddress, "BT_系统发现测试", lastSeen.AddSeconds(1), true, -61);
        refresh.Invoke(window, null);
        Check(discoveryList.Items.Cast<DiscoveredDevice>().Any(d => d.Address == systemAddress &&
            d.Source == BleDiscoverySource.SystemRecent), "系统广播更新在两版默认附近列表可见");
        Check(discoveryList.Items.Cast<DiscoveredDevice>().Single(d => d.Address == systemAddress)
            .ToString().Contains("系统发现"), "系统广播更新明确标注来源");
        type.GetField("_watcher", PrivateInstance)!.SetValue(window, null);
        refresh.Invoke(window, null);
        Check(discoveryList.Items.Count == 0, "新搜索状态不继承旧列表");
        Check(window.FindName("CancelConnectionButton") is Button, "两版均提供取消连接按钮");

        var deviceList = (ComboBox)window.FindName("DeviceList");
        MethodInfo upsert = type.GetMethod("UpsertDevice", PrivateInstance)!;
        const ulong address = 0xAABBCCDDEEFF;
        upsert.Invoke(window, new object[] { new DiscoveredDevice(address, "BT_D0148s", -62) });
        deviceList.SelectedIndex = 0;
        upsert.Invoke(window, new object[] { new DiscoveredDevice(address, "BT_D0148s", -48) });
        Check(deviceList.SelectedItem is DiscoveredDevice selected && selected.Address == address,
            "同一 MAC 的重复广播不会清除已选设备");

        Arrange((FrameworkElement)window.Content, new Size(1280, 850));
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
        var realtimeScroller = (ScrollViewer)window.FindName("RealtimeScrollViewer");
        Check(realtimeScroller.HorizontalScrollBarVisibility == ScrollBarVisibility.Disabled &&
              realtimeScroller.VerticalScrollBarVisibility == ScrollBarVisibility.Auto,
            "三栏实时监控按窗口宽度排版并提供纵向滚动");
        var root = (FrameworkElement)window.Content;
        foreach (var size in new[] { new Size(900,600), new Size(1180,760), new Size(1920,1000) })
        {
            Arrange(root,size);
            Check(realtimeScroller.ScrollableWidth < 1,$"三栏实时监控在 {size.Width} 宽度无横向溢出");
        }
        foreach (string name in new[] { "CustomerAlarm1Text", "CustomerAlarm2Text", "CustomerAlarm3Text" })
        {
            var alarm = (TextBlock)window.FindName(name);
            Check(Ancestors(alarm).OfType<ScrollViewer>().Any(), $"{name} 长文本不会被固定高度裁剪");
        }

        type.GetMethod("AddParameterTab", PrivateInstance)!.Invoke(window, null);
        var tabs = (TabControl)window.FindName("MainTabs");
        var businessTab = (TabItem)type.GetField("_businessParameterTab", PrivateInstance)!.GetValue(window)!;
        Check(tabs.Items.Contains(businessTab), "现有参数设置页仍在顶栏中");
        string businessText = string.Join("|", VisibleText((DependencyObject)businessTab.Content));
        string[] verbosePhrases = {
            "读取的是设备当前保存值", "容量单位为", "SN 为工厂身份字段",
            "禁止后掉电", "压差相对有效最低单体计算", "请先断开充电器和负载",
            "读取设备当前完整 profile", "多个条件取最先到期项", "过放未恢复：固定"
        };
        Check(verbosePhrases.All(phrase => !businessText.Contains(phrase)),
            "业务参数页移除冗长说明文字");

        string? preview = Environment.GetEnvironmentVariable("BMS_BLE_UI_PREVIEW");
        if (!string.IsNullOrWhiteSpace(preview))
        {
            tabs.SelectedIndex = 0;
            historyBox.IsChecked = true;
            upsert.Invoke(window, new object[]{new DiscoveredDevice(historical,"BT_DEFAULT",0) {
                Source=BleDiscoverySource.History,LastSeenUtc=null,LastSeenTimestamp=null }});
            discoveryList.SelectedIndex = 0;
            ((TextBlock)window.FindName("BleDiscoveryStatusText")).Text = "实时搜索已中止，请检查电脑蓝牙。\n最近12秒收到广播：0台；历史记录：1台。历史记录不代表在线。";
            Arrange(root, new Size(1280, 850));
            var bitmap = new System.Windows.Media.Imaging.RenderTargetBitmap(1280, 850, 96, 96, System.Windows.Media.PixelFormats.Pbgra32);
            bitmap.Render(root);
            var encoder = new System.Windows.Media.Imaging.PngBitmapEncoder();
            encoder.Frames.Add(System.Windows.Media.Imaging.BitmapFrame.Create(bitmap));
            string path = preview + "-" + typeof(MainWindow).Assembly.GetName().Name + ".png";
            using var file = System.IO.File.Create(path); encoder.Save(file);
        }
        Check((window.FindName("CommunicationCountersText") is not null)==factory,
            "仅测试版显示底部通信统计");
        var sections=Descendants((DependencyObject)businessTab.Content).OfType<TabItem>();
        Check(sections.Any(tab=>Equals(tab.Header,"D015配置"))==factory,"客户版不创建D015配置页，测试版保留");
        ((TextBlock)window.FindName("IdentityText")).Text="硬件版本：HS-D015\n软件版本：3.0.0\n序列号：TEST123\n固件 Build ID：1234abcd · dirty · production";
        type.GetMethod("UpdateCustomerIdentityFooter",PrivateInstance)!.Invoke(window,null);
        Check(((TextBlock)window.FindName("CustomerBuildText")).Text.Contains("1234abcd · dirty · production"),
            "首页底部显示设备信息页同一个固件Build ID与模式");

        var client=new BmsClient(new DisplayTransport());
        type.GetField("_bms",PrivateInstance)!.SetValue(window,client);
        var freshness=typeof(BmsClient).GetField("_lastSuccessfulResponseTick",PrivateInstance)!;
        var checkFreshness=type.GetMethod("CheckDeviceDisplayFreshness",PrivateInstance)!;
        freshness.SetValue(client,System.Diagnostics.Stopwatch.GetTimestamp()-7L*System.Diagnostics.Stopwatch.Frequency);
        Check((bool)checkFreshness.Invoke(window,null)!,"7秒以内仍保留已读设备值");
        ((TextBlock)window.FindName("PackVoltageText")).Text="48.00 V";
        var parameter=(TextBox)type.GetField("_parameterCapacity",PrivateInstance)!.GetValue(window)!;
        parameter.Text="30";
        Check(window.FindName("DeviceClockText") is TextBlock && window.FindName("CustomerClockText") is TextBlock,
            "设备信息与首页均提供板载时间");
        ((TextBlock)window.FindName("DeviceClockText")).Text = "板载时间：2026-11-03 18:00:00 +08:00";
        ((TextBlock)window.FindName("CustomerClockText")).Text = "板载时间：2026-11-03 18:00:00 +08:00";
        var cellPanel=(Panel)window.FindName("CustomerCellPanel");cellPanel.Children.Add(new Border());
        freshness.SetValue(client,System.Diagnostics.Stopwatch.GetTimestamp()-8L*System.Diagnostics.Stopwatch.Frequency-1);
        Check(!(bool)checkFreshness.Invoke(window,null)!,"达到8秒无成功响应即清空，不等待轮询任务完成");
        Check(((TextBlock)window.FindName("PackVoltageText")).Text=="—" && cellPanel.Children.Count==0,
            "电压与电芯卡片已清空");
        Check(((TextBlock)window.FindName("IdentityText")).Text=="—" && ((TextBlock)window.FindName("CustomerBuildText")).Text.EndsWith("—"),
            "身份、版本、序列号与Build ID同步清空");
        Check(((TextBlock)window.FindName("DeviceClockText")).Text == "—" &&
              ((TextBlock)window.FindName("CustomerClockText")).Text == "—", "通信超时清空两处板载时间");
        Check(parameter.Text=="" && ((TextBlock)window.FindName("CustomerAlarm3Text")).Text=="—",
            "业务参数与保护状态显示未知，不伪装成0或无故障");
        Check((bool)type.GetField("_identityRefreshRequired",PrivateInstance)!.GetValue(window)!,"恢复后需重新读取身份");
        freshness.SetValue(client,System.Diagnostics.Stopwatch.GetTimestamp());
        Check((bool)checkFreshness.Invoke(window,null)!,"收到新成功响应可恢复显示");
        type.GetMethod("ApplyBatterySnapshot",PrivateInstance)!.Invoke(window,new object[]{new BatterySnapshot {
            PackVoltageV=49.4,SocPercent=50,CellMillivolts=Enumerable.Repeat((ushort)3800,13).ToArray()}});
        Check(((TextBlock)window.FindName("PackVoltageText")).Text.Contains("49.40"),"恢复时显示新读数");
        type.GetField("_bms",PrivateInstance)!.SetValue(window,null);
        checkFreshness.Invoke(window,null);
        Check(((TextBlock)window.FindName("PackVoltageText")).Text=="—","手动断开与更换连接立即清空");
        type.GetMethod("InitializeAdvancedFeatures", PrivateInstance)!.Invoke(window, null);
        var hsTab = tabs.Items.OfType<TabItem>().SingleOrDefault(item => Equals(item.Header, "3520 参数"));
        Check((hsTab is not null) == factory, "3520维护页仅在内部版开放");
        if (hsTab is not null)
        {
            var hsGrid = Descendants((DependencyObject)hsTab.Content).OfType<DataGrid>().Single();
            Check(hsGrid.Columns.Count == 5 && !hsGrid.AutoGenerateColumns && !hsGrid.CanUserAddRows,
                "3520 参数使用明确列定义，禁止插入或隐式生成寄存器行");
            string hsText = string.Join("|", VisibleText((DependencyObject)hsTab.Content));
            Check(hsText.Contains("只读测量值"), "3520 页面区分测量和可写配置");
            var hsButtons = Descendants((DependencyObject)hsTab.Content).OfType<Button>().ToArray();
            Check(hsButtons.Any(b => Equals(b.Content, "保存整组硬件保护并读回")) &&
                  hsButtons.Any(b => Equals(b.Content, "保存 SN")), "内部版提供3520硬件保护和SN读写入口");
            hsButtons.Single(b => Equals(b.Content, "读取 3520 参数及基本信息")).RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            var hsStatus = (TextBlock)type.GetField("_hs3520Status", PrivateInstance)!.GetValue(window)!;
            Check(hsStatus.Text.Contains("请先连接"), "3520页面未连接时不执行事务");
        }

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

sealed class DisplayTransport : IBmsTransport
{
    public bool IsConnected=>true;
    public string DiscoveryDescription=>"display test";
    public event Action<ReadOnlyMemory<byte>>? DataReceived {add{}remove{}}
    public event Action<string>? ConnectionProgress {add{}remove{}}
    public Task ReconnectAsync(CancellationToken ct=default)=>Task.CompletedTask;
    public Task WriteAsync(ReadOnlyMemory<byte> data,CancellationToken ct=default)=>Task.CompletedTask;
    public ValueTask DisposeAsync()=>ValueTask.CompletedTask;
}
