using System.Collections.ObjectModel;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using Microsoft.Win32;

namespace BmsTool.Windows;
public partial class MainWindow
{
    private TabItem? _businessParameterTab,_c060ParameterTab;
    private C060ParameterCapture? _c060Capture;
    private BmsClient? _c060CaptureClient;
    private readonly ObservableCollection<C060ParameterRow> _c060Rows=new();
    private readonly DataGrid _c060Grid=new(){AutoGenerateColumns=false,CanUserAddRows=false,CanUserDeleteRows=false,MinHeight=280,MaxHeight=550};
    private readonly TextBlock _c060Status=new(){TextWrapping=TextWrapping.Wrap,Margin=new Thickness(6)};
    private readonly CheckBox _c060Compatibility=new(){Content="编辑兼容参数（部分不参与当前运行）",Margin=new Thickness(8)};
    private readonly CheckBox _c060ImportCalibration=new(){Content="导入工厂校准/采样参数",Margin=new Thickness(8)};
    private readonly TextBox _c060Soc=new(){Width=70};
    private readonly TextBox _c060Serial=new(){Width=260},_c060Hardware=new(){Width=260},_c060Software=new(){Width=260};
#if BMS_FACTORY_APP
    private static bool C060FactoryEdition=>true;
#else
    private static bool C060FactoryEdition=>false;
#endif
    private UIElement CreateParameterProfileSelector()
    {
        var row=new StackPanel{Orientation=Orientation.Horizontal,Margin=new Thickness(4)};
        row.Children.Add(new TextBlock{Text="参数协议：",VerticalAlignment=VerticalAlignment.Center});
        var selector=new ComboBox{Width=235,Margin=new Thickness(4)};
        selector.Items.Add("自动识别（推荐）");selector.Items.Add("C060（手动选择旧项目）");selector.SelectedIndex=0;
        row.Children.Add(selector);
        var apply=new Button{Content="应用 / 重新识别",Padding=new Thickness(8,4,8,4),Margin=new Thickness(4)};
        apply.Click+=async(_,_)=>await RunC060ParameterAsync(async(client,ct)=>{
            if(selector.SelectedIndex==1)await client.SelectC060ParametersAsync(ct);
            else {client.ResetParameterProfile();await RefreshIdentityAsync(ct);}
            UpdateParameterProfileUi();
        },"切换参数协议",false,requireCapture:false);
        row.Children.Add(apply);return row;
    }
    private void AddC060ParameterTab()
    {
        var page=new DockPanel{Margin=new Thickness(12)};
        var top=new StackPanel();DockPanel.SetDock(top,Dock.Top);page.Children.Add(top);
        top.Children.Add(CreateParameterProfileSelector());
        top.Children.Add(new TextBlock{Text="C060 · 20串分口。软件保护在“保护参数”页。蓝色栏为待保存值；保存前重新读取整组并保留未编辑字段。",TextWrapping=TextWrapping.Wrap,Margin=new Thickness(6)});
        top.Children.Add(_c060Status);
        var tools=new WrapPanel();top.Children.Add(tools);
        void Button(string label,Func<Task> action) {
            var b=new Button{Content=label,Margin=new Thickness(4),Padding=new Thickness(10,5,10,5)};
            b.Click+=async(_,_)=>await action();tools.Children.Add(b);
        }
        Button("一键读取常规参数",()=>RunC060ParameterAsync(null));
        Button("保存选中组",()=>SaveC060RowsAsync(true));
        Button("保存全部修改",()=>SaveC060RowsAsync(false));
        Button("导出参数包",()=>{
            if(_c060Capture is null||!ReferenceEquals(_c060CaptureClient,_bms)){_c060Status.Text="请先读取当前设备参数。";return Task.CompletedTask;}
            var dialog=new SaveFileDialog{Filter="C060参数 (*.json)|*.json",FileName=$"C060_parameters_{DateTime.Now:yyyyMMdd_HHmmss}.json"};
            if(dialog.ShowDialog(this)==true)try{C060Parameters.Export(dialog.FileName,_c060Capture);_c060Status.Text="已导出设备读回值；分段电流校准完整数据尚未提供。";}catch(Exception e){ShowError("导出失败",e);}
            return Task.CompletedTask;
        });
        Button("导入并预览修改",()=>{ImportC060Parameters();return Task.CompletedTask;});
        Button("恢复业务参数默认",()=>RunC060ResetAsync());
        var socRow=new StackPanel{Orientation=Orientation.Horizontal,Margin=new Thickness(6)};
        socRow.Children.Add(new TextBlock{Text="当前SOC / %：",VerticalAlignment=VerticalAlignment.Center});socRow.Children.Add(_c060Soc);
        var socSave=new Button{Content="设置当前SOC",Margin=new Thickness(8,0,8,0)};
        socSave.Click+=async(_,_)=>{
            if(!ushort.TryParse(_c060Soc.Text,out ushort value)||value>100){_c060Status.Text="SOC必须为0～100整数。";return;}
            if(MessageBox.Show(this,$"确认将当前SOC设置为{value}%？该值先更新RAM，运行快照按固件既有规则保存。","设置SOC",MessageBoxButton.YesNo,MessageBoxImage.Question)!=MessageBoxResult.Yes)return;
            await RunC060ParameterAsync(async(c,t)=>{await c.SetSocAndVerifyAsync(value,t);},"设置当前SOC");
        };
        socRow.Children.Add(socSave);socRow.Children.Add(new TextBlock{Text="RAM值；深睡/升级时按固件规则保存快照。",VerticalAlignment=VerticalAlignment.Center});top.Children.Add(socRow);
        top.Children.Add(_c060Compatibility);
        if(C060FactoryEdition)top.Children.Add(_c060ImportCalibration);
        var identity=new WrapPanel{Margin=new Thickness(4)};top.Children.Add(identity);
        void Identity(string label,TextBox text,ushort address,bool factory) {
            var panel=new StackPanel{Margin=new Thickness(4)};panel.Children.Add(new TextBlock{Text=label});panel.Children.Add(text);
            text.IsReadOnly=factory&&!C060FactoryEdition;
            if(!factory||C060FactoryEdition) {
                var save=new Button{Content="保存"+label,Margin=new Thickness(0,4,0,0)};
                save.Click+=async(_,_)=>{
                    if(MessageBox.Show(this,"确认保存"+label+"？空文本会清除该项。","生产信息",MessageBoxButton.YesNo,MessageBoxImage.Question)!=MessageBoxResult.Yes)return;
                    string value=text.Text;
                    await RunC060ParameterAsync(async(c,t)=>{await c.WriteC060IdentityAsync(address,value,t);await RefreshIdentityAsync(t);},"保存"+label,!factory?false:true);
                };
                panel.Children.Add(save);
            }
            identity.Children.Add(panel);
        }
        Identity("SN",_c060Serial,0xFFF0,false);Identity("硬件版本",_c060Hardware,0xFFF1,true);Identity("软件版本",_c060Software,0xFFF2,true);
        foreach(var field in C060Parameters.Fields)_c060Rows.Add(new(){Field=field});
        _c060Grid.ItemsSource=_c060Rows;
        void Column(string label,string path,bool readOnly=true,int width=150)=>_c060Grid.Columns.Add(new DataGridTextColumn{Header=label,Binding=new Binding(path){UpdateSourceTrigger=UpdateSourceTrigger.PropertyChanged},IsReadOnly=readOnly,Width=width});
        Column("类别","Group");Column("参数","Name",width:200);Column("设备值","DeviceValue",width:110);Column("待保存值","EditValue",false,120);Column("单位","Unit",width:115);Column("说明","Hint",width:420);
        var editStyle=new Style(typeof(TextBlock));
        editStyle.Setters.Add(new Setter(TextBlock.BackgroundProperty,System.Windows.Media.Brushes.AliceBlue));
        editStyle.Setters.Add(new Setter(TextBlock.PaddingProperty,new Thickness(8,4,8,4)));
        void ReadOnlyStyle(string path,object value) {
            var trigger=new DataTrigger{Binding=new Binding(path),Value=value};
            trigger.Setters.Add(new Setter(TextBlock.BackgroundProperty,System.Windows.Media.Brushes.WhiteSmoke));
            trigger.Setters.Add(new Setter(TextBlock.ForegroundProperty,System.Windows.Media.Brushes.Gray));editStyle.Triggers.Add(trigger);
        }
        ReadOnlyStyle("Field.Editable",false);
        if(!C060FactoryEdition)ReadOnlyStyle("Field.FactoryOnly",true);
        _c060Grid.BeginningEdit+=(_,e)=>{if(e.Row.Item is C060ParameterRow row&&!CanEditC060Field(row.Field))e.Cancel=true;};
        page.Children.Add(CreateParameterBrowser(_c060Grid,item=>((C060ParameterRow)item).Group,_c060Rows.Select(r=>r.Group)));
        ((DataGridTextColumn)_c060Grid.Columns[3]).ElementStyle=editStyle;
        _c060ParameterTab=new TabItem{Header="C060参数",Content=new ScrollViewer{Content=page,VerticalScrollBarVisibility=ScrollBarVisibility.Auto,HorizontalScrollBarVisibility=ScrollBarVisibility.Disabled},Visibility=Visibility.Collapsed};MainTabs.Items.Add(_c060ParameterTab);
    }
    private bool CanEditC060Field(C060ParameterField field)=>field.Editable&&(!field.FactoryOnly||C060FactoryEdition)&&(!field.Compatibility||_c060Compatibility.IsChecked==true);
    private void UpdateParameterProfileUi()
    {
        bool c060=_bms?.IsC060==true;
        if(_c060ParameterTab is null||_businessParameterTab is null)return;
        _c060ParameterTab.Visibility=c060?Visibility.Visible:Visibility.Collapsed;
        _businessParameterTab.Visibility=c060?Visibility.Collapsed:Visibility.Visible;
        if(c060&&MainTabs.SelectedItem==_businessParameterTab)MainTabs.SelectedItem=_c060ParameterTab;
        else if(!c060&&MainTabs.SelectedItem==_c060ParameterTab)MainTabs.SelectedItem=_businessParameterTab;
        if(!c060)ClearC060ParameterCapture();
    }
    private void ClearC060ParameterCapture()
    {
        _c060Capture=null;_c060CaptureClient=null;
        foreach(var row in _c060Rows)row.Clear();
        _c060Soc.Clear();_c060Serial.Clear();_c060Hardware.Clear();_c060Software.Clear();_c060Status.Text="当前连接尚未读取C060参数。";
    }
    private async Task RunC060ParameterAsync(Func<BmsClient,CancellationToken,Task>? action,string operation="保存参数",bool requiresUnlock=true,bool requireCapture=true)
    {
        if(_parameterBusy||_diagBusy||_eventLogReadInProgress||_otaRunning||_shBusy||_autoReconnectRunning){_c060Status.Text="设备正忙，请稍后操作。";return;}
        var client=_bms;if(client is null){_c060Status.Text="请先连接设备。";return;}
#if !BMS_FACTORY_APP
        if(action is not null&&requiresUnlock&&!_protectedFeaturesUnlocked){MessageBox.Show(this,"请先解锁高级功能。","操作未执行");return;}
#endif
        if(action is not null&&requireCapture&&(!ReferenceEquals(_c060CaptureClient,client)||_c060Capture is null)){_c060Status.Text="请先读取当前设备参数。";return;}
        _parameterBusy=_diagBusy=_eventLogReadInProgress=true;
        using var timeout=new CancellationTokenSource(TimeSpan.FromSeconds(120));
        try {
            _pollTimer.Stop();await WaitForCommunicationIdleAsync();
            if(!ReferenceEquals(client,_bms))throw new InvalidOperationException("连接已改变。");
            _c060Status.Text="正在执行："+operation;
            if(action is not null)await action(client,timeout.Token);
            if(!ReferenceEquals(client,_bms))throw new InvalidOperationException("连接已改变。");
            UpdateParameterProfileUi();
            if(!client.IsC060){_parameterProbedClient=null;return;}
            var capture=await client.ReadC060ParametersAsync(timeout.Token);
            if(!ReferenceEquals(client,_bms))throw new InvalidOperationException("连接已改变。");
            _c060Capture=capture;_c060CaptureClient=_parameterProbedClient=client;
            foreach(var row in _c060Rows)if(C060Parameters.TryValue(capture,row.Field,out ushort raw))row.Load(raw);else row.Clear();
            if(capture.Blocks.TryGetValue("Realtime",out var realtime)&&realtime.Length==63&&realtime[52]<=100)_c060Soc.Text=realtime[52].ToString();else _c060Soc.Clear();
            if(capture.Blocks.TryGetValue("Identity",out var identity)) {
                _c060Serial.Text=DeviceSerialBox.Text=BmsClient.DecodeIdentityText(identity[..16]);
                _c060Hardware.Text=BmsClient.DecodeIdentityText(identity[16..32]);_c060Software.Text=BmsClient.DecodeIdentityText(identity[32..48]);
            }
            _c060Status.Text=(action is null?"C060参数读取完成":operation+"已完成，已重新读取设备参数")+
                (capture.Errors.Count==0?"":"；部分块未提供："+string.Join("；",capture.Errors));
            DeviceSerialStatusText.Text=_parameterStatus.Text=_c060Status.Text;
            if(action is not null&&requireCapture)ShowParameterSaved(_c060Status.Text);
        } catch(Exception ex){
            _c060Status.Text="操作失败或结果未确认："+ex.Message+"；请重新读取，未自动重发。";
            _c060CaptureClient=null;DeviceSerialStatusText.Text=_c060Status.Text;
            if(action is not null)ShowError("C060参数操作",ex);
        } finally{_parameterBusy=_diagBusy=_eventLogReadInProgress=false;UpdateBusinessParameterControls();StartAutomaticRefresh();}
    }
    private async Task SaveC060RowsAsync(bool selectedGroup)
    {
        try {
            if(_c060Capture is null||!ReferenceEquals(_c060CaptureClient,_bms)){_c060Status.Text="请先读取当前设备参数。";return;}
            if(_parameterBusy){_c060Status.Text="当前事务尚未完成。";return;}
            if(!_c060Grid.CommitEdit(DataGridEditingUnit.Cell,true)||!_c060Grid.CommitEdit(DataGridEditingUnit.Row,true))return;
            ushort? group=selectedGroup?(_c060Grid.SelectedItem as C060ParameterRow)?.Field.WriteAddress:null;
            if(selectedGroup&&group is null){_c060Status.Text="请先选择一个参数组。";return;}
            var changes=new Dictionary<ushort,ushort>();
            foreach(var row in _c060Rows.Where(r=>!selectedGroup||r.Field.WriteAddress==group)) {
                if(row.DeviceRaw is not ushort current||row.EditValue==row.DeviceValue)continue;
                if(!CanEditC060Field(row.Field))throw new ArgumentException(row.Name+"当前不可编辑。");
                ushort value=row.Field.Parse(row.EditValue);if(value!=current)changes[row.Field.Address]=value;
            }
            if(changes.Count==0){_c060Status.Text="没有待保存修改。";return;}
            string detail=string.Join("\n",changes.Select(c=>{var row=_c060Rows.Single(r=>r.Field.Address==c.Key);return row.Name+"："+row.DeviceValue+" → "+row.Field.Format(c.Value)+" "+row.Unit;}));
            if(changes.Keys.Any(a=>a is 0x230A or 0x230B)&&_c060Capture.Blocks.TryGetValue("2308",out var currentGroup)&&_c060Capture.Blocks.TryGetValue("231C",out var system)) {
                var candidate=(ushort[])currentGroup.Clone();
                foreach(var change in changes.Where(c=>c.Key>=0x2308&&c.Key<0x2310))candidate[change.Key-0x2308]=change.Value;
                ushort resistance=changes.TryGetValue(0x231D,out var r)?r:system[1],parallel=changes.TryGetValue(0x231E,out var p)?p:system[2];
                var effective=C060Parameters.NormalizeCurrentGroup(candidate,resistance,parallel);
                detail+=$"\n短路档位预计有效值：{effective[2]/10m}μs，{effective[3]/10m}A；参数回读不能证明实际AFE写入成功。";
            }
            if(changes.Keys.Any(a=>a is >=0x2318 and <=0x231B))detail+="\n容量/SOC参数组保存会重建相关学习状态，保留当前SOC百分比。";
            if(MessageBox.Show(this,detail+"\n\n按组分别保存并回读；中途失败时前组可能已经保存。是否继续？","确认C060参数",MessageBoxButton.YesNo,MessageBoxImage.Warning)!=MessageBoxResult.Yes)return;
            bool compatibility=_c060Compatibility.IsChecked==true;
            await RunC060ParameterAsync((c,t)=>c.WriteC060ParameterChangesAsync(changes,(address,values)=>{
                if(!ReferenceEquals(c,_bms))throw new InvalidOperationException("连接已改变。");
                _c060Status.Text=$"0x{address:X4}整组已确认";
            },compatibility,t));
        } catch(Exception ex){ShowError("C060输入无效",ex);}
    }
    private void ImportC060Parameters()
    {
        try {
            if(_c060Capture is null||!ReferenceEquals(_c060CaptureClient,_bms))throw new InvalidOperationException("请先读取当前设备参数。");
            var dialog=new OpenFileDialog{Filter="C060参数 (*.json)|*.json"};if(dialog.ShowDialog(this)!=true)return;
            var package=C060Parameters.Import(dialog.FileName);var pending=new List<(C060ParameterRow Row,ushort Value)>();
            foreach(var row in _c060Rows) {
                if(!CanEditC060Field(row.Field)||row.Field.FactoryOnly&&_c060ImportCalibration.IsChecked!=true)continue;
                if(C060Parameters.TryValue(package,row.Field,out ushort value)&&row.DeviceRaw is ushort current&&value!=current){row.Field.Parse(row.Field.Format(value));pending.Add((row,value));}
            }
            int protectionCount=0;ushort[]? importedProtection=null,currentProtection=null;
            if(package.Blocks.TryGetValue("Protection",out importedProtection)&&_c060Capture.Blocks.TryGetValue("Protection",out currentProtection))protectionCount=importedProtection.Where((v,i)=>v!=currentProtection[i]).Count();
            if(MessageBox.Show(this,$"载入{pending.Count}项常规参数、{protectionCount}项软件保护修改？\n此步只填入编辑框；常规参数和保护参数需分别点击保存。\n保留字段、生产身份、未勾选的校准参数不会覆盖。","导入预览",MessageBoxButton.YesNo,MessageBoxImage.Question)!=MessageBoxResult.Yes)return;
            foreach(var item in pending)item.Row.EditValue=item.Row.Field.Format(item.Value);
            if(protectionCount!=0&&importedProtection is not null&&currentProtection is not null){
                var formatted=ProtectionParameterCatalog.Create();
                for(int i=0;i<65;i++){_protectionRows[i].LoadFromDevice(currentProtection[i]);formatted[i].LoadFromDevice(importedProtection[i]);_protectionRows[i].EditValue=formatted[i].DeviceDisplayValue;}
            }
            _c060Status.Text="修改已载入，尚未写设备；请核对并分别保存常规/保护参数。";
        } catch(Exception ex){ShowError("导入参数失败",ex);}
    }
    private async Task RunC060ResetAsync()
    {
        if(MessageBox.Show(this,"恢复全部32字业务参数，包含均衡、CBC、SOC表、容量/循环/端点、串数/采样参数和旧Sleep；会改变相关学习状态。请先导出备份。是否继续？","恢复C060业务参数默认",MessageBoxButton.YesNo,MessageBoxImage.Warning)!=MessageBoxResult.Yes)return;
        await RunC060ParameterAsync((c,t)=>c.WriteSingleRegisterAsync(0x1003,1,t),"恢复业务参数默认");
    }
}
