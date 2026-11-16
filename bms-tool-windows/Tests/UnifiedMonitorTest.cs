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
    static IEnumerable<DependencyObject> Desc(DependencyObject root)
    {
        yield return root;
        for (int i=0;i<VisualTreeHelper.GetChildrenCount(root);i++)
            foreach(var x in Desc(VisualTreeHelper.GetChild(root,i))) yield return x;
    }
    static string Text(DependencyObject root) => string.Join("|",Desc(root).OfType<TextBlock>().Select(x=>x.Text));
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
        var view=(UnifiedMonitorView)Field("_unifiedMonitor")!;
        var tabs=(TabControl)window.FindName("MainTabs");
        bool factory=type.Assembly.GetName().Name!.Contains("Factory");
        Check(tabs.TabStripPlacement==Dock.Left,"使用统一侧边导航");
        Check(Equals(((TabItem)tabs.Items[0]).Header,"实时总览")&&Equals(((TabItem)tabs.Items[1]).Header,"趋势与记录"),"主页面与记录页共享导航");
        Check(((ComboBox)window.FindName("ConnectionModeBox")).Items.Count==3,"蓝牙/串口/一线通三个入口");
        for(int i=0;i<30;i++) Call("PublishUnifiedReading",Sample("chart-"+i) with {
            ObservedUtc=DateTimeOffset.UtcNow.AddSeconds(i-30),Voltage=76.1+i*.008+Math.Sin(i*.3)*.07 });
        Call("PublishUnifiedReading",Sample());
        void Render(string name,int width=1360,int height=980)
        {
            var root=(FrameworkElement)window.Content;
            root.Measure(new Size(width,height));root.Arrange(new Rect(0,0,width,height));root.UpdateLayout();
            var bitmap=new RenderTargetBitmap(width,height,96,96,PixelFormats.Pbgra32);
            bitmap.Render(root);var png=new PngBitmapEncoder();png.Frames.Add(BitmapFrame.Create(bitmap));
            using var stream=File.Create(Path.Combine(output,name+".png"));png.Save(stream);
        }
        Call("UpdateUnifiedMonitorState");
        Render("unified-a133");
        Check(Text(view).Contains("76.4")&&Text(view).Contains(factory?"-12.3":"12.3"),"真实主窗口消费有符号测量");
        Check(Desc(view).Contains(view.Raw)==factory,"客户版不展示原始报文，测试版保留");
        view.OperationsContainer.IsExpanded=true; Render("unified-operations");
        Check(Desc(view).OfType<Button>().Any(x=>x.ToolTip?.ToString()?.Contains("用户许可")==true),"保留既有MOS操作入口和门禁");
        view.OperationsContainer.IsExpanded=factory;
        var raw=Wire(-12);
        var reading=MonitorReading.FromOwc(new Dictionary<byte,OwcSnapshot>{{0,raw},{0x80,raw}},"COM-DEMO");
        Check(reading?.Current==-12&&reading.FaultCode==4&&reading.HasCurrent,"扩展帧进入主页面模型");
        Call("PublishUnifiedReading",reading);
        Render("unified-lishen-extension");
        raw=Wire(0,0);
        reading=MonitorReading.FromOwc(new Dictionary<byte,OwcSnapshot>{{0,raw},{0x80,raw}},"COM-DEMO");
        Check(reading?.HasCurrent==false&&reading.Current is null&&reading.Fault is null,"标准锂神保留字节不伪造成测量");
        Call("PublishUnifiedReading",reading);
        Render("unified-lishen-standard");
        Check(!Text(view).Contains("充电电流")&&!Text(view).Contains("单体压差"),"标准锂神隐藏不存在的数据");
        var stale=Sample() with {SourceAgeMs=9000};
        Call("PublishUnifiedReading",stale);Call("UpdateUnifiedMonitorState");
        Check(!Text(view).Contains("12.3"),"失联清除当前值");
        Render("unified-offline");
        Call("PublishUnifiedReading",Sample("2"));Render("unified-compact",960,760);
        Check(view.ActualWidth<=960,"小窗口页面宽度受约束");
        var recorder=new MonitorRecording(output,1);
        type.GetField("_recording",Flags)!.SetValue(window,recorder);
        type.GetField("_recordDirectory",Flags)!.SetValue(window,recorder.DirectoryPath);
        recorder.Capture(Sample("3"));
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
