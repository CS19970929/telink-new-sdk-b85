using System.ComponentModel;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Text;
using System.Text.Json;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using Microsoft.Win32;

namespace BmsTool.Windows;

// 仅内部测试版拥有此窗口。普通客户版继续使用完整保护参数事务。
public sealed class AfeRegisterWindow : Window
{
    private readonly BmsClient? _bms;
    private readonly ComboBox _offlineFamily = new() { Width = 200, Margin = new Thickness(5), ItemsSource = new[] { "DVC1124-2", "SH3673510 / SH3673520" }, SelectedIndex = 0 };
    private readonly DataGrid _registers = new() { AutoGenerateColumns = false, CanUserAddRows = false, IsReadOnly = true, SelectionMode = DataGridSelectionMode.Single };
    private readonly DataGrid _fields = new() { AutoGenerateColumns = false, CanUserAddRows = false, SelectionMode = DataGridSelectionMode.Single };
    private readonly TextBlock _status = new() { TextWrapping = TextWrapping.Wrap, Margin = new Thickness(5) };
    private readonly TextBlock _details = new() { TextWrapping = TextWrapping.Wrap, Margin = new Thickness(5) };
    private readonly TextBox _search = new() { Width = 220, Margin = new Thickness(5) };
    private readonly TextBox _log = new() { IsReadOnly = true, TextWrapping = TextWrapping.Wrap, VerticalScrollBarVisibility = ScrollBarVisibility.Auto, Height = 105 };
    private readonly CheckBox _oldSh = new() { Content = "查阅 SH CV0.2C（仅离线参考）", Margin = new Thickness(5) };
    private readonly List<Button> _buttons = new();
    private AfeRegisterCatalog _catalog = AfeRegisterCatalog.Load(true);
    private AfeRegisterDeviceInfo? _info;
    private ushort _token;
    private bool _busy, _mayHold;
    private bool MatchesCatalog => _info is not null && _info.CatalogId == _catalog.Id && _oldSh.IsChecked != true;

    public AfeRegisterWindow(BmsClient? bms)
    {
        _bms = bms; Title = "AFE 全寄存器调试 · 内部测试版"; Width = 1320; Height = 900;
        var root = new DockPanel { Margin = new Thickness(10) }; Content = root;
        var top = new StackPanel(); DockPanel.SetDock(top, Dock.Top); root.Children.Add(top);
        top.Children.Add(new TextBlock { Text = "原厂目录覆盖 DVC1124-2 / SH36735xx 的全部地址和位。普通读取跳过读清除位；修改仅作用于 AFE RAM。保护参数长期保存请使用原 AFE 硬件保护页。进入冻结前需有效测量且电流在 ±200mA 内，固件确认关闭 MOS/均衡后才开放写入；这不代表实测 Gate 已关闭。", TextWrapping = TextWrapping.Wrap });
        var tools = new WrapPanel(); top.Children.Add(tools);
        AddButton(tools, "读取设备 / 新会话", Connect);
        AddButton(tools, "读取全部安全地址", ReadAll);
        AddButton(tools, "读取选中", () => ReadSelected(false));
        AddButton(tools, "显式读清除寄存器", () => ReadSelected(true));
        AddButton(tools, "进入调试冻结", Begin);
        AddButton(tools, "预览字段", Preview);
        AddButton(tools, "写选中字段并读回", WriteField);
        AddButton(tools, "恢复源码 / 持久配置", Restore);
        AddButton(tools, "导入源码调试文件", Import);
        AddButton(tools, "导出快照", Export);
        AddButton(tools, "打开原 PDF", OpenPdf);
        AddButton(tools, "原厂未决说明", () => {
            MessageBox.Show(this, "原文矛盾不能用目录或 GUI 自动裁定。实装/硅版本、分流器和物理动作须单独核实。\n\n" +
                _catalog.Issues, "原厂资料边界"); return Task.CompletedTask;
        });
        var filters = new WrapPanel(); top.Children.Add(filters);
        filters.Children.Add(new TextBlock { Text = "搜索地址/寄存器/位名：", VerticalAlignment = VerticalAlignment.Center });
        filters.Children.Add(_search); filters.Children.Add(_offlineFamily); filters.Children.Add(_oldSh);
        _offlineFamily.SelectionChanged += (_, _) => LoadCatalog();
        _search.TextChanged += (_, _) => Filter();
        _oldSh.Checked += (_, _) => LoadCatalog(); _oldSh.Unchecked += (_, _) => LoadCatalog();
        top.Children.Add(_status); top.Children.Add(_details);
        var bottom = new StackPanel(); DockPanel.SetDock(bottom, Dock.Bottom);root.Children.Add(bottom);
        bottom.Children.Add(new TextBlock { Text = "断连、授权超时和关闭窗口不会解除调试冻结。需显式恢复配置或重启；写入应答丢失时结果未知，先重新读取，禁止盲目重试。快照逐地址读取，非原子采样。", TextWrapping = TextWrapping.Wrap });
        bottom.Children.Add(_log);
        var split = new Grid();split.RowDefinitions.Add(new RowDefinition { Height = new GridLength(3, GridUnitType.Star) });
        split.RowDefinitions.Add(new RowDefinition { Height = new GridLength(2, GridUnitType.Star) });root.Children.Add(split);
        AddColumn(_registers, "地址", nameof(AfeRegisterRow.AddressText), 65);
        AddColumn(_registers, "寄存器", nameof(AfeRegisterRow.Name), 330);
        AddColumn(_registers, "实际读回 / bit7..0", nameof(AfeRegisterRow.Actual), 180);
        AddColumn(_registers, "原厂复位值", nameof(AfeRegisterRow.Reset), 90);
        AddColumn(_registers, "可写 mask", nameof(AfeRegisterRow.WriteMaskText), 90);
        AddColumn(_registers, "读取属性", nameof(AfeRegisterRow.ReadPolicy), 220);
        AddColumn(_registers, "PDF 页", nameof(AfeRegisterRow.Pages), 100);
        AddColumn(_fields, "字段", nameof(AfeRegisterField.Name), 130);
        AddColumn(_fields, "Mask", nameof(AfeRegisterField.MaskText), 70);
        AddColumn(_fields, "读写属性", nameof(AfeRegisterField.Access), 125);
        AddColumn(_fields, "读回编码", nameof(AfeRegisterField.Actual), 80);
        AddColumn(_fields, "修改编码", nameof(AfeRegisterField.Code), 85, false);
        AddColumn(_fields, "手册含义 / 编码", nameof(AfeRegisterField.Description), 650);
        _registers.SelectionChanged += (_, _) => ShowFields();
        split.Children.Add(_registers);Grid.SetRow(_fields, 1);split.Children.Add(_fields);
        LoadCatalog(); Closing += OnClosing;
        Closed += async (_, _) => {
            try { if (_token != 0 && _bms is not null) await _bms.TryCloseAfeHardwareAccessAsync(_token); }
            catch (Exception ex) { Log("关闭授权未确认；冻结状态保持：" + ex.Message); }
        };
    }

    private void AddButton(Panel panel, string text, Func<Task> action)
    {
        var button = new Button { Content = text, Margin = new Thickness(3), Padding = new Thickness(7,4,7,4) };
        button.Click += async (_, _) => {
            if (_busy) return; _busy = true;foreach(var item in _buttons)item.IsEnabled = false;
            _oldSh.IsEnabled = false;_offlineFamily.IsEnabled = false;
            try { await action(); }
            catch(Exception ex) { Log("失败：" + ex.Message);MessageBox.Show(this,ex.Message,"AFE 寄存器",MessageBoxButton.OK,MessageBoxImage.Warning); }
            finally { _busy = false;foreach(var item in _buttons)item.IsEnabled = true;_oldSh.IsEnabled = true;_offlineFamily.IsEnabled = _info is null; }
        };
        _buttons.Add(button);panel.Children.Add(button);
    }

    private static void AddColumn(DataGrid grid, string name, string path, double width, bool readOnly = true) =>
        grid.Columns.Add(new DataGridTextColumn { Header = name, Binding = new Binding(path) { Mode = readOnly ? BindingMode.OneWay : BindingMode.TwoWay, UpdateSourceTrigger = UpdateSourceTrigger.PropertyChanged }, Width = width, IsReadOnly = readOnly });

    private void Log(string text) { _log.AppendText($"{DateTime.Now:HH:mm:ss.fff} {text}\n");_log.ScrollToEnd(); }
    private void LoadCatalog() { bool dvc = _info is null ? _offlineFamily.SelectedIndex == 0 : _info.Model == 0x1124; _offlineFamily.IsEnabled = _info is null && !_busy; _catalog = AfeRegisterCatalog.Load(dvc, _oldSh.IsChecked == true);_registers.ItemsSource = _catalog.Rows;Filter();ShowStatus(); }
    private void Filter() { var view = CollectionViewSource.GetDefaultView(_catalog.Rows);string query = _search.Text.Trim();view.Filter = value => value is AfeRegisterRow r && (query.Length == 0 || (r.AddressText + " " + r.Name + " " + string.Join(" ",r.Fields.Select(f=>f.Name))).IndexOf(query,StringComparison.OrdinalIgnoreCase)>=0); }
    private void ShowStatus() => _status.Text = _info is null ? $"离线原厂目录 · {_catalog.Revision} · 未读取设备" :
        $"固件声明型号 {_info.ModelName} · Rsense={_info.ShuntText} · 调试接口={_info.Enabled} · 冻结={_info.Held} · 手册 {_catalog.Revision} · 目录 {_catalog.Id} / 固件 {_info.CatalogId}（{(MatchesCatalog ? "一致" : "不一致/旧版参考，禁止写入")}） · 实装/硅版本仍需板级核实";

    private void RequireSession(bool writing = false)
    {
        if (_info is null || !_info.Enabled || _token == 0) throw new InvalidOperationException("请先读取设备并打开授权会话；固件须启用开发寄存器接口。");
        if (_oldSh.IsChecked == true) throw new InvalidOperationException("旧版目录仅供离线对照，请切回工程使用的 CV1.0A。");
        if (writing && (!MatchesCatalog || !_mayHold)) throw new InvalidOperationException("写入需要匹配的目录及已确认的调试冻结。");
    }

    private async Task Connect()
    {
        if (_bms is null) throw new InvalidOperationException("当前为离线查阅；连接 BMS 后重新打开窗口可进行设备操作。");
        _token = 0;
        _info = await _bms!.ReadAfeRegisterInfoAsync(); _mayHold |= _info.Held;
        LoadCatalog();foreach(var row in _catalog.Rows)row.Invalidate("未读取");
        if (_info.Enabled) _token = (await _bms.OpenAfeHardwareAccessAsync()).Token;
        ShowStatus();Log("已读取接口元数据；尚未读取 AFE 原始寄存器。");
    }

    private async Task ReadAll()
    {
        RequireSession();foreach(var row in _catalog.Rows)row.Invalidate("本次未读取");
        int count=0;
        foreach(var row in _catalog.Rows) {
            if(!row.Readable || row.ReadClear)continue;
            try { row.Publish(await _bms!.ReadAfeRegisterAsync(_token,row.Address));count++; }
            catch { row.Invalidate("读取失败");throw; }
        }
        ShowFields();Log($"安全地址逐项读取完成：{count} 个；RC/地址未决项未读，非同时刻快照。");
    }

    private AfeRegisterRow Selected() => _registers.SelectedItem as AfeRegisterRow ?? throw new InvalidOperationException("请选择寄存器。");
    private async Task ReadSelected(bool consume)
    {
        RequireSession();var row=Selected();
        if(!row.Readable)throw new InvalidOperationException("该起始地址在原手册中存在边界矛盾，当前不读。");
        if(row.ReadClear) {
            if(!consume || !_mayHold)throw new InvalidOperationException("该寄存器读取会清除标志；进入冻结后使用显式读取。");
            if(MessageBox.Show(this,"本次读取会消耗硬件就绪/事件标志，是否执行一次？","读清除寄存器",MessageBoxButton.YesNo)!=MessageBoxResult.Yes)return;
        }
        row.Invalidate("正在读取");row.Publish(await _bms!.ReadAfeRegisterAsync(_token,row.Address,consume));ShowFields();Log($"读取 {row.AddressText}={row.Value:X2}");
    }

    private async Task Begin()
    {
        RequireSession();if(!MatchesCatalog)throw new InvalidOperationException("固件与桌面目录不一致，不能进入调试写入。");
        if(MessageBox.Show(this,"请断开充电/负载并确认使用的手册版本。固件将检查 ±200mA，确认 MOS/均衡关闭后暂停正常采样/控制。临时寄存器修改不保存到 Flash；恢复按钮重新应用源码和持久参数。是否进入？","AFE 调试冻结",MessageBoxButton.YesNo,MessageBoxImage.Warning)!=MessageBoxResult.Yes)return;
        _mayHold=true; // BEGIN 应答丢失时仍可能已冻结。
        await _bms!.BeginAfeRegisterDebugAsync(_token);_info=await _bms!.ReadAfeRegisterInfoAsync();ShowStatus();Log("调试冻结已确认；这不等于 Gate/Vgs 实测。");
    }

    private void ShowFields()
    {
        if(_registers.SelectedItem is not AfeRegisterRow row){_fields.ItemsSource=null;return;}
        foreach(var f in row.Fields){f.Actual=row.Value is byte raw?((raw&f.Mask)>>f.Shift).ToString(CultureInfo.InvariantCulture):"未知";f.Code=row.Value is byte?f.Actual:"";}
        _fields.ItemsSource=null;_fields.ItemsSource=row.Fields;_details.Text=Physics(row,row.Value);
    }

    private string Physics(AfeRegisterRow row,byte? raw)
    {
        if(_info is null || raw is null || _info.ParallelCount==0 || _info.ShuntElementMicroOhm==0)return "物理值：需有效原始读回和分流器比例。";
        if(_info.Model==0x1124)return "DVC 字段的原厂公式/编码见下表；保护请求与有效值请同时读取原硬件保护页。";
        int uv=row.Address switch {0x4D=>(1+(raw.Value&15))*5000,0x4E=>(1+(raw.Value&15))*10000,0x50=>(1+(raw.Value&31))*1375,_=>0};
        if(row.Address==0x4F) {
            var baseRow=_catalog.Rows.First(r=>r.Address==0x4E);
            if(baseRow.Value is not byte ocd2)return "SC 依赖 OCD2V：请先读取 0x4E，不能把短路电流作为独立连续值。";
            uv=(1+(ocd2&15))*10000*new[]{2,3,4,6}[(raw.Value>>4)&3];
        }
        return uv==0?"物理值与跨字节/温度换算按原厂说明；表中复位值不是设备读回。":
            $"原厂编码名义感测电压 {uv/1000m:0.###} mV；按精确分流器换算电流 {(decimal)uv*_info.ParallelCount/_info.ShuntElementMicroOhm:0.######} A。不包含芯片/分流器容差。";
    }

    private (AfeRegisterRow Row,AfeRegisterField Field,byte Target) Candidate()
    {
        _fields.CommitEdit(DataGridEditingUnit.Cell,true);_fields.CommitEdit(DataGridEditingUnit.Row,true);
        var row=Selected();var field=_fields.SelectedItem as AfeRegisterField??throw new InvalidOperationException("请选择要写的字段。");
        if(row.Value is not byte previous)throw new InvalidOperationException("请先读取该寄存器。");
        if(field.Reserved || (field.Mask & ~row.WriteMask)!=0)throw new InvalidOperationException("该字段属于只读、保留或专用命令，不能通用写入。");
        byte code=AfeRegisterCatalog.ParseCode(field.Code,field.Mask>>field.Shift);
        return(row,field,(byte)((previous&~field.Mask)|(code<<field.Shift)));
    }
    private Task Preview(){var c=Candidate();_details.Text=$"{c.Row.AddressText} mask={c.Field.MaskText} 目标0x{c.Target:X2}；"+Physics(c.Row,c.Target);return Task.CompletedTask;}
    private async Task WriteField()
    {
        RequireSession(true);var c=Candidate();byte expected=c.Row.Value!.Value;
        if(MessageBox.Show(this,$"{c.Row.AddressText} {c.Field.Name}：0x{expected:X2} → 0x{c.Target:X2}\n{Physics(c.Row,c.Target)}\n仅临时 RAM；是否写入并读回？","确认寄存器字段",MessageBoxButton.YesNo)!=MessageBoxResult.Yes)return;
        c.Row.Invalidate("写入结果待确认");
        byte actual=await _bms!.WriteAfeRegisterAsync(_token,c.Row.Address,c.Field.Mask,expected,c.Target);
        c.Row.Publish(actual);ShowFields();Log($"写后读回 {c.Row.AddressText}={actual:X2} mask={c.Field.Mask:X2}；未保存 Flash。");
    }

    private async Task Restore()
    {
        RequireSession();await _bms!.RestoreAfeRegisterDebugAsync(_token);_info=await _bms!.ReadAfeRegisterInfoAsync();
        _mayHold=_info.Held;foreach(var row in _catalog.Rows)row.Invalidate("恢复后需重读");ShowStatus();ShowFields();
        Log("恢复命令已确认；正常输出仍须完整采样、保护和原 guard 资格，不能据命令认定 MOS 已开启。");
    }

    private async Task Import()
    {
        RequireSession(true);var dialog=new OpenFileDialog { Filter="AFE 调试文件|*.json" };if(dialog.ShowDialog(this)!=true)return;
        var file=new FileInfo(dialog.FileName);if(file.Length>65536)throw new IOException("调试文件超过 64KiB。");
        using var doc=JsonDocument.Parse(File.ReadAllText(dialog.FileName,Encoding.UTF8));var root=doc.RootElement;
        string expectedModel=_info!.Model switch{0x1124=>"dvc1124",0x3510=>"sh3673510",0x3520=>"sh3673520",_=>""};
        if(root.GetProperty("schema").GetInt32()!=1 || root.GetProperty("catalog_id").GetString()!=_catalog.Id || root.GetProperty("model").GetString()!=expectedModel)
            throw new IOException("调试文件版本、型号或目录不匹配。");
        var entries=root.GetProperty("registers").EnumerateArray().ToArray();
        if(entries.Length<1 || entries.Length>32)throw new IOException("每次只接受 1..32 个寄存器字段。");
        var operations=new List<(AfeRegisterRow Row,byte Mask,byte Value)>();var addresses=new HashSet<byte>();
        foreach(var entry in entries){byte address=entry.GetProperty("address").GetByte(),mask=entry.GetProperty("mask").GetByte(),value=entry.GetProperty("value").GetByte();
            var row=_catalog.Rows.SingleOrDefault(r=>r.Address==address);
            if(row is null || !addresses.Add(address) || mask==0 || (mask&~row.WriteMask)!=0 || (value&~mask)!=0)throw new IOException("调试文件含重复、非法或禁止字段。");
            operations.Add((row,mask,value));}
        string plan=string.Join("\n",operations.Select(o=>$"{o.Row.AddressText} mask={o.Mask:X2} value={o.Value:X2}"));
        if(MessageBox.Show(this,plan+"\n这是逐项临时写入，非原子事务；遇失败立即停止，已写项可能生效。是否执行？","导入寄存器调试",MessageBoxButton.YesNo)!=MessageBoxResult.Yes)return;
        foreach(var op in operations){byte previous=await _bms!.ReadAfeRegisterAsync(_token,op.Row.Address);op.Row.Invalidate("写入结果待确认");
            byte actual=await _bms!.WriteAfeRegisterAsync(_token,op.Row.Address,op.Mask,previous,op.Value);op.Row.Publish(actual);Log($"导入写后读回 {op.Row.AddressText}={actual:X2}");}
        ShowFields();
    }

    private Task Export()
    {
        var dialog=new SaveFileDialog { Filter="寄存器快照|*.json",FileName="afe-register-snapshot.json" };if(dialog.ShowDialog(this)!=true)return Task.CompletedTask;
        var snapshot=new { schema=1,captured_at=DateTimeOffset.Now,device=_info,catalog_id=_catalog.Id,revision=_catalog.Revision,atomic=false,
            registers=_catalog.Rows.Select(r=>new {address=r.Address,name=r.Name,value=r.Value,status=r.Actual,read_clear=r.ReadClear,pdf=r.Pdf,pages=r.Pages}) };
        File.WriteAllText(dialog.FileName,JsonSerializer.Serialize(snapshot,new JsonSerializerOptions { WriteIndented=true }),Encoding.UTF8);return Task.CompletedTask;
    }
    private Task OpenPdf()
    {
        var row=Selected();var dialog=new OpenFileDialog { Filter="原厂 PDF|*.pdf",FileName=row.Pdf,Title=$"选择 {row.Pdf}；定位 PDF 第 {row.Pages} 页" };
        if(dialog.ShowDialog(this)==true)Process.Start(new ProcessStartInfo(dialog.FileName) { UseShellExecute=true });return Task.CompletedTask;
    }
    private void OnClosing(object? sender,CancelEventArgs e)
    {
        if(_busy){e.Cancel=true;return;}
        if(_mayHold && MessageBox.Show(this,"设备可能仍处于调试冻结。关闭窗口不会恢复输出；可取消后点击恢复，或稍后重新打开窗口恢复。仍要关闭？","保留调试冻结",MessageBoxButton.YesNo,MessageBoxImage.Warning)!=MessageBoxResult.Yes)e.Cancel=true;
    }
}
