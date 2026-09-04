using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Media;

namespace BmsTool.Windows;

public partial class MainWindow
{
    private void AddAfeHardwareTab()
    {
        if (MainTabs.Items.OfType<TabItem>().Any(t => string.Equals(t.Header?.ToString(), "AFE硬件保护", StringComparison.Ordinal)))
            return;

        var tab = new TabItem { Header = "AFE硬件保护" };
        var root = new Grid { Margin = new Thickness(12) };
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        root.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });

        var top = new StackPanel();
        top.Children.Add(new TextBlock
        {
            Text = "SH367309 AFE硬件保护参数。此页与“保护 / BMS 参数”的MCU软件保护参数完全独立。保存后固件会持久化，并异步更新AFE EEPROM；请勿高频、重复写入。",
            TextWrapping = TextWrapping.Wrap,
            Foreground = new SolidColorBrush(Color.FromRgb(150, 60, 0)),
            FontWeight = FontWeights.SemiBold
        });

        var buttons = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 10, 0, 8) };
        var readButton = new Button { Content = "读取AFE硬件参数", Width = 138, Height = 30 };
        readButton.Click += ReadAfeHardware_Click;
        buttons.Children.Add(readButton);

        var applyButton = new Button { Content = "保存 / 应用修改", Width = 132, Height = 30, Margin = new Thickness(8, 0, 0, 0) };
        applyButton.Click += ApplyAfeHardware_Click;
        buttons.Children.Add(applyButton);

        var resetButton = new Button { Content = "撤销本地修改", Width = 118, Height = 30, Margin = new Thickness(8, 0, 0, 0) };
        resetButton.Click += (_, _) =>
        {
            _afeHardwareModel.ResetEditsToCurrent();
            if (_afeHardwareStatus is not null) _afeHardwareStatus.Text = "已恢复为上次读取值";
        };
        buttons.Children.Add(resetButton);

        _afeHardwareStatus = new TextBlock { Text = "未读取", VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(14, 0, 0, 0) };
        buttons.Children.Add(_afeHardwareStatus);
        top.Children.Add(buttons);
        Grid.SetRow(top, 0);
        root.Children.Add(top);

        _afeHardwareGrid = new DataGrid
        {
            AutoGenerateColumns = false,
            CanUserAddRows = false,
            CanUserDeleteRows = false,
            SelectionMode = DataGridSelectionMode.Single,
            SelectionUnit = DataGridSelectionUnit.FullRow,
            ItemsSource = _afeHardwareModel.Rows,
            Margin = new Thickness(0, 0, 0, 8)
        };
        _afeHardwareGrid.Columns.Add(new DataGridTextColumn { Header = "类别", Binding = new Binding(nameof(AfeParameterRow.Group)), Width = 120, IsReadOnly = true });
        _afeHardwareGrid.Columns.Add(new DataGridTextColumn { Header = "AFE硬件参数", Binding = new Binding(nameof(AfeParameterRow.Name)), Width = 145, IsReadOnly = true });
        _afeHardwareGrid.Columns.Add(new DataGridTextColumn { Header = "当前值", Binding = new Binding(nameof(AfeParameterRow.CurrentValue)), Width = 100, IsReadOnly = true });
        _afeHardwareGrid.Columns.Add(new DataGridTextColumn
        {
            Header = "修改值",
            Binding = new Binding(nameof(AfeParameterRow.EditValue)) { Mode = BindingMode.TwoWay, UpdateSourceTrigger = UpdateSourceTrigger.PropertyChanged },
            Width = 110
        });
        _afeHardwareGrid.Columns.Add(new DataGridTextColumn { Header = "单位", Binding = new Binding(nameof(AfeParameterRow.Unit)), Width = 70, IsReadOnly = true });
        _afeHardwareGrid.Columns.Add(new DataGridTextColumn { Header = "合法范围 / 档位", Binding = new Binding(nameof(AfeParameterRow.Hint)), Width = new DataGridLength(1, DataGridLengthUnitType.Star), IsReadOnly = true });
        Grid.SetRow(_afeHardwareGrid, 1);
        root.Children.Add(_afeHardwareGrid);

        var bottom = new TextBlock
        {
            Text = "注意：充电过流的0x2406/0x2408以及延时0x2407/0x2409是同一SH367309硬件参数的协议别名；上位机仅显示一组并自动保持别名一致。当前电流离散档位按D3PRO硬件采样配置处理。",
            TextWrapping = TextWrapping.Wrap,
            Foreground = Brushes.DimGray
        };
        Grid.SetRow(bottom, 2);
        root.Children.Add(bottom);

        tab.Content = root;
        MainTabs.Items.Insert(Math.Min(2, MainTabs.Items.Count), tab);
    }

    private async void ReadAfeHardware_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            _pollTimer.Stop();
            await WaitForCommunicationIdleAsync();
            BmsClient bms = _bms ?? throw new InvalidOperationException("请先连接BMS。");
            BmsBleTransport transport = _bmsTransport ?? throw new InvalidOperationException("BLE通信未就绪。");
            _afeHardwareStatus!.Text = "正在读取...";

            var client = new AfeHardwareClient(bms, transport);
            ushort[] raw = await client.ReadAllAsync();
            _afeHardwareModel.Load(raw);
            _afeHardwareStatus.Text = $"读取完成 · {DateTime.Now:HH:mm:ss}";
            AppendLog("AFE_HW_READ_OK count=24", "AFE");
        }
        catch (Exception ex)
        {
            if (_afeHardwareStatus is not null) _afeHardwareStatus.Text = "读取失败";
            ShowError("读取AFE硬件参数失败", ex);
        }
        finally
        {
            StartAutomaticRefresh();
        }
    }

    private async void ApplyAfeHardware_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            _afeHardwareGrid?.CommitEdit(DataGridEditingUnit.Cell, true);
            _afeHardwareGrid?.CommitEdit(DataGridEditingUnit.Row, true);

            if (!_afeHardwareModel.TryBuildCandidate(out ushort[] candidate, out string validationError))
                throw new InvalidOperationException(validationError);

            IReadOnlyList<AfeWriteGroup> groups = _afeHardwareModel.GetChangedWriteGroups(candidate);
            if (groups.Count == 0)
            {
                _afeHardwareStatus!.Text = "没有待写入的AFE硬件参数修改";
                return;
            }

            string groupNames = string.Join("、", groups.Select(g => g.Name));
            if (MessageBox.Show(
                    $"确认修改SH367309 AFE硬件保护参数？\n\n将写入：{groupNames}\n\n上位机只写发生变化的原子参数组。固件ACK表示参数合法、已持久化并已排队更新AFE；完成后上位机会再次读取全部24项进行一致性校验。",
                    "确认修改AFE硬件保护参数",
                    MessageBoxButton.YesNo,
                    MessageBoxImage.Warning) != MessageBoxResult.Yes)
                return;

            _pollTimer.Stop();
            await WaitForCommunicationIdleAsync();
            BmsClient bms = _bms ?? throw new InvalidOperationException("请先连接BMS。");
            BmsBleTransport transport = _bmsTransport ?? throw new InvalidOperationException("BLE通信未就绪。");
            var client = new AfeHardwareClient(bms, transport);

            for (int i = 0; i < groups.Count; i++)
            {
                AfeWriteGroup group = groups[i];
                _afeHardwareStatus!.Text = $"正在写入 {i + 1}/{groups.Count}：{group.Name}";
                AppendLog($"AFE_HW_WRITE group='{group.Name}' start=0x{group.StartRegister:X4} qty={group.Values.Length}", "AFE");
                await client.WriteGroupAsync(group);
                await Task.Delay(120);
            }

            await Task.Delay(800);
            ushort[] readback = await client.ReadAllAsync();
            if (!readback.SequenceEqual(candidate))
            {
                int mismatch = Enumerable.Range(0, candidate.Length).First(i => candidate[i] != readback[i]);
                throw new IOException($"AFE参数回读不一致：索引 {mismatch}，目标 {candidate[mismatch]}，回读 {readback[mismatch]}。");
            }

            _afeHardwareModel.Load(readback);
            _afeHardwareStatus!.Text = $"保存成功并回读一致 · {DateTime.Now:HH:mm:ss} · AFE硬件同步由固件异步完成";
            AppendLog($"AFE_HW_WRITE_VERIFY_OK groups={groups.Count}", "AFE");
        }
        catch (Exception ex)
        {
            if (_afeHardwareStatus is not null) _afeHardwareStatus.Text = "保存失败";
            ShowError("保存AFE硬件参数失败", ex);
        }
        finally
        {
            StartAutomaticRefresh();
        }
    }
}
