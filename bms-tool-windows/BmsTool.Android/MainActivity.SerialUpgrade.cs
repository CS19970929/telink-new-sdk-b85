using Android.Views;
using Android.Widget;
using BmsTool.Windows;
using System.Text.RegularExpressions;

namespace BmsTool.Android;

public sealed partial class MainActivity
{
    private const int FirmwarePage=8;
    private TextView? _firmwareStatus;
    private ProgressBar? _upgradeProgress;
    private TextView? _upgradeTargetView, _selectedFirmwareView;
    private string _firmwarePath = string.Empty;
    private View BuildFirmwarePage()
    {
        var page=Page();
        page.AddView(ActionButton("‹ 返回服务设置",()=>{ShowPage(DevicePage);return Task.CompletedTask;},false));
        page.AddView(Note("升级期间请保持供电和蓝牙连接。"));
        _upgradeTargetView=DataBlock("请先扫描或扫码选择设备");page.AddView(_upgradeTargetView);
        page.AddView(ActionButton("选择固件",PickFirmware,false));
        _selectedFirmwareView=DataBlock("尚未选择固件");page.AddView(_selectedFirmwareView);
        var inbox=Page();inbox.AddView(ActionButton("刷新文件列表",RefreshFirmwareInboxAsync,false));
        _firmwareInboxStatus=DataBlock("暂无已导入固件");inbox.AddView(_firmwareInboxStatus);
        _firmwareInbox=Page();inbox.AddView(_firmwareInbox);AddFold(page,"已导入固件",inbox);
        if(AppEdition.IsInternal) {
            var verification=Page();
            _expectedSerialInput=Input("升级后期望SN（可选）","");verification.AddView(_expectedSerialInput);
            AddFold(page,"额外身份核对",verification);
        }
        _firmwareStatus=DataBlock("选择固件后开始升级");page.AddView(_firmwareStatus);
        _upgradeProgress=new ProgressBar(this,null,global::Android.Resource.Attribute.ProgressBarStyleHorizontal){Max=100};page.AddView(_upgradeProgress);
        _otaButton=ActionButton("开始升级",StartOtaAsync,false);page.AddView(_otaButton);
        var cancel=ActionButton("取消升级",()=>{CancelCurrentOperation();return Task.CompletedTask;},false);
        _busyAllowedButtons.Add(cancel);page.AddView(cancel);
        return Wrap(page);
    }
    private void SelectFirmware(string path)
    {
        _firmwarePath=path;
        RunOnUiThread(()=>{if(_selectedFirmwareView is not null)_selectedFirmwareView.Text=Path.GetFileName(path);});
    }
    private void UpgradeStatus(string text,bool error=false)
    {
        SetStatus(text,error);RunOnUiThread(()=>{if(_firmwareStatus is not null)_firmwareStatus.Text=text;});
    }
    private void CheckUpgradeEndpoint(string mac,bool requireExisting)
    {
        if(!Regex.IsMatch(mac,"^([0-9A-F]{2}:){5}[0-9A-F]{2}$"))throw new ArgumentException("请先扫描或扫码选择设备。");
        if(!string.Equals(mac,_selectedMac,StringComparison.OrdinalIgnoreCase))
            throw new IOException("所选设备已改变，请重新确认升级目标。");
        if(!string.IsNullOrWhiteSpace(_connectedMac)&&!string.Equals(mac,_connectedMac,StringComparison.OrdinalIgnoreCase))
            throw new IOException("目标与当前连接不同，请先断开后明确选择新设备。");
        if(requireExisting&&(_client is null||_transport is null||!string.Equals(mac,_connectedMac,StringComparison.OrdinalIgnoreCase)))
            throw new IOException("已授权的原连接失效，未执行升级。");
    }
    private async Task RunSerialUpgradeAsync(FirmwareImage image,string mac,string expectedSerial)
    {
        if(!HasBluetoothPermissions()){RequestBluetoothPermissions();return;}
        if(_uiBusy){UpgradeStatus("当前操作未完成，请稍后重试",true);return;}
        var package=image.Package??throw new InvalidDataException("需要带身份和CRC的YMODEM包。");
        CheckUpgradeEndpoint(mac,false);
        string detail=$"设备 {mac}\n固件 {image.FileName}\n版本 {package.Version:X8}\n传输 标准\n\n确认开始升级？";
        if(AppEdition.IsInternal) detail+=$"\n板号 {package.BoardId:X8} · APP {package.AppAddress:X8}\n长度 {package.PayloadLength} · CRC {package.PayloadCrc:X8}";
        if(!await ConfirmAsync("确认升级",detail))return;
        _operationCts?.Cancel();_operationCts?.Dispose();
        var operation=new CancellationTokenSource(TimeSpan.FromSeconds(Math.Clamp(90+image.ImageSize/200d,360,86400)));
        _operationCts=operation;CancellationToken token=operation.Token;bool locked=false;
        SetBusy(true);RunOnUiThread(()=>Window?.AddFlags(WindowManagerFlags.KeepScreenOn));
        try {
            await _operationGate.WaitAsync(token);locked=true;
            CheckUpgradeEndpoint(mac,false);
            await DisposeConnectionCoreAsync();
            UpgradeStatus("正在连接设备…");
            SerialUpgradeReadback verified;
            await using(var transport=new AndroidBmsBleTransport(this,mac)) {
                transport.ConnectionProgress+=AppendLog;await transport.ConnectAsync(token);
                var device=await UpgradeDetection.WaitForYmodemAsync(transport,token,package);
                UpgradeDetection.CheckImage(device,image);
                var sender=new SerialYmodemClient(transport,chunkForBle:true);
                sender.Log+=AppendLog;
                sender.Progress+=progress=>{
                    RunOnUiThread(()=>{if(_upgradeProgress is not null)_upgradeProgress.Progress=(int)progress.Percent;});
                    UpgradeStatus($"{progress.Stage}\n{progress.Percent:F1}% · {progress.SentBytes}/{progress.TotalBytes} bytes");
                };
                await sender.UpgradeAsync(image,device,token);
                verified=sender.Verified??throw new IOException("未获得APP确认，不能判定升级成功。");
            }
            string result=$"升级成功\n版本 {verified.Version:X8}";
            if(AppEdition.IsInternal) result+=$"\n板号 {verified.Board:X8} · 长度 {verified.PayloadLength} · CRC {verified.PayloadCrc:X8}";
            if(!string.IsNullOrWhiteSpace(expectedSerial)&&!string.Equals(verified.Identity?.Serial,expectedSerial,StringComparison.Ordinal))
                throw new IOException(result+"\n额外SN核对未通过，请核对设备身份；不要直接重复写入。");
            AppendLog($"TEST_RESULT YMODEM_OK board={verified.Board:X8} version={verified.Version:X8} bytes={verified.PayloadLength} crc32={verified.PayloadCrc:X8} mac={mac}");
            UpgradeStatus(result);
            // Upgrade confirmation does not depend on legacy business views.
            try {await ConnectSelectedCoreAfterOtaAsync(mac,token);UpgradeStatus(result+"\n实时连接已恢复。");}
            catch(Exception ex)when(ex is not OperationCanceledException){await DisposeConnectionCoreAsync();UpgradeStatus(result+"\n实时视图暂未恢复，可在设备页重新连接。");AppendLog("POST_YMODEM_VIEW "+ex.Message);}
        } catch(OperationCanceledException) {
            UpgradeStatus("升级已取消或等待超时；最终提交若已尝试，结果待确认，请先重连核对。");AppendLog("TEST_RESULT YMODEM_CANCELLED");
        } catch(Exception ex) {
            UpgradeStatus("升级未完成全部确认："+ex.Message,true);AppendLog("TEST_RESULT YMODEM_FAIL "+ex.Message);
        } finally {
            if(locked)_operationGate.Release();
            if(ReferenceEquals(_operationCts,operation))_operationCts=null;
            operation.Dispose();SetBusy(false);RunOnUiThread(()=>Window?.ClearFlags(WindowManagerFlags.KeepScreenOn));
        }
    }
}
