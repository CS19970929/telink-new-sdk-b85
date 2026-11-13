using System.Globalization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
using Microsoft.Win32;

namespace BmsTool.Windows;
public partial class MainWindow
{
    private D008ParameterCapture? _parameterCapture;
    private BmsClient? _parameterProbedClient;
    private bool _parameterBusy;
    private readonly TextBlock _parameterStatus=new(){TextWrapping=TextWrapping.Wrap,Margin=new Thickness(6)};
    private readonly TextBox _parameterCapacity=new(),_parameterSoc=new(),_parameterCycle=new();
    private readonly TextBox _parameterHeatStart=new(),_parameterHeatStop=new();
    private readonly CheckBox _parameterHeatEnable=new(){Content="启用加热"};
    private readonly TextBox _parameterBalanceStart=new(),_parameterBalanceDeltaStart=new(),_parameterBalanceDeltaStop=new();
    private readonly TextBlock _parameterBalancePermission=new(){Text="均衡：未读取",TextWrapping=TextWrapping.Wrap,Margin=new Thickness(4)};
    private readonly Button _parameterHeatSave=new(),_parameterBalanceSave=new(),_parameterBalanceDisable=new(),_parameterBalanceAllow=new();
    private readonly TextBlock _parameterBusinessReadback=new(){Text="设备参数：未读取",TextWrapping=TextWrapping.Wrap,Margin=new Thickness(6)};
    private readonly TextBox _parameterReadback=new(){IsReadOnly=true,AcceptsReturn=true,TextWrapping=TextWrapping.Wrap,MinHeight=150};
    private TabItem? _d015ParameterTab;
    private readonly List<TabItem> _d015LegacyTabs=new();
    private void AddParameterTab()
    {
        var page=new DockPanel{Margin=new Thickness(14)};
        var statusCard=new Border{Padding=new Thickness(10),Margin=new Thickness(0,0,0,12),
            Background=System.Windows.Media.Brushes.AliceBlue,Child=_parameterStatus};
        DockPanel.SetDock(statusCard,Dock.Top);page.Children.Add(statusCard);
        var selector=CreateParameterProfileSelector();DockPanel.SetDock(selector,Dock.Top);page.Children.Add(selector);
        var sections=new TabControl();page.Children.Add(sections);
        StackPanel Section(string title) {
            var panel=new StackPanel{Margin=new Thickness(12),MaxWidth=850,HorizontalAlignment=HorizontalAlignment.Left};
            var tab=new TabItem{Header=title,Content=new ScrollViewer{Content=panel,VerticalScrollBarVisibility=ScrollBarVisibility.Auto}};
            sections.Items.Add(tab);
            if(title=="D015配置") { _d015ParameterTab=tab;tab.Visibility=Visibility.Collapsed; }
            if(title is "加热" or "均衡" or "休眠" or "电池类型" or "SOC / OCV" or "恢复默认")_d015LegacyTabs.Add(tab);
            return panel;
        }
        var root=Section("基本参数");
        void Button(string label,Func<Task> action) {
            var button=new Button{Content=label,Margin=new Thickness(4),Padding=new Thickness(10,4,10,4),HorizontalAlignment=HorizontalAlignment.Left};
            button.Click+=async(_,_)=>await action();root.Children.Add(button);
        }
        void Field(string label,Control value) {
            var row=new DockPanel{Margin=new Thickness(4)};
            var title=new TextBlock{Text=label,Width=220,VerticalAlignment=VerticalAlignment.Center};
            DockPanel.SetDock(title,Dock.Left);row.Children.Add(title);value.Width=320;value.HorizontalAlignment=HorizontalAlignment.Left;row.Children.Add(value);root.Children.Add(row);
        }
        Button("读取全部参数",()=>ParameterRunAsync(null));
        Button("导出参数 ZIP（包含部分失败项）",()=>{
            if(_parameterCapture is null){_parameterStatus.Text="请先读取参数";return Task.CompletedTask;}
            var dialog=new SaveFileDialog{Filter="参数包 (*.zip)|*.zip",FileName=$"D008_parameters_{DateTime.Now:yyyyMMdd_HHmmss}.zip"};
            if(dialog.ShowDialog(this)==true)try{D008Parameters.Export(dialog.FileName,_parameterCapture);_parameterStatus.Text="参数包已导出";}catch(Exception e){ShowError("导出失败",e);}
            return Task.CompletedTask;
        });
        AddBatteryTypeControls(Section("电池类型"));
#if BMS_FACTORY_APP
        AddSocProfileControls(Section("SOC / OCV"));
#endif
        Field("额定容量 / Ah",_parameterCapacity);
        Button("保存额定容量",()=>ParameterRunAsync((c,t)=>c.WriteD008VerifiedAsync(0x2318,new[]{D008Parameters.Capacity(_parameterCapacity.Text)},false,t)));
        Field("当前 SOC / %",_parameterSoc);
        Button("设置并保存 SOC",()=>ParameterRunAsync((c,t)=>{
            ushort value=ushort.Parse(_parameterSoc.Text);if(value>100)throw new ArgumentException("SOC 必须为 0～100");
            if(value==100 && MessageBox.Show(this,"配套更新固件会立即显示并保存100%，重启后仍恢复100%。这是人工指定的满电状态，不代表已通过电压判满。是否继续？","人工SOC服务操作",MessageBoxButton.YesNo,MessageBoxImage.Information)!=MessageBoxResult.Yes)return Task.CompletedTask;
            return c.WriteD008VerifiedAsync(0x1005,new[]{value},false,t);
        }));
        Field("循环次数",_parameterCycle);
        Button("保存循环次数",()=>ParameterRunAsync((c,t)=>c.WriteD008VerifiedAsync(0x2319,new[]{ushort.Parse(_parameterCycle.Text)},false,t)));
        root.Children.Add(_parameterBusinessReadback);
        root=Section("加热");
        root.Children.Add(new TextBlock{Text="加热参数",FontWeight=FontWeights.Bold,Margin=new Thickness(4,12,4,4)});
        root.Children.Add(_parameterHeatEnable);
        Field("加热启动温度 / ℃",_parameterHeatStart);Field("加热停止温度 / ℃",_parameterHeatStop);
        void SaveButton(Button button,string label,Func<Task> action) {
            button.Content=label;button.Margin=new Thickness(4);button.Padding=new Thickness(10,4,10,4);
            button.HorizontalAlignment=HorizontalAlignment.Left;button.IsEnabled=false;
            button.Click+=async(_,_)=>await action();root.Children.Add(button);
        }
        SaveButton(_parameterHeatSave,"保存加热参数",()=>ParameterRunAsync((c,t)=>
            c.WriteD008VerifiedAsync(D008Parameters.HeaterBase,D008Parameters.Heater(
                _parameterHeatEnable.IsChecked==true,_parameterHeatStart.Text,_parameterHeatStop.Text),false,t)));
        root=Section("均衡");
        root.Children.Add(new TextBlock{Text="均衡参数",FontWeight=FontWeights.Bold,Margin=new Thickness(4,12,4,4)});
        root.Children.Add(_parameterBalancePermission);
        SaveButton(_parameterBalanceDisable,"禁止均衡",()=>ParameterRunAsync((c,t)=>c.SetD008BalanceAllowedAsync(false,t)));
        SaveButton(_parameterBalanceAllow,"允许均衡",()=>ParameterRunAsync((c,t)=>c.SetD008BalanceAllowedAsync(true,t)));
        Field("均衡起始单体电压 / mV",_parameterBalanceStart);
        Field("均衡启动压差 / mV",_parameterBalanceDeltaStart);
        Field("均衡停止压差 / mV",_parameterBalanceDeltaStop);
        SaveButton(_parameterBalanceSave,"保存均衡参数",()=>ParameterRunAsync((c,t)=>
            c.WriteD008BalanceThresholdsAsync(_parameterBalanceStart.Text,
                _parameterBalanceDeltaStart.Text,_parameterBalanceDeltaStop.Text,t)));
        root=Section("休眠");
        AddSleepParameterControls(root,Field,SaveButton);
        UpdateBusinessParameterControls();
        AddCurrentCalibrationControls(Section("电流校准"));
#if BMS_FACTORY_APP
        AddD015ParameterControls(Section("D015配置"));
#endif
        root=Section("恢复默认");
        foreach(var g in new[]{(Id:(ushort)1,Name:"软件保护"),(Id:(ushort)2,Name:"AFE 硬件保护"),(Id:(ushort)3,Name:"业务参数（容量、加热及均衡）")})
            Button("恢复默认："+g.Name,()=>{
                if(MessageBox.Show(this,"仅恢复"+g.Name+"，不会清除 SN、校准或日志。是否继续？","恢复分组默认",MessageBoxButton.YesNo,MessageBoxImage.Question)!=MessageBoxResult.Yes)return Task.CompletedTask;
                return ParameterRunAsync((c,t)=>c.ResetD008GroupAsync(g.Id,t));
            });
#if BMS_FACTORY_APP
        root.Children.Add(new Expander{Header="原始读回",Content=_parameterReadback,Margin=new Thickness(4)});
#endif
        _businessParameterTab=new TabItem{Header="业务参数",Content=page};MainTabs.Items.Add(_businessParameterTab);
        AddC060ParameterTab();
        var timer=new DispatcherTimer{Interval=TimeSpan.FromSeconds(2)};
        timer.Tick+=async(_,_)=>{
            if(_bms is null){_batteryTypeStatus.Text="电池类型：未连接";_parameterProbedClient=null;_parameterCapture=null;_parameterStatus.Text="未连接";_parameterBusinessReadback.Text="设备参数：未连接";UpdateBusinessParameterControls();return;}
            if(!_identityRefreshRequired && !ReferenceEquals(_bms,_parameterProbedClient) && !_parameterBusy && !_diagBusy && !_otaRunning && !_eventLogReadInProgress && !_shBusy && !ShFactoryBusy())await ParameterRunAsync(null);
        };
        timer.Start();Closed+=(_,_)=>timer.Stop();
    }
    private async Task ParameterRunAsync(Func<BmsClient,CancellationToken,Task>? action,bool refresh=true,
        string? operation="保存参数",bool requiresUnlock=true)
    {
        if(_bms?.UsesC060Protocol==true){await RunC060ParameterAsync(action,operation??"读取参数",requiresUnlock);return;}
        bool modifying=action is not null && operation is not null;
        void Reject(string message) {
            _parameterStatus.Text=message;
            DeviceSerialStatusText.Text=message;
            if(modifying)MessageBox.Show(this,message,"操作未执行",MessageBoxButton.OK,MessageBoxImage.Warning);
        }
        if(_parameterBusy || _diagBusy || _otaRunning || _eventLogReadInProgress || _shBusy || ShFactoryBusy() || _autoReconnectRunning){Reject("设备正忙，请等待当前操作完成后再试。");return;}
        var client=_bms;if(client is null){Reject("请先连接设备。");return;}
#if !BMS_FACTORY_APP
        if(modifying && requiresUnlock && !_protectedFeaturesUnlocked){Reject("请先解锁高级功能，才能修改参数。");return;}
#endif
        if(modifying && (!ReferenceEquals(client,_parameterProbedClient) || _parameterCapture?.Supported!=true)){Reject("请先读取当前设备参数并确认支持，再执行修改。");return;}
        _parameterBusy=true;_diagBusy=true;_eventLogReadInProgress=true;
        UpdateBusinessParameterControls();
        using var timeout=new CancellationTokenSource(TimeSpan.FromSeconds(60));
        try {
            _pollTimer.Stop();await WaitForCommunicationIdleAsync();
            if(!ReferenceEquals(client,_bms))throw new InvalidOperationException("连接已改变");
            _parameterStatus.Text="正在读取/执行参数事务…";
            if(action is not null)await action(client,timeout.Token);
            if(!ReferenceEquals(client,_bms))throw new InvalidOperationException("连接已改变");
            if(refresh){
                _parameterCapture=await client.ReadD008ParametersAsync(timeout.Token);
                if(!ReferenceEquals(client,_bms))throw new InvalidOperationException("连接已改变，请重新读取");
                _parameterProbedClient=client;ShowD008ParameterCapture(_parameterCapture);
                if(action is not null && _parameterCapture.Errors.Count==0 && _parameterCapture.Supported)_parameterStatus.Text="设备已确认保存；参数读回完成";
            }
            if(modifying) {
                if(refresh && (_parameterCapture?.Supported!=true || _parameterCapture.Errors.Count!=0)) {
                    _parameterStatus.Text="写入事务已完成，但参数复查不完整；请重新读取确认设备状态。";
                    MessageBox.Show(this,_parameterStatus.Text,"结果需要确认",MessageBoxButton.OK,MessageBoxImage.Warning);
                } else {
                    _parameterStatus.Text=operation+"：设备已应答，参数复查完成 · "+DateTime.Now.ToString("HH:mm:ss");
                    ShowParameterSaved(_parameterStatus.Text);
                }
            }
        } catch(Exception ex){
            _parameterCapture=null;_parameterProbedClient=null;
            _parameterBusinessReadback.Text="设备参数：读取失败";
            _parameterStatus.Text="参数操作失败或结果未确认："+ex.Message+"；请重新读取确认设备状态";
            if(modifying)MessageBox.Show(this,_parameterStatus.Text,"操作失败 / 结果未确认",MessageBoxButton.OK,MessageBoxImage.Error);
        }
        finally{DeviceSerialStatusText.Text=_parameterStatus.Text;_parameterBusy=false;_diagBusy=false;_eventLogReadInProgress=false;UpdateBusinessParameterControls();StartAutomaticRefresh();}
    }
    private void ShowD008ParameterCapture(D008ParameterCapture c)
    {
        _parameterCapacity.Clear();_parameterSoc.Clear();_parameterCycle.Clear();DeviceSerialBox.Clear();
        _parameterHeatStart.Clear();_parameterHeatStop.Clear();_parameterHeatEnable.IsChecked=false;
        _parameterBalanceStart.Clear();_parameterBalanceDeltaStart.Clear();_parameterBalanceDeltaStop.Clear();_parameterBalancePermission.Text="均衡：未确认";
        ShowD015Parameters(c);
        _parameterStatus.Text=c.Status+"；"+string.Join("；",c.Errors);
        var business=new List<string>{"设备值 "+c.CapturedUtc.ToLocalTime().ToString("HH:mm:ss")};
        if(_bms?.IsD015==true) {
            if(c.Blocks.TryGetValue("D015Basic",out var d015) && d015.Length==32)
                business.Add($"D015：均衡{(d015[15]!=0?"允许":"禁止")}，起始{d015[12]}mV，压差{d015[13]}mV；RTC {(d015[22]!=0?"允许":"禁止")}，ON {d015[17]}s / OFF {d015[18]}s");
        } else {
        if(c.Blocks.TryGetValue("Heater",out var heat)) {
            try {
                D008Parameters.ValidateHeater(heat);
                if(!D008Parameters.HeaterAvailable(c)) {
                    _parameterHeatEnable.IsChecked=false;_parameterHeatStart.Clear();_parameterHeatStop.Clear();
                    business.Add("加热：本产品不支持，通用占位参数不生效");
                } else {
                _parameterHeatEnable.IsChecked=heat[0]!=0;_parameterHeatStart.Text=((heat[1]-400)/10m).ToString();_parameterHeatStop.Text=((heat[2]-400)/10m).ToString();
                business.Add($"加热：{(heat[0]!=0?"启用":"关闭")}，启动 {_parameterHeatStart.Text} ℃，停止 {_parameterHeatStop.Text} ℃"+
                    (D008Parameters.HeaterAvailable(c)?"":"；设备未声明支持加热，禁止写入"));
                }
            } catch(ArgumentException){business.Add("加热：读回值无效，禁止写入");}
        } else business.Add("加热：未读到参数，禁止写入");
        if(c.Blocks.TryGetValue("Balance",out var bal)) {
            try {
                D008Parameters.ValidateBalance(bal);
                _parameterBalancePermission.Text=bal[0]!=0?"均衡：允许":"均衡：禁止";
                _parameterBalanceStart.Text=bal[1].ToString();
                _parameterBalanceDeltaStart.Text=bal[2].ToString();_parameterBalanceDeltaStop.Text=bal[3].ToString();
                business.Add($"均衡：{(bal[0]!=0?"允许":"禁止")}，{bal[1]} / {bal[2]} / {bal[3]} mV");
            } catch(ArgumentException){business.Add("均衡：读回值无效，禁止写入");}
        } else business.Add("均衡：设备未支持或本次读取失败，禁止写入");
        if(c.Blocks.TryGetValue("FeatureStatus",out var featureStatus)) business.Add(D008Parameters.FeatureStatusText(featureStatus));
        ShowSleepParameters(c,business);
        }
        _parameterBusinessReadback.Text=string.Join(Environment.NewLine,business);
        if(c.Blocks.TryGetValue("Capacity",out var cap)){_parameterCapacity.Text=(cap[0]/10m).ToString();_parameterCycle.Text=cap[1].ToString();}
        if(c.Blocks.TryGetValue("SOC",out var soc))_parameterSoc.Text=soc[0].ToString();
        if(c.Blocks.TryGetValue("Serial",out var sn))DeviceSerialBox.Text=D008Parameters.Serial(sn);
        DeviceSerialStatusText.Text=_parameterStatus.Text;
        _parameterReadback.Text=string.Join(Environment.NewLine,c.Blocks.Select(b=>b.Key+": "+string.Join(" ",b.Value.Select(v=>v.ToString("X4")))));
    }
    private void UpdateBusinessParameterControls()
    {
        bool Ready(string group,Action<ushort[]> validate) {
            if(_parameterBusy || _parameterCapture is not { } c || !c.Blocks.TryGetValue(group,out var words))return false;
            try{validate(words);return group=="Heater"?D008Parameters.HeaterAvailable(c):D008Parameters.BalanceAvailable(c);}
            catch(ArgumentException){return false;}
        }
        bool heaterReady=Ready("Heater",D008Parameters.ValidateHeater),balanceReady=Ready("Balance",D008Parameters.ValidateBalance);
        foreach(var control in new Control[]{_parameterHeatEnable,_parameterHeatStart,_parameterHeatStop,_parameterHeatSave})control.IsEnabled=heaterReady;
        foreach(var control in new Control[]{_parameterBalanceStart,_parameterBalanceDeltaStart,_parameterBalanceDeltaStop,_parameterBalanceSave,_parameterBalanceDisable,_parameterBalanceAllow})control.IsEnabled=balanceReady;
        if(!balanceReady && !_parameterBusy)_parameterBalancePermission.Text="均衡：未确认";
        UpdateSleepParameterControls();
        foreach(var button in _d015SaveButtons)button.IsEnabled=!_parameterBusy && _parameterCapture is not null && _bms?.IsD015==true;
    }
}
