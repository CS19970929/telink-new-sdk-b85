using System.Globalization;
using System.IO;

namespace BmsTool.Windows;

public enum Sh309Readback { Unknown, Matches, Different }

public sealed record Sh309ChipProtection(int Index, ushort[] Saved, byte[]? Eeprom,
    ushort[]? Expected, Sh309Readback Readback, string Detail)
{
    public int? Tr => Eeprom is { } bytes ? bytes[25] & 127 : null;
    public int[]? Actual => Eeprom is { } bytes ? C077Hardware.Decode(bytes, bytes[25]) : null;
    public string Name => $"AFE{Index + 1}";
}

public sealed record Sh309ProtectionSnapshot(string Product, IReadOnlyList<Sh309ChipProtection> Chips);

public static class Sh309Protection
{
    public static async Task<Sh309ProtectionSnapshot> ReadAsync(BmsClient client, CancellationToken ct = default)
    {
        if (client.IsC073) return new("C073", new[] { await C073Protection.InspectAsync(client, ct).ConfigureAwait(false) });
        if (!client.IsC077) throw new NotSupportedException("设备未识别为受支持的 SH367309 产品。");

        C077HardwareSnapshot data;
        string? failure = null;
        try {
            data = await client.ReadC077HardwareAsync(verifyEeprom: true, ct).ConfigureAwait(false);
        }
        catch (Exception ex) when (!ct.IsCancellationRequested && ex is IOException or TimeoutException) {
            failure = ex.Message;
            data = await client.ReadC077HardwareAsync(verifyEeprom: false, ct).ConfigureAwait(false);
        }

        var chips = new Sh309ChipProtection[data.Actual.Chips.Count];
        for (int i = 0; i < chips.Length; i++)
        {
            var chip = data.Actual.Chips[i];
            var service = data.Actual;
            bool reportedForChip = service.ServiceFailed && service.ServicePosition >> 8 == i;
            Sh309Readback result = Sh309Readback.Unknown;
            string detail = failure ?? "本次芯片读回未确认";
            if (failure is null && service.ServiceState == 0)
            {
                if (reportedForChip && service.ServiceError == 4)
                    result = Sh309Readback.Different;
                else if (!reportedForChip && chip.ProfileMatches)
                    result = Sh309Readback.Matches;
                else if (!reportedForChip && service.ServiceFailed && chip.LastError == 4)
                {
                    // C087 checks every chip and reports only the first failure in the service result.
                    result = Sh309Readback.Different;
                    detail = $"配置不匹配，寄存器 0x{chip.LastRegister:X2}";
                }
                if (reportedForChip)
                    detail = $"{C077Parameters.ErrorText(service.ServiceError)}，寄存器 0x{service.ServicePosition & 255:X2}";
                else if (chip.LastError != 0 && result == Sh309Readback.Unknown)
                    detail = $"{C077Parameters.ErrorText(chip.LastError)}，寄存器 0x{chip.LastRegister:X2}（最近诊断）";
            }
            chips[i] = new(i, data.Saved[i], result == Sh309Readback.Unknown ? null : chip.Eeprom,
                result == Sh309Readback.Unknown ? null : data.Expected[i], result, detail);
        }
        return new("C077", chips);
    }

    public static string ReadbackText(Sh309ChipProtection chip) => chip.Readback switch
    {
        Sh309Readback.Matches => "本次 EEPROM 读回与 Flash 目标一致",
        Sh309Readback.Different => $"本次 EEPROM 读回与 Flash 目标不同，{chip.Detail}",
        _ => $"EEPROM 读回未确认，{chip.Detail}"
    };

    public static string FieldName(string product, C077HardwareField field) =>
        product == "C073" ? C073Protection.FieldName(field) : field.Name;

    public static string Unit(string product, C077HardwareField field) =>
        product == "C073" && C073Protection.IsCurrentField(field.Index) ? "A" : field.Signed ? "℃" : field.Unit;

    public static string Limits(string product, C077HardwareField field)
    {
        if (product == "C073" && C073Protection.IsCurrentField(field.Index) && field.Choices is { } choices)
            return string.Join(" / ", choices.Select(v => Format(product, field, v)));
        if (field.Signed)
            return $"{Format(product, field, field.Minimum)}..{Format(product, field, field.Maximum)}，整 ℃ 档位，按本片 TR 校验";
        return field.Limits;
    }

    public static ushort Encode(string product, C077HardwareField field, string text)
    {
        const NumberStyles style = NumberStyles.AllowDecimalPoint | NumberStyles.AllowLeadingSign |
            NumberStyles.AllowLeadingWhite | NumberStyles.AllowTrailingWhite;
        if (!decimal.TryParse(text, style, CultureInfo.InvariantCulture, out decimal value) &&
            !decimal.TryParse(text, style, CultureInfo.CurrentCulture, out value))
            throw new ArgumentException($"{FieldName(product, field)}的数值格式无效。");
        if (product == "C073" && C073Protection.IsCurrentField(field.Index)) value /= 3;
        if (field.Signed) value *= 10;
        if (value != decimal.Truncate(value) || value < field.Minimum || value > field.Maximum)
            throw new ArgumentException($"{FieldName(product, field)}不在允许的精度或范围内。");
        return field.Signed ? unchecked((ushort)(short)value) : (ushort)value;
    }

    public static string Format(string product, C077HardwareField field, int value)
    {
        if (product == "C073") value = C073Protection.DisplayValue(field.Index, value);
        return field.Signed ? (value / 10m).ToString("0.0", CultureInfo.InvariantCulture) : value.ToString(CultureInfo.InvariantCulture);
    }
}
