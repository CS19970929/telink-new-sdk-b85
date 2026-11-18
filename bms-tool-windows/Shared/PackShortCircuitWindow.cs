using System.IO;
using System.Windows;
using System.Windows.Controls;

namespace BmsTool.Windows;

internal sealed class PackShortCircuitWindow : Window
{
    private readonly BmsClient _client;
    private readonly ComboBox _chip = new() { ItemsSource = new[] { "AFE1", "AFE2" }, SelectedIndex = 0, Width = 100 };
    private readonly ComboBox _current = new() { DisplayMemberPath = nameof(CurrentChoice.Text), Width = 150 };
    private readonly ComboBox _delay = new() { Width = 150 };
    private readonly TextBlock _active = new() { TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 8, 0, 12) };
    private readonly TextBlock _status = new() { Text = "未读取", TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 12, 0, 0) };
    private readonly Button _read = new() { Content = "读取", MinWidth = 90, Padding = new Thickness(8, 4, 8, 4) };
    private readonly Button _save = new() { Content = "保存并验证", MinWidth = 110, Padding = new Thickness(8, 4, 8, 4), Margin = new Thickness(8, 0, 0, 0) };

    private BqShortConfig? _bq;
    private C077HardwareSnapshot? _sh;
    private bool _busy;

    private sealed record BqShortConfig(ushort[] Group, ushort Resistance, ushort Parallel);
    private sealed record CurrentChoice(ushort Value, byte Range, string Text);

    public PackShortCircuitWindow(BmsClient client)
    {
        _client = client;
        Title = "硬件短路保护";
        Width = 465;
        MinWidth = 420;
        SizeToContent = SizeToContent.Height;
        ResizeMode = ResizeMode.NoResize;
        WindowStartupLocation = WindowStartupLocation.CenterOwner;

        var panel = new StackPanel { Margin = new Thickness(20) };
        panel.Children.Add(new TextBlock { Text = client.IsC077 ? "C077 · 双 SH367309" : "C060 · BQ76930 主片", FontWeight = FontWeights.Bold });
        if (client.IsC077) AddField("芯片", _chip);
        panel.Children.Add(_active);
        AddField("短路电流 / A", _current);
        AddField("短路延时 / µs", _delay);
        var buttons = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 10, 0, 0) };
        buttons.Children.Add(_read);
        buttons.Children.Add(_save);
        panel.Children.Add(buttons);
        panel.Children.Add(_status);
        Content = panel;

        _chip.SelectionChanged += (_, _) => ShowValues();
        _read.Click += async (_, _) => await ReadAsync();
        _save.Click += async (_, _) => await SaveAsync();
        Loaded += async (_, _) => await ReadAsync();
        Closing += (_, e) => e.Cancel = _busy;
        SetBusy(false);

        void AddField(string label, ComboBox control)
        {
            control.HorizontalAlignment = HorizontalAlignment.Left;
            var row = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 6, 0, 0) };
            row.Children.Add(new TextBlock { Text = label, Width = 112, VerticalAlignment = VerticalAlignment.Center });
            row.Children.Add(control);
            panel.Children.Add(row);
        }
    }

    private void SetBusy(bool busy)
    {
        _busy = busy;
        _chip.IsEnabled = _read.IsEnabled = !busy;
        bool ready = !busy && (_bq is not null || _sh is not null);
        _save.IsEnabled = _current.IsEnabled = _delay.IsEnabled = ready;
    }

    private async Task<BqShortConfig> ReadBqAsync()
    {
        var identity = UpgradeDetection.ParseCapabilities(await _client.ReadRegistersAsync(0xD170, 12));
        if (identity is not { Board: 0xC0600020, Mode: 0 }) throw new IOException("请先连接 C060 APP。");
        var board = await _client.ReadRegistersAsync(0x231C, 4);
        var current = await _client.ReadRegistersAsync(0x2308, 8);
        if (board[0] != 20 || !current.SequenceEqual(C060Parameters.NormalizeCurrentGroup(current, board[1], board[2])))
            throw new IOException("C060 短路参数或采样电阻读回无效。");
        return new(current, board[1], board[2]);
    }

    private ushort[] ReadBqChoices(BqShortConfig config)
    {
        return C060Parameters.ShortThresholdMillivolts.Select(mv =>
            checked((ushort)((uint)mv * config.Parallel * 10u / config.Resistance))).ToArray();
    }

    private static bool ContainsOnlyShortDifference(C077HardwareSnapshot snapshot, int chip)
    {
        for (int i = 0; i < 25; ++i)
        {
            if (i == 14) continue;
            ushort word = snapshot.Expected[chip][i / 2];
            byte expected = (byte)((i % 2 == 0) ? word >> 8 : word);
            if (expected != snapshot.Actual.Chips[chip].Eeprom[i]) return false;
        }
        return true;
    }

    private void ShowValues()
    {
        _active.Text = "";
        _current.ItemsSource = null;
        _delay.ItemsSource = null;
        if (_bq is { } bq)
        {
            var choices = ReadBqChoices(bq).Select((value, i) =>
                new CurrentChoice(value, (byte)(i >= 2 ? 1 : 0), $"{value / 10m:0.0}")).ToArray();
            // Choose only values that survive the board's quantization on reread.
            var stable = choices.Where((choice, i) => choice.Value != 0 && (i == 0 || choice.Value > choices[i - 1].Value)).ToArray();
            _current.ItemsSource = stable;
            _current.SelectedItem = stable.First(c => c.Value == bq.Group[3]);
            _delay.ItemsSource = C060Parameters.ShortDelayTenthsUs.Select(x => x / 10).ToArray();
            _delay.SelectedItem = bq.Group[2] / 10;
            _active.Text = $"当前配置  {bq.Group[3] / 10m:0.0} A / {bq.Group[2] / 10} µs";
        }
        else if (_sh is { } sh)
        {
            int chip = _chip.SelectedIndex;
            var saved = sh.Saved[chip];
            byte wire = sh.Actual.Chips[chip].Eeprom[14];
            int[] delays = Enumerable.Range(0, 16).Select(i => i * 64).ToArray();
            var choices = C077Parameters.ShortMillivolts.Select(mv => new CurrentChoice(mv, 0, $"{mv * 2}")).ToArray();
            _current.ItemsSource = choices;
            _current.SelectedItem = choices.FirstOrDefault(c => c.Value == saved[16]);
            _delay.ItemsSource = delays;
            _delay.SelectedItem = (int)saved[17];
            _active.Text = $"EEPROM  {C077Parameters.ShortMillivolts[wire >> 4] * 2} A / {(wire & 15) * 64} µs";
            if (!sh.Actual.Chips[chip].ProfileMatches) _active.Text += " · 与保存档案有差异";
        }
    }

    private async Task ReadCurrentAsync()
    {
        _bq = null;
        _sh = null;
        if (_client.IsC077)
        {
            var snapshot = await _client.ReadC077HardwareAsync(verifyEeprom: true);
            if (snapshot.Actual.ServiceState != 0 || snapshot.Actual.ServiceError is not (0 or 4))
                throw new IOException("未能读取两片 SH367309 的实际配置。");
            _sh = snapshot;
        }
        else _bq = await ReadBqAsync();
        ShowValues();
    }

    private async Task ReadAsync()
    {
        if (_busy) return;
        SetBusy(true);
        _status.Text = "读取中…";
        try
        {
            await ReadCurrentAsync();
            _status.Text = "已读取";
        }
        catch (Exception ex)
        {
            _bq = null;
            _sh = null;
            ShowValues();
            _status.Text = ex.Message;
        }
        finally { SetBusy(false); }
    }

    private async Task SaveAsync()
    {
        if (_busy || _current.SelectedItem is not CurrentChoice choice || _delay.SelectedItem is not int delay) return;
        string action = $"{choice.Text} A / {delay} µs";
        string message;
        int chip = _chip.SelectedIndex;
        if (_bq is { } bq)
        {
            if (choice.Value == bq.Group[3] && delay * 10 == bq.Group[2]) { _status.Text = "参数未变化"; return; }
            int oldRange = Array.IndexOf(ReadBqChoices(bq), bq.Group[3]) >= 2 ? 1 : 0;
            message = $"保存短路配置 {action}？";
            if (oldRange != choice.Range) message += "\n此档会同时切换 BQ76930 硬件过流量程。";
        }
        else if (_sh is { } sh)
        {
            if (!ContainsOnlyShortDifference(sh, chip)) { _status.Text = "该片存在其它档案差异，请通过维护工具核对。"; return; }
            if (sh.Saved[chip][16] == choice.Value && sh.Saved[chip][17] == delay && sh.Actual.Chips[chip].ProfileMatches)
            { _status.Text = "参数未变化"; return; }
            message = $"向 AFE{chip + 1} 写入 {action}？\n请先断开负载和充电器，并关闭充放电及均衡。";
        }
        else return;

        if (MessageBox.Show(this, message, "保存硬件短路配置", MessageBoxButton.YesNo, MessageBoxImage.Warning) != MessageBoxResult.Yes) return;
        SetBusy(true);
        _status.Text = "正在保存并验证…";
        bool saved309 = false;
        try
        {
            if (_bq is { } observed)
            {
                var live = await ReadBqAsync();
                if (live.Resistance != observed.Resistance || live.Parallel != observed.Parallel ||
                    !live.Group.AsSpan(2, 2).SequenceEqual(observed.Group.AsSpan(2, 2)))
                    throw new IOException("设备短路配置或采样电阻已变化，请重新读取。");
                await _client.WriteC060ParameterChangesAsync(new Dictionary<ushort, ushort> {
                    [0x230A] = (ushort)(delay * 10), [0x230B] = choice.Value
                }, (_, _) => { });
            }
            else if (_sh is { } snapshot)
            {
                var live = await _client.ReadC077HardwareAsync(verifyEeprom: true);
                if (live.Actual.ServiceState != 0 || live.Actual.ServiceError is not (0 or 4) ||
                    !ContainsOnlyShortDifference(live, chip))
                    throw new IOException("SH367309 实际配置未确认，或存在其它档案差异。");
                var values = (ushort[])snapshot.Saved[chip].Clone();
                values[16] = choice.Value;
                values[17] = (ushort)delay;
                await _client.SaveC077HardwareAsync(chip, values, snapshot.Saved[chip]);
                saved309 = true;

                // Confirm the saved profile still differs only in the SCD byte before authorizing EEPROM.
                var pending = await _client.ReadC077HardwareAsync(verifyEeprom: true);
                if (pending.Actual.ServiceState != 0 || pending.Actual.ServiceError is not (0 or 4) ||
                    !pending.Saved[chip].SequenceEqual(values) || !ContainsOnlyShortDifference(pending, chip))
                    throw new IOException("保存后配置已变化或读回异常。");
                if (!pending.Actual.Chips[chip].ProfileMatches)
                {
                    var applied = await _client.ProgramC077HardwareAsync(1 << chip);
                    byte actual = applied.Chips[chip].Eeprom[14];
                    if (C077Parameters.ShortMillivolts[actual >> 4] != choice.Value || (actual & 15) * 64 != delay)
                        throw new IOException("EEPROM 短路档位读回不一致。");
                }
            }
            await ReadCurrentAsync();
            if (_bq is { } verifiedBq && (verifiedBq.Group[2] != delay * 10 || verifiedBq.Group[3] != choice.Value) ||
                _sh is { } verifiedSh && (!verifiedSh.Actual.Chips[chip].ProfileMatches ||
                    verifiedSh.Saved[chip][16] != choice.Value || verifiedSh.Saved[chip][17] != delay))
                throw new IOException("最终读回与所选档位不一致。");
            _status.Text = "已保存并验证";
        }
        catch (Exception ex)
        {
            _bq = null;
            _sh = null;
            ShowValues();
            _status.Text = (saved309 ? "Flash 已保存，EEPROM 应用待确认。 " : "") + ex.Message;
        }
        finally { SetBusy(false); }
    }
}
