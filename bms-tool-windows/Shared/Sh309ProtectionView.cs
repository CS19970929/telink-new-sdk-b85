using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;

namespace BmsTool.Windows;

internal sealed class Sh309ProtectionView : DockPanel
{
    private static readonly string[] RegisterNames =
    [
        "SCONF1", "SCONF2", "OVT/LDRT/OVH", "OVL", "UVT/OVRH", "UVRL", "UV", "UVR",
        "BALV", "PREV", "L0V", "PFV", "OCD1", "OCD2", "SC", "OCC", "CHS/MOST/OCRT/PFT",
        "OTC", "OTCR", "UTC", "UTCR", "OTD", "OTDR", "UTD", "UTDR"
    ];

    private readonly ComboBox _chip = new() { MinWidth = 100, DisplayMemberPath = nameof(Sh309ChipProtection.Name) };
    private readonly TextBlock _readback = new() { TextWrapping = TextWrapping.Wrap, Margin = new Thickness(4) };
    private readonly DataGrid _parameters = NewGrid();
    private readonly DataGrid _registers = NewGrid();
    private Sh309ProtectionSnapshot? _snapshot;
    public TextBlock Status { get; } = new() { TextWrapping = TextWrapping.Wrap, Margin = new Thickness(4) };
    public WrapPanel Actions { get; } = new();

    public Sh309ProtectionView()
    {
        var header = new StackPanel();
        SetDock(header, Dock.Top);
        header.Children.Add(Actions);
        Actions.Children.Add(_chip);
        header.Children.Add(Status);
        header.Children.Add(_readback);
        Children.Add(header);

        Column(_parameters, "参数", "Name", 255);
        Column(_parameters, "Flash 已保存", "Saved", 105);
        Column(_parameters, "本次芯片读回", "Actual", 115);
        Column(_parameters, "单位", "Unit", 75);
        Column(_parameters, "允许值", "Limits", 235);

        Column(_registers, "地址", "Address", 85);
        Column(_registers, "寄存器", "Name", 205);
        Column(_registers, "Flash 目标字节", "Expected", 120);
        Column(_registers, "本次芯片读回", "Actual", 120);
        Column(_registers, "结果", "Result", 120);

        var tabs = new TabControl();
        tabs.Items.Add(new TabItem { Header = "物理参数和功能位", Content = _parameters });
        tabs.Items.Add(new TabItem { Header = "25 字节配置核对", Content = _registers });
        Children.Add(tabs);
        _chip.SelectionChanged += (_, _) => ShowSelected();
        Clear();
    }

    private static DataGrid NewGrid() => new()
    {
        AutoGenerateColumns = false, IsReadOnly = true, CanUserAddRows = false, CanUserDeleteRows = false
    };

    private static void Column(DataGrid grid, string header, string property, int width) =>
        grid.Columns.Add(new DataGridTextColumn { Header = header, Binding = new Binding(property), Width = width });

    public void AddAction(string label, Func<Task> action)
    {
        var button = new Button { Content = label, Margin = new Thickness(4), Padding = new Thickness(8, 4, 8, 4) };
        button.Click += async (_, _) => await action();
        Actions.Children.Add(button);
    }

    public void Load(Sh309ProtectionSnapshot snapshot)
    {
        int selected = Math.Max(0, _chip.SelectedIndex);
        _snapshot = snapshot;
        _chip.ItemsSource = snapshot.Chips;
        _chip.SelectedIndex = Math.Min(selected, snapshot.Chips.Count - 1);
        Status.Text = $"{snapshot.Product} · SH367309 ×{snapshot.Chips.Count}";
        ShowSelected();
    }

    public void Clear()
    {
        _snapshot = null;
        _chip.ItemsSource = null;
        Status.Text = "请读取当前设备的硬保护配置。";
        ShowSelected();
    }

    private void ShowSelected()
    {
        if (_snapshot is not { } snapshot || _chip.SelectedItem is not Sh309ChipProtection selected)
        {
            _parameters.ItemsSource = _registers.ItemsSource = null;
            _readback.Text = string.Empty;
            return;
        }

        _readback.Text = $"{selected.Name} · {Sh309Protection.ReadbackText(selected)}";
        int[]? actual = selected.Actual;
        string Value(C077HardwareField field, int value) => Sh309Protection.Format(snapshot.Product, field, value);
        var rows = new List<object>();
        for (int bit = 0; bit < C077Hardware.FeatureNames.Length; bit++)
            rows.Add(new { Name = C077Hardware.FeatureNames[bit], Saved = ((selected.Saved[0] >> bit) & 1).ToString(),
                Actual = actual is null ? "—" : ((actual[0] >> bit) & 1).ToString(), Unit = "0/1", Limits = "0 / 1" });
        foreach (var f in C077Hardware.Fields.Skip(1))
            rows.Add(new { Name = Sh309Protection.FieldName(snapshot.Product, f), Saved = Value(f, f.Value(selected.Saved[f.Index])),
                Actual = actual is null ? "—" : (f.Signed ? "≈" : "") + Value(f, actual[f.Index]),
                Unit = Sh309Protection.Unit(snapshot.Product, f), Limits = Sh309Protection.Limits(snapshot.Product, f) });
        _parameters.ItemsSource = rows;

        _registers.ItemsSource = RegisterNames.Select((name, index) =>
        {
            int? expected = selected.Expected is { } words ? (index % 2 == 0 ? words[index / 2] >> 8 : words[index / 2] & 255) : null;
            byte? readback = selected.Eeprom?[index];
            return new { Address = $"0x{index:X2}", Name = name, Expected = expected is int e ? $"0x{e:X2}" : "—",
                Actual = readback is byte b ? $"0x{b:X2}" : "—",
                Result = expected is null || readback is null ? "未确认" : expected == readback ? "一致" : "不同" };
        }).ToArray();
    }
}
