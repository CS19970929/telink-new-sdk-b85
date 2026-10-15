using System.Globalization;
using System.Windows;
using System.Windows.Controls;

namespace BmsTool.Windows;

public partial class MainWindow
{
    private readonly Dictionary<string, TextBox> _socProfileFields = new();
    private readonly TextBlock _socProfileStatus = new() { Text="SOC / OCV：尚未读取", TextWrapping=TextWrapping.Wrap, Margin=new Thickness(4) };
    private readonly TextBox _socOcvPoints = new() { AcceptsReturn=true, MinHeight=180, VerticalScrollBarVisibility=ScrollBarVisibility.Auto, FontFamily=new System.Windows.Media.FontFamily("Consolas") };
    private SocProfileParameters? _socProfile;

    private void AddSocProfileControls(Panel root)
    {
        var group = new GroupBox { Header="SOC / OCV 算法参数", Margin=new Thickness(4,12,4,4), Padding=new Thickness(8) };
        var panel = new StackPanel(); group.Content=panel; root.Children.Add(group);
        void Button(string label, Func<Task> action)
        {
            var b=new Button{Content=label,Margin=new Thickness(4),Padding=new Thickness(10,4,10,4),HorizontalAlignment=HorizontalAlignment.Left};
            b.Click+=async(_,_)=>await action();panel.Children.Add(b);
        }
        void Field(string key,string label)
        {
            var row=new DockPanel{Margin=new Thickness(4)};
            var title=new TextBlock{Text=label,Width=250,VerticalAlignment=VerticalAlignment.Center};
            DockPanel.SetDock(title,Dock.Left);row.Children.Add(title);
            var input=new TextBox{Width=220,HorizontalAlignment=HorizontalAlignment.Left};
            _socProfileFields[key]=input;row.Children.Add(input);panel.Children.Add(row);
        }
        Button("读取 SOC / OCV 参数",()=>ParameterRunAsync(async(c,t)=>{
            _socProfile=await c.ReadSocProfileAsync(t);ShowSocProfile(_socProfile);
        },refresh:false));
        panel.Children.Add(_socProfileStatus);
        Field("deadband","静置电流死区 / mA");Field("rest","OCV 静置准备 / s");Field("band","OCV 误差带 / %");
        Field("validMin","有效电压下限 / mV");Field("validMax","有效电压上限 / mV");
        Field("full","满电同步电压 / mV");Field("fullMargin","满电最小裕量 / mV");
        Field("empty","空电同步电压 / mV");Field("emptyMargin","空电最大裕量 / mV");Field("minBand","OCV 最小误差带 / %");
        Field("fullDelta","满电最大压差 / mV");Field("terminalDelta","末端最大压差 / mV");
        Field("terminalStart","末端起始偏移 / mV");Field("terminalL1","末端 L1 偏移 / mV");
        Field("terminalL2","末端 L2 偏移 / mV");Field("terminalL3","末端 L3 偏移 / mV");
        panel.Children.Add(new TextBlock{Text="OCV 点 / mV=SOC%",Margin=new Thickness(4)});panel.Children.Add(_socOcvPoints);
        Button("保存 SOC / OCV 参数并核对",()=>{
            if(MessageBox.Show(this,"这会改变 SOC 估算、静置校正和端点判断。请确认 OCV 数据来自对应电芯验证，是否继续？","保存 SOC / OCV",MessageBoxButton.YesNo,MessageBoxImage.Warning)!=MessageBoxResult.Yes)return Task.CompletedTask;
            return ParameterRunAsync(async(c,t)=>{SocProfileParameters candidate=ReadSocProfileEditor();_socProfile=await c.SaveSocProfileAsync(candidate,BmsServiceAccess.DefaultPassword,t);ShowSocProfile(_socProfile);},refresh:false);
        });
        Button("恢复当前电池类型的 SOC / OCV 默认",()=>{
            if(MessageBox.Show(this,"仅恢复当前电池类型的 SOC 算法参数与 OCV 曲线，不改变当前 SOC、容量或历史。是否继续？","恢复 SOC / OCV 默认",MessageBoxButton.YesNo,MessageBoxImage.Question)!=MessageBoxResult.Yes)return Task.CompletedTask;
            return ParameterRunAsync(async(c,t)=>{_socProfile=await c.ResetSocProfileAsync(BmsServiceAccess.DefaultPassword,t);ShowSocProfile(_socProfile);},refresh:false);
        });
    }

    private void ShowSocProfile(SocProfileParameters p)
    {
        _socProfileStatus.Text=$"{(p.Chemistry==1?"磷酸铁锂":"三元锂")} · profile v{p.ProfileVersion} · {p.OcvPoints.Count} 个 OCV 点 · {(p.Persisted?"设备已保存":"使用编译默认")}";
        void Set(string key,ushort value)=>_socProfileFields[key].Text=value.ToString(CultureInfo.InvariantCulture);
        Set("deadband",p.CurrentDeadbandMa);Set("rest",p.RestPrepareSeconds);Set("band",p.ErrorBandPercent);
        Set("validMin",p.ValidMinMv);Set("validMax",p.ValidMaxMv);Set("full",p.FullSyncMv);Set("fullMargin",p.FullMinMarginMv);
        Set("empty",p.EmptySyncMv);Set("emptyMargin",p.EmptyMaxMarginMv);Set("minBand",p.MinimumErrorBandPercent);
        Set("fullDelta",p.FullCellDeltaMaxMv);Set("terminalDelta",p.TerminalCellDeltaMaxMv);Set("terminalStart",p.TerminalStartOffsetMv);
        Set("terminalL1",p.TerminalL1OffsetMv);Set("terminalL2",p.TerminalL2OffsetMv);Set("terminalL3",p.TerminalL3OffsetMv);
        _socOcvPoints.Text=string.Join(Environment.NewLine,p.OcvPoints.Select(x=>$"{x.Millivolts}={x.Percent}"));
    }

    private SocProfileParameters ReadSocProfileEditor()
    {
        if(_socProfile is null)throw new InvalidOperationException("请先读取设备当前 SOC / OCV 参数。");
        ushort U(string key)=>ushort.Parse(_socProfileFields[key].Text.Trim(),CultureInfo.InvariantCulture);
        var p=new SocProfileParameters{Chemistry=_socProfile.Chemistry,ProfileId=_socProfile.ProfileId,ProfileVersion=_socProfile.ProfileVersion,
            CurrentDeadbandMa=U("deadband"),RestPrepareSeconds=U("rest"),ErrorBandPercent=U("band"),ValidMinMv=U("validMin"),ValidMaxMv=U("validMax"),
            FullSyncMv=U("full"),FullMinMarginMv=U("fullMargin"),EmptySyncMv=U("empty"),EmptyMaxMarginMv=U("emptyMargin"),
            MinimumErrorBandPercent=U("minBand"),FullCellDeltaMaxMv=U("fullDelta"),TerminalCellDeltaMaxMv=U("terminalDelta"),
            TerminalStartOffsetMv=U("terminalStart"),TerminalL1OffsetMv=U("terminalL1"),TerminalL2OffsetMv=U("terminalL2"),TerminalL3OffsetMv=U("terminalL3")};
        foreach(string line in _socOcvPoints.Text.Split(new[]{'\r','\n'},StringSplitOptions.RemoveEmptyEntries)){
            string[] pair=line.Trim().Split(new[]{'=','，',',',':'},StringSplitOptions.RemoveEmptyEntries);
            if(pair.Length!=2)throw new FormatException("OCV 点格式应为 mV=SOC%。");
            p.OcvPoints.Add(new SocOcvPoint(ushort.Parse(pair[0].Trim(),CultureInfo.InvariantCulture),ushort.Parse(pair[1].Trim().TrimEnd('%'),CultureInfo.InvariantCulture)));
        }
        return p;
    }
}
