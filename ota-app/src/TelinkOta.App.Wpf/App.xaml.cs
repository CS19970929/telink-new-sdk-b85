using System.Windows;

namespace TelinkOta.App.Wpf;

public partial class App : Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        DispatcherUnhandledException += (_, args) =>
        {
            MessageBox.Show($"未处理异常：{args.Exception.Message}\n\n{args.Exception.StackTrace}",
                "Telink OTA", MessageBoxButton.OK, MessageBoxImage.Error);
            args.Handled = true;
        };
    }
}
