using System.IO;
namespace BmsTool.Windows;

public enum SerialUpgradeProtocol { D015Ymodem, D015Legacy, LegacyManual }
public sealed record SerialUpgradeDevice(SerialUpgradeProtocol Protocol, uint Board, uint Address, int Capacity, ushort Mode, string Evidence);

public static class UpgradeDetection
{
    public static SerialUpgradeDevice? ParseCapabilities(ushort[] words)
    {
        if (words.Length != 12 || words[0] != 0x5550) return null;
        uint board=((uint)words[2]<<16)|words[3], address=((uint)words[6]<<16)|words[7];
        uint capacity=((uint)words[8]<<16)|words[9];
        if(words[1]!=1 || words[4]!=2 || words[5]!=2 || words[10]>3 || words[11]!=0 ||
           board!=D015FirmwarePackage.BoardId || address!=D015FirmwarePackage.AppAddress || capacity!=D015FirmwarePackage.Capacity)
            throw new InvalidDataException("设备已公布升级能力，但此上位机不支持该板型、协议版本或分区；不会尝试其他升级命令。");
        return new(SerialUpgradeProtocol.D015Ymodem,board,address,(int)capacity,words[10],"D170 capability v1 / YMODEM CRC16 / package v2");
    }
    public static async Task<SerialUpgradeDevice> ReadAsync(IBmsTransport transport, bool allowLegacyManual, CancellationToken ct)
    {
        await using var client=new BmsClient(transport);
        ushort[]? words=null;
        try { words=await client.ReadRegistersAsync(0xD170,12,ct); }
        catch(BmsModbusException ex) when(ex.Code is 1 or 2) { }
        catch(TimeoutException) { }
        if(words is not null && ParseCapabilities(words) is { } advertised) return advertised;
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
        if(allowLegacyManual) return new(SerialUpgradeProtocol.LegacyManual,0,0x08001C00,55*1024,0,"用户明确选择旧 STM32 页协议；设备未提供可验证板型");
        throw new IOException("无法可靠识别升级协议。新 D015 中断后请等待约 30 秒再重试；旧工程须明确选择已确认的旧 STM32 协议。不会轮流发送升级命令。");
    }
    // Keep the selected endpoint open to catch the 500 ms power-on window.
    public static async Task<SerialUpgradeDevice> WaitForD015Async(IBmsTransport transport, CancellationToken ct)
    {
        await using var client=new BmsClient(transport);
        using var deadline=CancellationTokenSource.CreateLinkedTokenSource(ct);
        deadline.CancelAfter(35000);
        var token=deadline.Token;
        try {
            while(true) {
                try {
                    var words=await client.ReadRegistersAsync(0xD170,12,token,TimeSpan.FromMilliseconds(250));
                    return ParseCapabilities(words) ?? throw new InvalidDataException("设备没有公布新 D015 升级能力；拒绝发送升级命令。");
                } catch(InvalidDataException) { throw; }
                  catch(TimeoutException) { }
                  catch(IOException ex) when(ex is not BmsModbusException) {
                    // Retry only the same endpoint and only read-only discovery.
                    try { await transport.ReconnectAsync(token); }
                    catch(IOException) { }
                    await Task.Delay(250,token);
                }
                await Task.Delay(30,token);
            }
        } catch(OperationCanceledException) when(!ct.IsCancellationRequested) {
            throw new TimeoutException("未捕获 D015 APP/Boot 能力。保持所选串口连接，重新开始后给设备上电；检查波特率和接线。不会尝试旧升级命令。");
        }
    }
    public static void CheckImage(SerialUpgradeDevice device,FirmwareImage image)
    {
        if(device.Protocol==SerialUpgradeProtocol.D015Ymodem) {
            if(!image.IsD015Package) throw new InvalidDataException("此设备使用新 D015 Boot，请选择 .d015 升级包；旧 APP BIN 不能混用。");
            _=D015FirmwarePackage.Parse(image.Bytes); return;
        }
        if(image.IsD015Package) throw new InvalidDataException("设备仍使用旧 Boot，新 .d015 包不能通过旧升级协议迁移；首次须安装配套新 Boot 和 APP。");
        if(device.Protocol==SerialUpgradeProtocol.D015Legacy) image.ValidateD015Image();
        if(image.ImageSize>device.Capacity) throw new InvalidDataException("固件超出识别到的设备 APP 容量。");
        if(device.Protocol==SerialUpgradeProtocol.LegacyManual && image.HasD015Identity)
            throw new InvalidDataException("D015 镜像要求核对 D015 身份，不允许按未识别的通用 STM32 发送。");
    }
}
