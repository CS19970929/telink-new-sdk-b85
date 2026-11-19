using System.IO;
using System.Reflection;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using ClosedXML.Excel;
using BmsTool.Windows;

static class UnifiedMonitorTest
{
    const BindingFlags Flags = BindingFlags.Instance | BindingFlags.NonPublic;
    static int checks;
    static void Check(bool ok, string text) { if (!ok) throw new Exception(text); ++checks; Console.WriteLine("PASS " + text); }
    static void Wait(Task task)
    {
        var frame = new DispatcherFrame();
        var timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(10) };
        var deadline = DateTime.UtcNow.AddSeconds(20);
        timer.Tick += (_, _) => { if (task.IsCompleted || DateTime.UtcNow > deadline) frame.Continue = false; };
        timer.Start(); Dispatcher.PushFrame(frame); timer.Stop();
        if (!task.IsCompleted) throw new TimeoutException("等待后台记录完成超时");
        task.GetAwaiter().GetResult();
    }
    static MonitorReading Sample(string token="1") => new() {
        Source="一线通",Endpoint="COM-DEMO",Protocol="A1.3.3",SegmentKey="COM-DEMO/7",Sequence=token,
        Identity="DEMO-D008-023",Soc=72.5,Voltage=76.4,Current=-12.3,Temperature=31,
        Cycles=126,HasCurrent=true,HasFault=true,HasCells=true,FaultCode=0,Fault="无故障",
        CellsUtc=DateTimeOffset.UtcNow.AddSeconds(-12),
        Cells=Enumerable.Range(1,23).ToDictionary(x=>x,x=>(double)(3314+x*7%19))
    };
    static OwcSnapshot Wire(int current, byte status=0xC4)
    {
        byte[] raw=[unchecked((byte)current),73,76,31,status,0];
        raw[5]=(byte)(raw.Take(5).Sum(x=>(int)x)&255);
        return new(true,true,0,10,0,0,0,1,raw,7,3) {
            Protocol=OwcWireProtocol.LishenV111, FrameProtocol=OwcWireProtocol.LishenV111 };
    }
    [STAThread] static int Main()
    {
        string output=Environment.GetEnvironmentVariable("BMS_UNIFIED_TEST_OUTPUT") ?? throw new Exception("缺少输出目录");
        Directory.CreateDirectory(output);
        SynchronizationContext.SetSynchronizationContext(new DispatcherSynchronizationContext(Dispatcher.CurrentDispatcher));
        var app=new Application { ShutdownMode=ShutdownMode.OnExplicitShutdown };
        var window=new MainWindow();
        var type=typeof(MainWindow);
        object? Field(string name)=>type.GetField(name,Flags)!.GetValue(window);
        void Call(string name,params object?[] args)=>type.GetMethod(name,Flags)!.Invoke(window,args);
        T Find<T>(string name) where T : class => (T)window.FindName(name);
        string Text(string name) => Find<TextBlock>(name).Text;
        var tabs = Find<TabControl>("MainTabs");
        var realtime = Find<TabItem>("RealtimeTab");
        var mode = Find<ComboBox>("ConnectionModeBox");
        var ports = Find<ComboBox>("SerialPortList");
        var baud = Find<ComboBox>("SerialBaudRateBox");
        var history = Find<CheckBox>("ShowHistoryDevicesBox");
        var cells = Find<Panel>("CustomerCellPanel");
        var modeDetails = Find<ComboBox>("OwcModeBox");
        var monitor = Find<ScrollViewer>("RealtimeScrollViewer");
        tabs.SelectedItem = realtime;
        bool factory = type.Assembly.GetName().Name!.Contains("Factory");
        var otherTabs = tabs.Items.OfType<TabItem>().Where(t => t != realtime && !Equals(t.Header, "趋势与记录")).ToArray();
        Check(tabs.TabStripPlacement == Dock.Top && ReferenceEquals(tabs.Items[0], realtime) &&
            Equals(realtime.Header, "实时监控") && tabs.Items.OfType<TabItem>().Any(t => Equals(t.Header, "趋势与记录")) == factory,
            "实时监控保留在首页，独立趋势页仅供内部版使用");
        Check(mode.Items.Cast<ComboBoxItem>().Select(item => item.Content.ToString())
            .SequenceEqual(new[] { "蓝牙", "串口", "一线通" }), "同一个下拉框提供三种通信方式");
        Check(Find<ComboBox>("DeviceList").Visibility == Visibility.Visible &&
            (factory ? history?.Visibility == Visibility.Visible : history is null) && ports.Visibility == Visibility.Collapsed,
            "客户版蓝牙显示附近设备，隐藏 COM 端口");
        int windows = app.Windows.Count;
        mode.SelectedIndex = 1;
        Check(ports.Visibility == Visibility.Visible && baud.Visibility == Visibility.Visible &&
            Find<ComboBox>("DeviceList").Visibility == Visibility.Collapsed && (history is null || history.Visibility == Visibility.Collapsed) &&
            modeDetails.Visibility == Visibility.Collapsed, "串口使用原 COM 与波特率控件");
        mode.SelectedIndex = 2;
        Check(ReferenceEquals(tabs.SelectedItem, realtime) && app.Windows.Count == windows &&
            ports.Visibility == Visibility.Visible && baud.Visibility == Visibility.Collapsed &&
            modeDetails.Visibility == (factory ? Visibility.Visible : Visibility.Collapsed) &&
            Find<GroupBox>("MonitorMosGroup").Visibility == Visibility.Collapsed,
            "一线通留在原主窗，测试版才提供协议选项，设备操作隐藏");

        ports.ItemsSource = new[] { new SerialPortEndpoint("COM-DEMO") };
        ports.SelectedIndex = 0;
        var receive = type.GetField("_integratedOwcTask", Flags)!;
        receive.SetValue(window, Task.CompletedTask);
        Call("UpdateUnifiedMonitorState");
        mode.SelectedIndex = 0;
        Check(mode.SelectedIndex == 2 && !mode.IsEnabled && Find<Button>("DisconnectButton").IsEnabled &&
            otherTabs.All(tab => tab.Visibility == Visibility.Collapsed),
            "一线通接收中保持断开入口，保护其他通信方式及设备页");
        var root = (FrameworkElement)window.Content;
        void Render(string name, int width = 1360, int height = 900)
        {
            root.InvalidateMeasure();
            root.Measure(new Size(width, height));
            root.Arrange(new Rect(0, 0, width, height));
            root.UpdateLayout();
            var bitmap = new RenderTargetBitmap(width, height, 96, 96, PixelFormats.Pbgra32);
            bitmap.Render(root);
            var png = new PngBitmapEncoder();
            png.Frames.Add(BitmapFrame.Create(bitmap));
            using var stream = File.Create(Path.Combine(output, name + ".png"));
            png.Save(stream);
        }
        for (int i = 0; i < 30; i++) Call("PublishUnifiedReading", Sample("chart-" + i) with {
            ObservedUtc = DateTimeOffset.UtcNow.AddSeconds(i - 30), Voltage = 76.1 + i * .008 + Math.Sin(i * .3) * .07 });
        Call("PublishUnifiedReading", Sample());
        Call("RefreshCustomerUiFromExistingSnapshot");
        Call("UpdateUnifiedMonitorState");
        Render("classic-owc-a133");
        Check(Text("PackVoltageText") == "76.40 V" && Text("CurrentText") == "-12.3 A" &&
            Text("CustomerSocText") == "SOC:72.5%" && cells.Children.Count == 23 &&
            Text("OwcCellStatusText").StartsWith("单体更新时间") && Text("CommunicationStatusText").Contains("在线"),
            "A1.3.3 数据在三栏和底栏显示，保留电流符号、单体和独立时间");
        Check((window.FindName("OwcRawText") is not null) == factory, "接收原始报文只在测试版界面");
        var raw = Wire(-12);
        var reading = MonitorReading.FromOwc(new Dictionary<byte, OwcSnapshot> { { 0, raw }, { 0x80, raw } }, "COM-DEMO");
        Check(reading?.Current == -12 && reading.FaultCode == 4 && reading.HasCurrent, "锂神扩展帧进入现有模型");
        Call("PublishUnifiedReading", reading);
        Check(Text("CurrentText") == "-12 A" && Text("OwcProtocolText").Contains("锂神扩展") &&
            Text("CustomerAlarm1Text") != "—" && cells.Children.Count == 0, "锂神扩展显示实际电流和故障，无单体卡片");
        Render("classic-owc-lishen-extension");
        raw = Wire(0, 0);
        reading = MonitorReading.FromOwc(new Dictionary<byte, OwcSnapshot> { { 0, raw }, { 0x80, raw } }, "COM-DEMO");
        Check(reading?.HasCurrent == false && reading.Current is null && reading.Fault is null, "标准锂神保留字节不解释为测量");
        Call("PublishUnifiedReading", reading);
        Render("classic-owc-lishen-standard", 900, 600);
        Check(Text("CurrentText") == "—" && Text("CustomerAlarm1Text") == "当前协议未提供故障信息" &&
            cells.Children.Count == 0 && Text("OwcCellStatusText").Contains("未提供单体"),
            "标准锂神将未提供的数据留空，清掉前一帧扩展电流和单体");
        Call("PublishUnifiedReading", Sample() with { SourceAgeMs = 9000 });
        Call("UpdateUnifiedMonitorState");
        Check(Text("PackVoltageText") == "—" && Text("CurrentText") == "—" && Text("CustomerSocText") == "SOC:—" &&
            cells.Children.Count == 0 && Text("CommunicationStatusText").Contains("等待"),
            "一线通数据失效后清空当前读数，仍区分适配器正在接收");
        Check(monitor.ScrollableWidth < 1 && monitor.ScrollableHeight > 0, "小窗口三栏无横向溢出，可纵向滚动");
        receive.SetValue(window, null);
        Call("UpdateUnifiedMonitorState");
        mode.SelectedIndex = 0;
        Check(mode.IsEnabled && mode.SelectedIndex == 0 && Find<GroupBox>("MonitorMosGroup").Visibility == Visibility.Visible &&
            Find<ComboBox>("DeviceList").Visibility == Visibility.Visible && (history is null || history.Visibility == Visibility.Visible),
            "断开后可切回蓝牙，并恢复原设备入口");
        bool HasBalanceIcon(int cellIndex)
        {
            var contents = (StackPanel)((Grid)cells.Children[cellIndex]).Children[1];
            return contents.Children.OfType<Border>().Any(icon =>
                System.Windows.Automation.AutomationProperties.GetName(icon) == "均衡中");
        }
        void ShowBattery(BatterySnapshot snapshot)
        {
            Call("ApplyBatterySnapshot", snapshot);
            Call("UpdateCustomerCells");
        }

        BatterySnapshot C060(uint balanceCells) => new() {
            CellMillivolts = Enumerable.Repeat((ushort)4050, 20).ToArray(), BalanceCellMask = balanceCells
        };
        ShowBattery(C060((1u << 10) | (1u << 19)));
        Check(cells.Children.Count == 20 && Enumerable.Range(0, 20).Where(HasBalanceIcon).SequenceEqual(new[] { 10, 19 }),
            "C060 只在第 11、20 串显示均衡图标");
        ShowBattery(C060(0));
        Check(cells.Children.Count == 20 && !Enumerable.Range(0, 20).Any(HasBalanceIcon), "均衡停止后保留电压并清除图标");
        ShowBattery(C060(1u));
        ShowBattery(new BatterySnapshot { CellMillivolts = Enumerable.Repeat((ushort)61001, 32).ToArray() });
        Check(cells.Children.Count == 0, "没有有效串位时清除上一帧的均衡图标");

        var measuredWords = new ushort[C077Measurements.WordCount];
        measuredWords[0] = 0xC077; measuredWords[1] = 1; measuredWords[21] = 5;
        measuredWords[3] = measuredWords[6] = 0x1B;
        measuredWords[11] = 1;
        ShowBattery(new BatterySnapshot {
            Measurements = C077Measurements.Parse(measuredWords),
            CellMillivolts = Enumerable.Repeat((ushort)4050, 5).ToArray(),
            BalanceCellMask = (1u << 2) | (1u << 4)
        });
        Check(cells.Children.Count == 5 && Enumerable.Range(0, 5).Where(HasBalanceIcon).SequenceEqual(new[] { 4 }),
            "C077 只在有效的第 5 串显示均衡图标，隐藏无效的第 3 串标记");

        var interval = Find<ComboBox>("HomeRecordIntervalBox");
        interval.SelectedIndex = 2;
        var recordInterval = (ComboBox)Field("_recordInterval")!;
        Check(recordInterval.SelectedIndex == 2, "首页选择记录间隔");
        var trendButton = Find<Button>("HomeRecordsButton");
        if (factory)
        {
            recordInterval.SelectedIndex = 1;
            Check(interval.SelectedIndex == 1, "趋势页修改间隔会同步回首页");
            trendButton.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            var plot = (MonitorPlot)Field("_recordPlot")!;
            Check(Equals(((TabItem)tabs.SelectedItem).Header, "趋势与记录") &&
                plot.Entries.Any(entry => entry.Kind == "sample"), "内部版查看当前实时趋势");
        }
        else Check(trendButton is null && Find<Button>("HomeRecordExportButton").IsEnabled, "客户版首页可直接导出历史记录");
        tabs.SelectedItem = realtime;
        var recorder=new MonitorRecording(output,1);
        type.GetField("_recording",Flags)!.SetValue(window,recorder);
        type.GetField("_recordDirectory",Flags)!.SetValue(window,recorder.DirectoryPath);
        recorder.Capture(Sample("3"));
        Call("UpdateUnifiedMonitorState");
        Check(!interval.IsEnabled && Find<Button>("HomeRecordStopButton").IsEnabled && !Find<Button>("HomeRecordExportButton").IsEnabled,
            "首页和趋势页共享当前记录中的按钮门禁");
        tabs.SelectedIndex=1;tabs.SelectedIndex=0;
        Check(recorder.IsRunning,"切换页面不停止记录");
        var firstStop=(Task)type.GetMethod("StopUnifiedRecordingAsync",Flags)!.Invoke(window,null)!;
        var secondStop=(Task)type.GetMethod("StopUnifiedRecordingAsync",Flags)!.Invoke(window,null)!;
        Wait(Task.WhenAll(firstStop,secondStop));
        Check(MonitorRecording.Read(recorder.DirectoryPath).Any(x=>x.Kind=="sample"),"UI结束记录等待写盘完成");
        Wait(Task.Run(async()=>{
            var trend=new MonitorTrend();
            var origin=DateTimeOffset.UtcNow.AddHours(-24); var historySample=Sample();
            for(int i=0;i<=86400;i++) {
                trend.Add(new("sample",origin.AddSeconds(i),1,historySample,null));
                if(i==36000)trend.Add(new("gap",origin.AddSeconds(i),1,null,"断线"));
            }
            var points=trend.ToArray();
            Check(points.Length<=6000&&points.First().Utc<origin.AddHours(1),"24小时趋势保留全范围且内存有界");
            Check(points.Any(x=>x.Kind=="gap"),"趋势抽样保留数据缺口");
            var log=new MonitorRecording(output,1);
            var sample=Sample("disk-1");
            log.Capture(sample);log.Capture(sample);
            log.Capture(null);log.Capture(null);
            log.Capture(Sample("disk-2"));
            log.Note("=HYPERLINK(\"test\"),测试\n第二行");
            await log.StopAsync();
            var all=MonitorRecording.Read(log.DirectoryPath).ToList();
            Check(all.Count(x=>x.Kind=="sample")==2,"同一帧不重复记，缺口恢复生成新样本");
            Check(all.Count(x=>x.Kind=="gap")==1&&all.Count(x=>x.Kind=="segment")==2,"记录缺口合并并分段");
            Check(all.First(x=>x.Kind=="sample").Reading?.Current==-12.3,"JSONL保留有符号数值");
            string csv=Path.Combine(output,"export.csv");
            MonitorRecording.ExportCsv(log.DirectoryPath,csv);
            string csvText=File.ReadAllText(csv);
            Check(csvText.Contains("-12.3")&&csvText.Contains("gap")&&csvText.Contains("'=HYPERLINK"),"CSV保留缺口/负值并防止备注公式执行");
            string xlsx=Path.Combine(output,"export.xlsx");
            type.GetMethod("ExportUnifiedWorkbook",BindingFlags.Static|BindingFlags.NonPublic)!.Invoke(null,new object[]{log.DirectoryPath,xlsx});
            using var book=new XLWorkbook(xlsx);
            Check(book.Worksheet(1).RowsUsed().Count()==all.Count+1,"Excel导出包含所有记录事件");
            Check(!book.Worksheet(1).CellsUsed().Any(x=>x.HasFormula),"Excel备注作为文本");
            string blocked=Path.Combine(output,"blocked");File.WriteAllText(blocked,"x");
            bool failed=false;try{_ =new MonitorRecording(blocked,1);}catch(IOException){failed=true;}
            Check(failed,"存储创建失败明确返回错误");
            var tail=Directory.GetFiles(log.DirectoryPath,"records-*.jsonl").OrderBy(x=>x).Last();
            File.AppendAllText(tail,"{incomplete");
            var recovered=MonitorRecording.Read(log.DirectoryPath).ToList();
            Check(recovered.Last().Kind=="recovery_gap"&&recovered.Count==all.Count+1,"异常末行恢复完整历史并保留缺口");
            File.WriteAllText(tail,"broken\n{}\n");
            bool corrupt=false;try{_ = MonitorRecording.Read(log.DirectoryPath).ToList();}catch(InvalidDataException){corrupt=true;}
            Check(corrupt,"中间损坏不会静默丢弃");
        }));
        window.Close();
        Console.WriteLine("UNIFIED_MONITOR_PASS checks="+checks+" edition="+(factory?"test":"customer"));
        return 0;
    }
}
