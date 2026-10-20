using System.Windows;
using System.Windows.Controls;

namespace BmsTool.Windows;

public partial class MainWindow
{
    private readonly TextBlock _batteryTypeStatus = new()
    {
        Text = "电池类型：请先读取设备", TextWrapping = TextWrapping.Wrap, Margin = new Thickness(4)
    };

    private void AddBatteryTypeControls(StackPanel root)
    {
        root.Children.Add(new TextBlock { Text = "整套电池类型切换", FontWeight = FontWeights.Bold,
            Margin = new Thickness(4,12,4,4), ToolTip = "切换前断开充电器和负载；操作会恢复对应类型的保护、均衡和 SOC/OCV 默认。" });
        root.Children.Add(_batteryTypeStatus);
        var read = new Button { Content = "读取当前电池类型", Margin = new Thickness(4) };
        read.Click += async (_,_) => await ParameterRunAsync(async (client,ct) => {
            var state = await client.ReadBatteryTypeAsync(ct);
            _batteryTypeStatus.Text = state[2] == 1 ? "当前类型：磷酸铁锂" : state[2] == 2 ? "当前类型：三元锂" : "当前类型：自动选择，请核对实际电芯";
        }, operation: null, requiresUnlock: false);
        root.Children.Add(read);
        foreach (ushort chemistry in new ushort[] { 1, 2 })
        {
            var button = new Button { Content = chemistry == 1 ? "一键切换磷酸铁锂" : "一键切换三元锂", Margin = new Thickness(4) };
            button.Click += async (_,_) => await SwitchBatteryTypeFromUiAsync(chemistry);
            root.Children.Add(button);
        }
    }

    private async Task SwitchBatteryTypeFromUiAsync(ushort chemistry)
    {
        var expectedClient = _bms;
        if (expectedClient is null) { _batteryTypeStatus.Text = "请先连接设备"; return; }
#if !BMS_FACTORY_APP
        if (!_protectedFeaturesUnlocked) { _batteryTypeStatus.Text = "请先通过密码解锁高级功能"; return; }
#endif
        var input = new PasswordBox { Margin = new Thickness(16), MinWidth = 220 };
        var panel = new StackPanel();
        panel.Children.Add(new TextBlock { Text = "本次切换需要再次输入服务密码", Margin = new Thickness(16,12,16,0) });
        panel.Children.Add(input);
        var dialog = new Window { Title = "确认本次电池类型切换", Content = panel, Owner = this,
            SizeToContent = SizeToContent.WidthAndHeight, WindowStartupLocation = WindowStartupLocation.CenterOwner,
            ResizeMode = ResizeMode.NoResize, ShowInTaskbar = false };
        var confirm = new Button { Content = "确认", IsDefault = true, Margin = new Thickness(16,0,16,16) };
        confirm.Click += (_,_) => dialog.DialogResult = true;
        panel.Children.Add(confirm);
        dialog.Loaded += (_,_) => input.Focus();
        panel.Children.Add(new Button { Content = "取消", IsCancel = true, Margin = new Thickness(16,0,16,16) });
        if (dialog.ShowDialog() != true) { input.Clear(); return; }
        bool authorized = BmsServiceAccess.PasswordValid(input.Password);
        input.Clear();
        if (!authorized) { _batteryTypeStatus.Text = "服务密码错误"; MessageBox.Show(this,"服务密码错误，本次切换未执行。","验证失败",MessageBoxButton.OK,MessageBoxImage.Warning); return; }
        string name = chemistry == 1 ? "磷酸铁锂" : "三元锂";
        if (MessageBox.Show(this, $"将当前设备整套参数切换为{name}。请确认实际电芯类型匹配，且已断开充电器和负载。是否继续？",
            "切换电池类型", MessageBoxButton.YesNo, MessageBoxImage.Question) != MessageBoxResult.Yes) return;
        await ParameterRunAsync(async (client,ct) => {
            if (!ReferenceEquals(expectedClient,client)) throw new InvalidOperationException("连接已改变，请重新确认设备。");
            _batteryTypeStatus.Text = "正在切换，尚未确认设备结果…";
            try {
                var result = await client.SwitchBatteryTypeAsync(chemistry,BmsServiceAccess.DefaultPassword,ct);
                ProtectionGrid.CancelEdit(DataGridEditingUnit.Cell);
                ProtectionGrid.CancelEdit(DataGridEditingUnit.Row);
                _afeHardwareGrid?.CancelEdit(DataGridEditingUnit.Cell);
                _afeHardwareGrid?.CancelEdit(DataGridEditingUnit.Row);
                if (result.SoftwareProtection.Length != _protectionRows.Count)
                    throw new InvalidOperationException("软件保护读回数量不匹配，请重新读取。");
                for (int i=0;i<_protectionRows.Count;++i)
                    _protectionRows[i].LoadFromDevice(result.SoftwareProtection[i]);
                _afeHardwareModel.Load(result.HardwareProtection);
                ApplyAfeDeviceInfo(result.HardwareProtection.Info);
                ProtectionStatusText.Text = "电池类型切换后已重新读取";
                if (_afeHardwareStatus is not null) _afeHardwareStatus.Text = "电池类型切换后已重新读取";
                _socProfile = result.SocConfiguration;
                ShowSocProfile(result.SocConfiguration);
                _batteryTypeStatus.Text = result.Summary;
            } catch {
                foreach (var row in _protectionRows) row.EditValue = string.Empty;
                foreach (var row in _afeHardwareModel.Rows) row.EditValue = string.Empty;
                ProtectionStatusText.Text = "切换结果未确认，请重新读取保护参数";
                if (_afeHardwareStatus is not null) _afeHardwareStatus.Text = "切换结果未确认，请重新读取硬件保护";
                _batteryTypeStatus.Text = "本次结果未确认；请重新读取电池类型与保护参数。";
                throw;
            }
        }, operation: "切换电池类型");
    }
}
