using Microsoft.Win32;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Shapes;
using System.Windows.Threading;

namespace BmsTool.Windows;
public partial class MainWindow
{
    private DispatcherTimer? _diagTimer;
    private CancellationTokenSource? _diagCts;
    private bool _diagBusy;
    private BmsClient? _diagProbedClient;
    private DateTime _diagNextUtc;
    private DiagnosticCapture? _diagCapture;
    private BmsHealthReport? _diagHealthReport;
    private TextBlock _diagStatus = new();
    private CheckBox _diagAuto = new() {Content="每 5 秒刷新状态"};
    private readonly DataGrid _diagCurrent = DiagnosticGrid();
    private readonly DataGrid _diagSoc = DiagnosticGrid();
    private readonly DataGrid _diagPower = DiagnosticGrid();
    private readonly DataGrid _diagProtection = DiagnosticGrid();
    private readonly DataGrid _diagBoot = DiagnosticGrid();
    private readonly DataGrid _diagStorage = DiagnosticGrid();
    private readonly DataGrid _diagMos = DiagnosticGrid();
    private readonly DataGrid _diagTrace = DiagnosticGrid();
    private readonly DataGrid _diagHealth = DiagnosticGrid();
    private readonly DataGrid _diagTests = DiagnosticGrid();
    private readonly Canvas _socChart = new() {Height=420,Background=Brushes.WhiteSmoke,ClipToBounds=true};
    private readonly TextBlock _socRecordStatus = new() {Margin=new Thickness(6),TextWrapping=TextWrapping.Wrap};
    private readonly List<SocRecord> _socRecords = new();
    private bool _socRecording;
    private static DataGrid DiagnosticGrid()=>new() {IsReadOnly=true,AutoGenerateColumns=true,
        CanUserAddRows=false,CanUserDeleteRows=false,Margin=new Thickness(4)};
    private void AddDiagnosticTab()
    {
        AddParameterTab();
        var root=new DockPanel {Margin=new Thickness(12)};
        var controls=new WrapPanel {Orientation=Orientation.Horizontal};
        var read=new Button {Content="读取完整诊断",Margin=new Thickness(4),Padding=new Thickness(12,5,12,5)};
        read.Click+=async (_,_)=>await CaptureDiagnosticsAsync(true);
        var health=new Button {Content="设备健康检查",Margin=new Thickness(4),Padding=new Thickness(12,5,12,5)};
        health.Click+=async (_,_)=>await CaptureDiagnosticsAsync(true);
        var socTest=new Button {Content="SOC 自动测试",Margin=new Thickness(4),Padding=new Thickness(12,5,12,5)};
        socTest.Click+=async (_,_)=>await RunDiagnosticTestAsync(true);
        var diagTest=new Button {Content="诊断一致性测试",Margin=new Thickness(4),Padding=new Thickness(12,5,12,5)};
        diagTest.Click+=async (_,_)=>await RunDiagnosticTestAsync(false);
        var stop=new Button {Content="停止采集",Margin=new Thickness(4)};
        stop.Click+=(_,_)=>{_diagAuto.IsChecked=false;_diagCts?.Cancel();StopObservationSession("cancelled");};
        var export=new Button {Content="导出 AI 诊断包",Margin=new Thickness(4)};
        export.Click+=(_,_)=> {
            if(_diagCapture is null) {_diagStatus.Text="请先读取诊断";return;}
            var dialog=new SaveFileDialog {Filter="诊断包 (*.zip)|*.zip",FileName=$"BMS_AI_diag_{DateTime.Now:yyyyMMdd_HHmmss}.zip"};
            if(dialog.ShowDialog(this)!=true)return;
            try {BmsDiagnostics.Export(dialog.FileName,_diagCapture,_diagHealthReport);_diagStatus.Text="AI 诊断包已保存";}
            catch(Exception ex){ShowError("诊断导出失败",ex);}
        };
        var recordSoc=new Button {Content="开始 SOC 记录",Margin=new Thickness(4)};
        recordSoc.Click+=async (_,_)=> {
            _socRecords.Clear();_socRecording=true;_diagAuto.IsChecked=true;
            _socRecordStatus.Text="正在记录；每次诊断采样同时读取电池快照。";
            RenderSocChart();
            await CaptureDiagnosticsAsync(false);
        };
        var stopSoc=new Button {Content="停止 SOC 记录",Margin=new Thickness(4)};
        stopSoc.Click+=(_,_)=> {
            _socRecording=false;_diagAuto.IsChecked=false;
            _socRecordStatus.Text=$"记录已停止，共 {_socRecords.Count} 个观察样本；稀疏 CSV 用于趋势查看，不能直接精确重演算法。";
        };
        var exportSoc=new Button {Content="导出 SOC CSV",Margin=new Thickness(4)};
        exportSoc.Click+=(_,_)=> {
            if(_socRecords.Count==0){_socRecordStatus.Text="当前没有可导出的 SOC 样本。";return;}
            var dialog=new SaveFileDialog {Filter="SOC Record (*.csv)|*.csv",FileName=$"BMS_SOC_{DateTime.Now:yyyyMMdd_HHmmss}.csv"};
            if(dialog.ShowDialog(this)!=true)return;
            try {SocRecord.WriteCsv(dialog.FileName,_socRecords);_socRecordStatus.Text=$"已导出 {_socRecords.Count} 个样本：{dialog.FileName}";}
            catch(Exception ex){ShowError("SOC CSV 导出失败",ex);}
        };
        var loadSoc=new Button {Content="载入 SOC CSV",Margin=new Thickness(4)};
        loadSoc.Click+=(_,_)=> {
            var dialog=new OpenFileDialog {Filter="SOC Record (*.csv)|*.csv|All files (*.*)|*.*"};
            if(dialog.ShowDialog(this)!=true)return;
            try {
                _socRecording=false;_socRecords.Clear();_socRecords.AddRange(SocRecord.ReadCsv(dialog.FileName));
                int gaps=0, duplicates=0;
                for(int i=1;i<_socRecords.Count;i++) {
                    uint dt=unchecked(_socRecords[i].Timestamp32k-_socRecords[i-1].Timestamp32k);
                    if(dt>12800)gaps++;if(dt==0)duplicates++;
                }
                _socRecordStatus.Text=$"离线查看 {_socRecords.Count} 帧 · >400ms 间隔 {gaps} · 重复时间戳 {duplicates}；此处显示记录曲线，算法对比使用 SOC simulator。";
                RenderSocChart();
            } catch(Exception ex){ShowError("SOC CSV 载入失败",ex);}
        };
        controls.Children.Add(health);controls.Children.Add(read);controls.Children.Add(socTest);controls.Children.Add(diagTest);
        controls.Children.Add(stop);controls.Children.Add(export);controls.Children.Add(recordSoc);controls.Children.Add(stopSoc);
        controls.Children.Add(exportSoc);controls.Children.Add(loadSoc);controls.Children.Add(_diagAuto);
        AddObservationControls(controls);
        DockPanel.SetDock(controls,Dock.Top);root.Children.Add(controls);
        _diagStatus.Text="连接后自动探测运行状态；完整 Trace、事件、参数和 AFE 证据请点击读取。诊断只读，物理 MOS 反馈不可用。";
        _diagStatus.TextWrapping=TextWrapping.Wrap;_diagStatus.Margin=new Thickness(4);
        DockPanel.SetDock(_diagStatus,Dock.Top);root.Children.Add(_diagStatus);
        var tabs=new TabControl();
        var socChartPanel=new DockPanel();
        DockPanel.SetDock(_socRecordStatus,Dock.Top);socChartPanel.Children.Add(_socRecordStatus);
        var socScroll=new ScrollViewer {HorizontalScrollBarVisibility=ScrollBarVisibility.Auto,VerticalScrollBarVisibility=ScrollBarVisibility.Auto,Content=_socChart};
        socChartPanel.Children.Add(socScroll);
        tabs.Items.Add(new TabItem {Header="SOC 曲线/回放",Content=socChartPanel});
        foreach(var item in new[]{
            ("健康",_diagHealth),("自动测试",_diagTests),("电流",_diagCurrent),("SOC",_diagSoc),("低功耗",_diagPower),("保护",_diagProtection),
            ("MOS 决策",_diagMos),("启动",_diagBoot),("存储",_diagStorage),("RAM Trace",_diagTrace)})
            tabs.Items.Add(new TabItem {Header=item.Item1,Content=item.Item2});
        root.Children.Add(tabs);MainTabs.Items.Add(new TabItem {Header="BMS 诊断",Content=root});
        _socChart.SizeChanged+=(_,_)=>RenderSocChart();
        _diagTimer=new DispatcherTimer {Interval=TimeSpan.FromSeconds(1)};
        _diagTimer.Tick+=async (_,_)=> {
            if(_bms is null) { _diagProbedClient=null;_diagCts?.Cancel();ObservationGap("disconnected");return; }
            if(_observationSession is not null && !_diagBusy && (_otaRunning || _autoReconnectRunning || _shBusy || ShFactoryBusy()))
                ObservationGap("paused_for_other_operation");
            if(_otaRunning || _autoReconnectRunning || _diagBusy || _eventLogReadInProgress || _shBusy || ShFactoryBusy())return;
            bool first=!ReferenceEquals(_diagProbedClient,_bms);
            if(first || (_diagAuto.IsChecked==true && DateTime.UtcNow>=_diagNextUtc))
                await CaptureDiagnosticsAsync(false);
        };
        _diagTimer.Start();
        Closed+=(_,_)=>{_diagTimer.Stop();_diagCts?.Cancel();StopObservationSession("window_closed");};
    }
    private async Task CaptureDiagnosticsAsync(bool full)
    {
        if(_diagBusy || _eventLogReadInProgress || _otaRunning || _shBusy || ShFactoryBusy())return;
        var client=_bms;
        if(client is null){_diagStatus.Text="请先连接 BMS";return;}
        if(_observationSession is not null && !ReferenceEquals(client,_observationClient)) {
            ObservationGap("GUI connection replaced; start a new session on the selected device");
            StopObservationSession("connection_replaced");return;
        }
        if(full && _observationSession is null)_diagAuto.IsChecked=false;
        _diagBusy=true;_eventLogReadInProgress=true;
        _diagCts=new CancellationTokenSource(TimeSpan.FromSeconds(60));
        try {
            _pollTimer.Stop();await WaitForCommunicationIdleAsync();
            if(!ReferenceEquals(client,_bms))throw new InvalidOperationException("连接已改变");
            _diagStatus.Text="读取中；可停止，部分证据也可导出";
            var observed = _observationSession is null ? null :
                await _observationSession.CaptureAsync(client,_diagCts.Token,full);
            var result=observed?.Capture ?? await client.ReadDiagnosticsAsync(full,ConnectionText.Text,_diagCts.Token);
            DeviceIdentity? identity=observed?.Identity;BatterySnapshot? battery=null;
            if(full && result.Supported && identity is null) {
                identity=await client.ReadIdentityAsync("","",_diagCts.Token);
            }
            if((full || _socRecording) && result.Supported)
                battery=await client.ReadBatteryAsync(_diagCts.Token);
            if(!ReferenceEquals(client,_bms)) { result.Errors.Add("采集期间连接改变");result.Status="旧连接的部分证据";ObservationGap("connection_changed_during_capture"); }
            var health=BmsHealth.Evaluate(result,identity,battery);
            _diagCapture=result;_diagHealthReport=health;_diagProbedClient=client;
            _diagHealth.ItemsSource=health.Checks;
            _diagCurrent.ItemsSource=result.Current;_diagSoc.ItemsSource=result.Soc;
            _diagPower.ItemsSource=result.Power;_diagProtection.ItemsSource=result.Protection;
            _diagBoot.ItemsSource=result.Boot;_diagStorage.ItemsSource=result.Storage;
            _diagMos.ItemsSource=result.Mos;_diagTrace.ItemsSource=result.Trace;
            if(_socRecording && battery is not null && result.Words is not null) {
                try {
                    var soc=BmsDiagnostics.DecodeSocSnapshot(result.Words);
                    _socRecords.Add(SocRecord.From(result,soc,battery));
                    if(_socRecords.Count>20000)_socRecords.RemoveRange(0,_socRecords.Count-20000);
                    _socRecordStatus.Text=$"正在记录：{_socRecords.Count} 个样本 · SOC {soc.SocEstimate}%/{soc.SocDisplay}% · {soc.EndpointState} · {soc.LearningState}";
                    RenderSocChart();
                } catch(InvalidDataException ex) {_socRecordStatus.Text="SOC 记录不可用："+ex.Message;}
            }
            _diagStatus.Text=$"健康={health.Overall} · {health.Summary} · {result.Status} · {result.FinishedUtc.ToLocalTime():HH:mm:ss} · "+string.Join("；",result.Errors);
            if(_observationSession is not null) _diagStatus.Text += " · 会话写盘："+_observationSession.DirectoryPath;
            if(!result.Supported) { _diagAuto.IsChecked=false;StopObservationSession("unsupported_or_unavailable"); }
        }
        catch(DiagnosticSessionReadException ex) {ObservationGap(ex.Message);_diagStatus.Text="会话读取失败："+ex.Message;}
        catch(OperationCanceledException) {ObservationGap("capture_cancelled_or_timeout");}
        catch(Exception ex) {StopObservationSession("failed");_diagStatus.Text="诊断读取失败："+ex.Message;}
        finally {
            _diagCts.Dispose();_diagCts=null;_diagBusy=false;_eventLogReadInProgress=false;
            FinishObservationSession();
            _diagNextUtc=DateTime.UtcNow.AddSeconds(5);StartAutomaticRefresh();
        }
    }

    private void RenderSocChart()
    {
        _socChart.Children.Clear();
        double width=Math.Max(900,Math.Max(1,_socChart.ActualWidth));
        double height=Math.Max(360,Math.Max(1,_socChart.ActualHeight));
        _socChart.Width=width;
        const double left=54,right=18,top=28,bottom=54;
        double graphWidth=width-left-right,graphHeight=height-top-bottom;
        for(int pct=0;pct<=100;pct+=20) {
            double y=top+graphHeight*(100-pct)/100.0;
            _socChart.Children.Add(new Line {X1=left,X2=width-right,Y1=y,Y2=y,Stroke=Brushes.LightGray,StrokeThickness=1});
            var label=new TextBlock {Text=$"{pct}%",Foreground=Brushes.DimGray};
            Canvas.SetLeft(label,8);Canvas.SetTop(label,y-9);_socChart.Children.Add(label);
        }
        if(_socRecords.Count==0) {
            var empty=new TextBlock {Text="开始记录在线数据，或载入由本工具导出的 SOC CSV。",Foreground=Brushes.DimGray,FontSize=16};
            Canvas.SetLeft(empty,left+24);Canvas.SetTop(empty,top+30);_socChart.Children.Add(empty);return;
        }
        uint first=_socRecords[0].Timestamp32k,last=_socRecords[^1].Timestamp32k;
        uint span=unchecked(last-first);if(span==0)span=(uint)Math.Max(1,_socRecords.Count-1);
        double X(SocRecord r,int index) {
            uint elapsed=unchecked(r.Timestamp32k-first);
            double ratio=last==first ? index/(double)Math.Max(1,_socRecords.Count-1) : elapsed/(double)span;
            return left+Math.Clamp(ratio,0.0,1.0)*graphWidth;
        }
        void AddSocSeries(Brush color,Func<SocRecord,double> value,double thickness) {
            var line=new Polyline {Stroke=color,StrokeThickness=thickness};
            for(int i=0;i<_socRecords.Count;i++)line.Points.Add(new Point(X(_socRecords[i],i),top+graphHeight*(100-Math.Clamp(value(_socRecords[i]),0,100))/100.0));
            _socChart.Children.Add(line);
        }
        AddSocSeries(Brushes.RoyalBlue,r=>r.FirmwareSocEst,2.2);
        AddSocSeries(Brushes.SeaGreen,r=>r.FirmwareSocDisplay,1.8);
        AddSocSeries(Brushes.DarkOrange,r=>r.OcvSoc,1.4);
        double maxCurrent=Math.Max(500,_socRecords.Max(r=>Math.Abs((double)r.CurrentMa)));
        var currentLine=new Polyline {Stroke=Brushes.MediumVioletRed,StrokeThickness=1.2,StrokeDashArray=new DoubleCollection {4,3}};
        for(int i=0;i<_socRecords.Count;i++) {
            double scaled=50+45*Math.Clamp(_socRecords[i].CurrentMa/maxCurrent,-1.0,1.0);
            currentLine.Points.Add(new Point(X(_socRecords[i],i),top+graphHeight*(100-scaled)/100.0));
        }
        _socChart.Children.Add(currentLine);
        for(int i=0;i<_socRecords.Count;i++)if(!string.IsNullOrWhiteSpace(_socRecords[i].SocEvent)) {
            double x=X(_socRecords[i],i);
            _socChart.Children.Add(new Line {X1=x,X2=x,Y1=top,Y2=top+graphHeight,Stroke=Brushes.Firebrick,StrokeThickness=0.8,Opacity=0.45});
        }
        var legend=new TextBlock {
            Text=$"SOC estimate  {(_socRecords[^1].FirmwareSocEst)}%    display  {(_socRecords[^1].FirmwareSocDisplay)}%    OCV  {(_socRecords[^1].OcvSoc)}%    current  {_socRecords[^1].CurrentMa} mA (粉色虚线，±{maxCurrent:F0} mA 映射到 5..95%)    红线=事件",
            Foreground=Brushes.Black,TextWrapping=TextWrapping.Wrap};
        Canvas.SetLeft(legend,left);Canvas.SetTop(legend,height-bottom+14);_socChart.Children.Add(legend);
    }

    private async Task RunDiagnosticTestAsync(bool socTest)
    {
        if(_diagBusy || _eventLogReadInProgress || _otaRunning || _shBusy || ShFactoryBusy())return;
        var client=_bms;
        if(client is null){_diagStatus.Text="请先连接 BMS";return;}
        _diagAuto.IsChecked=false;_diagBusy=true;_eventLogReadInProgress=true;
        _diagCts=new CancellationTokenSource(TimeSpan.FromMinutes(2));
        try {
            _pollTimer.Stop();await WaitForCommunicationIdleAsync();
            if(!ReferenceEquals(client,_bms))throw new InvalidOperationException("连接已改变");
            _diagStatus.Text=socTest?"正在采集 10 组 SOC 诊断样本…":"正在执行 3 轮完整诊断一致性测试…";
            BmsTestReport report=socTest
                ? await BmsTestEngine.RunSocAsync(client,ConnectionText.Text,10,TimeSpan.FromSeconds(1),_diagCts.Token)
                : await BmsTestEngine.RunDiagnosticsAsync(client,ConnectionText.Text,3,true,TimeSpan.FromSeconds(1),_diagCts.Token);
            _diagTests.ItemsSource=report.Checks;
            _diagStatus.Text=$"{(socTest?"SOC":"诊断")}自动测试：{report.Summary} · {report.FinishedUtc.ToLocalTime():HH:mm:ss}";
        }
        catch(OperationCanceledException){_diagStatus.Text="自动测试已停止";}
        catch(Exception ex){_diagStatus.Text="自动测试失败："+ex.Message;}
        finally {
            _diagCts.Dispose();_diagCts=null;_diagBusy=false;_eventLogReadInProgress=false;
            FinishObservationSession();
            _diagNextUtc=DateTime.UtcNow.AddSeconds(5);StartAutomaticRefresh();
        }
    }
}
