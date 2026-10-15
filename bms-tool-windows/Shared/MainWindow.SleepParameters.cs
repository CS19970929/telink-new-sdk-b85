using System.Windows;
using System.Windows.Controls;

namespace BmsTool.Windows;
public partial class MainWindow
{
    private readonly TextBox _sleepLowMv=new(),_sleepLowMinutes=new(),_sleepNormalMv=new(),
        _sleepNormalMinutes=new();
    private readonly Button _sleepSave=new(),_sleepDefaults=new();

    private void AddSleepParameterControls(StackPanel root,Action<string,Control> field,
        Action<Button,string,Func<Task>> saveButton)
    {
        root.Children.Add(new TextBlock{Text="自动休眠设置",FontWeight=FontWeights.Bold,Margin=new Thickness(4,12,4,4),
            ToolTip="一级、二级条件独立计时并取最先到期项；过放未恢复固定 3 分钟。"});
        field("一级休眠电压 / mV",_sleepLowMv);
        field("一级休眠延时 / 分钟",_sleepLowMinutes);
        field("二级休眠电压 / mV",_sleepNormalMv);
        field("二级休眠延时 / 分钟",_sleepNormalMinutes);
        saveButton(_sleepSave,"保存休眠设置并读回",()=>ParameterRunAsync((client,ct)=>
            client.WriteD008VerifiedAsync(BmsSleepParameters.Base,BmsSleepParameters.Build(
                _sleepLowMv.Text,_sleepLowMinutes.Text,_sleepNormalMv.Text,_sleepNormalMinutes.Text),false,ct)));
        saveButton(_sleepDefaults,"恢复休眠默认设置并读回",()=>ParameterRunAsync((client,ct)=>
            client.RestoreSleepDefaultsAsync(ct)));
    }

    private void ShowSleepParameters(D008ParameterCapture capture,List<string> readback)
    {
        foreach(var box in new[]{_sleepLowMv,_sleepLowMinutes,_sleepNormalMv,_sleepNormalMinutes})box.Clear();
        if(!BmsSleepParameters.Available(capture)) {readback.Add("休眠：设备未声明支持配置");return;}
        if(!capture.Blocks.TryGetValue("Sleep",out var words)) {readback.Add("休眠：本次读取失败");return;}
        try {
            BmsSleepParameters.Validate(words);
            _sleepLowMv.Text=words[0].ToString();_sleepLowMinutes.Text=words[1].ToString();
            _sleepNormalMv.Text=words[2].ToString();_sleepNormalMinutes.Text=words[3].ToString();
            readback.Add($"休眠：一级休眠 <{words[0]} mV / {words[1]} 分钟，二级休眠 <{words[2]} mV / {words[3]} 分钟；过放未恢复固定 3 分钟");
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
            _sleepNormalMinutes,_sleepSave,_sleepDefaults})control.IsEnabled=ready;
    }
}
