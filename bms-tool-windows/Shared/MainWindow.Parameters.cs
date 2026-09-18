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
    private readonly TextBox _parameterCapacity=new(),_parameterSoc=new(),_parameterCycle=new(),_parameterSn=new();
    private readonly TextBox _parameterHeatStart=new(),_parameterHeatStop=new();
    private readonly CheckBox _parameterHeatEnable=new(){Content="启用加热（仍受原有安全条件约束）"};
#if BMS_FACTORY_APP
    private readonly TextBox _parameterOffset=new(),_parameterGain=new(),_parameterReference=new(){Text="0"};
#endif
    private readonly TextBox _parameterReadback=new(){IsReadOnly=true,AcceptsReturn=true,TextWrapping=TextWrapping.Wrap,MinHeight=150};
    private void AddParameterTab()
    {
        var root=new StackPanel{Margin=new Thickness(14)};
        root.Children.Add(new TextBlock{Text="D008 参数 · 修改后同步保存并读回核对。旧固件可备份已有保护参数，不发送新增写命令。",TextWrapping=TextWrapping.Wrap});
        root.Children.Add(_parameterStatus);
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
        Field("额定容量 / Ah",_parameterCapacity);
        Button("保存额定容量",()=>ParameterRunAsync((c,t)=>c.WriteD008VerifiedAsync(0x2318,new[]{D008Parameters.Capacity(_parameterCapacity.Text)},false,t)));
        root.Children.Add(new TextBlock{Text="容量改变会清除旧容量学习结果，保留当前 SOC 百分比和循环次数。",TextWrapping=TextWrapping.Wrap});
        Field("当前 SOC / %",_parameterSoc);
        Button("设置并保存 SOC",()=>ParameterRunAsync((c,t)=>{
            ushort value=ushort.Parse(_parameterSoc.Text);if(value>100)throw new ArgumentException("SOC 必须为 0～100");
            return c.WriteD008VerifiedAsync(0x1005,new[]{value},false,t);
        }));
        Field("循环次数",_parameterCycle);
        Button("保存循环次数",()=>ParameterRunAsync((c,t)=>c.WriteD008VerifiedAsync(0x2319,new[]{ushort.Parse(_parameterCycle.Text)},false,t)));
        Field("设备 SN（ASCII，最多32字符）",_parameterSn);
#if BMS_FACTORY_APP
        Button("保存 SN",()=>ParameterRunAsync((c,t)=>c.WriteD008SerialAsync(_parameterSn.Text,t)));
#else
        _parameterSn.IsReadOnly=true;
        root.Children.Add(new TextBlock{Text="SN 为工厂身份字段；客户版只读，写入请使用内部完整测试版并进入设备 Factory Mode。",TextWrapping=TextWrapping.Wrap});
#endif
        root.Children.Add(_parameterHeatEnable);
        Field("加热启动温度 / ℃",_parameterHeatStart);Field("加热停止温度 / ℃",_parameterHeatStop);
        Button("保存整组加热参数",()=>ParameterRunAsync((c,t)=>{
            ushort start=D008Parameters.Temperature(_parameterHeatStart.Text),stop=D008Parameters.Temperature(_parameterHeatStop.Text);
            if(start>=stop)throw new ArgumentException("启动温度必须低于停止温度");
            return c.WriteD008VerifiedAsync(0x2E20,new[]{(ushort)(_parameterHeatEnable.IsChecked==true?1:0),start,stop},false,t);
        }));
#if BMS_FACTORY_APP
        root.Children.Add(new TextBlock{Text="工厂电流校准：放电为正、充电为负，单位 mA。零点采集前须确认无实际电流；已知电流由外部仪表提供。先计算候选，再单独保存。硬件 AFE 保护量化不受软件校准替代。",TextWrapping=TextWrapping.Wrap});
        Field("零点偏移 / mA",_parameterOffset);Field("增益 / ppm（1000000=1倍）",_parameterGain);Field("外部参考电流 / mA",_parameterReference);
        Button("采集10个新鲜样本，生成零点候选",()=>ParameterRunAsync(async(c,t)=>{
            var sample=await c.ReadD008CalibrationSamplesAsync(t);
            _parameterOffset.Text=Math.Round(sample.Mean).ToString(CultureInfo.CurrentCulture);
            _parameterStatus.Text=$"零点候选 {sample.Mean:F1} mA，范围 {sample.Min}～{sample.Max}；尚未保存";
        },refresh:false));
        Button("采集10个新鲜样本，计算增益候选",()=>ParameterRunAsync(async(c,t)=>{
            var sample=await c.ReadD008CalibrationSamplesAsync(t);
            _parameterGain.Text=D008Parameters.Gain(sample.Mean,int.Parse(_parameterOffset.Text),int.Parse(_parameterReference.Text)).ToString();
            _parameterStatus.Text=$"原始均值 {sample.Mean:F1} mA，范围 {sample.Min}～{sample.Max}；增益候选尚未保存";
        },refresh:false));
        Button("保存电流校准并读回",()=>ParameterRunAsync((c,t)=>{
            int offset=int.Parse(_parameterOffset.Text);uint gain=uint.Parse(_parameterGain.Text);
            if(offset is <-1000000 or >1000000 || gain is <100000 or >10000000)throw new ArgumentException("校准系数超出协议范围");
            return c.WriteD008VerifiedAsync(0x2E24,D008Parameters.Calibration(offset,gain),true,t);
        }));
        Button("恢复电流校准为零偏移、1倍增益",()=>ParameterRunAsync((c,t)=>c.WriteD008VerifiedAsync(0x2E24,D008Parameters.Calibration(0,1000000),true,t)));
#endif
        foreach(var g in new[]{(Id:(ushort)1,Name:"软件保护"),(Id:(ushort)2,Name:"AFE 硬件保护"),(Id:(ushort)3,Name:"业务参数（容量及加热）")})
            Button("恢复默认："+g.Name,()=>{
                if(MessageBox.Show(this,"仅恢复"+g.Name+"，不会清除 SN、校准或日志。是否继续？","恢复分组默认",MessageBoxButton.YesNo,MessageBoxImage.Question)!=MessageBoxResult.Yes)return Task.CompletedTask;
                return ParameterRunAsync((c,t)=>c.ResetD008GroupAsync(g.Id,t));
            });
        root.Children.Add(_parameterReadback);
        MainTabs.Items.Add(new TabItem{Header="D008 参数",Content=new ScrollViewer{Content=root,VerticalScrollBarVisibility=ScrollBarVisibility.Auto}});
        var timer=new DispatcherTimer{Interval=TimeSpan.FromSeconds(2)};
        timer.Tick+=async(_,_)=>{
            if(_bms is null){_parameterProbedClient=null;_parameterCapture=null;_parameterStatus.Text="未连接";return;}
            if(!ReferenceEquals(_bms,_parameterProbedClient) && !_parameterBusy && !_diagBusy && !_otaRunning && !_eventLogReadInProgress && !_shBusy && !ShFactoryBusy())await ParameterRunAsync(null);
        };
        timer.Start();Closed+=(_,_)=>timer.Stop();
    }
    private async Task ParameterRunAsync(Func<BmsClient,CancellationToken,Task>? action,bool refresh=true)
    {
        if(_parameterBusy || _diagBusy || _otaRunning || _eventLogReadInProgress || _shBusy || ShFactoryBusy() || _autoReconnectRunning)return;
        var client=_bms;if(client is null){_parameterStatus.Text="请先连接";return;}
#if !BMS_FACTORY_APP
        if(action is not null && !_protectedFeaturesUnlocked){_parameterStatus.Text="请先解锁高级功能，才能修改参数";return;}
#endif
        if(action is not null && (!ReferenceEquals(client,_parameterProbedClient) || _parameterCapture?.Supported!=true)){_parameterStatus.Text="请先读取当前设备参数并确认支持，再执行修改";return;}
        _parameterBusy=true;_diagBusy=true;_eventLogReadInProgress=true;
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
        } catch(Exception ex){_parameterStatus.Text="参数操作失败："+ex.Message+"；请重新读取确认设备状态";}
        finally{_parameterBusy=false;_diagBusy=false;_eventLogReadInProgress=false;StartAutomaticRefresh();}
    }
    private void ShowD008ParameterCapture(D008ParameterCapture c)
    {
        _parameterCapacity.Clear();_parameterSoc.Clear();_parameterCycle.Clear();_parameterSn.Clear();
        _parameterHeatStart.Clear();_parameterHeatStop.Clear();_parameterHeatEnable.IsChecked=false;
#if BMS_FACTORY_APP
        _parameterOffset.Clear();_parameterGain.Clear();
#endif
        _parameterStatus.Text=c.Status+"；"+string.Join("；",c.Errors);
        if(c.Blocks.TryGetValue("Capacity",out var cap)){_parameterCapacity.Text=(cap[0]/10m).ToString();_parameterCycle.Text=cap[1].ToString();}
        if(c.Blocks.TryGetValue("SOC",out var soc))_parameterSoc.Text=soc[0].ToString();
        if(c.Blocks.TryGetValue("Serial",out var sn))_parameterSn.Text=D008Parameters.Serial(sn);
        if(c.Blocks.TryGetValue("Heater",out var heat)){_parameterHeatEnable.IsChecked=heat[0]!=0;_parameterHeatStart.Text=((heat[1]-400)/10m).ToString();_parameterHeatStop.Text=((heat[2]-400)/10m).ToString();}
#if BMS_FACTORY_APP
        if(c.Blocks.TryGetValue("Calibration",out var cal)){_parameterOffset.Text=D008Parameters.I32(cal,0).ToString();_parameterGain.Text=D008Parameters.U32(cal,2).ToString();}
#endif
        _parameterReadback.Text=string.Join(Environment.NewLine,c.Blocks.Select(b=>b.Key+": "+string.Join(" ",b.Value.Select(v=>v.ToString("X4")))));
    }
}
