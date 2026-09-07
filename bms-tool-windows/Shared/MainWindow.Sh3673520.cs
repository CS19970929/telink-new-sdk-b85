using System.Collections.ObjectModel;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
namespace BmsTool.Windows;

public partial class MainWindow
{
    private TabItem? _sh3520Tab;
    private readonly TextBlock _shStatus=new() { Text="连接后点击读取。仅支持带型号/版本标识的 3520 固件。", TextWrapping=TextWrapping.Wrap, Margin=new Thickness(8) };
    private readonly DataGrid _shSoftGrid=new(), _shHardGrid=new();
    private BmsClient? _shReadClient;
    private ushort[]? _shSoft,_shHard;
    private bool _shBusy;
    private void AddSh3520ParameterTab()
    {
        var root=new DockPanel(); var top=new StackPanel(); DockPanel.SetDock(top,Dock.Top);
        var bar=new StackPanel { Orientation=Orientation.Horizontal };
        void Button(string title,Func<Task> action) {
            var button=new Button { Content=title, Margin=new Thickness(6), Padding=new Thickness(12,6,12,6) };
            button.Click+=async (_,_)=>await ShOperationAsync(action); bar.Children.Add(button);
        }
        Button("读取软 / 硬件参数",ShReadAsync);
        Button("保存软件参数",()=>ShWriteAsync(false));
        Button("保存硬件参数",()=>ShWriteAsync(true));
        top.Children.Add(bar);top.Children.Add(_shStatus);
        top.Children.Add(new TextBlock { Margin=new Thickness(8), TextWrapping=TextWrapping.Wrap,
            Text="软件和硬件分别保存。硬件保存时暂时关闭 MOS，校验与有效采样完成后由保护服务恢复。模式、同/分口、看门狗为编译配置，只读。软件延时按 200 ms 周期向上取整。" });
        root.Children.Add(top);
        var tabs=new TabControl();
        void Grid(DataGrid grid,string title) {
            grid.AutoGenerateColumns=false;grid.CanUserAddRows=false;grid.CanUserDeleteRows=false;
            grid.Columns.Add(new DataGridTextColumn { Header="保护项",Binding=new Binding("Name"),IsReadOnly=true,Width=250 });
            grid.Columns.Add(new DataGridTextColumn { Header="单位",Binding=new Binding("Unit"),IsReadOnly=true,Width=65 });
            grid.Columns.Add(new DataGridTextColumn { Header="读取值",Binding=new Binding("Current"),IsReadOnly=true,Width=100 });
            grid.Columns.Add(new DataGridTextColumn { Header="待写值",Binding=new Binding("Edit") { UpdateSourceTrigger=UpdateSourceTrigger.PropertyChanged },Width=100 });
            grid.Columns.Add(new DataGridTextColumn { Header="范围 / 说明",Binding=new Binding("Range"),IsReadOnly=true,Width=new DataGridLength(1,DataGridLengthUnitType.Star) });
            grid.BeginningEdit+=(_,e)=>{if(e.Row.Item is Sh3520Field f && !f.Editable)e.Cancel=true;};
            tabs.Items.Add(new TabItem { Header=title,Content=grid });
        }
        Grid(_shSoftGrid,"MCU 软件保护");Grid(_shHardGrid,"AFE 硬件保护");root.Children.Add(tabs);
        _sh3520Tab=new TabItem { Header="3520 保护参数",Content=root };
        _sh3520Tab.SetBinding(VisibilityProperty,new Binding("Visibility") { Source=ShAccessSource() });
        MainTabs.Items.Add(_sh3520Tab);
    }
    private async Task ShOperationAsync(Func<Task> action)
    {
        if(_shBusy) return;
        try {
            if(_otaRunning || ShFactoryBusy()) throw new InvalidOperationException("请等待 OTA / 工厂测试完成。");
            _shBusy=true;MainTabs.IsEnabled=false;_pollTimer.Stop();await WaitForCommunicationIdleAsync();
            await action();
        } catch(Exception ex) {
            _shStatus.Text="操作未完成："+ex.Message+" 写入超时后请重新读取，勿假定未保存。";
            AppendLog(_shStatus.Text,"3520");ShowError("3520 参数",ex);
        } finally {_shBusy=false;MainTabs.IsEnabled=true;StartAutomaticRefresh();}
    }
    private async Task ShReadAsync()
    {
        _shReadClient=null;
        var bms=_bms??throw new InvalidOperationException("请先连接 BMS。");
        var hard=await bms.ReadRegistersAsync(Sh3520Parameters.HardwareBase,32);
        Sh3520Parameters.CheckIdentity(hard);
        var soft=await bms.ReadRegistersAsync(Sh3520Parameters.SoftwareBase,24);
        if(!ReferenceEquals(bms,_bms)) throw new IOException("连接已改变，请重新读取。");
        _shHard=hard;_shSoft=soft;
        _shHardGrid.ItemsSource=Sh3520Parameters.HardwareFields(hard);
        _shSoftGrid.ItemsSource=Sh3520Parameters.SoftwareFields(soft);
        _shReadClient=bms;
        string mode=hard[24]==1?"仅软件":hard[24]==2?"仅硬件":"软件 + 硬件";
        _shStatus.Text=$"SH3673520 v{hard[1]}；保护：{mode}；{(hard[26]==1?"同口 MOS_EN=1":"分口 MOS_EN=0")}；看门狗：{(hard[27]==0?"关闭":"开启")}；SPI：{(hard[28]==0?"软件":"硬件")}；分流器 {hard[29]} mΩ × {hard[30]} 并联；AFE 配置：{(hard[25]==1?"已校验":"待恢复 / 未校验")}。";
        if(hard[29]>0) _shStatus.Text+=$" 硬件电流 A = 压差 mV × {hard[30]} / {hard[29]}。";
        AppendLog(_shStatus.Text,"3520");
    }
    private async Task ShWriteAsync(bool hardware)
    {
        var bms=_bms??throw new InvalidOperationException("请先连接 BMS。");
        if(!ReferenceEquals(bms,_shReadClient)||_shHard is null||_shSoft is null) throw new InvalidOperationException("必须先读取当前连接的参数。");
        var grid=hardware?_shHardGrid:_shSoftGrid;
        grid.CommitEdit(DataGridEditingUnit.Cell,true);grid.CommitEdit(DataGridEditingUnit.Row,true);
        var original=hardware?_shHard:_shSoft;
        var candidate=Sh3520Parameters.Build(original,grid.Items.Cast<Sh3520Field>(),hardware);
        if(candidate.SequenceEqual(original.Take(24))) {_shStatus.Text="参数未改变，无需保存。";return;}
        var identity=await bms.ReadRegistersAsync(Sh3520Parameters.HardwareBase,32);
        Sh3520Parameters.CheckIdentity(identity);
        ushort address=hardware?Sh3520Parameters.HardwareBase:Sh3520Parameters.SoftwareBase;
        var live=hardware?identity:await bms.ReadRegistersAsync(address,24);
        if(!live.Take(24).SequenceEqual(original.Take(24))) throw new IOException("设备参数已被修改，请重新读取后编辑。");
        if(!ReferenceEquals(bms,_bms)) throw new IOException("连接已改变。");
        await bms.Write3520ParametersAsync(address,candidate);
        bool applied=!hardware;
        for(int attempt=0;attempt<(hardware?12:1);attempt++) {
            var back=await bms.ReadRegistersAsync(address,(ushort)(hardware?32:24));
            if(!back.Take(24).SequenceEqual(candidate)) throw new IOException("参数写入回读不一致。");
            if(!hardware || back[25]==1) {applied=true;break;}
            await Task.Delay(250);
        }
        await ShReadAsync();
        _shStatus.Text=(hardware?"硬件":"软件")+"参数已保存并回读一致。"+(applied?"配置已校验。":"AFE 尚未应用成功，MOS 保持保护状态；请检查通信后重新读取。")+" "+_shStatus.Text;
        AppendLog(_shStatus.Text,"3520");
    }
}
