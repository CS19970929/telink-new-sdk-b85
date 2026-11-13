using System.IO;

namespace BmsTool.Windows;

public sealed partial class BmsClient : IC11ValidationDevice
{
    public async Task<byte[]> C11ExchangeAsync(byte command, byte[] payload, CancellationToken ct)
    {
        if (payload.Length > 15)
            throw new ArgumentOutOfRangeException(nameof(payload));
        byte[] body = new byte[3 + payload.Length];
        body[0] = 1;
        body[1] = 0x41;
        body[2] = command;
        payload.CopyTo(body, 3);
        byte[] response = await TransactAsync(ModbusRtu.Frame(body), ct, TimeSpan.FromSeconds(2));
        if (response.Length < 6 || response[1] != 0x41 || response[2] != command)
            throw new IOException($"C11 命令 0x{command:X2} 响应格式不匹配。");
        if (response[3] != 0)
        {
            string reason = response[3] switch {
                1 => "请求参数或长度不合法",
                2 => "会话令牌或通信通道不匹配，或会话已结束",
                3 when command is 0x22 or 0x23 or 0x24 or 0x25 => "当前固件不支持此操作，请更新配套 controlled 固件",
                3 => "当前固件未开放此功能",
                4 when command == 0x14 => "MOS 联动前置条件不满足：检查真实保护、NTC/采样诊断、安全保持、AFE 状态及真实电流（须在 ±0.5 A 内）",
                4 when command == 1 => "已有注入会话占用设备",
                5 => "注入事务序号或暂存状态不匹配，本次提交未完成；请清除注入并确认配套固件",
                _ => "设备拒绝执行，请读取状态确认原因"
            };
            throw new IOException(
                $"C11 命令 0x{command:X2}：{reason}，status={response[3]}。");
        }
        if (command >= 0x10)
        {
            if (response.Length != response[4] + 7)
                throw new IOException("C11 扩展响应长度不匹配。");
            byte[] data = response.AsSpan(5, response[4]).ToArray();
            if (command == 0x10)
            {
                _ = C11Capabilities.Parse(data);
                _faultInjectionProbeEnabled = true;
            }
            return data;
        }
        return response.AsSpan(4, response.Length - 6).ToArray();
    }
}
