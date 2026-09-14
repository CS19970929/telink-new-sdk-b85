using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Text;
using System.Text.Json;
using System.Windows;
using System.Windows.Controls;
using Microsoft.Win32;

namespace BmsTool.Windows;

public partial class MainWindow
{
    private CancellationTokenSource? _flashTestCts;
    private readonly ComboBox _flashPattern=new(){ItemsSource=new[]{"SOC循环变化 0..100", "重复SOC 50", "小幅往返 49/50"},SelectedIndex=0,Width=170};
    private readonly CheckBox _flashForce=new(){Content="持续压力（跳过SOC变化判断）",Margin=new Thickness(8)};
    private readonly TextBox _flashCycles=new(){Text="10000",Width=75};
    private readonly TextBox _flashDaily=new(){Text="100",Width=65};
    private readonly TextBox _flashEndurance=new(){Text="10000",Width=65};
    private readonly TextBox _flashHistory=new(){Text="0",Width=65};
    private readonly TextBlock _flashStatus=new(){Text="需要专用测试固件；此功能会真实消耗SOC页寿命。",TextWrapping=TextWrapping.Wrap,Margin=new Thickness(8)};
    private readonly TextBox _flashFlow=new(){IsReadOnly=true,AcceptsReturn=true,VerticalScrollBarVisibility=ScrollBarVisibility.Auto,FontFamily=new System.Windows.Media.FontFamily("Consolas")};
    private StreamWriter? _flashFlowFile;
    private string? _flashReportDirectory;
    private sealed record FlashBackup(string Serial, DateTime Created, ushort[] Words);

    private void AddFlashEnduranceTab()
    {
        var panel=new DockPanel{Margin=new Thickness(12)};
        var settings=new WrapPanel{Margin=new Thickness(0,0,0,8)};
        void Label(string t)=>settings.Children.Add(new TextBlock{Text=t,VerticalAlignment=VerticalAlignment.Center,Margin=new Thickness(6)});
        settings.Children.Add(_flashPattern); settings.Children.Add(_flashForce);
        Label("提交次数");settings.Children.Add(_flashCycles);
        Label("日常提交/天");settings.Children.Add(_flashDaily);
        Label("耐久指标/页");settings.Children.Add(_flashEndurance);
        Label("历史最忙页擦除（未知填0）");settings.Children.Add(_flashHistory);
        var start=new Button{Content="开始SOC加速测试",Margin=new Thickness(6),Padding=new Thickness(8)};
        start.Click+=RunFlashEndurance_Click;settings.Children.Add(start);
        var stop=new Button{Content="停止并恢复",Margin=new Thickness(6),Padding=new Thickness(8)};
        stop.Click+=(_,_)=>_flashTestCts?.Cancel();settings.Children.Add(stop);
        var restore=new Button{Content="从备份恢复SOC",Margin=new Thickness(6),Padding=new Thickness(8)};
        restore.Click+=RestoreFlashBackup_Click;settings.Children.Add(restore);
        var verify=new Button{Content="重启后校验备份",Margin=new Thickness(6),Padding=new Thickness(8)};
        verify.Click+=VerifyFlashBackup_Click;settings.Children.Add(verify);
        var folder=new Button{Content="查看测试记录",Margin=new Thickness(6),Padding=new Thickness(8)};
        folder.Click+=(_,_)=>{ if(_flashReportDirectory is not null) Process.Start(new ProcessStartInfo(_flashReportDirectory){UseShellExecute=true}); };
        settings.Children.Add(folder);
        DockPanel.SetDock(settings,Dock.Top);panel.Children.Add(settings);
        DockPanel.SetDock(_flashStatus,Dock.Top);panel.Children.Add(_flashStatus);panel.Children.Add(_flashFlow);
        MainTabs.Items.Add(new TabItem{Header="Flash寿命测试",Content=panel});
    }

    private void FlashFlow(string text)
    {
        string line=$"{DateTime.Now:O} {text}";
        try { _flashFlowFile?.WriteLine(line);_flashFlowFile?.Flush(); }
        catch(IOException ex) { _flashFlowFile?.Dispose();_flashFlowFile=null;_flashTestCts?.Cancel();AppendLog("测试日志写入失败："+ex.Message,"ERROR"); }
        if(_flashFlow.Text.Length>100000) _flashFlow.Clear();
        _flashFlow.AppendText(line+Environment.NewLine);_flashFlow.ScrollToEnd();
    }
    private static uint FlashU32(ushort[] w,int n)=>(uint)w[n]<<16|w[n+1];
    private static async Task<ushort[]> FlashStatusAsync(BmsClient bms)
    {
        ushort[] w=await bms.ReadRegistersAsync(0x2700,96);
        if(w[0]!=0x4654 || w[1]!=1 || w[48]!=20) throw new IOException("设备不是兼容的Flash测试固件。");
        return w;
    }
    private static ushort FlashSample(int pattern,int i)=>(ushort)(pattern==0?i%101:pattern==1?50:49+i%2);

    private async void RunFlashEndurance_Click(object sender,RoutedEventArgs e)
    {
        if(_flashTestCts is not null || _factoryTestCts is not null || _otaRunning || _shBusy || _eventLogReadInProgress) return;
        if(!int.TryParse(_flashCycles.Text,out int cycles) || cycles<1 || cycles>1000000 ||
           !uint.TryParse(_flashDaily.Text,out uint daily) || daily==0 ||
           !uint.TryParse(_flashEndurance.Text,out uint endurance) || endurance==0 ||
           !uint.TryParse(_flashHistory.Text,out uint history))
        { MessageBox.Show("次数范围1..1000000；日常提交和耐久指标必须大于0。历史擦除为非负整数。");return; }
        BmsClient? bms=_bms;
        if(bms is null) { MessageBox.Show("请先连接测试板。");return; }
        if(MessageBox.Show(this,"将真实写入SOC存储区并消耗Flash寿命。请使用稳定供电的专用测试板并断开充电器/负载。中断后可从电脑备份恢复SOC。是否开始？","SOC加速擦写",MessageBoxButton.YesNo,MessageBoxImage.Warning,MessageBoxResult.No)!=MessageBoxResult.Yes)return;
        int pattern=_flashPattern.SelectedIndex;
        ushort mode=_flashForce.IsChecked==true?(ushort)0xA502:(ushort)0xA501;
        _flashReportDirectory=null;
        _flashTestCts=new(); var ct=_flashTestCts.Token;
        _eventLogReadInProgress=true;_shBusy=true;
        if(sender is Button button)button.IsEnabled=false;
        bool started=false,restored=false;
        string outcome="未开始";
        ushort[]? last=null;
        FlashBackup? backup=null;
        StreamWriter? samples=null;
        var clock=Stopwatch.StartNew();
        try
        {
            _pollTimer.Stop();await WaitForCommunicationIdleAsync();
            if(!ReferenceEquals(bms,_bms))throw new IOException("连接已改变。");
            var identity=await bms.ReadIdentityAsync();
            if(string.IsNullOrWhiteSpace(identity.Serial))throw new IOException("设备序列号为空，无法绑定恢复备份。");
            var initial=await FlashStatusAsync(bms);
            if(initial[2]!=0)throw new IOException("设备仍有活动测试，请先停止或等待失联恢复。");
            _flashReportDirectory=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments),"BmsFlashTests",DateTime.Now.ToString("yyyyMMdd-HHmmss")+"-"+Guid.NewGuid().ToString("N")[..6]);
            Directory.CreateDirectory(_flashReportDirectory);
            _flashFlow.Clear();
            _flashFlowFile=new StreamWriter(Path.Combine(_flashReportDirectory,"流程.log"),false,Encoding.UTF8){AutoFlush=true};
            FlashFlow($"设备={identity.Serial}; 模式={mode:X4}; 数据模式={pattern}; 次数={cycles}; 日常提交={daily}; 耐久={endurance}; 历史={history}");
            // Start freezes normal SOC persistence; no synthetic writes until backup is on disk.
            started=true;
            await bms.WriteSingleRegisterAsync(0x2700,mode);
            var frozen=await FlashStatusAsync(bms);
            backup=new FlashBackup(identity.Serial,DateTime.Now,frozen.Skip(72).Take(20).ToArray());
            await File.WriteAllTextAsync(Path.Combine(_flashReportDirectory,"SOC备份.json"),JsonSerializer.Serialize(backup,new JsonSerializerOptions{WriteIndented=true}));
            FlashFlow("原SOC快照已备份；开始逐笔写入并回读校验。");
            samples=new StreamWriter(Path.Combine(_flashReportDirectory,"数据.csv"),false,Encoding.UTF8){AutoFlush=true};
            samples.WriteLine("time,seconds,submitted,saved,skipped,saveFailures,mismatches,programFailures,pageE000,pageE400,pageE800,pageEC00,pageF000,pageF400,pageF800,pageFC00,okE000,okE400,okE800,okEC00,okF000,okF400,okF800,okFC00,soc");
            long lastSample=-1000;
            for(int i=0;i<cycles;i++)
            {
                ct.ThrowIfCancellationRequested();
                if(!ReferenceEquals(bms,_bms))throw new IOException("连接改变，测试中止。");
                await bms.WriteSingleRegisterAsync(0x2701,FlashSample(pattern,i));
                if(clock.ElapsedMilliseconds-lastSample>=500 || i==cycles-1)
                {
                    last=await FlashStatusAsync(bms);
                    if(last[2]!=mode || last[3]!=0 || FlashU32(last,4)!=(uint)i+1)throw new IOException("测试会话丢失、计数不连续或Flash错误。");
                    uint hot=Math.Max(FlashU32(last,24),FlashU32(last,26));
                    double perDay=hot/(double)(i+1)*daily;
                    string estimate=perDay==0?"擦除样本不足":$"按当前负荷推算剩余 {Math.Max(0,(double)endurance-history-hot)/perDay/365:F2} 年（历史未知时非真实剩余寿命）";
                    _flashStatus.Text=$"{i+1}/{cycles} · 保存 {FlashU32(last,6)} · 跳过 {FlashU32(last,8)} · SOC页擦除 {FlashU32(last,24)}/{FlashU32(last,26)} · {estimate}";
                    FlashFlow(_flashStatus.Text+$" · 保存失败 {FlashU32(last,10)} · 回读不一致 {FlashU32(last,12)} · 编程失败 {FlashU32(last,14)}");
                    FlashFlow("各页擦除尝试/成功："+string.Join("  ",Enumerable.Range(0,8).Select(p=>$"{0x0800E000+p*1024:X8}={FlashU32(last,16+p*2)}/{FlashU32(last,32+p*2)}")));
                    samples.WriteLine(string.Join(",",new[]{DateTime.Now.ToString("O"),clock.Elapsed.TotalSeconds.ToString("F3",CultureInfo.InvariantCulture)}.Concat(new[]{4,6,8,10,12,14,16,18,20,22,24,26,28,30,32,34,36,38,40,42,44,46}.Select(n=>FlashU32(last,n).ToString())).Append(last[51].ToString())));
                    lastSample=clock.ElapsedMilliseconds;
                }
            }
            outcome="完成";
        }
        catch(OperationCanceledException){outcome="用户停止";FlashFlow(outcome);}
        catch(Exception ex)
        {
            outcome="失败："+ex.Message;FlashFlow(outcome);_flashStatus.Text=outcome;
            if(started) { try { last=await FlashStatusAsync(bms);FlashFlow($"故障计数：保存失败={FlashU32(last,10)}，回读不一致={FlashU32(last,12)}，编程失败={FlashU32(last,14)}"); } catch { /* Original failure is retained; do not resend a test write. */ } }
        }
        finally
        {
            if(started)
            {
                try
                {
                    last=await FlashStatusAsync(bms);
                    FlashFlow($"最终计数：提交={FlashU32(last,4)}，保存={FlashU32(last,6)}，跳过={FlashU32(last,8)}，失败={FlashU32(last,10)}，不一致={FlashU32(last,12)}，SOC页擦除={FlashU32(last,24)}/{FlashU32(last,26)}");
                    samples?.WriteLine(string.Join(",",new[]{DateTime.Now.ToString("O"),clock.Elapsed.TotalSeconds.ToString("F3",CultureInfo.InvariantCulture)}.Concat(new[]{4,6,8,10,12,14,16,18,20,22,24,26,28,30,32,34,36,38,40,42,44,46}.Select(n=>FlashU32(last,n).ToString())).Append(last[51].ToString())));
                }
                catch(Exception ex){FlashFlow("最终统计未完整读取："+ex.Message);}
                try
                {
                    await bms.WriteSingleRegisterAsync(0x2700,0);
                    var state=await FlashStatusAsync(bms);
                    restored=state[2]==0 && state[49]==1 && backup is not null && state.Skip(50).Take(20).SequenceEqual(backup.Words);
                    FlashFlow(restored?"原SOC快照已恢复并回读通过。":"恢复未确认，保留备份，请手动恢复。");
                }
                catch(Exception ex){FlashFlow("恢复失败："+ex.Message+"；请使用SOC备份.json恢复。");}
            }
            samples?.Dispose();
            if(_flashReportDirectory is not null)
            {
                try { File.WriteAllText(Path.Combine(_flashReportDirectory,"结果.json"),JsonSerializer.Serialize(new{serial=backup?.Serial,outcome,restored,estimatedYears=last is not null && FlashU32(last,4)>0 && Math.Max(FlashU32(last,24),FlashU32(last,26))>0 ? Math.Max(0,(double)endurance-history-Math.Max(FlashU32(last,24),FlashU32(last,26)))/(Math.Max(FlashU32(last,24),FlashU32(last,26))/(double)FlashU32(last,4)*daily)/365 : (double?)null,seconds=clock.Elapsed.TotalSeconds,cycles,pattern,mode,daily,endurance,history,last},new JsonSerializerOptions{WriteIndented=true})); }
                catch(Exception ex){AppendLog("测试报告保存失败："+ex.Message,"ERROR");}
            }
            _flashFlowFile?.Dispose();_flashFlowFile=null;
            _flashTestCts.Dispose();_flashTestCts=null;_shBusy=false;_eventLogReadInProgress=false;
            if(sender is Button startButton)startButton.IsEnabled=true;
            _flashStatus.Text=$"{outcome}；SOC恢复{(restored?"通过":"未确认") }。记录：{_flashReportDirectory}";
            StartAutomaticRefresh();
        }
    }

    private async void RestoreFlashBackup_Click(object sender,RoutedEventArgs e)=>await ProcessFlashBackupAsync(true);
    private async void VerifyFlashBackup_Click(object sender,RoutedEventArgs e)=>await ProcessFlashBackupAsync(false);
    private async Task ProcessFlashBackupAsync(bool restore)
    {
        if(_flashTestCts is not null || _factoryTestCts is not null || _otaRunning || _shBusy || _eventLogReadInProgress)return;
        var dialog=new OpenFileDialog{Filter="SOC备份|*.json"};if(dialog.ShowDialog()!=true)return;
        _shBusy=true;_eventLogReadInProgress=true;
        try
        {
            _pollTimer.Stop();await WaitForCommunicationIdleAsync();
            var bms=_bms??throw new IOException("请先连接BMS。");
            var backup=JsonSerializer.Deserialize<FlashBackup>(await File.ReadAllTextAsync(dialog.FileName))??throw new IOException("备份无效。");
            if(backup.Words.Length!=20 || backup.Words[0]!=3)throw new IOException("备份格式不支持。");
            var identity=await bms.ReadIdentityAsync();
            if(string.IsNullOrWhiteSpace(backup.Serial) || identity.Serial!=backup.Serial)throw new IOException("备份序列号与当前设备不同，拒绝恢复/校验。");
            var state=await FlashStatusAsync(bms);
            if(state[2]!=0)throw new IOException("测试尚未停止。");
            if(restore)
            {
                if(MessageBox.Show(this,"将覆盖此板SOC持久化快照。恢复成功后请重启板子，让运行SOC重新加载。确认恢复？","恢复SOC",MessageBoxButton.YesNo,MessageBoxImage.Warning,MessageBoxResult.No)!=MessageBoxResult.Yes)return;
                await bms.RestoreFlashTestSnapshotAsync(backup.Words);
                state=await FlashStatusAsync(bms);
            }
            bool match=state[49]==1 && state.Skip(50).Take(20).SequenceEqual(backup.Words);
            string result=$"{DateTime.Now:O} {(restore?"恢复":"重启后回读")} {(match?"通过":"不一致（运行期间正常SOC保存也可能改变快照）")} 设备={identity.Serial}";
            File.AppendAllText(Path.Combine(Path.GetDirectoryName(dialog.FileName)!,"恢复校验.log"),result+Environment.NewLine);
            FlashFlow(result);MessageBox.Show(result);
        }
        catch(Exception ex){ShowError("SOC备份操作失败",ex);}
        finally{_shBusy=false;_eventLogReadInProgress=false;StartAutomaticRefresh();}
    }
}
