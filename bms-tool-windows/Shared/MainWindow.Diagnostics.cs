using Microsoft.Win32;
using System.Windows;
using System.Windows.Controls;
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
    private static DataGrid DiagnosticGrid()=>new() {IsReadOnly=true,AutoGenerateColumns=true,
        CanUserAddRows=false,CanUserDeleteRows=false,Margin=new Thickness(4)};
    private void AddDiagnosticTab()
    {
        AddParameterTab();
        var root=new DockPanel {Margin=new Thickness(12)};
        var controls=new StackPanel {Orientation=Orientation.Horizontal};
        var read=new Button {Content="读取完整诊断",Margin=new Thickness(4),Padding=new Thickness(12,5,12,5)};
        read.Click+=async (_,_)=>await CaptureDiagnosticsAsync(true);
        var stop=new Button {Content="停止采集",Margin=new Thickness(4)};
        stop.Click+=(_,_)=>{_diagAuto.IsChecked=false;_diagCts?.Cancel();};
        var export=new Button {Content="导出 AI 诊断包",Margin=new Thickness(4)};
        export.Click+=(_,_)=> {
            if(_diagCapture is null) {_diagStatus.Text="请先读取诊断";return;}
            var dialog=new SaveFileDialog {Filter="诊断包 (*.zip)|*.zip",FileName=$"D008_AI_diag_{DateTime.Now:yyyyMMdd_HHmmss}.zip"};
            if(dialog.ShowDialog(this)!=true)return;
            try {BmsDiagnostics.Export(dialog.FileName,_diagCapture);_diagStatus.Text="AI 诊断包已保存";}
            catch(Exception ex){ShowError("诊断导出失败",ex);}
        };
        controls.Children.Add(read);controls.Children.Add(stop);controls.Children.Add(export);controls.Children.Add(_diagAuto);
        DockPanel.SetDock(controls,Dock.Top);root.Children.Add(controls);
        _diagStatus.Text="连接后自动探测运行状态；完整 Trace、事件、参数和 AFE 证据请点击读取。诊断只读，物理 MOS 反馈不可用。";
        _diagStatus.TextWrapping=TextWrapping.Wrap;_diagStatus.Margin=new Thickness(4);
        DockPanel.SetDock(_diagStatus,Dock.Top);root.Children.Add(_diagStatus);
        var tabs=new TabControl();
        foreach(var item in new[]{
            ("电流",_diagCurrent),("SOC",_diagSoc),("低功耗",_diagPower),("保护",_diagProtection),
            ("MOS 决策",_diagMos),("启动",_diagBoot),("存储",_diagStorage),("RAM Trace",_diagTrace)})
            tabs.Items.Add(new TabItem {Header=item.Item1,Content=item.Item2});
        root.Children.Add(tabs);MainTabs.Items.Add(new TabItem {Header="BMS 诊断",Content=root});
        _diagTimer=new DispatcherTimer {Interval=TimeSpan.FromSeconds(1)};
        _diagTimer.Tick+=async (_,_)=> {
            if(_bms is null) { _diagProbedClient=null;_diagCts?.Cancel();return; }
            if(_otaRunning || _autoReconnectRunning || _diagBusy || _eventLogReadInProgress || _shBusy || ShFactoryBusy())return;
            bool first=!ReferenceEquals(_diagProbedClient,_bms);
            if(first || (_diagAuto.IsChecked==true && DateTime.UtcNow>=_diagNextUtc))
                await CaptureDiagnosticsAsync(false);
        };
        _diagTimer.Start();
        Closed+=(_,_)=>{_diagTimer.Stop();_diagCts?.Cancel();};
    }
    private async Task CaptureDiagnosticsAsync(bool full)
    {
        if(_diagBusy || _eventLogReadInProgress || _otaRunning || _shBusy || ShFactoryBusy())return;
        var client=_bms;
        if(client is null){_diagStatus.Text="请先连接 BMS";return;}
        if(full)_diagAuto.IsChecked=false;
        _diagBusy=true;_eventLogReadInProgress=true;
        _diagCts=new CancellationTokenSource(TimeSpan.FromSeconds(60));
        try {
            _pollTimer.Stop();await WaitForCommunicationIdleAsync();
            if(!ReferenceEquals(client,_bms))throw new InvalidOperationException("连接已改变");
            _diagStatus.Text="读取中；可停止，部分证据也可导出";
            var result=await client.ReadDiagnosticsAsync(full,ConnectionText.Text,_diagCts.Token);
            if(!ReferenceEquals(client,_bms)) { result.Errors.Add("采集期间连接改变");result.Status="旧连接的部分证据"; }
            _diagCapture=result;_diagProbedClient=client;
            _diagCurrent.ItemsSource=result.Current;_diagSoc.ItemsSource=result.Soc;
            _diagPower.ItemsSource=result.Power;_diagProtection.ItemsSource=result.Protection;
            _diagBoot.ItemsSource=result.Boot;_diagStorage.ItemsSource=result.Storage;
            _diagMos.ItemsSource=result.Mos;_diagTrace.ItemsSource=result.Trace;
            _diagStatus.Text=$"{result.Status} · {result.FinishedUtc.ToLocalTime():HH:mm:ss} · "+string.Join("；",result.Errors);
            if(!result.Supported)_diagAuto.IsChecked=false;
        }
        catch(Exception ex) {_diagStatus.Text="诊断读取失败："+ex.Message;}
        finally {
            _diagCts.Dispose();_diagCts=null;_diagBusy=false;_eventLogReadInProgress=false;
            _diagNextUtc=DateTime.UtcNow.AddSeconds(5);StartAutomaticRefresh();
        }
    }
}
