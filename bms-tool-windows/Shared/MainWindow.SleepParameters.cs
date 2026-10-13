using System.Windows;
using System.Windows.Controls;

namespace BmsTool.Windows;
public partial class MainWindow
{
    private readonly TextBox _sleepLowMv=new(),_sleepLowMinutes=new(),_sleepNormalMv=new(),
        _sleepNormalMinutes=new(),_sleepAfeMinutes=new();
    private readonly Button _sleepSave=new(),_sleepDefaults=new();

    private void AddSleepParameterControls(StackPanel root,Action<string,Control> field,
        Action<Button,string,Func<Task>> saveButton)
    {
        root.Children.Add(new TextBlock{Text="保护性休眠参数",FontWeight=FontWeights.Bold,Margin=new Thickness(4,12,4,4)});
        field("下档单体电压 / mV",_sleepLowMv);
        field("下档延时 / 分钟",_sleepLowMinutes);
        field("上档单体电压 / mV",_sleepNormalMv);
        field("上档无可靠充电延时 / 分钟",_sleepNormalMinutes);
        field("AFE 异常延时 / 分钟",_sleepAfeMinutes);
        root.Children.Add(new TextBlock{Text="最低单体低于下档时累计下档时间；低于上档且没有可靠充电时累计上档时间。AFE 异常独立计时。条件有效恢复才清零，无新鲜数据继续保持已确认条件。多个条件取最先到期项；D008 到期关闭 MCU 供电。",TextWrapping=TextWrapping.Wrap,Margin=new Thickness(4)});
        root.Children.Add(new TextBlock{Text="过放未恢复：固定 3 分钟。三级单体或总压欠压保护持续未恢复时独立计时，上位机不能修改；其它条件可更早触发关机。",TextWrapping=TextWrapping.Wrap,Margin=new Thickness(4)});
        saveButton(_sleepSave,"保存整组休眠参数并读回",()=>ParameterRunAsync((client,ct)=>
            client.WriteD008VerifiedAsync(BmsSleepParameters.Base,BmsSleepParameters.Build(
                _sleepLowMv.Text,_sleepLowMinutes.Text,_sleepNormalMv.Text,_sleepNormalMinutes.Text,
                _sleepAfeMinutes.Text),false,ct)));
        saveButton(_sleepDefaults,"恢复设备休眠默认值并读回",()=>ParameterRunAsync((client,ct)=>
            client.RestoreSleepDefaultsAsync(ct)));
    }

    private void ShowSleepParameters(D008ParameterCapture capture,List<string> readback)
    {
        foreach(var box in new[]{_sleepLowMv,_sleepLowMinutes,_sleepNormalMv,_sleepNormalMinutes,_sleepAfeMinutes})box.Clear();
        if(!BmsSleepParameters.Available(capture)) {readback.Add("休眠：设备未声明支持配置");return;}
        if(!capture.Blocks.TryGetValue("Sleep",out var words)) {readback.Add("休眠：本次读取失败");return;}
        try {
            BmsSleepParameters.Validate(words);
            _sleepLowMv.Text=words[0].ToString();_sleepLowMinutes.Text=words[1].ToString();
            _sleepNormalMv.Text=words[2].ToString();_sleepNormalMinutes.Text=words[3].ToString();
            _sleepAfeMinutes.Text=words[4].ToString();
            readback.Add($"休眠：下档 <{words[0]} mV / {words[1]} 分钟，上档 <{words[2]} mV 且无可靠充电 / {words[3]} 分钟，AFE 异常 {words[4]} 分钟；过放未恢复固定 3 分钟");
        } catch(ArgumentException){readback.Add("休眠：读回值无效，禁止写入");}
    }

    private void UpdateSleepParameterControls()
    {
        bool ready=false;
        if(!_parameterBusy && _parameterCapture is { } capture && BmsSleepParameters.Available(capture) &&
           capture.Blocks.TryGetValue("Sleep",out var values)) {
            try{BmsSleepParameters.Validate(values);ready=true;}catch(ArgumentException){}
        }
        foreach(var control in new Control[]{_sleepLowMv,_sleepLowMinutes,_sleepNormalMv,
            _sleepNormalMinutes,_sleepAfeMinutes,_sleepSave,_sleepDefaults})control.IsEnabled=ready;
    }
}
