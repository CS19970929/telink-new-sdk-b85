using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Media;

namespace BmsTool.Windows;

public partial class MainWindow
{
    private readonly AfeHardwareParameterModel _afeHardwareModel = new();
    private readonly bool _afeHardwareEditorLaunchEnabled = Environment.GetCommandLineArgs()
        .Any(arg => string.Equals(arg, "--enable-afe-hw-editor", StringComparison.OrdinalIgnoreCase));
    private TabItem? _afeHardwareTab;
    private DataGrid? _afeHardwareGrid;
    private TextBlock? _afeHardwareStatus;
    private TextBlock? _afeHardwareDeviceInfo;

    private void AddAfeHardwareTab()
    {
        if (_afeHardwareTab is not null) return;

        _afeHardwareTab = new TabItem
        {
            Header = "AFE硬件保护",
            Visibility = Visibility.Collapsed
        };

        var root = new Grid { Margin = new Thickness(12) };
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        root.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });

        var top = new StackPanel();
        top.Children.Add(new TextBlock
        {
            Text = "AFE硬件保护参数与普通“软件保护/BMS参数”完全独立。该页面只用于内部/授权调试。所有修改按完整35-word事务一次写入，固件完成校验、持久化、AFE应用、有效值回读和失败回滚。不要把硬件参数当作软件三级参数的镜像。",
            TextWrapping = TextWrapping.Wrap,
            Foreground = new SolidColorBrush(Color.FromRgb(150, 60, 0)),
            FontWeight = FontWeights.SemiBold
        });

        _afeHardwareDeviceInfo = new TextBlock
        {
            Text = "设备：未读取",
            Margin = new Thickness(0, 7, 0, 0),
            TextWrapping = TextWrapping.Wrap,
            Foreground = Brushes.DimGray
        };
        top.Children.Add(_afeHardwareDeviceInfo);

        var buttons = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 10, 0, 8) };
        var readButton = new Button { Content = "读取AFE硬件参数", Width = 138, Height = 30 };
        readButton.Click += ReadAfeHardware_Click;
        buttons.Children.Add(readButton);

        var applyButton = new Button { Content = "保存 / 原子应用", Width = 132, Height = 30, Margin = new Thickness(8, 0, 0, 0) };
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
        _afeHardwareGrid.Columns.Add(new DataGridTextColumn { Header = "类别", Binding = new Binding(nameof(AfeParameterRow.Group)), Width = 110, IsReadOnly = true });
        _afeHardwareGrid.Columns.Add(new DataGridTextColumn { Header = "AFE硬件参数", Binding = new Binding(nameof(AfeParameterRow.Name)), Width = 135, IsReadOnly = true });
        _afeHardwareGrid.Columns.Add(new DataGridTextColumn { Header = "启用", Binding = new Binding(nameof(AfeParameterRow.EnabledText)), Width = 64, IsReadOnly = true });
        _afeHardwareGrid.Columns.Add(new DataGridTextColumn { Header = "请求/保存值", Binding = new Binding(nameof(AfeParameterRow.RequestedValue)), Width = 100, IsReadOnly = true });
        _afeHardwareGrid.Columns.Add(new DataGridTextColumn { Header = "AFE有效值", Binding = new Binding(nameof(AfeParameterRow.EffectiveValue)), Width = 100, IsReadOnly = true });
        _afeHardwareGrid.Columns.Add(new DataGridTextColumn
        {
            Header = "修改值",
            Binding = new Binding(nameof(AfeParameterRow.EditValue)) { Mode = BindingMode.TwoWay, UpdateSourceTrigger = UpdateSourceTrigger.PropertyChanged },
            Width = 105
        });
        _afeHardwareGrid.Columns.Add(new DataGridTextColumn { Header = "单位", Binding = new Binding(nameof(AfeParameterRow.Unit)), Width = 60, IsReadOnly = true });
        _afeHardwareGrid.Columns.Add(new DataGridTextColumn { Header = "说明", Binding = new Binding(nameof(AfeParameterRow.Hint)), Width = new DataGridLength(1, DataGridLengthUnitType.Star), IsReadOnly = true });
        Grid.SetRow(_afeHardwareGrid, 1);
        root.Children.Add(_afeHardwareGrid);

        var bottom = new TextBlock
        {
            Text = "安全边界：页面只显示设备 capability 声明支持的语义参数；仅允许显式编辑 SCD 使能（0/1），其他使能位保持现状；启用前必须确认短路电流和延时。WDT/Body-Diode 保持固定配置。写入成功后必须同时满足 requested 回读一致、AFE effective 可读、apply_state=OK。",
            TextWrapping = TextWrapping.Wrap,
            Foreground = Brushes.DimGray
        };
        Grid.SetRow(bottom, 2);
        root.Children.Add(bottom);

        _afeHardwareTab.Content = root;
        MainTabs.Items.Insert(Math.Min(2, MainTabs.Items.Count), _afeHardwareTab);
        UpdateAfeHardwareTabVisibility();
    }

    private void UpdateAfeHardwareTabVisibility()
    {
        if (_afeHardwareTab is null) return;
        _afeHardwareTab.Visibility = _afeHardwareEditorLaunchEnabled && _protectedFeaturesUnlocked
            ? Visibility.Visible
            : Visibility.Collapsed;
    }

    private void EnsureAfeHardwareEditorAuthorized()
    {
        if (!_afeHardwareEditorLaunchEnabled)
            throw new InvalidOperationException("AFE硬件保护编辑器未启用。请使用 --enable-afe-hw-editor 启动内部/授权模式。");
        if (!_protectedFeaturesUnlocked)
            throw new InvalidOperationException("请先通过“高级功能”验证。普通客户模式禁止修改 AFE 硬件保护参数。");
    }

    private void ApplyAfeDeviceInfo(AfeHardwareDeviceInfo info)
    {
        if (_afeHardwareDeviceInfo is null) return;
        _afeHardwareDeviceInfo.Text =
            $"Backend: {info.BackendName} (0x{info.BackendModel:X4}) · Capability=0x{info.Capabilities:X4} · " +
            $"Cells={info.CellCount} · Rsense={info.ShuntMicroOhm}uΩ · AFE WDT={info.WatchdogSeconds}s · " +
            $"Interface=v{info.InterfaceVersion} · Apply={info.ApplyStateText} · Error={info.ErrorText}";
    }

    private async void ReadAfeHardware_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            EnsureAfeHardwareEditorAuthorized();
            _pollTimer.Stop();
            await WaitForCommunicationIdleAsync();
            BmsClient bms = _bms ?? throw new InvalidOperationException("请先连接BMS。");
            _afeHardwareStatus!.Text = "正在读取 requested / metadata / effective...";

            var client = new AfeHardwareClient(bms);
            AfeHardwareSnapshot snapshot = await client.ReadAllAsync();
            _afeHardwareModel.Load(snapshot);
            ApplyAfeDeviceInfo(snapshot.Info);
            _afeHardwareStatus.Text = $"读取完成 · {DateTime.Now:HH:mm:ss}";
            AppendLog($"AFE_HW_V2_READ_OK backend=0x{snapshot.Info.BackendModel:X4}; caps=0x{snapshot.Info.Capabilities:X4}; apply={snapshot.Info.ApplyState}; error={snapshot.Info.LastError}", "AFE");
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
            EnsureAfeHardwareEditorAuthorized();
            _afeHardwareGrid?.CommitEdit(DataGridEditingUnit.Cell, true);
            _afeHardwareGrid?.CommitEdit(DataGridEditingUnit.Row, true);

            if (!_afeHardwareModel.TryBuildCandidate(out ushort[] candidate, out string validationError))
                throw new InvalidOperationException(validationError);
            if (!_afeHardwareModel.HasChanges(candidate))
            {
                _afeHardwareStatus!.Text = "没有待写入的AFE硬件参数修改";
                return;
            }

            AfeHardwareDeviceInfo info = _afeHardwareModel.DeviceInfo ?? throw new InvalidOperationException("请先读取AFE参数。");
            string transportText = _connectionMode == ConnectionMode.Serial ? "直连串口" : "BLE透明通道";
            if (MessageBox.Show(
                    $"确认修改 {info.BackendName} AFE硬件保护参数？\n\n" +
                    $"通信：{transportText}\n" +
                    "这与软件 First/Second/Third 参数完全独立。\n\n" +
                    "将执行：打开60秒授权会话 → 完整35-word原子写入 → 固件校验/持久化 → AFE应用 → requested/effective回读 → 失败自动回滚。\n\n" +
                    "BLE MTU=23 可使用新固件的暂存/提交事务，无需修改 MTU。请确认 SCD 使能、电流和延时。是否继续？",
                    "确认修改AFE硬件保护参数",
                    MessageBoxButton.YesNo,
                    MessageBoxImage.Warning) != MessageBoxResult.Yes)
                return;

            _pollTimer.Stop();
            await WaitForCommunicationIdleAsync();
            BmsClient bms = _bms ?? throw new InvalidOperationException("请先连接BMS。");
            var client = new AfeHardwareClient(bms);
            _afeHardwareStatus!.Text = "正在执行AFE硬件参数原子事务...";
            AppendLog($"AFE_HW_V2_WRITE_BEGIN backend=0x{info.BackendModel:X4}; transport={transportText}; words={candidate.Length}", "AFE");

            AfeHardwareSnapshot readback = await client.WriteAllAsync(candidate);
            _afeHardwareModel.Load(readback);
            ApplyAfeDeviceInfo(readback.Info);
            _afeHardwareStatus.Text = $"保存成功 · requested一致 · effective已回读 · {DateTime.Now:HH:mm:ss}";
            AppendLog($"AFE_HW_V2_WRITE_VERIFY_OK backend=0x{readback.Info.BackendModel:X4}; apply={readback.Info.ApplyState}; error={readback.Info.LastError}", "AFE");
        }
        catch (Exception ex)
        {
            if (_afeHardwareStatus is not null) _afeHardwareStatus.Text = "保存失败 / 请重新读取状态";
            ShowError("保存AFE硬件参数失败", ex,
                "以下为本次失败原因；此提示不代表设备已进入 CONFIG_INCONSISTENT。若提交结果不确定，请重新读取 requested/effective 和 apply_state/last_error，不要直接重复写入。");
        }
        finally
        {
            StartAutomaticRefresh();
        }
    }
}
