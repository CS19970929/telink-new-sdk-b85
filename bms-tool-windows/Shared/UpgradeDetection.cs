using System.IO;
namespace BmsTool.Windows;

public enum SerialUpgradeProtocol { YmodemV2, D015Legacy, LegacyManual }
public sealed record SerialUpgradeDevice(SerialUpgradeProtocol Protocol,uint Board,uint Address,int Capacity,ushort Mode,string Evidence);

public static class UpgradeDetection
{
    // C060 board evidence: the APP releases its UART reply buffer after physical
    // TC in the main loop. Preserve at least 50 ms even across client instances.
    public static readonly TimeSpan ResponseTurnaround=TimeSpan.FromMilliseconds(50);
    public static SerialUpgradeDevice? ParseCapabilities(ushort[] words) {
        if(words.Length!=12 || words[0]!=0x5550)return null;
        uint board=((uint)words[2]<<16)|words[3],address=((uint)words[6]<<16)|words[7];
        uint capacity=((uint)words[8]<<16)|words[9];
        if(words[1]!=1 || words[4]!=2 || words[5]!=2 || words[10]>3 || words[11]!=0 ||
           board is 0 or uint.MaxValue || !SerialFirmwarePackage.ValidLayout(address,capacity))
            throw new InvalidDataException("设备公布的升级协议版本、模式或分区不受支持；不会尝试其他升级命令。");
        return new(SerialUpgradeProtocol.YmodemV2,board,address,(int)capacity,words[10],
            $"D170 v1 / YMODEM CRC16 / package v2 / board={board:X8} / APP={address:X8} / capacity={capacity}");
    }
    public static async Task<SerialUpgradeDevice> ReadAsync(IBmsTransport transport,bool allowLegacyManual,CancellationToken ct) {
        await Task.Delay(ResponseTurnaround,ct);
        await using var client=new BmsClient(transport,responseTurnaround:ResponseTurnaround);
        ushort[]? words=null;
        try { words=await client.ReadRegistersAsync(0xD170,12,ct); }
        catch(BmsModbusException ex) when(ex.Code is 1 or 2) { }
        catch(TimeoutException) { }
        if(words is not null && ParseCapabilities(words) is {} advertised)return advertised;
        words=null;
        try { words=await client.ReadRegistersAsync(0xD130,4,ct); }
        catch(BmsModbusException ex) when(ex.Code is 1 or 2) { }
        catch(TimeoutException) { }
        if(words is { Length:4 } && words[0]==0xD015 && words[1]==0x0320) {
            uint version=((uint)words[2]<<16)|words[3];
            if(version>=0x00020000 && version<0x00030000)
                return new(SerialUpgradeProtocol.D015Legacy,D015FirmwarePackage.BoardId,0x08001C00,47*1024,0,"D130 board identity / APP 2.x legacy page protocol");
            throw new IOException("识别到 D015，但未得到匹配的升级能力信息；请等待 Boot 恢复或检查固件版本。");
        }
        if(allowLegacyManual)return new(SerialUpgradeProtocol.LegacyManual,0,0x08001C00,55*1024,0,"用户明确选择旧 STM32 页协议；设备未提供可验证板型");
        throw new IOException("无法可靠识别升级协议。使用统一升级包时可通过串口恢复识别 Boot；旧工程须明确选择已确认的旧 STM32 协议。");
    }
    // Keep the selected endpoint open to catch the 500 ms power-on window.
    public static async Task<SerialUpgradeDevice> WaitForYmodemAsync(IBmsTransport transport,CancellationToken ct,SerialFirmwarePackage? package=null) {
        await Task.Delay(ResponseTurnaround,ct);
        await using var client=new BmsClient(transport,responseTurnaround:ResponseTurnaround);
        using var deadline=CancellationTokenSource.CreateLinkedTokenSource(ct);deadline.CancelAfter(35000);
        var token=deadline.Token;
        try {
            while(true) {
                try {
                    var words=await client.ReadRegistersAsync(0xD170,12,token,TimeSpan.FromMilliseconds(250));
                    var device=ParseCapabilities(words) ?? throw new InvalidDataException("设备没有公布统一 YMODEM 升级能力；拒绝发送升级命令。");
                    if(package is not null)CheckPackage(device,package);
                    return device;
                } catch(InvalidDataException) { throw; }
                  catch(TimeoutException) { }
                  catch(IOException ex) when(ex is not BmsModbusException) {
                    try { await transport.ReconnectAsync(token); } catch(IOException) { }
                    await Task.Delay(250,token);
                }
                await Task.Delay(30,token);
            }
        } catch(OperationCanceledException) when(!ct.IsCancellationRequested) {
            throw new TimeoutException("未捕获 APP/Boot 的 YMODEM 能力。保持所选串口或蓝牙模块连接，重新开始后给设备上电；检查波特率和接线。");
        }
    }
    public static void CheckPackage(SerialUpgradeDevice device,SerialFirmwarePackage package) {
        if(device.Protocol!=SerialUpgradeProtocol.YmodemV2 || !SerialFirmwarePackage.ValidLayout(device.Address,unchecked((uint)device.Capacity)) || device.Board!=package.BoardId ||
           device.Address!=package.AppAddress || package.PayloadLength>device.Capacity)
            throw new InvalidDataException($"升级包与所选设备不匹配：包 board={package.BoardId:X8} APP={package.AppAddress:X8} bytes={package.PayloadLength}；设备 board={device.Board:X8} APP={device.Address:X8} capacity={device.Capacity}。");
    }
    public static void CheckImage(SerialUpgradeDevice device,FirmwareImage image) {
        if(device.Protocol==SerialUpgradeProtocol.YmodemV2) {
            if(!image.IsSerialPackage)throw new InvalidDataException("此设备使用统一 YMODEM Boot，请选择带身份和 CRC 的升级包；旧 APP BIN 不能混用。");
            CheckPackage(device,SerialFirmwarePackage.Parse(image.Bytes));return;
        }
        if(image.IsSerialPackage)throw new InvalidDataException("设备仍使用旧 Boot，统一升级包不能通过旧页协议迁移；首次须安装配套 Boot 和 APP。");
        if(device.Protocol==SerialUpgradeProtocol.D015Legacy)image.ValidateD015Image();
        if(image.ImageSize>device.Capacity)throw new InvalidDataException("固件超出识别到的设备 APP 容量。");
        if(device.Protocol==SerialUpgradeProtocol.LegacyManual && image.HasD015Identity)
            throw new InvalidDataException("D015 镜像要求核对 D015 身份，不允许按未识别的通用 STM32 发送。");
    }
}
