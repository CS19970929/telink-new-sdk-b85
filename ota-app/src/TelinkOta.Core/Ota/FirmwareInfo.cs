namespace TelinkOta.Core.Ota;

/// <summary>
/// 固件文件预检查失败原因（用户可操作建议见 <see cref="ErrorMapper"/>）。
/// </summary>
public enum FirmwareCheckCode
{
    FileNotFound,
    FileTooSmall,
    DeclaredSizeZero,
    DeclaredSizeExceedsFile,
    DeclaredSizeTooBig,          // 超过目标分区最大尺寸
    FileTooBig,                  // 文件超过分区边界（最大尺寸 + CRC 尾）
    MarkMissing,                 // 偏移 0x08 非 "TLNK"
    TailCrcMismatch,             // 已带尾部 CRC 但与内容不符
    UnexpectedTail,              // 文件比声明尺寸多出且非 4 字节 CRC
    Crc32WasAppended,            // 信息级：自动补齐了 CRC32
}

/// <summary>
/// 解析并校验后的固件。Payload 为"实际要发送的字节"（必要时已自动附加 CRC32 尾部）。
/// </summary>
public sealed class OtaFirmware
{
    public required string SourcePath { get; init; }
    public required byte[] Payload { get; init; }
    public required uint DeclaredSize { get; init; }
    public required ushort BinVersion { get; init; }
    public required bool MarkValid { get; init; }
    public required bool CrcWasAppended { get; init; }
    public required bool CrcVerified { get; init; }
    public string? SdkVersion { get; init; }

    /// <summary>内容 SHA-256（十六进制小写），用于日志与（可选）Manifest 校验。</summary>
    public string Sha256Hex { get; init; } = "";
}
