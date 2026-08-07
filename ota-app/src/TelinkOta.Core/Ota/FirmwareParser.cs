using System.Security.Cryptography;

namespace TelinkOta.Core.Ota;

/// <summary>
/// Firmware BIN 解析与预检查。
/// 规则依据 OTA_PROTOCOL_FACTS.md 第 3 节（含 tl_check_fw2.exe 实机验证）：
///  - 本 SDK 后处理格式：文件 = 固件 + CRC32(4)，且 Size@0x18 = 文件总长（含 CRC）；
///  - Mark @0x08 = 0x544C4E4B("TLNK")；
///  - 尾部 4 字节为 Telink CRC32（init 0xFFFFFFFF，无 final xor，LE 存储），覆盖文件前 len-4 字节。
/// </summary>
public static class FirmwareParser
{
    /// <param name="autoAppendCrc32">文件未带合法尾部 CRC32 时是否自动补齐（默认 true，并回写 Size@0x18）。</param>
    /// <param name="requireMark">Mark 非法时是否直接失败（默认 true；false 时仅记录警告）。</param>
    /// <param name="maxFirmwareSize">目标设备分区最大固件尺寸（本工程 124K，按 Size@0x18 语义含 CRC 尾）。</param>
    public static ParseResult Parse(string path, uint maxFirmwareSize = OtaConstants.MaxFirmwareSizeBytes,
        bool autoAppendCrc32 = true, bool requireMark = true)
    {
        if (!File.Exists(path))
            return ParseResult.Fail(FirmwareCheckCode.FileNotFound, "文件不存在: " + path);

        byte[] file = File.ReadAllBytes(path);
        var warnings = new List<string>();
        var errors = new List<string>();

        if (file.Length < OtaConstants.MinFirmwareHeader)
            return ParseResult.Fail(FirmwareCheckCode.FileTooSmall,
                $"文件过小（{file.Length} B < {OtaConstants.MinFirmwareHeader} B），不是合法的 Telink BIN。");

        uint declared = BitConverter.ToUInt32(file, OtaConstants.FwSizeOffset);
        if (declared == 0)
            errors.Add("偏移 0x18 声明尺寸为 0。");

        bool markValid = file.Length >= OtaConstants.FwMarkOffset + 4 &&
                         BitConverter.ToUInt32(file, OtaConstants.FwMarkOffset) == OtaConstants.FirmwareMark;
        if (!markValid)
        {
            string markHex = file.Length >= OtaConstants.FwMarkOffset + 4
                ? Convert.ToHexString(file, OtaConstants.FwMarkOffset, 4)
                : "(截断)";
            if (requireMark)
                errors.Add($"Firmware Mark 非法：偏移 0x08 = {markHex}，期望 544C4E4B(\"TLNK\")。设备将返回 OTA_FIRMWARE_MARK_ERR(0x0A)。");
            else
                warnings.Add($"Firmware Mark 非法：偏移 0x08 = {markHex}，期望 544C4E4B(\"TLNK\")（已按警告继续）。");
        }

        // ---- 尾部 CRC32 检测（覆盖文件前 len-4 字节）----
        bool hasValidTailCrc = file.Length >= OtaConstants.Crc32TailLength + OtaConstants.MinFirmwareHeader &&
                               Crc32.Compute(file.AsSpan(0, file.Length - 4)) ==
                               BitConverter.ToUInt32(file, file.Length - 4);

        // ---- 语义判定：本 SDK 后处理格式要求 Size@0x18 == 文件总长（含 CRC）----
        bool sdkPostProcessed = declared == file.Length;

        if (errors.Count == 0)
        {
            if (hasValidTailCrc)
            {
                // 已带合法尾部 CRC32：原样发送（Size@0x18 应为 len 或 len-4）
                if (declared != file.Length && declared != file.Length - 4)
                    warnings.Add($"Size@0x18=0x{declared:X} 与文件长度 0x{file.Length:X} 不一致（期望 len 或 len-4），以设备读取为准。");
            }
            else if (sdkPostProcessed && declared == file.Length)
            {
                // 原始构建产物：Size=len、无尾部 CRC → 追加 CRC32 并回写 Size@0x18 = len+4（与 tl_check_fw2.exe 语义一致）
                if (autoAppendCrc32)
                {
                    warnings.Add($"BIN 未带尾部 CRC32（原始构建产物），已自动追加并回写 Size@0x18 = 0x{declared + 4:X}。");
                }
                else
                {
                    return ParseResult.Fail(FirmwareCheckCode.TailCrcMismatch,
                        "BIN 未带尾部 CRC32 且未启用自动补齐，设备将返回 OTA_FW_CHECK_ERR(0x07)。", warnings);
                }
            }
            else
            {
                // 长度/Size 关系无法可靠解释
                return ParseResult.Fail(FirmwareCheckCode.UnexpectedTail,
                    $"Size@0x18=0x{declared:X} 与文件长度 0x{file.Length:X} 不匹配，且尾部 CRC32 校验失败" +
                    (file.Length > declared + 4 ? $"（尾部多出 {file.Length - declared} 字节）。" : "。"), warnings);
            }
        }

        if (errors.Count > 0)
            return ParseResult.Fail(FirmwareCheckCode.DeclaredSizeZero, string.Join(" ", errors), warnings);

        // ---- 尺寸边界检查（按设备将收到的总字节数 = Size@0x18）----
        uint effectiveSize = hasValidTailCrc ? declared : declared + 4;
        if (effectiveSize > maxFirmwareSize)
            return ParseResult.Fail(FirmwareCheckCode.DeclaredSizeTooBig,
                $"声明尺寸 0x{effectiveSize:X}（{effectiveSize} B）超过目标分区上限 0x{maxFirmwareSize:X}（{maxFirmwareSize} B）。", warnings);
        if (effectiveSize < 0x40)
            return ParseResult.Fail(FirmwareCheckCode.DeclaredSizeZero,
                $"声明尺寸 0x{effectiveSize:X} 过小，不是合法的 Telink BIN。", warnings);

        // ---- 构造实际发送载荷 ----
        byte[] payload;
        bool crcWasAppended = false;
        bool crcVerified = hasValidTailCrc;
        if (hasValidTailCrc)
        {
            payload = file;
        }
        else
        {
            payload = new byte[file.Length + 4];
            file.CopyTo(payload, 0);
            // 先回写 Size@0x18 = 总长（与 tl_check_fw2.exe 后处理语义一致）
            BitConverter.TryWriteBytes(payload.AsSpan(OtaConstants.FwSizeOffset, 4), (uint)payload.Length);
            // CRC32 必须覆盖最终内容（含回写后的 Size 字段）
            uint crc = Crc32.Compute(payload.AsSpan(0, payload.Length - 4));
            BitConverter.TryWriteBytes(payload.AsSpan(payload.Length - 4, 4), crc);
            crcWasAppended = true;
        }

        ushort binVersion = file.Length >= OtaConstants.FwVersionOffset + 2
            ? BitConverter.ToUInt16(file, OtaConstants.FwVersionOffset)
            : (ushort)0;

        return ParseResult.Ok(new OtaFirmware
        {
            SourcePath = path,
            Payload = payload,
            DeclaredSize = effectiveSize,
            BinVersion = binVersion,
            MarkValid = markValid,
            CrcWasAppended = crcWasAppended,
            CrcVerified = crcVerified,
            SdkVersion = ExtractSdkVersion(file),
            Sha256Hex = Convert.ToHexString(SHA256.HashData(file)).ToLowerInvariant(),
        }, warnings);
    }

    /// <summary>提取 BIN 尾部的 $$$...$$$ SDK 版本串（存在时）。</summary>
    public static string? ExtractSdkVersion(byte[] file)
    {
        for (int i = 0; i + 3 <= file.Length; i++)
        {
            if (file[i] == '$' && file[i + 1] == '$' && file[i + 2] == '$')
            {
                int end = i + 3;
                while (end + 2 < file.Length)
                {
                    if (file[end] == '$' && file[end + 1] == '$' && file[end + 2] == '$')
                    {
                        return System.Text.Encoding.ASCII.GetString(file, i, end - i + 3);
                    }
                    end++;
                }
                break;
            }
        }
        return null;
    }
}

public sealed class ParseResult
{
    public bool Success { get; private init; }
    public OtaFirmware? Firmware { get; private init; }
    public FirmwareCheckCode? ErrorCode { get; private init; }
    public string Error { get; private init; } = "";
    public IReadOnlyList<string> Warnings { get; private init; } = Array.Empty<string>();

    public static ParseResult Ok(OtaFirmware fw, IReadOnlyList<string> warnings) => new()
    {
        Success = true, Firmware = fw, Warnings = warnings,
    };

    public static ParseResult Fail(FirmwareCheckCode code, string error, IReadOnlyList<string>? warnings = null) => new()
    {
        Success = false, ErrorCode = code, Error = error, Warnings = warnings ?? Array.Empty<string>(),
    };
}
