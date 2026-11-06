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
    private View BuildFirmwarePage()
    {
        var page=Page();
        page.AddView(ActionButton("‹ 返回设备",()=>{ShowPage(DevicePage);return Task.CompletedTask;},false));
        page.AddView(SectionTitle("蓝牙固件升级"));
        page.AddView(Note("选择目标设备和固件。C060、D015及同一合同的新项目自动核对身份；APP无法连接时，也可指定原蓝牙模块直接识别Boot。"));
        page.AddView(Note("升级期间保持页面在前台与稳定供电；中断后可从头重新传输。"));
        page.AddView(ActionButton("从文件选择固件",PickFirmware,false));
        page.AddView(ActionButton("刷新固件收件箱",RefreshFirmwareInboxAsync,false));
        _firmwareInboxStatus=DataBlock("固件收件箱为空");page.AddView(_firmwareInboxStatus);
        _firmwareInbox=Page();page.AddView(_firmwareInbox);
        _firmwareInput=Input("固件路径（可通过文件选择器导入）","");page.AddView(_firmwareInput);
        _expectedSerialInput=Input("升级后期望SN（可选）","");page.AddView(_expectedSerialInput);
        _firmwareStatus=DataBlock("尚未选择固件");page.AddView(_firmwareStatus);
        _upgradeProgress=new ProgressBar(this,null,global::Android.Resource.Attribute.ProgressBarStyleHorizontal){Max=100};page.AddView(_upgradeProgress);
        _otaButton=ActionButton("开始升级并验证",StartOtaAsync,false);page.AddView(_otaButton);
        var cancel=ActionButton("取消升级 / 当前操作",()=>{CancelCurrentOperation();return Task.CompletedTask;},false);
        _busyAllowedButtons.Add(cancel);page.AddView(cancel);
        page.AddView(Note("请在设备页选择或填写明确的MAC；不会自动选第一台。协议或分区不匹配时不发送固件。"));
        return Wrap(page);
    }
    private void UpgradeStatus(string text,bool error=false)
    {
        SetStatus(text,error);RunOnUiThread(()=>{if(_firmwareStatus is not null)_firmwareStatus.Text=text;});
    }
    private void CheckUpgradeEndpoint(string mac,bool requireExisting)
    {
        if(!Regex.IsMatch(mac,"^([0-9A-F]{2}:){5}[0-9A-F]{2}$"))throw new ArgumentException("请扫描或填写有效的蓝牙MAC地址。");
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
        string detail=$"目标 {mac}\n{package.BoardName} · 板号 {package.BoardId:X8}\nAPP {package.AppAddress:X8} · 版本 {package.Version:X8}\n载荷 {package.PayloadLength} bytes · CRC {package.PayloadCrc:X8}\n\n核对后开始，期间请保持页面前台与稳定供电。";
        if(!await ConfirmAsync("确认YMODEM升级",detail))return;
        _operationCts?.Cancel();_operationCts?.Dispose();
        var operation=new CancellationTokenSource(TimeSpan.FromSeconds(Math.Clamp(90+image.ImageSize/200d,360,86400)));
        _operationCts=operation;CancellationToken token=operation.Token;bool locked=false;
        SetBusy(true);RunOnUiThread(()=>Window?.AddFlags(WindowManagerFlags.KeepScreenOn));
        try {
            await _operationGate.WaitAsync(token);locked=true;
            CheckUpgradeEndpoint(mac,false);
            await DisposeConnectionCoreAsync();
            UpgradeStatus("正在识别所选蓝牙模块的APP/Boot…");
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
            string result=$"升级已确认\n板号 {verified.Board:X8} · 版本 {verified.Version:X8}\n长度 {verified.PayloadLength} · CRC {verified.PayloadCrc:X8}";
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
