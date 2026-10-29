using System.IO;
using System.Reflection;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using BmsTool.Windows;

// 只实例化编译后的窗口并离屏布局，不显示窗口、不连接设备。
static class BusinessParametersUiTest
{
    const BindingFlags Private=BindingFlags.Instance|BindingFlags.NonPublic;
    static void Check(bool ok,string message){if(!ok)throw new Exception(message);}
    [STAThread]
    static void Main(string[] args)
    {
        var app=new Application();
        var window=new MainWindow();
        var type=typeof(MainWindow);
        void Call(string name,params object[] values)=>type.GetMethod(name,Private)!.Invoke(window,values);
        T Field<T>(string name)=>(T)type.GetField(name,Private)!.GetValue(window)!;
        void Load(D008ParameterCapture capture){
            type.GetField("_parameterCapture",Private)!.SetValue(window,capture);
            Call("ShowD008ParameterCapture",capture);Call("UpdateBusinessParameterControls");
        }
        Call("AddParameterTab");
        Check(!Field<Button>("_parameterBalanceSave").IsEnabled && !Field<Button>("_parameterHeatSave").IsEnabled,"未读参数时不允许写入");
        var capture=new D008ParameterCapture{Supported=true,ProtocolVersion=2};
        var header=new ushort[16];header[0]=0xd008;header[1]=2;header[2]=0x007f;header[9]=1;
        capture.Blocks["Capability"]=header;
        capture.Blocks["Balance"]=new ushort[]{1,3300,30,15};
        capture.Blocks["Heater"]=new ushort[]{1,350,450};
        Load(capture);
        Check(Field<Button>("_parameterBalanceSave").IsEnabled && Field<Button>("_parameterHeatSave").IsEnabled,"支持且读回有效后启用两组保存");
        Check(Field<Button>("_parameterBalanceDisable").IsEnabled && Field<Button>("_parameterBalanceAllow").IsEnabled,"明确提供禁止和允许两个入口");
        Check(Field<TextBlock>("_parameterBalancePermission").Text=="均衡：允许","简洁显示均衡许可");
        capture.Blocks["Balance"][0]=0;Load(capture);
        Check(Field<TextBlock>("_parameterBalancePermission").Text=="均衡：禁止","简洁显示设备禁止许可");
        Check(Field<Button>("_parameterBalanceSave").IsEnabled,"禁止时仍可保存阈值");
        capture.Blocks["Balance"][0]=1;Load(capture);
        type.GetField("_parameterBusy",Private)!.SetValue(window,true);Call("UpdateBusinessParameterControls");
        Check(!Field<Button>("_parameterBalanceDisable").IsEnabled && !Field<Button>("_parameterBalanceAllow").IsEnabled,"参数事务期间不能并发切换许可");
        type.GetField("_parameterBusy",Private)!.SetValue(window,false);Call("UpdateBusinessParameterControls");
        Check(Field<TextBox>("_parameterBalanceDeltaStart").Text=="30" && Field<TextBox>("_parameterBalanceDeltaStop").Text=="15","显示设备实际 30/15 参数");
        Check(Field<TextBox>("_parameterHeatStart").Text=="-5" && Field<TextBox>("_parameterHeatStop").Text=="5","温度单位解码");
        string actual=Field<TextBlock>("_parameterBusinessReadback").Text;
        Check(actual.Contains("均衡：允许，3300 / 30 / 15 mV"),"设备读回区简洁显示完整均衡参数");
        Field<TextBox>("_parameterBalanceDeltaStop").Text="3";
        Check(Field<TextBlock>("_parameterBusinessReadback").Text==actual,"编辑候选不能冒充设备读回");
        Load(capture);
        var tabs=(TabControl)window.FindName("MainTabs");
        var tab=tabs.Items.OfType<TabItem>().Single(x=>Equals(x.Header,"业务参数"));
        tabs.SelectedItem=tab;
        var page=(DockPanel)tab.Content;
        var sections=page.Children.OfType<TabControl>().Single();
        sections.SelectedItem=sections.Items.OfType<TabItem>().Single(x=>Equals(x.Header,"均衡"));
        bool customer=type.GetField("_protectedFeaturesUnlocked",Private)!=null;
        Check(sections.Items.OfType<TabItem>().Any(x=>Equals(x.Header,"SOC / OCV"))!=customer,"只有内部版提供 SOC / OCV 参数入口");
        Check(Field<TextBox>("DeviceSerialBox").MaxLength==32,"设备信息页 SN 限制为 32 字符");
        var root=(FrameworkElement)page;
        // 分区页面只布局当前分类，状态条保持可见。
        root.Measure(new Size(980,720));
        root.Arrange(new Rect(0,0,980,720));root.UpdateLayout();
        Check(Field<TextBox>("_parameterBalanceDeltaStop").ActualWidth>0,"均衡输入框参与布局");
        if(args.Length>0){
            Directory.CreateDirectory(args[0]);
            var bitmap=new RenderTargetBitmap(980,(int)Math.Ceiling(root.ActualHeight),96,96,PixelFormats.Pbgra32);
            var background=new DrawingVisual();
            using(var drawing=background.RenderOpen())drawing.DrawRectangle(Brushes.White,null,new Rect(0,0,980,root.ActualHeight));
            bitmap.Render(background);bitmap.Render(root);var encoder=new PngBitmapEncoder();encoder.Frames.Add(BitmapFrame.Create(bitmap));
            using var output=File.Create(Path.Combine(args[0],"business-parameters.png"));encoder.Save(output);
        }
        header[9]=0;Load(capture);
        Check(!Field<Button>("_parameterHeatSave").IsEnabled && Field<Button>("_parameterBalanceSave").IsEnabled,"无加热板型只禁用加热写入");
        capture.Blocks.Remove("Balance");Load(capture);
        Check(!Field<Button>("_parameterBalanceSave").IsEnabled && Field<TextBox>("_parameterBalanceDeltaStop").Text=="","读取失败清除旧均衡值并禁止写入");
        Check(!Field<Button>("_parameterBalanceDisable").IsEnabled && !Field<Button>("_parameterBalanceAllow").IsEnabled,"读取失败不能盲目切换许可");
        capture.Blocks["Balance"]=new ushort[]{1,3300,30,30};Load(capture);
        Check(!Field<Button>("_parameterBalanceSave").IsEnabled && Field<TextBlock>("_parameterBusinessReadback").Text.Contains("读回值无效"),"非法读回不允许保存");
        capture.Blocks["Balance"]=new ushort[]{1,3300,30,15};capture.ProtocolVersion=1;Load(capture);
        Check(!Field<Button>("_parameterBalanceSave").IsEnabled,"v1 不开放均衡写入");
        type.GetField("_parameterCapture",Private)!.SetValue(window,null);Call("UpdateBusinessParameterControls");
        Check(!Field<Button>("_parameterBalanceSave").IsEnabled && !Field<Button>("_parameterHeatSave").IsEnabled,"断连或事务失败后撤销写入资格");
        capture.ProtocolVersion=2;capture.Blocks["Capability"][2]|=BmsSleepParameters.Capability;
        capture.Blocks["Sleep"]=new ushort[]{2800,1,3000,1440,30};Load(capture);
        Check(Field<TextBox>("_sleepLowMv").Text=="2800" && Field<TextBox>("_sleepLowMinutes").Text=="1","休眠电压和分钟显示设备值");
        Check(Field<Button>("_sleepSave").IsEnabled && Field<TextBlock>("_parameterBusinessReadback").Text.Contains("固定 3 分钟"),"可配置组可保存，过放时间只读");
        Check(type.GetField("_sleepAfeMinutes",Private)==null,"AFE异常时间不提供编辑控件");
        capture.Blocks.Remove("Sleep");Load(capture);
        Check(!Field<Button>("_sleepSave").IsEnabled && Field<TextBox>("_sleepLowMv").Text=="","休眠读取失败清旧值并禁止保存");
        var d015Client=new BmsClient(new UiTransport());
        typeof(BmsClient).GetProperty("IsD015")!.SetValue(d015Client,true);
        type.GetField("_bms",Private)!.SetValue(window,d015Client);
        var d015Capture=new D008ParameterCapture{Supported=true,ProtocolVersion=2};
        d015Capture.Blocks["D015Basic"]=new ushort[32];
        d015Capture.Blocks["D015Ocv"]=new ushort[42];
        Load(d015Capture);
        Check(Field<TabItem>("_d015ParameterTab").Visibility==Visibility.Visible,"D015配置入口可见");
        Check(Field<List<TabItem>>("_d015LegacyTabs").All(t=>t.Visibility==Visibility.Collapsed),"隐藏不适用D015的其他产品页面");
        Check(Field<DataGrid>("_d015BasicGrid").Items.Count==24,"D015实际基本配置项");
        Check((Field<DataGrid>("_d015OcvGrid").Parent is not null)!=customer,"D015 OCV编辑仅内部版提供");
        Check((window.FindName("D015IapRecoveryBox") is CheckBox)!=customer,"仅内部版提供IAP恢复入口");
        type.GetField("_bms",Private)!.SetValue(window,null);
        d015Client.DisposeAsync().AsTask().GetAwaiter().GetResult();
        window.Close();
        Console.WriteLine("PASS business parameters UI: grouped layout, edition-specific SOC / OCV, SN editor, actual/candidate separation and write gates");
    }
}

sealed class UiTransport : IBmsTransport
{
    public bool IsConnected=>true;
    public string DiscoveryDescription=>"UI-only, no hardware";
    public event Action<ReadOnlyMemory<byte>>? DataReceived {add{} remove{}}
    public event Action<string>? ConnectionProgress {add{} remove{}}
    public Task ReconnectAsync(CancellationToken ct=default)=>Task.CompletedTask;
    public Task WriteAsync(ReadOnlyMemory<byte> data,CancellationToken ct=default)=>throw new InvalidOperationException("UI test must not send frames");
    public ValueTask DisposeAsync()=>ValueTask.CompletedTask;
}
