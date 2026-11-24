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
    private Sh309ChipProtection? _shownChip;
    private bool _changingSelection, _busy, _editing;
    private readonly List<ParameterRow> _rows = new();
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
        _chip.SelectionChanged += (_, _) =>
        {
            if (_changingSelection) return;
            if (_shownChip is { } shown && HasDraft)
            {
                _changingSelection = true;
                _chip.SelectedItem = shown;
                _changingSelection = false;
                Status.Text = "当前片还有未保存草稿，请先保存或重新读取。";
                return;
            }
            ShowSelected();
        };
        Clear();
    }

    private static DataGrid NewGrid() => new()
    {
        AutoGenerateColumns = false, IsReadOnly = true, CanUserAddRows = false, CanUserDeleteRows = false
    };

    private static void Column(DataGrid grid, string header, string property, int width) =>
        grid.Columns.Add(new DataGridTextColumn { Header = header, Binding = new Binding(property), Width = width, IsReadOnly = true });

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
        _changingSelection = true;
        _chip.ItemsSource = snapshot.Chips;
        _chip.SelectedIndex = Math.Min(selected, snapshot.Chips.Count - 1);
        _changingSelection = false;
        Status.Text = $"{snapshot.Product} · SH367309 ×{snapshot.Chips.Count}";
        ShowSelected();
    }

    public void Clear()
    {
        _snapshot = null;
        _changingSelection = true;
        _chip.ItemsSource = null;
        _changingSelection = false;
        Status.Text = "请读取当前设备的硬保护配置。";
        ShowSelected();
    }

    public void EnableEditing()
    {
        if (_editing) return;
        _editing = true;
        _parameters.Columns.Insert(2, new DataGridTextColumn
        {
            Header = "待保存", Width = 105,
            Binding = new Binding(nameof(ParameterRow.Draft)) { UpdateSourceTrigger = UpdateSourceTrigger.PropertyChanged }
        });
        UpdateEditing();
    }

    public void SetBusy(bool busy)
    {
        if (busy) CommitDraft();
        _busy = busy;
        Actions.IsEnabled = !busy;
        UpdateEditing();
    }

    private void UpdateEditing() => _parameters.IsReadOnly = _busy || !_editing || _shownChip?.Tr is null;

    private void CommitDraft()
    {
        _parameters.CommitEdit(DataGridEditingUnit.Cell, true);
        _parameters.CommitEdit(DataGridEditingUnit.Row, true);
    }

    public bool HasDraft => _rows.Any(row => row.Draft != row.Saved);
    public Sh309ProtectionSnapshot RequireSnapshot() => _snapshot ?? throw new InvalidOperationException("请先读取当前设备。");
    public Sh309ChipProtection RequireChip() => _shownChip ?? throw new InvalidOperationException("请先选择 AFE。");

    public ushort[] ReadDraft()
    {
        CommitDraft();
        var snapshot = RequireSnapshot();
        var chip = RequireChip();
        var words = (ushort[])chip.Saved.Clone();
        foreach (var row in _rows)
        {
            if (row.Bit < 0)
            {
                words[row.Index] = Sh309Protection.Encode(snapshot.Product, C077Hardware.Fields[row.Index], row.Draft);
                continue;
            }
            if (row.Draft is not ("0" or "1")) throw new ArgumentException($"{row.Name}只接受 0 或 1。");
            int mask = 1 << row.Bit;
            words[0] = (ushort)((words[0] & ~mask) | (row.Draft == "1" ? mask : 0));
        }
        ValidateDraft(snapshot.Product, chip, words);
        return words;
    }

    public void SetDraft(ushort[] values)
    {
        var snapshot = RequireSnapshot();
        var chip = RequireChip();
        ValidateDraft(snapshot.Product, chip, values);
        foreach (var row in _rows)
            row.Draft = RowValue(snapshot.Product, row.Index, row.Bit, values);
        _parameters.Items.Refresh();
    }

    private static void ValidateDraft(string product, Sh309ChipProtection chip, ushort[] words)
    {
        if (chip.Tr is not int tr) throw new InvalidOperationException("请先确认当前片 EEPROM 读回与 TR。");
        if (product == "C073") C073Protection.Validate(words, tr);
        else C077Hardware.Validate(words, tr);
    }

    private void ShowSelected()
    {
        _shownChip = null;
        _parameters.ItemsSource = null;
        _rows.Clear();
        if (_snapshot is not { } snapshot || _chip.SelectedItem is not Sh309ChipProtection selected)
        {
            _registers.ItemsSource = null;
            _readback.Text = string.Empty;
            UpdateEditing();
            return;
        }

        _shownChip = selected;
        _readback.Text = $"{selected.Name} · {Sh309Protection.ReadbackText(selected)}";
        int[]? actual = selected.Actual;
        string Value(C077HardwareField field, int value) => Sh309Protection.Format(snapshot.Product, field, value);
        for (int bit = 0; bit < C077Hardware.FeatureNames.Length; bit++)
        {
            var saved = RowValue(snapshot.Product, 0, bit, selected.Saved);
            _rows.Add(new(0, bit, C077Hardware.FeatureNames[bit], saved,
                actual is null ? "—" : ((actual[0] >> bit) & 1).ToString(), "0/1", "0 / 1"));
        }
        foreach (var f in C077Hardware.Fields.Skip(1))
        {
            string saved = RowValue(snapshot.Product, f.Index, -1, selected.Saved);
            _rows.Add(new(f.Index, -1, Sh309Protection.FieldName(snapshot.Product, f), saved,
                actual is null ? "—" : (f.Signed ? "≈" : "") + Value(f, actual[f.Index]),
                Sh309Protection.Unit(snapshot.Product, f), Sh309Protection.Limits(snapshot.Product, f)));
        }
        _parameters.ItemsSource = _rows;
        UpdateEditing();

        _registers.ItemsSource = RegisterNames.Select((name, index) =>
        {
            int? expected = selected.Expected is { } words ? (index % 2 == 0 ? words[index / 2] >> 8 : words[index / 2] & 255) : null;
            byte? readback = selected.Eeprom?[index];
            return new { Address = $"0x{index:X2}", Name = name, Expected = expected is int e ? $"0x{e:X2}" : "—",
                Actual = readback is byte b ? $"0x{b:X2}" : "—",
                Result = expected is null || readback is null ? "未确认" : expected == readback ? "一致" : "不同" };
        }).ToArray();
    }

    private static string RowValue(string product, int index, int bit, ushort[] values)
    {
        if (bit >= 0) return ((values[0] >> bit) & 1).ToString();
        var field = C077Hardware.Fields[index];
        return Sh309Protection.Format(product, field, field.Value(values[index]));
    }

    private sealed record ParameterRow(int Index, int Bit, string Name, string Saved, string Actual,
        string Unit, string Limits)
    {
        public string Draft { get; set; } = Saved;
    }
}
