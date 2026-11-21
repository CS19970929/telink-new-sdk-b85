using System.Collections.ObjectModel;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using Microsoft.Win32;

namespace BmsTool.Windows;
public partial class MainWindow
{
    private bool _c060RowsC077;
    private readonly TextBlock _c060Intro=new(){TextWrapping=TextWrapping.Wrap,Margin=new Thickness(6)};
    private readonly WrapPanel _c077Tools=new(){Visibility=Visibility.Collapsed};
    private readonly TextBlock _c077Status=new(){TextWrapping=TextWrapping.Wrap,Margin=new Thickness(6),Visibility=Visibility.Collapsed};
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
    private Button? _c060SoftwareSave;
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
        if (C060FactoryEdition) top.Children.Add(_c060Intro);
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
            var dialog=new SaveFileDialog{Filter="C060/C077参数 (*.json)|*.json",FileName=$"{_c060Capture.Product}_parameters_{DateTime.Now:yyyyMMdd_HHmmss}.json"};
            if(dialog.ShowDialog(this)==true)try{C060Parameters.Export(dialog.FileName,_c060Capture);_c060Status.Text="已导出"+_c060Capture.Product+"设备读回值；"+string.Join("；",_c060Capture.Incomplete);}catch(Exception e){ShowError("导出失败",e);}
            return Task.CompletedTask;
        });
        Button("导入并预览修改",()=>{ImportC060Parameters();return Task.CompletedTask;});
        Button("恢复业务参数默认",()=>RunC060ResetAsync());
        Button("蓝牙名称",()=>RunC060ParameterAsync(EditBleNameAsync,"修改蓝牙名称",requireCapture:false,refreshAfter:false));
        top.Children.Add(_c077Tools);top.Children.Add(_c077Status);
        void AfeButton(string label,Func<Task> action) {
            var b=new Button{Content=label,Margin=new Thickness(4),Padding=new Thickness(10,5,10,5)};
            b.Click+=async(_,_)=>await action();_c077Tools.Children.Add(b);
        }
        AfeButton("读取双309诊断",()=>RunC060ParameterAsync(async(c,t)=>{
            _c077Status.Text=(await c.ReadC077AfeAsync(t)).Summary;
        },"读取309诊断",false,requireCapture:false));
        AfeButton("关闭输出后重试零偏",async()=>{
            if(MessageBox.Show(this,"请先确认充/放电及预充关闭，当前没有真实通路电流。确认重新采集AFE1 CADC零偏（AFE2未启用）？偏置只保存在RAM。","309零偏重试",MessageBoxButton.YesNo,MessageBoxImage.Question)!=MessageBoxResult.Yes)return;
            await RunC060ParameterAsync(async(c,t)=>{_c077Status.Text=(await c.RunC077ServiceAsync(false,t)).Summary;},"重新检查电流采样",requireCapture:false);
        });
#if BMS_FACTORY_APP
        AfeButton("编辑双309硬件保护（Flash / EEPROM）",()=>RunC060ParameterAsync((c,t)=>{
            var editor=new C077AfeEditorWindow(c){Owner=this};editor.ShowDialog();return Task.CompletedTask;
        },"双309硬保护编辑",requireCapture:false,refreshAfter:false));
        AfeButton("维修：核对并写入双309档案",async()=>{
            if(MessageBox.Show(this,"将当前已保存参数对应的硬件保护档案写入两颗309，只写差异字节。EEPROM编程寿命≤100次；两颗不能原子提交，中断后保持关闭并需明确重试。请先断开负载/充电器并确认输出关闭。是否执行？","309 EEEPROM维修",MessageBoxButton.YesNo,MessageBoxImage.Warning)!=MessageBoxResult.Yes)return;
            await RunC060ParameterAsync(async(c,t)=>{_c077Status.Text=(await c.RunC077ServiceAsync(true,t)).Summary;},"双309硬件档案维修",requireCapture:false);
        });
#endif
        var socRow=new StackPanel{Orientation=Orientation.Horizontal,Margin=new Thickness(6)};
        socRow.Children.Add(new TextBlock{Text="当前SOC / %：",VerticalAlignment=VerticalAlignment.Center});socRow.Children.Add(_c060Soc);
        var socSave=new Button{Content="设置当前SOC",Margin=new Thickness(8,0,8,0)};
        socSave.Click+=async(_,_)=>{
            if(!ushort.TryParse(_c060Soc.Text,out ushort value)||value>100){_c060Status.Text="SOC必须为0～100整数。";return;}
            if(MessageBox.Show(this,$"确认将当前SOC设置为{value}%？该值先更新RAM，运行快照按固件既有规则保存。","设置SOC",MessageBoxButton.YesNo,MessageBoxImage.Question)!=MessageBoxResult.Yes)return;
            await RunC060ParameterAsync(async(c,t)=>{await c.SetSocAndVerifyAsync(value,t);},"设置当前SOC");
        };
        socRow.Children.Add(socSave);
        top.Children.Add(socRow);
        top.Children.Add(_c060Compatibility);
        if(C060FactoryEdition)top.Children.Add(_c060ImportCalibration);
        var identity=new WrapPanel{Margin=new Thickness(4)};top.Children.Add(identity);
        void Identity(string label,TextBox text,ushort address,bool factory) {
            var panel=new StackPanel{Margin=new Thickness(4)};panel.Children.Add(new TextBlock{Text=label});panel.Children.Add(text);
            text.IsReadOnly=factory&&!C060FactoryEdition;
            if(!factory||C060FactoryEdition) {
                var save=new Button{Content="保存"+label,Margin=new Thickness(0,4,0,0)};
                if(address==0xFFF2)_c060SoftwareSave=save;
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
        foreach(var field in C060Parameters.FieldsFor(false))_c060Rows.Add(new(){Field=field});
        _c060Grid.ItemsSource=_c060Rows;
        void Column(string label,string path,bool readOnly=true,int width=150)=>_c060Grid.Columns.Add(new DataGridTextColumn{Header=label,Binding=new Binding(path){UpdateSourceTrigger=UpdateSourceTrigger.PropertyChanged},IsReadOnly=readOnly,Width=width});
        Column("类别","Group");Column("参数","Name",width:200);Column("设备值","DeviceValue",width:110);Column("待保存值","EditValue",false,120);Column("单位","Unit",width:115);
        var rowStyle=new Style(typeof(DataGridRow));rowStyle.Setters.Add(new Setter(ToolTipProperty,new Binding("Hint")));_c060Grid.RowStyle=rowStyle;
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
        bool ShowField(C060ParameterRow row) => C060FactoryEdition || row.Field.WriteAddress is 0x2300 or 0x2310 or 0x2318;
        page.Children.Add(CreateParameterBrowser(_c060Grid,item=>((C060ParameterRow)item).Group,
            _c060Rows.Where(ShowField).Select(r=>r.Group),item=>ShowField((C060ParameterRow)item)));
        ((DataGridTextColumn)_c060Grid.Columns[3]).ElementStyle=editStyle;
        _c060ParameterTab=new TabItem{Header="基本参数",Content=new ScrollViewer{Content=page,VerticalScrollBarVisibility=ScrollBarVisibility.Auto,HorizontalScrollBarVisibility=ScrollBarVisibility.Disabled},Visibility=Visibility.Collapsed};_parameterSections.Items.Insert(0,_c060ParameterTab);
    }
    private bool CanEditC060Field(C060ParameterField field)=>field.Editable&&
        (C060FactoryEdition || !field.FactoryOnly && field.WriteAddress is 0x2300 or 0x2310 or 0x2318)&&
        (!field.Compatibility||_c060Compatibility.IsChecked==true);
    private async Task EditBleNameAsync(BmsClient client,CancellationToken ct)
    {
        if(_bmsTransport is not BmsBleTransport transport)throw new InvalidOperationException("请通过蓝牙连接后修改模块名称。");
        string original=await transport.ReadDeviceNameAsync(ct);
        if(!ReferenceEquals(_bms,client))throw new InvalidOperationException("连接已改变，请重新操作。");
        var panel=new StackPanel{Margin=new Thickness(18)};
        panel.Children.Add(new TextBlock{Text="蓝牙名称",Margin=new Thickness(0,0,0,8)});
        var name=new TextBox{Text=original,MinWidth=320};panel.Children.Add(name);
        panel.Children.Add(new TextBlock{Text="以 BT_ 或 BT- 开头，最多16个UTF-8字节。\n保存后断开并重新搜索，确认广播名称。",TextWrapping=TextWrapping.Wrap,Margin=new Thickness(0,10,0,8)});
        var error=new TextBlock{TextWrapping=TextWrapping.Wrap};panel.Children.Add(error);
        var dialog=new Window{Title="蓝牙名称",Owner=this,Content=panel,SizeToContent=SizeToContent.WidthAndHeight,ResizeMode=ResizeMode.NoResize,WindowStartupLocation=WindowStartupLocation.CenterOwner};
        var save=new Button{Content="保存",IsDefault=true,HorizontalAlignment=HorizontalAlignment.Right,Padding=new Thickness(18,5,18,5),Margin=new Thickness(0,10,0,0)};
        save.Click+=(_,_)=>{try{BmsBleTransport.ValidateDeviceName(name.Text);dialog.DialogResult=true;}catch(ArgumentException ex){error.Text=ex.Message;}};
        panel.Children.Add(save);
        if(dialog.ShowDialog()!=true)return;
        ct.ThrowIfCancellationRequested();
        if(!ReferenceEquals(_bms,client)||!ReferenceEquals(_bmsTransport,transport))throw new InvalidOperationException("连接已改变，请重新读取名称。");
        if(name.Text==original){_c060Status.Text="名称未改变。";return;}
        await transport.WriteDeviceNameAsync(name.Text,ct);
        _c060Status.Text="模块名称已写入并读回。请断开后重新搜索确认广播名称；断电保持由模块固件决定。";
    }
    private void UpdateParameterProfileUi()
    {
        bool c060=_bms?.UsesC060Protocol==true,c077=_bms?.IsC077==true;
        _c077Tools.Visibility=_c077Status.Visibility=c077&&C060FactoryEdition?Visibility.Visible:Visibility.Collapsed;
        _c060Intro.Text=c077 ? "C077 · 16串分口 / 双SH367309。AFE1采电流，AFE2仅采电压与温度。硬保护在内部版独立编辑：先保存Flash，再明确下发EEPROM；软件过流不再自动改变硬保护。" :
            "参数按组保存，保存后自动读回。软件保护在“保护参数”页。";
        _c060Compatibility.Visibility=Visibility.Collapsed;
        _c060Software.IsReadOnly=true;
        if(_c060SoftwareSave is not null)_c060SoftwareSave.Visibility=Visibility.Collapsed;
        _c060ImportCalibration.Content="导入工厂采样参数";
        if(_c060RowsC077!=c077 || _c060Rows.Count!=C060Parameters.FieldsFor(c077).Count) {
            _c060Rows.Clear();foreach(var field in C060Parameters.FieldsFor(c077))_c060Rows.Add(new(){Field=field});
            _c060RowsC077=c077;
        }
        if(_c060ParameterTab is not null)_c060ParameterTab.Header="基本参数";
        if(_c060ParameterTab is null||_businessParameterTab is null)return;
        _c060ParameterTab.Visibility=c060?Visibility.Visible:Visibility.Collapsed;
        if(_currentCalibrationParameterTab is not null)_currentCalibrationParameterTab.Visibility=Visibility.Visible;
        _businessParameterTab.Visibility=Visibility.Visible;
        foreach(var tab in _parameterProductTabs)tab.Visibility=c060?Visibility.Collapsed:Visibility.Visible;
        UpdateD015ParameterVisibility();
        bool c073 = _bms?.IsC073 == true;
        _c073Tab.Visibility = c073 ? Visibility.Visible : Visibility.Collapsed;
        if (c073) {
            foreach (var tab in _parameterProductTabs) tab.Visibility = Visibility.Collapsed;
            if (_currentCalibrationParameterTab is not null) _currentCalibrationParameterTab.Visibility = Visibility.Collapsed;
            _parameterSections.SelectedItem = _c073Tab;
            return;
        }
        if (_parameterSections.SelectedItem == _c073Tab)
            _parameterSections.SelectedItem = c060 ? _c060ParameterTab : _parameterProductTabs.FirstOrDefault();
        if(c060 && _parameterSections.SelectedItem is TabItem selected && selected.Visibility!=Visibility.Visible)
            _parameterSections.SelectedItem=_c060ParameterTab;
        else if(!c060&&_parameterSections.SelectedItem==_c060ParameterTab)
            _parameterSections.SelectedItem=_parameterProductTabs.FirstOrDefault(t=>t.Visibility==Visibility.Visible);
        if(!c060)ClearC060ParameterCapture();
    }
    private void ClearC060ParameterCapture()
    {
        _c060Capture=null;_c060CaptureClient=null;
        foreach(var row in _c060Rows)row.Clear();
        _c060Soc.Clear();_c060Serial.Clear();_c060Hardware.Clear();_c060Software.Clear();_c060Status.Text="当前连接尚未读取产品参数。";_c077Status.Text="";
    }
    private async Task RunC060ParameterAsync(Func<BmsClient,CancellationToken,Task>? action,string operation="保存参数",bool requiresUnlock=true,bool requireCapture=true,bool refreshAfter=true)
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
            if(!refreshAfter)return;
            if(!ReferenceEquals(client,_bms))throw new InvalidOperationException("连接已改变。");
            UpdateParameterProfileUi();
            if(!client.UsesC060Protocol){_parameterProbedClient=null;return;}
            var capture=await client.ReadC060ParametersAsync(timeout.Token);
            if(!ReferenceEquals(client,_bms))throw new InvalidOperationException("连接已改变。");
            _c060Capture=capture;_c060CaptureClient=_parameterProbedClient=client;
            if(client.IsC077&&capture.Blocks.TryGetValue("C077Afe",out var afeWords))
                _c077Status.Text=C077AfeSnapshot.Parse(afeWords).Summary;
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
            if(action is not null)ShowError("产品参数操作",ex);
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
                var effective=_bms?.IsC077==true?C077Parameters.NormalizeCurrentGroup(candidate,resistance,parallel):C060Parameters.NormalizeCurrentGroup(candidate,resistance,parallel);
                detail+=$"\n短路档位预计有效值：{effective[2]/10m}μs，{effective[3]/10m}A；参数回读不能证明实际AFE写入成功；C077需随后执行内部版双309工厂配置。";
            }
            if(changes.Keys.Any(a=>a is >=0x2318 and <=0x231B))detail+="\n容量/SOC参数组保存会重建相关学习状态，保留当前SOC百分比。";
            if(MessageBox.Show(this,detail+"\n\n按组分别保存并回读；中途失败时前组可能已经保存。是否继续？","确认C060参数",MessageBoxButton.YesNo,MessageBoxImage.Warning)!=MessageBoxResult.Yes)return;
            bool compatibility=_c060Compatibility.IsChecked==true;
            await RunC060ParameterAsync((c,t)=>c.WriteC060ParameterChangesAsync(changes,(address,values)=>{
                if(!ReferenceEquals(c,_bms))throw new InvalidOperationException("连接已改变。");
                _c060Status.Text=$"0x{address:X4}整组已确认";
            },compatibility,t));
        } catch(Exception ex){ShowError("产品参数输入无效",ex);}
    }
    private void ImportC060Parameters()
    {
        try {
            if(_c060Capture is null||!ReferenceEquals(_c060CaptureClient,_bms))throw new InvalidOperationException("请先读取当前设备参数。");
            var dialog=new OpenFileDialog{Filter="C060/C077参数 (*.json)|*.json"};if(dialog.ShowDialog(this)!=true)return;
            var package=C060Parameters.Import(dialog.FileName,_bms?.ParameterProduct??"C060");var pending=new List<(C060ParameterRow Row,ushort Value)>();
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
