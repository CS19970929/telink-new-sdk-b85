using System.Windows;
using System.Windows.Controls;

namespace BmsTool.Windows;

public partial class MainWindow
{
    private const string ProtectedFeaturesPassword = "hs456";
    private bool _protectedFeaturesUnlocked;

    private void OpenProtectedFeatures_Click(object sender, RoutedEventArgs e)
    {
        if (_protectedFeaturesUnlocked)
        {
            LockProtectedFeatures();
            return;
        }

        var passwordBox = new PasswordBox
        {
            Width = 230,
            Height = 30,
            FontSize = 15,
            Margin = new Thickness(0, 10, 0, 14)
        };

        var dialog = new Window
        {
            Title = "高级功能验证",
            Width = 360,
            Height = 190,
            ResizeMode = ResizeMode.NoResize,
            WindowStartupLocation = WindowStartupLocation.CenterOwner,
            Owner = this,
            ShowInTaskbar = false
        };

        var okButton = new Button { Content = "确认", Width = 84, Height = 30, IsDefault = true };
        var cancelButton = new Button { Content = "取消", Width = 84, Height = 30, Margin = new Thickness(10, 0, 0, 0), IsCancel = true };
        okButton.Click += (_, _) => dialog.DialogResult = true;

        var buttons = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            HorizontalAlignment = HorizontalAlignment.Right
        };
        buttons.Children.Add(okButton);
        buttons.Children.Add(cancelButton);

        var panel = new StackPanel { Margin = new Thickness(20) };
        panel.Children.Add(new TextBlock
        {
            Text = "请输入高级功能密码：",
            FontSize = 15,
            FontWeight = FontWeights.SemiBold
        });
        panel.Children.Add(passwordBox);
        panel.Children.Add(buttons);
        dialog.Content = panel;

        dialog.Loaded += (_, _) => passwordBox.Focus();
        if (dialog.ShowDialog() != true) return;

        if (!string.Equals(passwordBox.Password, ProtectedFeaturesPassword, StringComparison.Ordinal))
        {
            AppendLog("客户版高级功能密码验证失败。", "ACCESS");
            MessageBox.Show("密码错误。", "验证失败", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        _protectedFeaturesUnlocked = true;
        ProtectionTab.Visibility = Visibility.Visible;
        OtaTab.Visibility = Visibility.Visible;
        ProtectedFeaturesButton.Content = "锁定高级功能";
        AppendLog("客户版高级功能已解锁：软件保护/BMS 参数、OTA。", "ACCESS");
        MainTabs.SelectedItem = ProtectionTab;
    }

    private void LockProtectedFeatures()
    {
        if (_otaRunning)
        {
            MessageBox.Show("OTA 正在进行，完成后才能锁定高级功能。", "无法锁定", MessageBoxButton.OK, MessageBoxImage.Information);
            return;
        }

        _protectedFeaturesUnlocked = false;
        ProtectionTab.Visibility = Visibility.Collapsed;
        OtaTab.Visibility = Visibility.Collapsed;
        ProtectedFeaturesButton.Content = "高级功能";
        if (MainTabs.SelectedItem == ProtectionTab || MainTabs.SelectedItem == OtaTab)
            MainTabs.SelectedIndex = 0;
        AppendLog("客户版高级功能已锁定。", "ACCESS");
    }
}
