using System.Text;
using NUnit.Framework;
using TelinkOta.Core.Ota;

namespace TelinkOta.Core.Tests;

public class FirmwareParserTests
{
    private const uint MaxSize = 124 * 1024;

    /// <summary>构造合法 BIN：头部含 Mark "TLNK"、Size@0x18，其余数据任意。</summary>
    private static byte[] BuildBin(int size, bool markValid = true, int tailBytes = 0)
    {
        var bin = new byte[size + tailBytes];
        if (markValid)
        {
            bin[0x08] = 0x4B; bin[0x09] = 0x4E; bin[0x0A] = 0x4C; bin[0x0B] = 0x54; // "TLNK"
        }
        BitConverter.TryWriteBytes(bin.AsSpan(0x18, 4), (uint)size);
        return bin;
    }

    private static string WriteTemp(byte[] bin)
    {
        string path = Path.Combine(Path.GetTempPath(), $"ota_test_{Guid.NewGuid():N}.bin");
        File.WriteAllBytes(path, bin);
        return path;
    }

    [Test]
    public void Parse_RawBin_NoCrc_AppendsCrc32_AndPatchesSize()
    {
        // 原始构建产物：Size@0x18 = len、无尾部 CRC → 自动追加 CRC32 并回写 Size@0x18 = len+4
        int size = 0x1000;
        string path = WriteTemp(BuildBin(size));
        try
        {
            var result = FirmwareParser.Parse(path, MaxSize);
            Assert.That(result.Success, Is.True, result.Error);
            var fw = result.Firmware!;
            Assert.That(fw.CrcWasAppended, Is.True);
            Assert.That(fw.DeclaredSize, Is.EqualTo((uint)size + 4)); // 设备将收到的总字节数
            Assert.That(fw.Payload.Length, Is.EqualTo(size + 4));
            Assert.That(BitConverter.ToUInt32(fw.Payload, 0x18), Is.EqualTo((uint)size + 4)); // Size 已回写
            Assert.That(BitConverter.ToUInt32(fw.Payload, 0x08), Is.EqualTo(OtaConstants.FirmwareMark));
            // 尾部 CRC32 = calCrc32(前 size 字节)
            uint crc = Crc32.Compute(fw.Payload.AsSpan(0, size));
            Assert.That(BitConverter.ToUInt32(fw.Payload, size), Is.EqualTo(crc));
            Assert.That(result.Warnings, Has.Some.Contains("追加"));
        }
        finally { File.Delete(path); }
    }

    [Test]
    public void Parse_BinWithValidTailCrc_VerifiesAndKeeps()
    {
        // 兼容旧格式：若 Size@0x18 为 len-4，发送前必须规范化为含 CRC 的总长并重算 CRC。
        int size = 0x1000;
        var bin = BuildBin(size);
        uint crc = Crc32.Compute(bin);
        var withCrc = new byte[size + 4];
        bin.CopyTo(withCrc, 0);
        BitConverter.TryWriteBytes(withCrc.AsSpan(size, 4), crc);

        string path = WriteTemp(withCrc);
        try
        {
            var result = FirmwareParser.Parse(path, MaxSize);
            Assert.That(result.Success, Is.True, result.Error);
            Assert.That(result.Firmware!.CrcVerified, Is.True);
            Assert.That(result.Firmware.CrcWasAppended, Is.False);
            Assert.That(result.Firmware.Payload.Length, Is.EqualTo(size + 4));
            Assert.That(result.Firmware.DeclaredSize, Is.EqualTo((uint)size + 4));
            Assert.That(BitConverter.ToUInt32(result.Firmware.Payload, 0x18), Is.EqualTo((uint)size + 4));
            Assert.That(Crc32.Compute(result.Firmware.Payload.AsSpan(0, size)),
                Is.EqualTo(BitConverter.ToUInt32(result.Firmware.Payload, size)));
        }
        finally { File.Delete(path); }
    }

    [Test]
    public void Parse_BinWithValidTailCrc_SizeIncludesCrc_Verifies()
    {
        // tl_check_fw2.exe 后处理语义：Size@0x18 = 文件总长（含 CRC）—— 真实固件 BIN 的格式。
        // 注意：CRC32 覆盖"回写 Size 之后的完整内容"。
        int fwSize = 0x1000;
        var bin = BuildBin(fwSize);
        var withCrc = new byte[fwSize + 4];
        bin.CopyTo(withCrc, 0);
        BitConverter.TryWriteBytes(withCrc.AsSpan(0x18, 4), (uint)withCrc.Length); // 回写总长
        uint crc = Crc32.Compute(withCrc.AsSpan(0, fwSize)); // CRC 覆盖含回写 Size 的内容
        BitConverter.TryWriteBytes(withCrc.AsSpan(fwSize, 4), crc);

        string path = WriteTemp(withCrc);
        try
        {
            var result = FirmwareParser.Parse(path, MaxSize);
            Assert.That(result.Success, Is.True, result.Error);
            Assert.That(result.Firmware!.CrcVerified, Is.True);
            Assert.That(result.Firmware.CrcWasAppended, Is.False);
            Assert.That(result.Firmware.DeclaredSize, Is.EqualTo((uint)withCrc.Length));
            Assert.That(result.Firmware.Payload.Length, Is.EqualTo(withCrc.Length));
            Assert.That(result.Warnings, Is.Empty);
        }
        finally { File.Delete(path); }
    }

    [Test]
    public void Parse_BinWithWrongTailCrc_Rejected()
    {
        // Size@0x18 与长度不匹配且尾部 CRC 无效 → 拒绝，不静默修改
        int size = 0x1000;
        var bin = BuildBin(size);
        var withCrc = new byte[size + 4];
        bin.CopyTo(withCrc, 0);
        BitConverter.TryWriteBytes(withCrc.AsSpan(size, 4), 0xDEADBEEF);

        string path = WriteTemp(withCrc);
        try
        {
            var result = FirmwareParser.Parse(path, MaxSize);
            Assert.That(result.Success, Is.False);
            Assert.That(result.Error, Does.Contain("不匹配"));
        }
        finally { File.Delete(path); }
    }

    [Test]
    public void Parse_FileTooSmall()
    {
        string path = WriteTemp(new byte[8]);
        try
        {
            var result = FirmwareParser.Parse(path, MaxSize);
            Assert.That(result.Success, Is.False);
            Assert.That(result.ErrorCode, Is.EqualTo(FirmwareCheckCode.FileTooSmall));
        }
        finally { File.Delete(path); }
    }

    [Test]
    public void Parse_DeclaredSizeZero()
    {
        string path = WriteTemp(new byte[0x40]);
        try
        {
            var result = FirmwareParser.Parse(path, MaxSize);
            Assert.That(result.Success, Is.False);
            Assert.That(result.ErrorCode, Is.EqualTo(FirmwareCheckCode.DeclaredSizeZero));
        }
        finally { File.Delete(path); }
    }

    [Test]
    public void Parse_DeclaredSizeExceedsFile()
    {
        int size = 0x2000;
        var bin = BuildBin(size);      // 文件只有 size 长
        BitConverter.TryWriteBytes(bin.AsSpan(0x18, 4), (uint)size + 0x100); // 声明更大
        string path = WriteTemp(bin);
        try
        {
            var result = FirmwareParser.Parse(path, MaxSize);
            Assert.That(result.Success, Is.False);
            Assert.That(result.Error, Does.Contain("不匹配"));
        }
        finally { File.Delete(path); }
    }

    [Test]
    public void Parse_DeclaredSizeTooBig_ForPartition()
    {
        var bin = BuildBin((int)MaxSize + 0x1000);
        string path = WriteTemp(bin);
        try
        {
            var result = FirmwareParser.Parse(path, MaxSize);
            Assert.That(result.Success, Is.False);
            Assert.That(result.Error, Does.Contain("分区上限"));
        }
        finally { File.Delete(path); }
    }

    [Test]
    public void Parse_BadMark_FailsByDefault()
    {
        int size = 0x1000;
        string path = WriteTemp(BuildBin(size, markValid: false));
        try
        {
            var result = FirmwareParser.Parse(path, MaxSize);
            Assert.That(result.Success, Is.False);
            Assert.That(result.ErrorCode, Is.EqualTo(FirmwareCheckCode.MarkMissing));
            Assert.That(result.Error, Does.Contain("Mark"));
        }
        finally { File.Delete(path); }
    }

    [Test]
    public void Parse_BadMark_WarnOnly_WhenAllowed()
    {
        int size = 0x1000;
        string path = WriteTemp(BuildBin(size, markValid: false));
        try
        {
            var result = FirmwareParser.Parse(path, MaxSize, requireMark: false);
            Assert.That(result.Success, Is.True, result.Error);
            Assert.That(result.Firmware!.MarkValid, Is.False);
            Assert.That(result.Warnings, Has.Some.Contains("Mark"));
        }
        finally { File.Delete(path); }
    }

    [Test]
    public void Parse_UnexpectedTail_Rejected()
    {
        var bin = BuildBin(0x1000, tailBytes: 2);
        string path = WriteTemp(bin);
        try
        {
            var result = FirmwareParser.Parse(path, MaxSize);
            Assert.That(result.Success, Is.False);
            Assert.That(result.ErrorCode, Is.EqualTo(FirmwareCheckCode.UnexpectedTail));
        }
        finally { File.Delete(path); }
    }

    [Test]
    public void Parse_FileNotFound()
    {
        var result = FirmwareParser.Parse("Z:\\definitely_missing_xxx.bin", MaxSize);
        Assert.That(result.Success, Is.False);
        Assert.That(result.ErrorCode, Is.EqualTo(FirmwareCheckCode.FileNotFound));
    }

    [Test]
    public void Parse_SdkVersionExtraction()
    {
        var bin = BuildBin(0x200);
        var body = Encoding.ASCII.GetBytes("$$$tc_ble_single_sdk_V3.4.2.8$$$");
        bin = bin.Concat(body).ToArray();
        // 修正声明尺寸使其等于文件长度（模拟链接产物）
        BitConverter.TryWriteBytes(bin.AsSpan(0x18, 4), (uint)bin.Length);
        string path = WriteTemp(bin);
        try
        {
            var result = FirmwareParser.Parse(path, MaxSize);
            Assert.That(result.Success, Is.True, result.Error);
            Assert.That(result.Firmware!.SdkVersion, Is.EqualTo("$$$tc_ble_single_sdk_V3.4.2.8$$$"));
        }
        finally { File.Delete(path); }
    }

    [Test]
    public void Parse_BinVersion_FromOffset2()
    {
        var bin = BuildBin(0x1000);
        bin[0x02] = 0x34;
        bin[0x03] = 0x12;
        string path = WriteTemp(bin);
        try
        {
            var result = FirmwareParser.Parse(path, MaxSize);
            Assert.That(result.Success, Is.True, result.Error);
            Assert.That(result.Firmware!.BinVersion, Is.EqualTo(0x1234));
        }
        finally { File.Delete(path); }
    }

    [Test]
    public void Parse_Sha256_Computed()
    {
        int size = 0x1000;
        string path = WriteTemp(BuildBin(size));
        try
        {
            var result = FirmwareParser.Parse(path, MaxSize);
            Assert.That(result.Success, Is.True, result.Error);
            Assert.That(result.Firmware!.Sha256Hex.Length, Is.EqualTo(64));
        }
        finally { File.Delete(path); }
    }

    [Test]
    public void Parse_RealFirmwareBin_Integration()
    {
        // 使用真实设备固件（TestData/real_fw.bin）验证：
        // Size@0x18 = 0x163C4 = 文件总长（含 CRC）、Mark=TLNK、尾部 CRC32 验证通过、原样发送
        string? realBin = FindRealBin();
        if (realBin is null)
        {
            Assert.Ignore("未找到真实 BIN 测试数据");
        }
        var result = FirmwareParser.Parse(realBin, MaxSize);
        Assert.That(result.Success, Is.True, result.Error);
        var fw = result.Firmware!;
        Assert.That(fw.DeclaredSize, Is.EqualTo(0x163C4u));
        Assert.That(fw.MarkValid, Is.True);
        Assert.That(fw.CrcVerified, Is.True);
        Assert.That(fw.CrcWasAppended, Is.False);
        Assert.That(fw.Payload.Length, Is.EqualTo(91076)); // 原样发送
        Assert.That(fw.SdkVersion, Is.Not.Null);
        Assert.That(fw.SdkVersion, Does.Contain("3.4.2.8"));
    }

    private static string? FindRealBin()
    {
        string candidate = Path.Combine(AppContext.BaseDirectory, "TestData", "real_fw.bin");
        return File.Exists(candidate) ? candidate : null;
    }
}
