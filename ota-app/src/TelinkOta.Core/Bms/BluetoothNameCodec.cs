using System.Text;

namespace TelinkOta.Core.Bms;

/// <summary>
/// 固件 btname_modbus.c 的蓝牙名称契约：设备固定添加 BT_ 前缀，后缀仅允许
/// ASCII 字母、数字、下划线和连字符。当前寄存器读窗口可完整读回 24 字节，
/// 因此上位机把后缀限制为 21 字节（BT_ + 21 = 24）。
/// </summary>
public static class BluetoothNameCodec
{
    public const string Prefix = "BT_";
    public const int MaxSuffixLength = 21;
    public const int MaxFullNameLength = 24;

    public static bool TryNormalize(string? input, out string suffix, out string fullName, out string error)
    {
        suffix = "";
        fullName = "";
        error = "";

        string value = (input ?? "").Trim();
        if (value.StartsWith(Prefix, StringComparison.OrdinalIgnoreCase))
            value = value[Prefix.Length..];

        if (value.Length == 0)
        {
            error = "蓝牙名后缀不能为空。";
            return false;
        }
        if (value.Length > MaxSuffixLength)
        {
            error = $"蓝牙名后缀最多 {MaxSuffixLength} 个字符（完整名称最多 {MaxFullNameLength} 个字符）。";
            return false;
        }
        if (value.Any(c => !IsAllowed(c)))
        {
            error = "蓝牙名仅允许英文字母、数字、下划线和连字符。";
            return false;
        }

        suffix = value;
        fullName = Prefix + suffix;
        return true;
    }

    /// <summary>编码为 0x10 的寄存器数据区；奇数字节末尾补 0，保持大端字符顺序。</summary>
    public static byte[] EncodeSuffix(string suffix)
    {
        if (!TryNormalize(suffix, out string normalized, out _, out string error))
            throw new ArgumentException(error, nameof(suffix));

        byte[] ascii = Encoding.ASCII.GetBytes(normalized);
        if ((ascii.Length & 1) == 0)
            return ascii;

        var padded = new byte[ascii.Length + 1];
        ascii.CopyTo(padded, 0);
        return padded;
    }

    private static bool IsAllowed(char c) =>
        c is >= '0' and <= '9' or >= 'A' and <= 'Z' or >= 'a' and <= 'z' or '_' or '-';
}
