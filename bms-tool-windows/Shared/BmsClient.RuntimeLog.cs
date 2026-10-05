// 设备运行日志协议：复用 BmsClient 的串行事务锁，串口和 BLE 共用同一解析器。
using System.IO;

namespace BmsTool.Windows;

public sealed record RuntimeLogCursor(uint Next, uint BootTick, uint BuildId);
public sealed record RuntimeLogEntry(uint Sequence, uint Tick32k, int Level, int Module,
    ushort Event, uint Arg0, uint Arg1)
{
    public string Severity => Level switch { 1 => "ERROR", 2 => "WARN", 3 => "INFO", 4 => "DEBUG", _ => "?" };
    public string ModuleName => Module is >= 0 and < 9 ? ModuleNames[Module] : "未知";
    public static readonly string[] ModuleNames = { "系统", "AFE/采样", "保护/MOS", "SOC", "通信", "低功耗", "存储", "均衡/功能", "OTA" };
    public string DeviceTime => $"{Tick32k / 32000.0:F3} s (32K原始计时)";
    public string Detail => Event switch {
        1 => "启动：RAM 日志会话开始",
        2 => $"存储初始化：domain={Arg0}，result={Arg1} (1=OK, 6=默认值, 14=开始)",
        3 => $"存储失败/延期：reason={Arg0} (8=写校验,9=擦除校验,10=OTA,11=锁,12=退避)，address=0x{Arg1:X8}",
        4 => $"参数状态位=0x{Arg0:X8} (bit0=有效，bit1=升级中)",
        5 => $"MOS 请求=0x{Arg0 >> 16:X}，CHG阻断=[{Reasons(Arg0 & 65535)}]，DSG阻断=[{Reasons(Arg1)}]；不代表物理 Gate",
        6 => $"AFE MOS 命令=0x{Arg0:X}，命令有效={Arg1}",
        7 => $"启动完成：boot[24]={Arg0}，boot[25]={Arg1}；详见启动诊断",
        8 => $"MOS 驱动缓存位=0x{Arg0:X}，有效={Arg1}",
        9 => $"启动参数更新：stage={Arg0}，invalid_mask=0x{Arg1:X}",
        10 => $"电流资格恢复：state={Arg0}，detail=0x{Arg1:X8}",
        11 => $"电源策略：允许suspend={Arg0 & 255}，低压区={(Arg0 >> 8) & 255}，BLE={(Arg0 >> 16) & 1}，阻断=0x{Arg1:X8}",
        12 => $"保护位图：First=0x{Arg0 & 0xFFFF:X4}，Second=0x{Arg0 >> 16:X4}，Third=0x{Arg1:X4}",
        13 => $"采样资格变化：valid={Arg0}，current={unchecked((int)Arg1)} mA（正放电、负充电）",
        32 => $"采样：valid={Arg0}，current={unchecked((int)Arg1)} mA",
        33 => $"SOC 决策：action={Arg0 >> 24}，before={(Arg0 >> 16) & 255}% → after={(Arg0 >> 8) & 255}%，target={Arg0 & 255}%，detail={Arg1 >> 16}，rest={Arg1 & 65535} s",
        34 => $"SOC 积分：间隔={Arg0} ticks32k，增量={Arg1} As×10",
        35 or 36 => $"{(Event == 35 ? "UART" : "BLE")} 帧：接收={Arg0 >> 16} B，function=0x{Arg0 & 65535:X2}，响应={Arg1} B (0=无响应)",
        37 => $"CRC 拒绝：长度={Arg0} B，计算=0x{Arg1 >> 16:X4}，接收=0x{Arg1 & 65535:X4}",
        38 => $"UART 非法长度：收到={Arg0} B，容量={Arg1} B",
        39 => $"UART 连续坏帧恢复：count={Arg0}",
        40 => $"BLE Notify 失败：status={Arg0}，已提交偏移={Arg1} B",
        41 => $"BLE 连接状态={Arg0} (0=断开，1=连接)",
        42 => $"Deep sleep 尝试：需要 AFE 低功耗={Arg0}，模式={(Arg1 == 1 ? "D008 shutdown" : "SH sleep")}",
        43 => $"Deep sleep 中止：{(Arg0 == 1 ? (Arg1 == 1 ? "AFE shutdown 失败" : "AFE sleep 失败") : "唤醒 GPIO 已有效")}",
        44 => $"Deep sleep 调用返回：status=0x{Arg0:X8}；成功 reset 后旧 RAM 不保留",
        45 => $"OTA 工作状态={Arg0}",
        46 => $"均衡请求 mask：0x{Arg0:X8} → 0x{Arg1:X8}",
        47 => $"均衡应用失败：desired=0x{Arg0:X8}，result=0x{Arg1:X8}",
        48 => $"保护 Level {Arg0}：位图 0x{Arg1 >> 16:X4} → 0x{Arg1 & 65535:X4}",
        49 => $"Modbus 异常：function=0x{Arg0:X2}，exception=0x{Arg1:X2}",
        50 => $"Cell min={Arg0 >> 16} mV (位置 {Arg1 >> 16})，max={Arg0 & 65535} mV (位置 {Arg1 & 65535})；有效性参见采样日志",
        51 => $"SOC 状态：估计={Arg0 >> 16}%，显示={Arg0 & 65535}%，OCV state={Arg1}",
        52 => $"MOS 阻断原因：CHG=0x{Arg0:X8} [{Reasons(Arg0)}]，DSG=0x{Arg1:X8} [{Reasons(Arg1)}]",
        53 => $"AFE guard=0x{Arg0 >> 16:X4}，backend=0x{Arg0 & 65535:X4}，FLAG1=0x{Arg1 >> 16:X4}，FLAG2=0x{Arg1 & 65535:X4}",
        54 => $"Suspend 退出：本周期 {Arg0} 次，累计 {Arg1} 次 (u32回绕)",
        55 => $"SH AFE 单次事务失败：status={Arg0}，之前连续最终失败={Arg1}",
        56 => $"SH AFE 重试耗尽：status={Arg0}，连续最终失败={Arg1}",
        57 => $"SH AFE 事务恢复：之前连续失败={Arg0}",
        58 => $"UART TX busy 拒绝：长度={Arg0} B，累计拒绝={Arg1}",
        _ => $"event={Event}，arg0=0x{Arg0:X8}，arg1=0x{Arg1:X8}"
    };
    private static string Reasons(uint bits) {
        string[] names = { "参数", "升级", "输出未授权", "通信", "软件保护", "硬件保护", "Open-Wire", "加热", "关机", "温度", "backend" };
        return bits == 0 ? "无" : string.Join("/", names.Where((_, i) => (bits & (1u << i)) != 0));
    }
    public string ExportLine => $"{Sequence}\t{Tick32k}\t{Severity}\t{ModuleName}\t{Event}\t0x{Arg0:X8}\t0x{Arg1:X8}\t{Detail}";
}

public sealed record RuntimeLogBatch(bool Supported, bool Enabled, RuntimeLogCursor? Cursor,
    uint Overwritten, uint Missed, bool Reset, int DeviceLevel, ushort ModuleMask,
    IReadOnlyList<RuntimeLogEntry> Entries);

public sealed partial class BmsClient
{
    public async Task<RuntimeLogBatch> ReadRuntimeLogsAsync(RuntimeLogCursor? cursor,
        CancellationToken ct = default)
    {
        const ushort logBase = 0x3000, capacity = 64, wordsPerRecord = 10;
        static uint U32(ushort[] w, int i) => ((uint)w[i] << 16) | w[i + 1];
        ushort[] h;
        try { h = await ReadRegistersAsync(logBase, 16, ct); }
        catch (BmsModbusException ex) when (ex.Function == 3 && ex.Code == 2) {
            return new(false, false, null, 0, 0, false, 0, 0, Array.Empty<RuntimeLogEntry>());
        }
        if (h[0] != 0x4C47 || h[1] != 1)
            return new(false, false, null, 0, 0, false, 0, 0, Array.Empty<RuntimeLogEntry>());
        if (h[2] > 1 || h[3] != capacity || h[4] != wordsPerRecord || h[5] > capacity || h[14] > 4)
            throw new InvalidDataException("运行日志能力窗口非法。");
        var next = new RuntimeLogCursor(U32(h, 6), U32(h, 10), U32(h, 12));
        if (h[2] == 0)
            return new(true, false, next, 0, 0, false, 0, 0, Array.Empty<RuntimeLogEntry>());
        uint available = h[5], missed = 0;
        bool reset = cursor is not null && (cursor.BootTick != next.BootTick || cursor.BuildId != next.BuildId ||
            unchecked(next.Next - cursor.Next) > 0x80000000u);
        uint pending = cursor is null || reset ? available : unchecked(next.Next - cursor.Next);
        if (pending > available) { missed = pending - available; pending = available; }
        uint sequence = unchecked(next.Next - pending);
        var entries = new List<RuntimeLogEntry>();
        while (pending != 0) {
            ct.ThrowIfCancellationRequested();
            uint slot = sequence % capacity;
            // 4 records = 85 RTU bytes; bounded burst fits the existing BLE fragmentation path.
            uint take = Math.Min(pending, Math.Min(4u, capacity - slot));
            ushort[] words = await ReadRegistersAsync((ushort)(logBase + 16 + slot * wordsPerRecord),
                (ushort)(take * wordsPerRecord), ct);
            for (int i = 0; i < take; i++) {
                int at = i * wordsPerRecord;
                // The firmware can overwrite a slot between requests. Never show it as the expected event.
                if (U32(words, at) != sequence) missed++;
                else {
                    int level = words[at + 4] >> 8, module = words[at + 4] & 255;
                    if (level is < 1 or > 4 || module > 8) throw new InvalidDataException("运行日志记录格式非法。");
                    entries.Add(new(sequence, U32(words, at + 2), level, module,
                        words[at + 5], U32(words, at + 6), U32(words, at + 8)));
                }
                sequence = unchecked(sequence + 1);
            }
            pending -= take;
        }
        ushort[] end = await ReadRegistersAsync(logBase, 16, ct);
        if (end[0] != h[0] || end[1] != h[1] || end[2] != h[2] ||
            U32(end, 10) != next.BootTick || U32(end, 12) != next.BuildId ||
            unchecked(U32(end, 6) - next.Next) > 0x80000000u)
            throw new IOException("采集期间设备重启或日志会话改变，请重新读取。");
        return new(true, true, next, U32(h, 8), missed, reset, h[14], h[15], entries);
    }
}
