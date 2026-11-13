using System.Globalization;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;

namespace BmsTool.Windows;

public partial class MainWindow
{
    private readonly DataGrid _hs3520Grid = new() { AutoGenerateColumns = false, MinHeight = 450, CanUserAddRows = false };
    private readonly TextBlock _hs3520Status = new() { Text = "先读取设备。HS-D015 只使用 AFE 硬件保护。", TextWrapping = TextWrapping.Wrap };
    private readonly TextBox _hs3520Capacity = new(), _hs3520Soc = new(), _hs3520Cycles = new(), _hs3520Serial = new();
    private ushort[]? _hs3520Words;
    private BmsClient? _hs3520Owner;

    private void AddSh3520ParameterTab()
    {
        var root = new StackPanel { Margin = new Thickness(14) };
        root.Children.Add(_hs3520Status);
        root.Children.Add(new TextBlock { Text = "修改前断开弱电开关。使能及通道受本板接线约束；电压、电流、温度是只读测量值。", TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 8, 0, 8) });
        void Button(string title, Func<BmsClient, CancellationToken, Task>? action)
        {
            var button = new Button { Content = title, HorizontalAlignment = HorizontalAlignment.Left, Margin = new Thickness(4), Padding = new Thickness(10, 5, 10, 5) };
            button.Click += async (_, _) => await Hs3520RunAsync(action);
            root.Children.Add(button);
        }
        void Field(string title, TextBox box)
        {
            var row = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(4) };
            row.Children.Add(new TextBlock { Text = title, Width = 180 });
            box.Width = 300;
            row.Children.Add(box);
            root.Children.Add(row);
        }
        Button("读取 3520 参数及基本信息", null);
        Field("额定容量 / Ah", _hs3520Capacity);
        Button("保存额定容量", async (c, t) => {
            ushort capacity = D008Parameters.Capacity(_hs3520Capacity.Text);
            if (capacity < 10) throw new ArgumentException("HS-D015 额定容量范围为 1.0～655.3 Ah。");
            await Hs3520WriteWordAsync(c, 0x2318, capacity, t);
        });
        Button("核准当前完整配置（允许进入预充/运行）", async (c, t) => {
            var config = await c.ReadRegistersAsync(0x3400, 40, t);
            config[0] = 1;
            await c.WriteRegistersAsync(0x3400, config, t);
            if (!(await c.ReadRegistersAsync(0x3400, 40, t)).SequenceEqual(config)) throw new IOException("完整配置核准未回读确认。");
        });
        Field("SOC / %", _hs3520Soc);
        Button("保存 SOC", async (c, t) => {
            ushort value = ushort.Parse(_hs3520Soc.Text, CultureInfo.CurrentCulture);
            if (value > 100) throw new ArgumentException("SOC 必须为 0～100。");
            await Hs3520WriteWordAsync(c, 0x1005, value, t);
        });
        Field("循环次数", _hs3520Cycles);
        Button("保存循环次数", async (c, t) => await Hs3520WriteWordAsync(c, 0x2319, ushort.Parse(_hs3520Cycles.Text), t));
        Field("SN / 最多 32 字节 ASCII", _hs3520Serial);
        Button("保存 SN", async (c, t) => {
            string text = _hs3520Serial.Text.Trim();
            if (text.Length > 32 || text.Any(ch => ch < 32 || ch > 126)) throw new ArgumentException("SN 必须是最多 32 字节的可打印 ASCII。");
            var words = new ushort[16];
            for (int i = 0; i < text.Length; i++) words[i / 2] |= (ushort)(text[i] << (i % 2 == 0 ? 8 : 0));
            await c.WriteRegistersAsync(0xC002, words, t);
            if (!(await c.ReadRegistersAsync(0xC002, 16, t)).SequenceEqual(words)) throw new IOException("SN 读回不一致。");
        });
        _hs3520Grid.Columns.Add(new DataGridTextColumn { Header = "参数", Binding = new Binding(nameof(Sh3520Field.Name)), IsReadOnly = true, Width = 260 });
        _hs3520Grid.Columns.Add(new DataGridTextColumn { Header = "设备配置回读值", Binding = new Binding(nameof(Sh3520Field.Current)), IsReadOnly = true, Width = 120 });
        _hs3520Grid.Columns.Add(new DataGridTextColumn { Header = "设置值", Binding = new Binding(nameof(Sh3520Field.Edit)) { UpdateSourceTrigger = UpdateSourceTrigger.PropertyChanged }, Width = 120 });
        _hs3520Grid.Columns.Add(new DataGridTextColumn { Header = "单位", Binding = new Binding(nameof(Sh3520Field.Unit)), IsReadOnly = true, Width = 80 });
        _hs3520Grid.Columns.Add(new DataGridTextColumn { Header = "范围", Binding = new Binding(nameof(Sh3520Field.Range)), IsReadOnly = true, Width = new DataGridLength(1, DataGridLengthUnitType.Star) });
        _hs3520Grid.BeginningEdit += (_, e) => {
            if (e.Row.Item is Sh3520Field field && !field.Editable) e.Cancel = true;
        };
        root.Children.Add(_hs3520Grid);
        Button("保存整组硬件保护并读回", async (c, t) => {
            if (_hs3520Words is null || !ReferenceEquals(_hs3520Owner, c)) throw new InvalidOperationException("请先读取当前连接的参数。");
            _hs3520Grid.CommitEdit(DataGridEditingUnit.Cell, true);
            _hs3520Grid.CommitEdit(DataGridEditingUnit.Row, true);
            var values = Sh3520Parameters.Build(_hs3520Words, (IEnumerable<Sh3520Field>)_hs3520Grid.ItemsSource, true);
            await c.Write3520ParametersAsync(Sh3520Parameters.HardwareBase, values, t);
        });
        MainTabs.Items.Add(new TabItem { Header = "3520 参数", Content = new ScrollViewer { Content = root, VerticalScrollBarVisibility = ScrollBarVisibility.Auto } });
    }

    private static async Task Hs3520WriteWordAsync(BmsClient client, ushort address, ushort value, CancellationToken ct)
    {
        await client.WriteSingleRegisterAsync(address, value, ct);
        if ((await client.ReadRegistersAsync(address, 1, ct))[0] != value)
            throw new IOException("设备读回不一致，请重新读取确认。");
    }

    private async Task Hs3520RunAsync(Func<BmsClient, CancellationToken, Task>? action)
    {
        if (_parameterBusy || _diagBusy || _otaRunning || _shBusy || _eventLogReadInProgress || _autoReconnectRunning || ShFactoryBusy()) return;
        var client = _bms;
        if (client is null) { _hs3520Status.Text = "请先连接设备。"; return; }
#if !BMS_FACTORY_APP
        if (action is not null && !_protectedFeaturesUnlocked) { _hs3520Status.Text = "请先解锁高级功能再修改参数。"; return; }
#endif
        _parameterBusy = true;
        _diagBusy = true;
        _eventLogReadInProgress = true;
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(30));
        try
        {
            _pollTimer.Stop();
            await WaitForCommunicationIdleAsync();
            if (!ReferenceEquals(client, _bms)) throw new IOException("连接已改变，请重新读取。");
            var identity = await client.ReadRegistersAsync(0x2500, 32, timeout.Token);
            Sh3520Parameters.CheckIdentity(identity);
            if (identity[24] != 0xD015 || identity[25] != 13) throw new NotSupportedException("当前页面只支持 HS-D015 的板级参数约束。");
            if (action is not null)
            {
                if (!ReferenceEquals(_hs3520Owner, client) || _hs3520Words is null) throw new IOException("请先读取当前设备后再修改。");
                await action(client, timeout.Token);
            }
            var words = await client.ReadRegistersAsync(0x2500, 32, timeout.Token);
            var capacity = await client.ReadRegistersAsync(0x2318, 2, timeout.Token);
            var soc = await client.ReadRegistersAsync(0x1005, 1, timeout.Token);
            var serial = await client.ReadRegistersAsync(0xC002, 16, timeout.Token);
            if (!ReferenceEquals(client, _bms)) throw new IOException("连接已改变，请重新读取。");
            _hs3520Words = words;
            _hs3520Owner = client;
            _hs3520Grid.ItemsSource = Sh3520Parameters.HardwareFields(words);
            _hs3520Capacity.Text = (capacity[0] / 10m).ToString(CultureInfo.CurrentCulture);
            _hs3520Cycles.Text = capacity[1].ToString();
            _hs3520Soc.Text = soc[0].ToString();
            _hs3520Serial.Text = ModbusRtu.DecodeAscii(serial);
            _hs3520Status.Text = ((words[27] & 2) == 0 ? "AFE 当前未确认应用，表中为保存配置/编辑模板。" : "AFE 配置已校验。") + ((words[27] & 1) == 0 ? "配置尚未核准，MOS保持禁止输出。" : "配置已核准。") + (action is null ? "已读取设备配置回读值。" : "设备已确认保存，读回复查完成。") + " NTC 电阻显示量化后的实际值；固定保护使能不能关闭，TS3 固定电阻不参与保护。";
        }
        catch (Exception ex)
        {
            _hs3520Owner = null;
            _hs3520Words = null;
            _hs3520Grid.ItemsSource = null;
            _hs3520Status.Text = "操作失败或结果未确认：" + ex.Message + " 请重新读取，不自动重试写入。";
        }
        finally
        {
            _parameterBusy = false;
            _diagBusy = false;
            _eventLogReadInProgress = false;
            StartAutomaticRefresh();
        }
    }
}
