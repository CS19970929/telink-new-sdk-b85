using System.Collections.ObjectModel;
using System.Globalization;
using System.IO;
using System.Text.Json;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using Microsoft.Win32;
namespace BmsTool.Windows;

/* Internal edition only; uses the shared BmsClient protocol and validators. */
public sealed class C077AfeEditorWindow:Window
{
    private readonly BmsClient client;
    private readonly ComboBox chip=new(){ItemsSource=new[]{"AFE1","AFE2"},SelectedIndex=0,Width=100,Margin=new Thickness(4)};
    private readonly TextBlock status=new(){TextWrapping=TextWrapping.Wrap,Margin=new Thickness(8)};
    private readonly DataGrid grid=new(){AutoGenerateColumns=false,CanUserAddRows=false,Margin=new Thickness(6),MinHeight=280};
    private readonly DataGrid rawGrid=new(){AutoGenerateColumns=false,IsReadOnly=true,CanUserAddRows=false,Margin=new Thickness(6)};
    private readonly ObservableCollection<Row> rows=new();
    private C077HardwareSnapshot? snapshot;
    private bool busy,resettingChip;
    private int shownChip;
    public C077AfeEditorWindow(BmsClient value)
    {
        client=value;Title="C077 双SH367309硬件保护 · Flash保存 / EEPROM明确下发";Width=1250;Height=850;MinWidth=900;MinHeight=600;
        WindowStartupLocation=WindowStartupLocation.CenterOwner;Background=SystemColors.WindowBrush;Foreground=SystemColors.WindowTextBrush;
        var panel=new DockPanel{Background=SystemColors.WindowBrush};Content=panel;
        var top=new StackPanel();DockPanel.SetDock(top,Dock.Top);panel.Children.Add(top);
        top.Children.Add(new TextBlock{Margin=new Thickness(8),TextWrapping=TextWrapping.Wrap,Text="先读取，按片编辑并保存到MCU Flash；保存不会写309。确认关闭输出/均衡后，再明确下发所选片或双片。软件保护参数在主窗口单独编辑。固定16串、CTL双路禁止、MCU均衡及AFE1单CADC；深睡不进入SHIP仓运。"});
        var bar=new WrapPanel();top.Children.Add(bar);bar.Children.Add(chip);
        Add("读取Flash + 核对两片EEPROM",async()=>await Reload());
        Add("关闭输出 / 均衡",CloseOutputs);
        Add("保存所选片到Flash",Save);
        Add("下发所选片EEPROM",()=>Program(1<<chip.SelectedIndex));
        Add("下发双片EEPROM",()=>Program(3));
        Add("导出硬保护JSON",Export);
        Add("导入到所选片草稿",Import);
        top.Children.Add(status);
        var tabs=new TabControl();panel.Children.Add(tabs);
        tabs.Items.Add(new TabItem{Header="物理参数及功能位",Content=grid});
        tabs.Items.Add(new TabItem{Header="25字节期望 / AFE实际读回",Content=rawGrid});
        void Col(DataGrid target,string label,string binding,bool readOnly,int width) {
            var style=new Style(typeof(TextBlock));style.Setters.Add(new Setter(ToolTipProperty,new Binding(binding)));
            target.Columns.Add(new DataGridTextColumn{Header=label,Binding=new Binding(binding){UpdateSourceTrigger=UpdateSourceTrigger.PropertyChanged},IsReadOnly=readOnly,Width=width,ElementStyle=style});
        }
        var mismatchStyle=new Style(typeof(DataGridRow));var mismatch=new DataTrigger{Binding=new Binding("Different"),Value=true};
        mismatch.Setters.Add(new Setter(BackgroundProperty,System.Windows.Media.Brushes.MistyRose));mismatch.Setters.Add(new Setter(ForegroundProperty,System.Windows.Media.Brushes.DarkRed));mismatchStyle.Triggers.Add(mismatch);rawGrid.RowStyle=mismatchStyle;
        Col(grid,"参数","Name",true,280);Col(grid,"Flash已保存","Saved",true,115);Col(grid,"待保存","Draft",false,115);Col(grid,"AFE实际读回","Actual",true,135);Col(grid,"单位","Unit",true,85);Col(grid,"允许值 / 说明","Hint",true,420);
        Col(rawGrid,"地址","Address",true,100);Col(rawGrid,"寄存器","Name",true,220);Col(rawGrid,"Flash期望字节","Expected",true,150);Col(rawGrid,"AFE读回字节","Actual",true,150);Col(rawGrid,"结果","Result",true,160);
        grid.ItemsSource=rows;
        chip.SelectionChanged+=(_,_)=>{
            if(busy||resettingChip)return;
            if(snapshot is not null && rows.Any(r=>r.Draft!=r.Saved)) {
                resettingChip=true;chip.SelectedIndex=shownChip;resettingChip=false;
                MessageBox.Show(this,"当前片有未保存草稿，请先保存Flash或重新读取，再切换芯片。","草稿尚未保存");return;
            }
            ShowChip();
        };
        Loaded+=async(_,_)=>await Run(Reload);
        Closing+=(_,e)=>{if(busy){e.Cancel=true;status.Text="当前操作尚未返回，结果确认后再关闭窗口。";}};
        void Add(string label,Func<Task> action){var b=new Button{Content=label,Margin=new Thickness(4),Padding=new Thickness(8,5,8,5)};b.Click+=async(_,_)=>await Run(action);bar.Children.Add(b);}
    }
    private async Task Run(Func<Task> action)
    {
        if(busy)return;busy=true;grid.IsReadOnly=true;chip.IsEnabled=false;
        try{await action();}
        catch(Exception ex){
            string failure=ex.Message;
            try{snapshot=await client.ReadC077HardwareAsync(false);ShowChip();failure+="\n"+Details(snapshot);}catch(Exception readError){failure+="；诊断读取未完成："+readError.Message;}
            status.Text="未完成 / 结果未确认："+failure+"。未自动重发。";MessageBox.Show(this,status.Text,"309维护",MessageBoxButton.OK,MessageBoxImage.Warning);}
        finally{busy=false;grid.IsReadOnly=false;chip.IsEnabled=true;}
    }
    private async Task Reload()
    {
        snapshot=await client.ReadC077HardwareAsync(true);ShowChip();
        status.Text=Details(snapshot)+"\nFlash与EEPROM分别显示；温度读回是寄存器反算近似值，是否一致以25字节比较为准。";
    }
    private static string Details(C077HardwareSnapshot h)
    {
        string text=h.Actual.Summary;
        for(int n=0;n<2;n++) {
            uint mask=(uint)h.Expected[n][13]|((uint)h.Expected[n][14]<<16);
            int bytes=h.Expected[n][15];
            text+=$"\nAFE{n+1} 差异位图0x{mask:X7}；记录字节：期望0x{bytes>>8:X2} / 实读0x{bytes&255:X2}（I2C/CRC错误时实读不可信）";
        }
        return text;
    }
    private void ShowChip()
    {
        rows.Clear();if(snapshot is null)return;
        int n=Math.Max(0,chip.SelectedIndex);shownChip=n;var saved=snapshot.Saved[n];var actual=C077Hardware.Decode(snapshot.Actual.Chips[n].Eeprom,snapshot.Actual.Chips[n].Tr);
        for(int bit=0;bit<7;bit++)rows.Add(new(){Index=0,Bit=bit,Name=C077Hardware.FeatureNames[bit],Saved=((saved[0]>>bit)&1).ToString(),Draft=((saved[0]>>bit)&1).ToString(),Actual=((actual[0]>>bit)&1).ToString(),Unit="0/1",Hint="1=该位开启；DIS_PF 的1表示禁止保护。"});
        foreach(var f in C077Hardware.Fields.Skip(1))rows.Add(new(){Index=f.Index,Name=f.Name,Saved=Display(f,f.Value(saved[f.Index])),Draft=Display(f,f.Value(saved[f.Index])),Actual=(f.Signed?"≈":"")+Display(f,actual[f.Index]),Unit=f.Signed?"℃":f.Unit,Hint=f.Signed?"−40..100℃整℃档位；受本片TR编码范围约束":f.Limits});
        string[] names={"SCONF1","SCONF2","OVT/LDRT/OVH","OVL","UVT/OVRH","OVRL","UV","UVR","BALV","PREV","L0V","PFV","OCD1","OCD2","SC","OCC","CHS/MOST/OCRT/PFT","OTC","OTCR","UTC","UTCR","OTD","OTDR","UTD","UTDR"};
        var expected=snapshot.Expected[n];var chipState=snapshot.Actual.Chips[n];
        rawGrid.ItemsSource=Enumerable.Range(0,25).Select(i=>{int e=i%2==0?expected[i/2]>>8:expected[i/2]&255;return new{Address=$"0x{i:X2}",Name=names[i],Expected=$"0x{e:X2}",Actual=$"0x{chipState.Eeprom[i]:X2}",Different=e!=chipState.Eeprom[i],Result=e==chipState.Eeprom[i]?(chipState.ProfileMatches?"一致":"字节相同，整片未确认"):"待下发 / 不一致"};}).ToArray();
    }
    private static string Display(C077HardwareField f,int value)=>f.Signed?(value/10m).ToString("0.0",CultureInfo.InvariantCulture):value.ToString(CultureInfo.InvariantCulture);
    private ushort[] Draft()
    {
        if(snapshot is null)throw new InvalidOperationException("请先读取设备。");
        grid.CommitEdit(DataGridEditingUnit.Cell,true);grid.CommitEdit(DataGridEditingUnit.Row,true);
        var words=(ushort[])snapshot.Saved[chip.SelectedIndex].Clone();words[0]=0;
        foreach(var row in rows){
            int v;
            if(row.Bit<0 && C077Hardware.Fields[row.Index].Signed) {
                if(!decimal.TryParse(row.Draft,NumberStyles.Number,CultureInfo.InvariantCulture,out decimal temperature) || temperature*10m!=decimal.Truncate(temperature*10m))throw new ArgumentException(row.Name+"温度格式无效。");
                v=checked((int)(temperature*10m));
            } else if(!int.TryParse(row.Draft,NumberStyles.Integer,CultureInfo.InvariantCulture,out v))throw new ArgumentException(row.Name+"必须是整数。");
            if(row.Bit>=0){if(v is not (0 or 1))throw new ArgumentException(row.Name+"只接受0/1。");words[0]|=(ushort)(v<<row.Bit);}
            else {var f=C077Hardware.Fields[row.Index];if(f.Signed){if(v<short.MinValue||v>short.MaxValue)throw new ArgumentOutOfRangeException(row.Name);words[row.Index]=unchecked((ushort)(short)v);}else words[row.Index]=checked((ushort)v);}}
        C077Hardware.Validate(words,snapshot.Actual.Chips[chip.SelectedIndex].Tr);return words;
    }
    private async Task Save()
    {
        var values=Draft();int n=chip.SelectedIndex;
        if(MessageBox.Show(this,$"确认将AFE{n+1}草稿保存到MCU Flash？不会写AFE EEPROM。","保存Flash",MessageBoxButton.YesNo)!=MessageBoxResult.Yes)return;
        await client.SaveC077HardwareAsync(n,values,snapshot!.Saved[n]);await Reload();status.Text="AFE"+(n+1)+"已保存并读回Flash；EEPROM尚需明确下发。\n"+status.Text;
    }
    private async Task Program(int targets)
    {
        if(snapshot is null)throw new InvalidOperationException("请先读取设备。");
        if(!Draft().SequenceEqual(snapshot.Saved[chip.SelectedIndex]))throw new InvalidOperationException("当前片仍有未保存草稿，请先保存Flash或重新读取。");
        if(MessageBox.Show(this,$"确认下发{(targets==3?"双片":"AFE"+(chip.SelectedIndex+1))} EEPROM？按Flash保存值写差异，逐字节读回；中断后可能部分完成，保持输出关闭。","明确写入EEPROM",MessageBoxButton.YesNo,MessageBoxImage.Warning)!=MessageBoxResult.Yes)return;
        await client.ProgramC077HardwareAsync(targets);await Reload();status.Text="所选EEPROM事务已完成并读回确认。MOS/均衡许可保持关闭；另一片未下发配置仍会限制输出。\n"+status.Text;
    }
    private async Task CloseOutputs()
    {
        await client.WriteSingleRegisterAsync(0x1103,3);await client.WriteSingleRegisterAsync(0x1103,1);
        for(int i=0;i<30;i++){await Task.Delay(100);var s=await client.ReadC077AfeAsync();if(s.OutputRequest==0&&s.Permissions==0&&s.BalanceOff){status.Text="输出/均衡已关闭，固件会在保存和下发时再次检查。";return;}}
        throw new TimeoutException("输出/均衡关闭尚未确认。");
    }
    private Task Export()
    {
        if(snapshot is null)throw new InvalidOperationException("请先读取设备。");
        var d=new SaveFileDialog{Filter="C077硬保护JSON|*.json",FileName="C077_309_hardware.json"};
        if(d.ShowDialog(this)==true){File.WriteAllText(d.FileName,JsonSerializer.Serialize(C077Hardware.File(snapshot.Saved),new JsonSerializerOptions{WriteIndented=true}));status.Text="已导出两片Flash实际保存值；未导出未保存草稿。";}
        return Task.CompletedTask;
    }
    private Task Import()
    {
        if(snapshot is null)throw new InvalidOperationException("请先读取设备。");
        var d=new OpenFileDialog{Filter="C077硬保护JSON|*.json"};if(d.ShowDialog(this)!=true)return Task.CompletedTask;
        var file=C077Hardware.ParseFile(File.ReadAllText(d.FileName));var words=C077Hardware.Import(file)[chip.SelectedIndex];C077Hardware.Validate(words,snapshot.Actual.Chips[chip.SelectedIndex].Tr);
        foreach(var row in rows)row.Draft=row.Bit>=0?((words[0]>>row.Bit)&1).ToString(CultureInfo.InvariantCulture):Display(C077Hardware.Fields[row.Index],C077Hardware.Fields[row.Index].Value(words[row.Index]));
        grid.Items.Refresh();status.Text="仅导入所选片草稿，尚未保存Flash或写入EEPROM。";return Task.CompletedTask;
    }
    private sealed class Row
    {
        public int Index{get;set;}public int Bit{get;set;}= -1;public string Name{get;set;}="";public string Saved{get;set;}="";public string Draft{get;set;}="";public string Actual{get;set;}="";public string Unit{get;set;}="";public string Hint{get;set;}="";
    }
}
