using System.IO;
using System.Windows;
using System.Windows.Controls;

namespace BmsTool.Windows;

public partial class MainWindow
{
    private readonly TabItem _c073Tab = new() { Header="C073 基本参数", Visibility=Visibility.Collapsed };
    private readonly TextBox[] _c073SocFields = Enumerable.Range(0,4).Select(_ => new TextBox { Width=110 }).ToArray();
    private readonly TextBox _c073Series = new() { Width=110 };
    private BmsClient? _c073ReadClient;
    private ushort[]? _c073SocBefore, _c073SystemBefore;

    private readonly Sh309ProtectionView _c073Sh309View = new();

    private void AddC073ParameterTab()
    {
        var panel = new StackPanel { Margin=new Thickness(16), HorizontalAlignment=HorizontalAlignment.Left };
        _c073Tab.Content = new ScrollViewer { Content=panel, VerticalScrollBarVisibility=ScrollBarVisibility.Auto };
        _parameterSections.Items.Insert(0,_c073Tab);
        void Field(string label, TextBox box) {
            var row = new StackPanel { Orientation=Orientation.Horizontal, Margin=new Thickness(0,4,0,4) };
            row.Children.Add(new TextBlock { Text=label, Width=160, VerticalAlignment=VerticalAlignment.Center });
            row.Children.Add(box); panel.Children.Add(row);
        }
        void Button(string label, Func<Task> run) {
            var button = new Button { Content=label, Margin=new Thickness(0,6,0,6), Padding=new Thickness(12,3,12,3),
                HorizontalAlignment=HorizontalAlignment.Left };
            button.Click += async (_,_) => await run(); panel.Children.Add(button);
        }
        Button("读取设备参数", () => RunC073OperationAsync(ReadC073FieldsAsync, false));
        string[] labels = ["额定容量 / Ah","循环次数","充满 · 最高单体 / mV","放空 · 最低单体 / mV"];
        for (int i=0; i<labels.Length; ++i) Field(labels[i], _c073SocFields[i]);
        Button("保存容量和 SOC 参数", () => RunC073OperationAsync((c,t) => SaveC073GroupAsync(c,0x2318,t), true));
        Field("电池串数", _c073Series);
        Button("保存串数", () => RunC073OperationAsync((c,t) => SaveC073GroupAsync(c,0x231C,t), true));
    }

    private FrameworkElement CreateC073ProtectionView()
    {
        _c073Sh309View.AddAction("读取 Flash 和 EEPROM", () =>
            RunC073OperationAsync(ReadC073ProtectionAsync, false, protection: true));
        return _c073Sh309View;
    }

    private void ClearC073ProtectionView() => _c073Sh309View.Clear();

    private async Task RunC073OperationAsync(Func<BmsClient,CancellationToken,Task> operation, bool writing, bool requiresUnlock=true, CancellationToken automatic=default, bool protection=false)
    {
        if (_bms is not { IsC073:true } client || _parameterBusy || _shBusy || _diagBusy ||
            _otaRunning || _autoReconnectRunning || _eventLogReadInProgress) return;
#if !BMS_FACTORY_APP
        if (writing && requiresUnlock && !_protectedFeaturesUnlocked) {
            _parameterStatus.Text="请先解锁高级功能，再保存参数。";
            if (protection) _c073Sh309View.Status.Text = _parameterStatus.Text;
            return;
        }
#endif
        _parameterBusy=_diagBusy=true; _pollTimer.Stop();
        using var timeout=CancellationTokenSource.CreateLinkedTokenSource(_connectionLifetime.Token, automatic);
        timeout.CancelAfter(TimeSpan.FromSeconds(45));
        try {
            await WaitForCommunicationIdleAsync();
            if (!ReferenceEquals(client,_bms)) return;
            await operation(client,timeout.Token);
            if (ReferenceEquals(client,_bms)) _parameterStatus.Text=writing ? "C073 参数已保存并读回。" : "已从 C073 读取参数。";
        }
        catch (Exception) when (automatic.IsCancellationRequested) { }
        catch (Exception ex) when (ReferenceEquals(client,_bms)) {
            _c073ReadClient=null;
            _parameterStatus.Text="C073 参数操作未确认："+ex.Message;
            if (protection) _c073Sh309View.Status.Text = _parameterStatus.Text;
        }
        catch (Exception) when (!ReferenceEquals(client,_bms)) { }
        finally {
            if (ReferenceEquals(client,_bms)) DeviceSerialStatusText.Text=_parameterStatus.Text;
            _parameterBusy=_diagBusy=false; StartAutomaticRefresh();
        }
    }

    private async Task ReadC073FieldsAsync(BmsClient client, CancellationToken token)
    {
        var soc=await client.ReadRegistersAsync(0x2318,4,token);
        var system=await client.ReadRegistersAsync(0x231C,4,token);
        var identity=await client.ReadRegistersAsync(0xC002,48,token);
        if (!ReferenceEquals(client,_bms)) throw new IOException("连接已改变。");
        _c073SocBefore=soc; _c073SystemBefore=system;
        _c073SocFields[0].Text=(soc[0]/10m).ToString("0.0");
        for (int i=1; i<4; ++i) _c073SocFields[i].Text=soc[i].ToString();
        _c073Series.Text=system[0].ToString();
        DeviceSerialBox.Text=BmsClient.DecodeIdentityText(identity[..16]);
        _c073ReadClient=_parameterProbedClient=client;
    }

    private async Task ReadC073ProtectionAsync(BmsClient client,CancellationToken ct)
    {
        _c073Sh309View.Clear();
        _c073Sh309View.Status.Text = "正在读取…";
        var profile = await Sh309Protection.ReadAsync(client, ct);
        ct.ThrowIfCancellationRequested();
        if (!ReferenceEquals(client, _bms)) throw new IOException("连接已改变。");
        _c073Sh309View.Load(profile);
    }

    private async Task SaveC073GroupAsync(BmsClient client, ushort address, CancellationToken token)
    {
        var original=address==0x2318 ? _c073SocBefore : _c073SystemBefore;
        if (!ReferenceEquals(_c073ReadClient,client) || original is null) throw new IOException("请先读取当前设备参数。");
        var candidate=(ushort[])original.Clone();
        if (address==0x2318) {
            decimal tenths=decimal.Parse(_c073SocFields[0].Text)*10m;
            if (tenths<1 || tenths>ushort.MaxValue || tenths!=decimal.Truncate(tenths)) throw new ArgumentException("容量按 0.1 Ah 输入。");
            candidate[0]=(ushort)tenths;
            for(int i=1; i<4; ++i) candidate[i]=ushort.Parse(_c073SocFields[i].Text);
            if (candidate[3]==0 || candidate[3]>=candidate[2]) throw new ArgumentException("SOC 0% 单体电压必须小于 100% 单体电压。");
        } else {
            candidate[0]=ushort.Parse(_c073Series.Text);
            if (candidate[0] is <5 or >16 || candidate[1]!=2 || candidate[2]!=6) throw new ArgumentException("C073 使用 5～16 串及固定 2:6 分流器。");
        }
        var current=await client.ReadRegistersAsync(address,4,token);
        if (!current.SequenceEqual(original)) throw new IOException("设备值已改变，请重新读取后编辑。");
        if (!candidate.SequenceEqual(current)) await client.WriteRegistersAsync(address,candidate,token);
        if (!(await client.ReadRegistersAsync(address,4,token)).SequenceEqual(candidate)) throw new IOException("参数回读不一致，请重新读取。");
        await ReadC073FieldsAsync(client,token);
    }
}
