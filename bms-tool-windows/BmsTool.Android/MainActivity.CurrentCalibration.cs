using Android.Views;
using Android.Widget;
using BmsTool.Windows;
using System.Globalization;
using System.Text;

namespace BmsTool.Android;

public sealed partial class MainActivity
{
    private CurrentCalibrationSession? _mobileCalibrationSession,_mobileCalibrationRecord;
    private CancellationTokenSource? _mobileCalibrationLease,_mobileCalibrationStep;
    private TextView? _mobileCalibrationStatus;
    private EditText? _mobileCalibrationReference;
    private Spinner? _mobileCalibrationDirection;
    private Button? _mobileZero,_mobileLoad,_mobileSave,_mobileZeroSave,_mobileCancel,_mobileRestore;
    private bool _mobileCalibrationRunning;
    private string _mobileCalibrationReport="";
    private readonly List<Button> _mobileCalibrationButtons=new();

    private void BuildMobileCurrentCalibration(LinearLayout parent)
    {
        var body=Page();body.AddView(Note("先断开负载和充电器采零点，再接入稳定电流并填写电流表实测值。"));
        _mobileCalibrationStatus=DataBlock("尚未开始");body.AddView(_mobileCalibrationStatus);
        Button Add(string text,Func<Task> action){var button=ActionButton(text,action);_mobileCalibrationButtons.Add(button);body.AddView(button);return button;}
        _mobileZero=Add("① 采集零点",()=>RunMobileCalibrationStepAsync(async(client,ct)=>{
            _mobileCalibrationLease=new CancellationTokenSource(TimeSpan.FromMinutes(10));
            var session=await client.BeginCurrentCalibrationAsync(ct);_mobileCalibrationSession=_mobileCalibrationRecord=session;
            _=KeepMobileCalibrationAliveAsync(session,_mobileCalibrationLease.Token);
            session.Zero=await client.CollectCurrentCalibrationAsync(session,true,CurrentCalibrationPolicy.Default,
                (done,total)=>MobileCalibrationStatus($"采集零点 {done}/{total}…"),ct);
            MobileCalibrationStatus("零点已采集，尚未保存。可仅保存零点，或继续采集电流点。");
        }));
        _mobileZeroSave=Add("仅保存零点（保留增益）",()=>SaveMobileCalibrationAsync(true));
        _mobileCalibrationDirection=new Spinner(this);
        _mobileCalibrationDirection.Adapter=new ArrayAdapter<string>(this,global::Android.Resource.Layout.SimpleSpinnerDropDownItem,new[]{"放电","充电"});
        body.AddView(_mobileCalibrationDirection);
        _mobileCalibrationReference=LabeledInput(body,"电流表实测电流 / A（输入正数）","");
        _mobileCalibrationReference.InputType=global::Android.Text.InputTypes.ClassNumber|global::Android.Text.InputTypes.NumberFlagDecimal;
        void InvalidateCandidate(){if(_mobileCalibrationSession is { } session&&!_mobileCalibrationRunning){session.Loaded=null;session.Candidate=null;UpdateMobileCalibrationControls();}}
        _mobileCalibrationReference.TextChanged+=(_,_)=>InvalidateCandidate();
        _mobileCalibrationDirection.ItemSelected+=(_,_)=>InvalidateCandidate();
        _mobileLoad=Add("② 采集电流点",()=>RunMobileCalibrationStepAsync(async(client,ct)=>{
            var session=_mobileCalibrationSession??throw new IOException("请先采集零点。");
            if(!decimal.TryParse(_mobileCalibrationReference.Text,NumberStyles.Number,CultureInfo.CurrentCulture,out decimal amperes)||amperes<1||amperes>1000||amperes*1000!=decimal.Truncate(amperes*1000))
                throw new ArgumentException("请输入电流表实测正数，至少1A，最多三位小数；不超过产品允许电流。");
            session.ReferenceMa=checked((int)(amperes*1000))*(_mobileCalibrationDirection.SelectedItemPosition==1?-1:1);
            session.Loaded=null;session.Candidate=null;
            session.Loaded=await client.CollectCurrentCalibrationAsync(session,false,CurrentCalibrationPolicy.Default,
                (done,total)=>MobileCalibrationStatus($"采集电流点 {done}/{total}…"),ct);
            session.Candidate=CurrentCalibrationPolicy.Default.Calculate(session.Zero!,session.Loaded,session.ReferenceMa);
            MobileCalibrationStatus("候选已生成，尚未保存。请点击保存校准。"+(AppEdition.IsInternal?$"\n零点{session.Zero!.MeanMa:F3}mA，offset={session.Candidate.OffsetMa}，gain={session.Candidate.GainPpm}ppm":""));
        }));
        _mobileSave=Add("③ 保存校准",()=>SaveMobileCalibrationAsync(false));
        _mobileCancel=Add("取消校准",StopMobileCalibrationAsync);_busyAllowedButtons.Add(_mobileCancel);
        _mobileRestore=Add("恢复本次校准前参数",()=>RunMobileCalibrationStepAsync(async(client,ct)=>{
            var previous=_mobileCalibrationRecord??throw new IOException("没有本次连接的恢复记录。");
            if(!previous.SaveVerified||previous.ConnectionLost||!ReferenceEquals(previous.Client,client))throw new IOException("恢复记录已失效。");
            await client.VerifyCurrentCalibrationContinuityAsync(previous,ct);
            var session=await client.BeginCurrentCalibrationAsync(ct);_mobileCalibrationSession=session;
            if(previous.Access.Token==ushort.MaxValue||session.Access.Token!=previous.Access.Token+1||session.Identity!=previous.Identity||session.Original!=previous.Candidate)
                throw new IOException("设备、参数或授权代次已改变，旧恢复记录作废。");
            await client.SaveCurrentCalibrationAsync(session,previous.Original,ct);
            previous.Outcome+="\n已恢复本次校准前参数并读回。";
            await EndMobileCalibrationCoreAsync(false);_mobileCalibrationReport=previous.Report;_mobileCalibrationRecord=null;
            MobileCalibrationStatus("已恢复并核对本次校准前参数。");
        }));
        Add("导出校准记录",()=>{if(string.IsNullOrEmpty(_mobileCalibrationReport)){MobileCalibrationStatus("暂无记录。");return Task.CompletedTask;}string path=OutputPath($"current_calibration_{DateTimeOffset.Now:yyyyMMdd_HHmmss}.txt");File.WriteAllText(path,_mobileCalibrationReport,new UTF8Encoding(false));MobileCalibrationStatus("校准记录已保存："+path);return Task.CompletedTask;});
        AddFold(parent,"电流校准",body);UpdateMobileCalibrationControls();
    }
    private void MobileCalibrationStatus(string text)=>RunOnUiThread(()=>{if(_mobileCalibrationStatus is not null)_mobileCalibrationStatus.Text=text;});
    private void UpdateMobileCalibrationControls()
    {
        bool ready=_client is not null&&!_uiBusy&&!_mobileCalibrationRunning;
        var session=_mobileCalibrationSession;
        if(_mobileZero is not null)_mobileZero.Enabled=ready&&session is null;
        if(_mobileLoad is not null)_mobileLoad.Enabled=ready&&session?.Zero is not null;
        if(_mobileSave is not null)_mobileSave.Enabled=ready&&session?.Candidate is not null;
        if(_mobileZeroSave is not null)_mobileZeroSave.Enabled=ready&&session?.Zero is not null&&session.Loaded is null;
        if(_mobileCancel is not null)_mobileCancel.Enabled=session is not null||_mobileCalibrationRunning;
        if(_mobileRestore is not null)_mobileRestore.Enabled=ready&&session is null&&_mobileCalibrationRecord is {SaveVerified:true,ConnectionLost:false} record&&ReferenceEquals(record.Client,_client);
        if(_mobileCalibrationReference is not null)_mobileCalibrationReference.Enabled=!_mobileCalibrationRunning;
        if(_mobileCalibrationDirection is not null)_mobileCalibrationDirection.Enabled=!_mobileCalibrationRunning;
        foreach(var button in _mobileCalibrationButtons)button.Alpha=button.Enabled?1f:0.4f;
    }
    private async Task RunMobileCalibrationStepAsync(Func<BmsClient,CancellationToken,Task> action)
    {
        if(_uiBusy||_mobileCalibrationRunning)return;
        if(!EnsureServiceAccess("电流校准"))return;
        var client=_client;if(client is null){MobileCalibrationStatus("请先连接设备。");return;}
        if(_mobileCalibrationSession is { } old&&!ReferenceEquals(old.Client,client)){await EndMobileCalibrationCoreAsync(true);MobileCalibrationStatus("连接已改变，请重新开始。");return;}
        _mobileCalibrationRunning=true;SetBusy(true);
        using var step=CancellationTokenSource.CreateLinkedTokenSource(_mobileCalibrationLease?.Token??CancellationToken.None);
        step.CancelAfter(TimeSpan.FromSeconds(45));_mobileCalibrationStep=step;bool locked=false;
        try {
            await _operationGate.WaitAsync(step.Token);locked=true;
            if(!ReferenceEquals(client,_client))throw new IOException("连接已改变。");
            await action(client,step.Token);
            if(!ReferenceEquals(client,_client))throw new IOException("连接已改变。");
        } catch(OperationCanceledException){MobileCalibrationStatus("校准已取消或超时；写入若已尝试，请先重新读取确认结果。");await EndMobileCalibrationCoreAsync(false);}
          catch(Exception ex){MobileCalibrationStatus("校准未完成："+ex.Message);AppendLog("CALIBRATION "+ex);await EndMobileCalibrationCoreAsync(false);_mobileCalibrationRecord=null;}
        finally {
            if(_mobileCalibrationSession is { } session)_mobileCalibrationReport=session.Report;
            _mobileCalibrationStep=null;if(locked)_operationGate.Release();_mobileCalibrationRunning=false;SetBusy(false);
        }
    }
    private Task SaveMobileCalibrationAsync(bool zeroOnly)=>RunMobileCalibrationStepAsync(async(client,ct)=>{
        var session=_mobileCalibrationSession??throw new IOException("请先采集零点。");
        session.ZeroOnly=zeroOnly;
        if(zeroOnly){session.Zero=await client.CollectCurrentCalibrationAsync(session,true,CurrentCalibrationPolicy.Default,null,ct);session.Candidate=CurrentCalibrationPolicy.Default.CalculateZeroOnly(session.Zero,session.Original);}
        var candidate=session.Candidate??throw new IOException("请先采集电流点。");
        await client.SaveCurrentCalibrationAsync(session,candidate,ct);session.Outcome="已保存并读回，新样本公式核对通过。";
        _mobileCalibrationReport=session.Report;_mobileCalibrationRecord=session;
        await EndMobileCalibrationCoreAsync(false);MobileCalibrationStatus("校准已保存并读回。请使用电流表验证零点及其他电流点。");
    });
    private async Task KeepMobileCalibrationAliveAsync(CurrentCalibrationSession session,CancellationToken ct)
    {
        try {
            while(true){await Task.Delay(2000,ct);if(!ReferenceEquals(_mobileCalibrationSession,session))return;
                if(!await _operationGate.WaitAsync(0,ct))continue;
                try {if(!ReferenceEquals(session.Client,_client))throw new IOException("连接已改变。");await session.Client.CheckCurrentCalibrationAsync(session,ct);}
                finally {_operationGate.Release();}
            }
        } catch(OperationCanceledException){if(ReferenceEquals(_mobileCalibrationSession,session)&&!_mobileCalibrationRunning)await StopMobileCalibrationAsync();}
          catch(Exception ex){MobileCalibrationStatus("校准连接失效："+ex.Message);await StopMobileCalibrationAsync();_mobileCalibrationRecord=null;RunOnUiThread(UpdateMobileCalibrationControls);}
    }
    private async Task EndMobileCalibrationCoreAsync(bool connectionChanged)
    {
        _mobileCalibrationLease?.Cancel();_mobileCalibrationLease?.Dispose();_mobileCalibrationLease=null;
        var session=_mobileCalibrationSession;_mobileCalibrationSession=null;
        if(session is null)return;
        if(connectionChanged||!ReferenceEquals(session.Client,_client))session.InvalidateConnection();
        await session.Client.EndCurrentCalibrationAsync(session,verifyConnection:!connectionChanged);
        _mobileCalibrationReport=session.Report;
    }
    private async Task StopMobileCalibrationAsync()
    {
        _mobileCalibrationStep?.Cancel();if(_mobileCalibrationRunning)return;
        await _operationGate.WaitAsync();
        try {await EndMobileCalibrationCoreAsync(false);MobileCalibrationStatus("校准已取消；未保存的候选作废。");}
        finally {_operationGate.Release();RunOnUiThread(RefreshActionAvailability);}
    }
    protected override void OnStop()
    {
        BatteryRecordingService.StatusChanged -= UpdateBatteryLogStatus;
        _mobileCalibrationStep?.Cancel();if(_mobileCalibrationSession is not null)_=StopMobileCalibrationAsync();base.OnStop();
    }
}
