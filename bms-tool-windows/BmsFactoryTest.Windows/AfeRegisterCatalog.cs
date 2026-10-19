using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Globalization;
using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Text.Json;

namespace BmsTool.Windows;

public sealed class AfeRegisterField
{
    public string Name { get; init; } = "";
    public byte Mask { get; init; }
    public int Shift { get; init; }
    public string Access { get; init; } = "";
    public string Description { get; init; } = "";
    public bool Reserved { get; init; }
    public string Code { get; set; } = "";
    public string Actual { get; set; } = "未知";
    public string MaskText => $"0x{Mask:X2}";
}

public sealed class AfeRegisterRow : INotifyPropertyChanged
{
    public byte Address { get; init; }
    public string AddressText => $"0x{Address:X2}";
    public string Name { get; init; } = "";
    public string Reset { get; init; } = "未规定";
    public byte WriteMask { get; init; }
    public string WriteMaskText => $"0x{WriteMask:X2}";
    public bool ReadClear { get; init; }
    public bool Readable { get; init; }
    public string Pages { get; init; } = "";
    public string Pdf { get; init; } = "";
    public List<AfeRegisterField> Fields { get; init; } = new();
    public byte? Value { get; private set; }
    private string _actual = "未读取";
    public string Actual { get => _actual; private set { _actual = value; OnPropertyChanged(); } }
    public string ReadPolicy => !Readable ? "原文地址边界未决" : ReadClear ? "读取会清标志；需显式读取" : "可读";
    public void Publish(byte value) { Value = value; Actual = $"0x{value:X2} / {Convert.ToString(value, 2).PadLeft(8, '0')}"; }
    public void Invalidate(string text) { Value = null; Actual = text; }
    public event PropertyChangedEventHandler? PropertyChanged;
    private void OnPropertyChanged([CallerMemberName] string? name = null) => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
}

public sealed class AfeRegisterCatalog
{
    public string Id { get; private set; } = "";
    public ObservableCollection<AfeRegisterRow> Rows { get; } = new();
    public string Revision { get; private set; } = "";
    public string Issues { get; private set; } = "";

    public static AfeRegisterCatalog Load(bool dvc, bool oldSh = false)
    {
        using Stream stream = Assembly.GetExecutingAssembly().GetManifestResourceStream("BmsFactoryTest.AfeRegisterCatalog")
            ?? throw new IOException("缺少内嵌 AFE 原厂寄存器目录。");
        using JsonDocument doc = JsonDocument.Parse(stream);
        var result = new AfeRegisterCatalog { Id = doc.RootElement.GetProperty("catalog_id").GetString() ?? "" };
        var family = doc.RootElement.GetProperty("families")[dvc ? 0 : oldSh ? 2 : 1];
        result.Revision = family.GetProperty("revision").GetString() ?? "";
        result.Issues = string.Join("\n", family.GetProperty("issues").EnumerateArray().Select(value => value.ToString()));
        foreach (JsonElement r in family.GetProperty("registers").EnumerateArray()) {
            List<AfeRegisterField> fields = new();
            foreach (JsonElement f in r.GetProperty("fields").EnumerateArray()) {
                fields.Add(new AfeRegisterField { Name = f.GetProperty("name").GetString() ?? "", Mask = f.GetProperty("mask").GetByte(),
                    Shift = f.GetProperty("shift").GetInt32(), Access = f.GetProperty("access").GetString() ?? "",
                    Reserved = f.GetProperty("reserved").GetBoolean(), Description = (f.GetProperty("description").GetString() ?? "") +
                    " " + f.GetProperty("encoding").GetRawText() });
            }
            var sources = r.GetProperty("sources").EnumerateArray().ToArray();
            var source = sources[sources.Length - 1];
            string pdf = source.TryGetProperty("filename", out var filename) ? filename.GetString() ?? "" : source.GetProperty("file").GetString() ?? "";
            result.Rows.Add(new AfeRegisterRow { Address = r.GetProperty("address").GetByte(), Name = r.GetProperty("name").GetString() ?? "",
                Reset = r.GetProperty("reset").ValueKind == JsonValueKind.String ? r.GetProperty("reset").GetString() ?? "未规定" : "未规定",
                WriteMask = r.GetProperty("write_mask").GetByte(), ReadClear = r.GetProperty("read_clear").GetBoolean(),
                Readable = r.GetProperty("readable").GetBoolean(), Fields = fields,
                Pages = string.Join(" / ", sources.Select(s => s.GetProperty("pdf_page").GetInt32())), Pdf = pdf });
        }
        return result;
    }

    public static byte ParseCode(string text, int maximum)
    {
        string value = text.Trim();
        int number = value.StartsWith("0x", StringComparison.OrdinalIgnoreCase)
            ? int.Parse(value.Substring(2), NumberStyles.HexNumber, CultureInfo.InvariantCulture)
            : int.Parse(value, NumberStyles.Integer, CultureInfo.InvariantCulture);
        if (number < 0 || number > maximum) throw new ArgumentOutOfRangeException(nameof(text), $"编码必须为 0..{maximum}。");
        return (byte)number;
    }
}
