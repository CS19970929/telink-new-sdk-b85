using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Media;

namespace BmsTool.Windows;

public partial class MainWindow
{
    private bool _hideD015SocProtection;
    private ListBox? _protectionNavigation;

    // 筛选只影响显示；所有行继续由原模型持有，整组校验和写入不依赖可见行。
    private static Grid CreateParameterBrowser(DataGrid table, Func<object, string> group,
        IEnumerable<string> categories, Func<object, bool>? include = null)
    {
        var root = new Grid();
        root.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(165) });
        root.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        var navigation = new ListBox { Margin = new Thickness(0, 0, 12, 8), FontSize = 14,
            Background = Brushes.White, BorderBrush = Brushes.LightGray };
        navigation.Items.Add("全部参数");
        foreach (string category in categories.Distinct()) navigation.Items.Add(category);
        var view = CollectionViewSource.GetDefaultView(table.ItemsSource);
        navigation.SelectionChanged += (_, _) => {
            // 先提交当前输入，再切换视图，避免编辑中的行被筛掉。
            if (!table.CommitEdit(DataGridEditingUnit.Cell, true) || !table.CommitEdit(DataGridEditingUnit.Row, true)) return;
            string? selected = navigation.SelectedItem as string;
            table.Columns[0].Visibility = selected == "全部参数" ? Visibility.Visible : Visibility.Collapsed;
            view.Filter = item => (include?.Invoke(item) ?? true) &&
                (selected is null || selected == "全部参数" || group(item) == selected);
        };
        navigation.SelectedIndex = navigation.Items.Count > 1 ? 1 : 0;
        root.Children.Add(navigation);
        table.RowHeight = 42;
        table.ColumnHeaderHeight = 38;
        table.FontSize = 14;
        table.GridLinesVisibility = DataGridGridLinesVisibility.Horizontal;
        table.HorizontalGridLinesBrush = Brushes.Gainsboro;
        table.AlternatingRowBackground = new SolidColorBrush(Color.FromRgb(247, 249, 252));
        table.BorderBrush = Brushes.LightGray;
        table.HeadersVisibility = DataGridHeadersVisibility.Column;
        foreach (var column in table.Columns) {
            if (column.IsReadOnly) continue;
            var style = new Style(typeof(TextBlock));
            style.Setters.Add(new Setter(TextBlock.BackgroundProperty, new SolidColorBrush(Color.FromRgb(235, 244, 255))));
            style.Setters.Add(new Setter(TextBlock.PaddingProperty, new Thickness(8, 4, 8, 4)));
            if (column is DataGridTextColumn text) text.ElementStyle = style;
        }
        table.Columns[0].Visibility = Visibility.Collapsed;
        var content = new DockPanel();
        var detail = new TextBlock { TextWrapping = TextWrapping.Wrap, Margin = new Thickness(8),
            Foreground = Brushes.DimGray, Text = "选择参数查看说明。蓝色栏为待保存值。" };
        table.SelectionChanged += (_, _) => detail.Text = table.SelectedItem switch {
            AfeParameterRow row => row.Group + " · " + row.Name + "：" + row.Hint,
            ProtectionParameterRow row => row.CustomerName + " · " + row.Hint,
            C060ParameterRow row => row.AddressText + " · " + row.Name + "：" + row.Hint,
            _ => "选择参数查看说明。蓝色栏为待保存值。"
        };
        // 长说明放在下方，避免把编辑数值挤出可视区域。
        if (table.Columns.Count > 5) table.Columns[table.Columns.Count - 1].Visibility = Visibility.Collapsed;
        DockPanel.SetDock(detail, Dock.Bottom); content.Children.Add(detail); content.Children.Add(table);
        Grid.SetColumn(content, 1); root.Children.Add(content);
        return root;
    }

    private void InitializeProtectionLayout()
    {
        var parent = (Grid)ProtectionGrid.Parent;
        parent.Children.Remove(ProtectionGrid);
        var browser = CreateParameterBrowser(ProtectionGrid, item => ((ProtectionParameterRow)item).Group,
            _protectionRows.Select(row => row.Group),
            item => !_hideD015SocProtection || ((ProtectionParameterRow)item).Address < 0x213C);
        _protectionNavigation = browser.Children.OfType<ListBox>().Single();
        Grid.SetRow(browser, 1); parent.Children.Add(browser);
        var header = parent.Children.OfType<StackPanel>().Single();
        var buttons = header.Children.OfType<StackPanel>().Single(p => p.Orientation == Orientation.Horizontal);
        var shortCircuit = new Button { Content = "硬件短路保护", MinWidth = 125, Height = 30,
            Margin = new Thickness(8, 0, 0, 0), ToolTip = "设置 AFE 短路电流和延时；保持放电过流2及其它参数。" };
        shortCircuit.Click += OpenShortCircuitSettings_Click;
        buttons.Children.Insert(3, shortCircuit);
    }

    private void UpdateProtectionProductDisplay(bool d015)
    {
        if (_protectionNavigation is null || _hideD015SocProtection == d015) return;
        _hideD015SocProtection = d015;
        string group = _protectionRows.First(row => row.Address == 0x213C).Group;
        if (d015) {
            if (_protectionNavigation.SelectedItem as string == group)
                _protectionNavigation.SelectedIndex = 0;
            _protectionNavigation.Items.Remove(group);
        } else if (!_protectionNavigation.Items.Contains(group)) {
            _protectionNavigation.Items.Add(group);
        }
        CollectionViewSource.GetDefaultView(ProtectionGrid.ItemsSource).Refresh();
    }

    private void ShowParameterSaved(string message)
    {
        MessageBox.Show(this, message, "保存成功", MessageBoxButton.OK, MessageBoxImage.Information);
    }

    private async void ReadDeviceSerial_Click(object sender, RoutedEventArgs e)
    {
        await ParameterRunAsync(null);
        DeviceSerialStatusText.Text = _parameterStatus.Text;
    }

    private async void WriteDeviceSerial_Click(object sender, RoutedEventArgs e)
    {
        // SN 身份写入复用原授权、分片暂存和读回事务，不复用保护参数的界面解锁门禁。
        string serial = DeviceSerialBox.Text;
        try { if(_bms?.UsesC060Protocol!=true)D008Parameters.SerialWords(serial); }
        catch (Exception ex) { ShowError("SN 格式无效", ex); return; }
        if (MessageBox.Show(this, $"确认将设备 SN 修改为：{serial}？", "修改设备 SN",
                MessageBoxButton.YesNo, MessageBoxImage.Question) != MessageBoxResult.Yes) return;
        await ParameterRunAsync(async (client, ct) => {
            if(client.UsesC060Protocol)await client.WriteC060IdentityAsync(0xFFF0,serial,ct);
            else await client.WriteD008SerialAsync(serial, ct);
            if (!ReferenceEquals(client, _bms)) throw new InvalidOperationException("连接已改变，请重新读取 SN。");
            await RefreshIdentityAsync(ct);
        }, operation: "保存 SN", requiresUnlock: false);
    }
}
