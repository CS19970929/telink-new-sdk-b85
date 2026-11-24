using System.IO;
using System.Text.Json;
using System.Windows;
using Microsoft.Win32;

namespace BmsTool.Windows;

public sealed class C077AfeEditorWindow : Window
{
    private readonly Sh309ProtectionView _view = new();
    private readonly BmsClient _client;
    private readonly CancellationToken _connection;
    private bool _busy;

    public C077AfeEditorWindow(BmsClient client, CancellationToken connection = default)
    {
        _client = client;
        _connection = connection;
        Title = "SH367309 硬件保护";
        Width = 1050;
        Height = 760;
        MinWidth = 800;
        MinHeight = 550;
        WindowStartupLocation = WindowStartupLocation.CenterOwner;
        Content = _view;
        _view.EnableEditing();
        _view.AddAction("读取 Flash 和 EEPROM", () => RunAsync(ReloadAsync));
        _view.AddAction("关闭输出 / 均衡", () => RunAsync(CloseOutputsAsync));
        _view.AddAction("保存所选片到 Flash", () => RunAsync(SaveAsync));
        _view.AddAction("下发所选片 EEPROM", () => RunAsync(token => ProgramAsync(false, token)));
        _view.AddAction("下发双片 EEPROM", () => RunAsync(token => ProgramAsync(true, token)));
        _view.AddAction("导出硬保护 JSON", () => RunAsync(ExportAsync));
        _view.AddAction("导入到所选片草稿", () => RunAsync(ImportAsync));
        Loaded += async (_, _) => await RunAsync(ReloadAsync);
        Closing += (_, e) => e.Cancel = _busy;
        var disconnect = connection.Register(() => Dispatcher.BeginInvoke(new Action(() =>
        {
            _view.Clear();
            _view.SetBusy(true);
            _view.Status.Text = "连接已改变，请关闭窗口后重新打开。";
        })));
        Closed += (_, _) => disconnect.Dispose();
    }

    private async Task RunAsync(Func<CancellationToken,Task> operation)
    {
        if (_busy) return;
        _busy = true;
        _view.SetBusy(true);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(_connection);
        timeout.CancelAfter(TimeSpan.FromSeconds(60));
        try
        {
            timeout.Token.ThrowIfCancellationRequested();
            await operation(timeout.Token);
        }
        catch (ArgumentException ex) { _view.Status.Text = ex.Message; }
        catch (Exception ex)
        {
            _view.Clear();
            _view.Status.Text = "操作未完成，请重新读取当前设备。 " + ex.Message;
        }
        finally
        {
            _busy = false;
            _view.SetBusy(_connection.IsCancellationRequested);
        }
    }

    private async Task ReloadAsync(CancellationToken token)
    {
        _view.Status.Text = "正在读取…";
        var snapshot = await Sh309Protection.ReadAsync(_client, token);
        token.ThrowIfCancellationRequested();
        _view.Load(snapshot);
    }

    private async Task SaveAsync(CancellationToken token)
    {
        var chip = _view.RequireChip();
        var candidate = _view.ReadDraft();
        if (candidate.SequenceEqual(chip.Saved))
        {
            _view.Status.Text = $"{chip.Name} 没有待保存的修改。";
            return;
        }
        if (MessageBox.Show(this, $"将 {chip.Name} 草稿保存到 MCU Flash？EEPROM 由单独的下发操作更新。",
                "保存 Flash", MessageBoxButton.YesNo) != MessageBoxResult.Yes) return;
        token.ThrowIfCancellationRequested();
        await _client.SaveC077HardwareAsync(chip.Index, candidate, chip.Saved, token);
        await ReloadAsync(token);
        _view.Status.Text = $"{chip.Name} Flash 已保存并读回。需要应用时请下发 EEPROM。";
    }

    private async Task ProgramAsync(bool all, CancellationToken token)
    {
        if (_view.HasDraft)
        {
            _view.Status.Text = "当前片有未保存草稿，请先保存 Flash 或重新读取。";
            return;
        }
        var snapshot = _view.RequireSnapshot();
        var chip = _view.RequireChip();
        string target = all ? "双片" : chip.Name;
        if (MessageBox.Show(this, $"按当前 Flash 档案下发 {target} EEPROM？请确认输出和均衡已关闭。中断可能导致部分完成。",
                "确认下发 EEPROM", MessageBoxButton.YesNo, MessageBoxImage.Warning) != MessageBoxResult.Yes) return;
        token.ThrowIfCancellationRequested();
        await _client.ProgramC077HardwareAsync(all ? 3 : 1 << chip.Index,
            observed: snapshot.Chips.Select(c => c.Saved).ToArray(), ct: token);
        await ReloadAsync(token);
        _view.Status.Text = $"{target} EEPROM 下发事务已确认。可切换实例查看各片读回。";
    }

    private async Task CloseOutputsAsync(CancellationToken token)
    {
        await _client.WriteSingleRegisterAsync(0x1103, 3, token);
        await _client.WriteSingleRegisterAsync(0x1103, 1, token);
        for (int i = 0; i < 30; i++)
        {
            await Task.Delay(100, token);
            var state = await _client.ReadC077AfeAsync(token);
            if (state.OutputRequest == 0 && state.Permissions == 0 && state.BalanceOff)
            {
                _view.Status.Text = "输出和均衡已关闭。";
                return;
            }
        }
        throw new TimeoutException("输出和均衡关闭尚未确认。");
    }

    private Task ExportAsync(CancellationToken token)
    {
        var snapshot = _view.RequireSnapshot();
        var dialog = new SaveFileDialog { Filter = "C077 硬保护 JSON|*.json", FileName = "C077_309_hardware.json" };
        if (dialog.ShowDialog(this) == true)
        {
            token.ThrowIfCancellationRequested();
            File.WriteAllText(dialog.FileName, JsonSerializer.Serialize(
                C077Hardware.File(snapshot.Chips.Select(c => c.Saved).ToArray()), new JsonSerializerOptions { WriteIndented = true }));
            _view.Status.Text = "已导出两片 Flash 保存值。";
        }
        return Task.CompletedTask;
    }

    private Task ImportAsync(CancellationToken token)
    {
        var chip = _view.RequireChip();
        var dialog = new OpenFileDialog { Filter = "C077 硬保护 JSON|*.json" };
        if (dialog.ShowDialog(this) == true)
        {
            token.ThrowIfCancellationRequested();
            var file = C077Hardware.ParseFile(File.ReadAllText(dialog.FileName));
            _view.SetDraft(C077Hardware.Import(file)[chip.Index]);
            _view.Status.Text = $"已载入 {chip.Name} 草稿，等待保存。";
        }
        return Task.CompletedTask;
    }
}
