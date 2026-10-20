using System.Globalization;
using System.IO;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using Microsoft.Win32;

namespace BmsTool.Windows;

public partial class MainWindow
{
    private readonly TextBlock _socProfileStatus = new() { Text="SOC：尚未读取设备", TextWrapping=TextWrapping.Wrap, Margin=new Thickness(4) };
    private readonly TextBox _socOcvPoints = new() { IsReadOnly=true, AcceptsReturn=true, MinHeight=220,
        VerticalScrollBarVisibility=ScrollBarVisibility.Auto, FontFamily=new System.Windows.Media.FontFamily("Consolas") };
    private SocProfileParameters? _socProfile;
    private SocProfileBundle? _socCandidate;

    private void AddSocProfileControls(Panel root)
    {
#if BMS_FACTORY_APP
        var panel=new StackPanel{Margin=new Thickness(8)};root.Children.Add(panel);
        void Button(string label,Func<Task> action) {
            var button=new Button{Content=label,Margin=new Thickness(4),Padding=new Thickness(10,4,10,4),HorizontalAlignment=HorizontalAlignment.Left};
            button.Click+=async(_,_)=>{try{await action();}catch(Exception ex){MessageBox.Show(this,ex.Message,"SOC模型",MessageBoxButton.OK,MessageBoxImage.Warning);}};
            panel.Children.Add(button);
        }
        panel.Children.Add(new TextBlock{Text="完整SOC模型配置：读取 → 选择或导入 → 比较 → 保存并核对。此页不修改当前SOC、容量或循环。",
            TextWrapping=TextWrapping.Wrap,Margin=new Thickness(4)});
        Button("读取设备SOC模型",()=>ParameterRunAsync(async(c,t)=>{
            _socProfile=await c.ReadSocProfileAsync(t);_socCandidate=null;ShowSocProfile(_socProfile);
        },refresh:false,operation:null,requiresUnlock:false));
        panel.Children.Add(_socProfileStatus);
        Button("选择LFP首轮评估模型",()=>{SelectSocCandidate(BmsClient.CreateSocTrialBundle(1));return Task.CompletedTask;});
        Button("选择NMC 4.20V首轮对照模型",()=>{SelectSocCandidate(BmsClient.CreateSocTrialBundle(2));return Task.CompletedTask;});
        Button("导入完整SOC模型JSON",()=>{
            var dialog=new OpenFileDialog{Filter="SOC模型JSON|*.json",CheckFileExists=true};
            if(dialog.ShowDialog(this)!=true)return Task.CompletedTask;
            if(new FileInfo(dialog.FileName).Length>1024*1024)throw new ArgumentException("配置文件不能超过1MiB。");
            SelectSocCandidate(BmsClient.ImportSocBundle(File.ReadAllText(dialog.FileName,Encoding.UTF8)));
            return Task.CompletedTask;
        });
        Button("导出设备SOC模型JSON",()=>ParameterRunAsync(async(c,t)=>{
            var current=await c.ReadSocProfileAsync(t);
            var dialog=new SaveFileDialog{Filter="SOC模型JSON|*.json",FileName="soc-device-profile.json"};
            if(dialog.ShowDialog(this)==true)File.WriteAllText(dialog.FileName,BmsClient.ExportSocBundle(new SocProfileBundle{
                Name="设备SOC模型备份",Source="设备完整读回；导出不表示模型已经适配验证",EngineeringCandidate=true,Profile=current}),new UTF8Encoding(false));
            _socProfile=current;ShowSocProfile(current);
        },refresh:false,operation:null,requiresUnlock:false));
        panel.Children.Add(new TextBlock{Text="LFP 3200～3400mV禁止普通OCV校正；普通OCV只向下，连续静置60分钟，最近15分钟逐串变化≤5mV，电流≤200mA、压差≤200mV。SOC不作温度修正。",
            TextWrapping=TextWrapping.Wrap,Margin=new Thickness(4)});
        panel.Children.Add(new TextBlock{Text="只有电压判满才允许目标显示100%；人工设置100%不会授予资格。运行显示每5秒最多变化1个百分点，保护不等待显示。工程候选须验证后才能发布客户。",
            TextWrapping=TextWrapping.Wrap,Margin=new Thickness(4)});
        panel.Children.Add(_socOcvPoints);
        Button("保存所选完整模型并核对",()=>{
            if(_socCandidate is null)throw new InvalidOperationException("请先选择或导入模型。");
            var candidate=_socCandidate;
            if(_socProfile is null)throw new InvalidOperationException("请先读取设备SOC模型。");
            if(MessageBox.Show(this,SocModelPreview(_socProfile,candidate)+"\n\n确认保存整套模型？", "确认SOC模型变更",MessageBoxButton.YesNo,MessageBoxImage.Warning)!=MessageBoxResult.Yes)
                return Task.CompletedTask;
            return ParameterRunAsync(async(c,t)=>{
                _socProfile=await c.SaveSocProfileAsync(candidate.Profile,BmsServiceAccess.DefaultPassword,t);
                _socCandidate=null;ShowSocProfile(_socProfile);
            },refresh:false);
        });
#endif
    }

    private static string SocModelDescription(SocProfileParameters p)
    {
        if(p.ProfileVersion!=3)return $"旧模型v{p.ProfileVersion}；仅作备份。配套v3固件在显式更新模型前暂停电压校准，安时积分继续。\n"+
            "OCV点："+string.Join(", ",p.OcvPoints.Select(x=>$"{x.Millivolts}mV={x.Percent}%"));
        return $"{(p.Chemistry==1?"LFP":"NMC")} / 模型版本 {p.ProfileVersion}\n"+
            $"满电：最高≥{p.FullSyncMv}mV，最低≥{p.FullSyncMv-p.FullMinMarginMv}mV，压差≤{p.FullCellDeltaMaxMv}mV，60秒\n"+
            $"空电：最低≤{p.EmptySyncMv}mV，压差≤{p.EmptyMaxMarginMv}mV，5秒\n"+
            $"普通OCV范围：{p.ValidMinMv}～{p.ValidMaxMv}mV；带宽：{Math.Max(p.ErrorBandPercent,p.MinimumErrorBandPercent)}个百分点\n"+
            "OCV点："+string.Join(", ",p.OcvPoints.Select(x=>$"{x.Millivolts}mV={x.Percent}%"));
    }

    private static string SocModelPreview(SocProfileParameters current,SocProfileBundle candidate)
    {
        return "当前设备\n"+SocModelDescription(current)+"\n\n所选模型："+candidate.Name+
            "\n来源："+candidate.Source+"\n"+(candidate.EngineeringCandidate?"工程候选，尚需适配验证\n":"仍须核对本电池包适用条件\n")+
            SocModelDescription(candidate.Profile);
    }

    private void SelectSocCandidate(SocProfileBundle candidate)
    {
        if(_socProfile is null)throw new InvalidOperationException("请先读取设备SOC模型，再比较候选。");
        if(!_socProfile.SupportsPolicyV3)throw new NotSupportedException("设备固件尚不支持SOC策略v3，请先升级配套固件。");
        if(candidate.Profile.Chemistry!=_socProfile.Chemistry)throw new ArgumentException("候选类型与设备不一致，请先通过整套电池类型切换操作处理。");
        _socCandidate=candidate;
        _socOcvPoints.Text=SocModelPreview(_socProfile,candidate);
    }

    private void ShowSocProfile(SocProfileParameters p)
    {
#if BMS_FACTORY_APP
        _socProfileStatus.Text=$"{(p.Chemistry==1?"磷酸铁锂":"三元锂")} · 模型v{p.ProfileVersion} · {(p.Persisted?"已保存":"编译默认")} · "+
            (p.SupportsPolicyV3?"支持SOC策略v3":"旧固件：只读备份，不写入v3模型");
        _socOcvPoints.Text=_socCandidate is null?SocModelDescription(p):SocModelPreview(p,_socCandidate);
#endif
    }
}
