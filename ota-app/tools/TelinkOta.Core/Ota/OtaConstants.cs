namespace TelinkOta.Core.Ota;

/// <summary>
/// Telink B85m BLE OTA 协议常量。
/// 全部依据 OTA_PROTOCOL_FACTS.md（SDK 源码 + 官方 OTA App 2.1.2 反汇编 + 实测 BIN）。
/// </summary>
public static class OtaConstants
{
    // ---- Opcode（wire 上 2 字节小端）----
    public const ushort CmdOtaVersion = 0xFF00;      // Legacy C->S
    public const ushort CmdOtaStart = 0xFF01;        // Legacy C->S
    public const ushort CmdOtaEnd = 0xFF02;          // 全部 C->S
    public const ushort CmdOtaStartExt = 0xFF03;     // Extend C->S
    public const ushort CmdOtaFwVersionReq = 0xFF04; // Extend C->S
    public const ushort CmdOtaFwVersionRsp = 0xFF05; // Extend S->C
    public const ushort CmdOtaResult = 0xFF06;       // 全部 S->C
    public const ushort CmdOtaSchedulePduNum = 0xFF08; // S->C
    public const ushort CmdOtaScheduleFwSize = 0xFF09; // S->C
    public const ushort CmdOtaSetFwIndex = 0xFF80;   // C->S（官方 App 支持）

    // ---- Firmware BIN 头部 ----
    public const int FwSizeOffset = 0x18;        // u32 LE（本 SDK 后处理格式：= 文件总长，含尾部 CRC32）
    public const int FwMarkOffset = 0x08;        // 4 Byte "TLNK" = 0x544C4E4B
    public const int FwVersionOffset = 0x02;     // u16 LE（版本协商用的 bin 版本）
    public const uint FirmwareMark = 0x544C4E4B; // "TLNK"
    public const int MinFirmwareHeader = 0x20;
    public const int Crc32TailLength = 4;

    // ---- PDU ----
    public const int PduMin = 16;
    public const int PduMax = 240;
    public const int PduStep = 16;
    public const int PduOverhead = 4;  // Index(2) + CRC16(2)
    public const int AttOverhead = 3;  // ATT write 头（opcode+handle），写入总长 = PDU + 4 + 3 <= MTU

    // ---- UUID（官方 App UuidInfo 与 uuid.h 一致）----
    public static readonly Guid OtaServiceUuid = new("00010203-0405-0607-0809-0a0b0c0d1912");
    public static readonly Guid OtaCharacteristicUuid = new("00010203-0405-0607-0809-0a0b0c0d2b12");

    // 业务 SPP 服务（用于升级后版本复核：Modbus RTU over BLE）
    public static readonly Guid SppServiceUuid = new("6e400001-b5a3-f393-e0a9-e50e24dcca9e");
    public static readonly Guid SppWriteUuid = new("6e400002-b5a3-f393-e0a9-e50e24dcca9e");
    public static readonly Guid SppNotifyUuid = new("6e400003-b5a3-f393-e0a9-e50e24dcca9e");

    // ---- 设备端超时（app_config.h:62-63，本工程）----
    public static readonly TimeSpan DeviceProcessTimeout = TimeSpan.FromSeconds(180);
    public static readonly TimeSpan DevicePacketTimeout = TimeSpan.FromSeconds(15);

    // ---- 分区限制（本工程：512K Flash，OTA 区 0x20000，最大 124K）----
    public const uint MaxFirmwareSizeBytes = 124 * 1024; // 0x1F000
}
