using System.Windows;
using System.Windows.Controls;

namespace BmsTool.Windows;

public partial class MainWindow
{
    private readonly DataGrid _d015BasicGrid=new(){AutoGenerateColumns=false,CanUserAddRows=false,Height=330};
    private readonly DataGrid _d015OcvGrid=new(){AutoGenerateColumns=false,CanUserAddRows=false,Height=330};
    private readonly List<Button> _d015SaveButtons=new();
    private void AddD015ParameterControls(StackPanel root)
    {
        root.Children.Add(new TextBlock{Text="D015固定13串三元。标为“固件宏”的项只显示设备实际值，需修改固件后编译生效。其他参数可保存选中项，设备提交Flash后读回；开关ON可写。",TextWrapping=TextWrapping.Wrap});
        void Column(DataGrid grid,string title,string field,bool readOnly=false) =>
            grid.Columns.Add(new DataGridTextColumn{Header=title,Binding=new System.Windows.Data.Binding(field),IsReadOnly=readOnly});
        Column(_d015BasicGrid,"地址",nameof(D015SettingRow.AddressText),true);
        Column(_d015BasicGrid,"参数",nameof(D015SettingRow.Name),true);
        Column(_d015BasicGrid,"值",nameof(D015SettingRow.Value));
        Column(_d015BasicGrid,"单位",nameof(D015SettingRow.Unit),true);
        Column(_d015BasicGrid,"配置方式",nameof(D015SettingRow.Source),true);
        _d015BasicGrid.BeginningEdit+=(_,e)=>{
            if(e.Row.Item is D015SettingRow {IsReadOnly:true})e.Cancel=true;
        };
        root.Children.Add(_d015BasicGrid);
        AddSave("保存选中参数",async (client,ct)=>{
            if(!_d015BasicGrid.CommitEdit(DataGridEditingUnit.Cell,true) || !_d015BasicGrid.CommitEdit(DataGridEditingUnit.Row,true))
                throw new ArgumentException("请先修正参数输入。");
            if(_d015BasicGrid.SelectedItem is not D015SettingRow row)throw new ArgumentException("请选择一项参数。");
            if(row.IsReadOnly)throw new ArgumentException("该项由固件宏配置，不能通过上位机保存。请修改固件并重新编译。");
            if(row.Value < (row.Signed ? short.MinValue : 0) || row.Value>(row.Signed ? short.MaxValue : ushort.MaxValue))
                throw new ArgumentException("参数超出16位范围。");
            await client.WriteD008VerifiedAsync(row.Address,new[]{unchecked((ushort)row.Value)},false,ct);
        });
#if BMS_FACTORY_APP
        root.Children.Add(new TextBlock{Text="OCV：21点，单体电压严格递减，SOC不递增。每次完整保存选中点的电压与SOC。",TextWrapping=TextWrapping.Wrap,Margin=new Thickness(0,12,0,4)});
        Column(_d015OcvGrid,"点",nameof(D015OcvRow.Index),true);
        Column(_d015OcvGrid,"单体mV",nameof(D015OcvRow.Millivolts));
        Column(_d015OcvGrid,"SOC%",nameof(D015OcvRow.Percent));
        root.Children.Add(_d015OcvGrid);
        AddSave("保存选中OCV点",async (client,ct)=>{
            if(!_d015OcvGrid.CommitEdit(DataGridEditingUnit.Cell,true) || !_d015OcvGrid.CommitEdit(DataGridEditingUnit.Row,true))
                throw new ArgumentException("请先修正OCV输入。");
            if(_d015OcvGrid.SelectedItem is not D015OcvRow row || row.Millivolts is <1000 or >5000 || row.Percent is <0 or >100)
                throw new ArgumentException("请选择合法OCV点，电压1000～5000mV，SOC 0～100%。");
            await client.WriteD008VerifiedAsync((ushort)(0x2200+2*row.Index),new[]{(ushort)row.Millivolts,(ushort)row.Percent},false,ct);
        });
#endif
        void AddSave(string text,Func<BmsClient,CancellationToken,Task> action) {
            var button=new Button{Content=text,Margin=new Thickness(4),IsEnabled=false};
            button.Click+=async(_,_)=>await ParameterRunAsync(action);
            _d015SaveButtons.Add(button);root.Children.Add(button);
        }
    }
    private void UpdateD015ParameterVisibility()
    {
        bool d015=_bms?.IsD015==true;
        UpdateProtectionProductDisplay(d015);
        if(_d015ParameterTab is not null)_d015ParameterTab.Visibility=d015?Visibility.Visible:Visibility.Collapsed;
        foreach(var tab in _d015LegacyTabs)tab.Visibility=d015||_bms?.UsesC060Protocol==true?Visibility.Collapsed:Visibility.Visible;
    }
    private void ShowD015Parameters(D008ParameterCapture capture)
    {
        bool d015=_bms?.IsD015==true;
        UpdateD015ParameterVisibility();
        _d015BasicGrid.ItemsSource=null;_d015OcvGrid.ItemsSource=null;
        if(!d015)return;
        if(capture.Blocks.TryGetValue("D015Basic",out var w) && w.Length==32) {
            var definitions=new (int Offset,string Name,string Unit,bool Signed)[]{
                (4,"分流电阻","µΩ",false),(7,"硬件过压恢复","mV",false),(8,"硬件欠压恢复","mV",false),
                (9,"共用恢复稳定时间","ms",false),(10,"预充时间","ms",false),
                (12,"均衡起始电压","mV",false),(13,"均衡压差","mV",false),(14,"均衡最高温度","0.1℃",true),
                (15,"允许均衡","0/1",false),(16,"空闲判断电流门限","mA",false),(17,"开关闭合时RTC唤醒周期","s",false),
                (18,"开关断开时RTC唤醒周期","s",false),(19,"欠压保护后深睡等待","min",false),(20,"空闲等待","s",false),
                (21,"MOS总许可","0/1",false),(22,"允许RTC","0/1",false),(23,"反向电流门限","mA",false),
                (24,"过流恢复电流","mA",false),(25,"充高温恢复","0.1℃",true),(26,"放高温恢复","0.1℃",true),
                (27,"充低温恢复","0.1℃",true),(28,"放低温恢复","0.1℃",true),(31,"SOC满电校正门限","mV",false)};
            _d015BasicGrid.ItemsSource=definitions.Select(d=>new D015SettingRow((ushort)(0x3400+d.Offset),d.Name,d.Signed?(short)w[d.Offset]:w[d.Offset],d.Unit,d.Signed)).ToArray();
        }
        if(capture.Blocks.TryGetValue("D015Ocv",out var points) && points.Length==42)
            _d015OcvGrid.ItemsSource=Enumerable.Range(0,21).Select(i=>new D015OcvRow(i,points[2*i],points[2*i+1])).ToArray();
    }
}

public sealed class D015SettingRow(ushort address,string name,int value,string unit,bool signed)
{
    public ushort Address { get; }=address;
    public string AddressText=>$"{Address:X4}";
    public string Name { get; }=name;
    public int Value { get; set; }=value;
    public string Unit { get; }=unit;
    public bool Signed { get; }=signed;
    public bool IsReadOnly=>Address is 0x340A or >=0x3410 and <=0x3413;
    public string Source=>IsReadOnly ? "固件宏，只读" : "Flash参数";
}
public sealed class D015OcvRow(int index,int millivolts,int percent)
{
    public int Index { get; }=index;
    public int Millivolts { get; set; }=millivolts;
    public int Percent { get; set; }=percent;
}
