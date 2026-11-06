using Android.App;
using Android.Content;

namespace BmsTool.Android;

public sealed partial class MainActivity
{
    private const int QrScanRequest=1004;
    private bool _scanAfterBluetoothPermission;
    private Task StartQrConnectAsync()
    {
        if(_uiBusy)return Task.CompletedTask;
        if(!HasBluetoothPermissions()) {
            _scanAfterBluetoothPermission=true;RequestBluetoothPermissions();return Task.CompletedTask;
        }
        StartActivityForResult(new Intent(this,typeof(QrScanActivity)),QrScanRequest);
        return Task.CompletedTask;
    }
    private async Task ConnectQrResultAsync(Intent? data)
    {
        if(_uiBusy){SetStatus("当前操作未完成，请稍后重新扫码",true);return;}
        if(!BluetoothMacCode.TryParse(data?.GetStringExtra(QrScanActivity.MacExtra),out string mac)) {
            SetStatus("扫码内容不是有效蓝牙MAC地址",true);return;
        }
        _macInput!.Text=mac;
        AppendLog("QR_CONNECT target="+mac);
        await ConnectSelectedAsync();
    }
}
