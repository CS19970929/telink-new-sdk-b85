using BmsTool.Windows;
using System.Globalization;

if (args.Length!=2) throw new ArgumentException("需要台架板 MAC 和已校验的通信测试镜像");
ulong mac=ulong.Parse(args[0].Replace(":",""),NumberStyles.HexNumber);
var firmware=FirmwareImage.LoadForTarget(args[1],OtaTargetKind.Telink);
if (!firmware.HasD008TlnkStartupMarker || firmware.ImageSize>124*1024) throw new InvalidDataException("D008 镜像不符");
using var timeout=new CancellationTokenSource(TimeSpan.FromMinutes(2));
var ct=timeout.Token;
async Task<string> Identity()
{
    var ble=new BmsBleTransport();
    await using var client=new BmsClient(ble);
    await ble.ConnectAsync(mac,ct);
    var identity=await client.ReadIdentityAsync(ct);
    var power=await client.ReadRegistersAsync(0x2f10,8,ct);
    if (power[2]!=0 || power[3]!=0 || power[4]!=0 || power[5]!=1 || identity.Software!="D008-FLASH-TEST")
        throw new InvalidOperationException("目标台架固件、AFE 或功率状态不符");
    return identity.Software;
}
Console.WriteLine("传输前 "+await Identity());
await using (var ota=new OtaBleTransport()) {
    await ota.ConnectAsync(mac,ct);
    await ota.WriteAsync(TelinkOtaProtocol.BuildLegacyStart(),ct);
    await Task.Delay(400,ct);
    for (int i=0;i<32;++i) {
        await ota.WriteAsync(TelinkOtaProtocol.BuildData(firmware.Bytes,i,16,false),ct);
        await Task.Delay(12,ct);
    }
    // 关闭 GATT 会话，模拟上位机在正常传输期间退出。
    Console.WriteLine("已发 32 个标准包，共 512 字节，关闭连接");
}
await Task.Delay(TimeSpan.FromSeconds(21),ct);
Console.WriteLine("中断后 "+await Identity());
