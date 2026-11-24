using System.Windows;

namespace BmsTool.Windows;

public sealed class C077AfeEditorWindow : Window
{
    private readonly Sh309ProtectionView _view = new();
    private readonly BmsClient _client;
    private bool _busy;

    public C077AfeEditorWindow(BmsClient client)
    {
        _client = client;
        Title = "SH367309 硬件保护";
        Width = 1050;
        Height = 760;
        MinWidth = 800;
        MinHeight = 550;
        WindowStartupLocation = WindowStartupLocation.CenterOwner;
        Content = _view;
        _view.AddAction("读取 Flash 和 EEPROM", ReadAsync);
        Loaded += async (_, _) => await ReadAsync();
        Closing += (_, e) => e.Cancel = _busy;
    }

    private async Task ReadAsync()
    {
        if (_busy) return;
        _busy = true;
        _view.Clear();
        _view.Status.Text = "正在读取…";
        try { _view.Load(await Sh309Protection.ReadAsync(_client)); }
        catch (Exception ex) { _view.Status.Text = "读取未完成：" + ex.Message; }
        finally { _busy = false; }
    }
}
