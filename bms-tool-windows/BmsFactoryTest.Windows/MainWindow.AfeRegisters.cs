using System.Windows;
using System.Windows.Controls;

namespace BmsTool.Windows;

public partial class MainWindow
{
    private void AddAfeRegisterTab()
    {
        var panel=new StackPanel { Margin=new Thickness(18) };
        panel.Children.Add(new TextBlock { Text="DVC1124-2 / SH3673510 / SH3673520 原厂寄存器、位定义与临时调试。固件须显式启用开发寄存器接口；生产构建关闭。原 AFE 硬件保护页继续负责持久保护参数。",TextWrapping=TextWrapping.Wrap });
        var button=new Button { Content="打开 AFE 全寄存器调试",HorizontalAlignment=HorizontalAlignment.Left,Margin=new Thickness(0,15,0,0),Padding=new Thickness(15,8,15,8) };
        button.Click+=async (_,_)=>{
            if(_currentCalibrationOwner is not null)return;
            button.IsEnabled=false;_pollTimer.Stop();_advancedFeatureTimer?.Stop();
            try{await WaitForCommunicationIdleAsync();if(_currentCalibrationOwner is not null)return;
                new AfeRegisterWindow(_bms){Owner=this}.ShowDialog();}
            catch(Exception ex){ShowError("AFE 寄存器调试",ex);}
            finally{button.IsEnabled=true;_advancedFeatureTimer?.Start();StartAutomaticRefresh();}
        };
        panel.Children.Add(button);MainTabs.Items.Add(new TabItem { Header="AFE 全寄存器",Content=panel });
    }
}
