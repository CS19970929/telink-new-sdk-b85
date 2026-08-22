using System.Text.Json;

namespace TelinkOta.Mobile.Ble;

/// <summary>从设备二维码提取可用于 BLE 匹配的名称、MAC 或 GUID。</summary>
public static class BleQrCodeParser
{
    private static readonly HashSet<string> IdentityKeys = new(StringComparer.OrdinalIgnoreCase)
    {
        "id", "deviceid", "uuid", "guid", "mac", "address", "bluetoothaddress",
        "name", "devicename", "bluetoothname", "btname", "blename"
    };

    public static IReadOnlyList<string> ExtractTokens(string raw)
    {
        var tokens = new List<string>();
        string text = raw.Trim();
        if (text.Length == 0) return tokens;

        if (TryReadJson(text, tokens) && tokens.Count > 0)
            return DistinctTokens(tokens);

        if (Uri.TryCreate(text, UriKind.Absolute, out Uri? uri))
        {
            foreach (var pair in ParseQuery(uri.Query)) AddIfUseful(pair.Key, pair.Value, tokens);
            foreach (var pair in ParseQuery(uri.Fragment.TrimStart('#', '?'))) AddIfUseful(pair.Key, pair.Value, tokens);
            if (tokens.Count > 0) return DistinctTokens(tokens);
        }

        foreach (string part in text.Split(new[] { '&', ';', '\n', '\r' }, StringSplitOptions.RemoveEmptyEntries))
        {
            int separator = part.IndexOf('=');
            if (separator > 0)
            {
                string key = part[..separator].Trim();
                string value = part[(separator + 1)..].Trim();
                AddIfUseful(key, value, tokens);
            }
        }

        if (tokens.Count == 0) tokens.Add(text);
        return DistinctTokens(tokens);
    }

    private static bool TryReadJson(string text, List<string> tokens)
    {
        try
        {
            using JsonDocument document = JsonDocument.Parse(text);
            VisitJson(document.RootElement, tokens);
            return true;
        }
        catch (JsonException)
        {
            return false;
        }
    }

    private static void VisitJson(JsonElement element, List<string> tokens)
    {
        if (element.ValueKind == JsonValueKind.Object)
        {
            foreach (JsonProperty property in element.EnumerateObject())
            {
                if (IdentityKeys.Contains(property.Name.Replace("_", "")) && property.Value.ValueKind == JsonValueKind.String)
                    AddToken(property.Value.GetString(), tokens);
                else VisitJson(property.Value, tokens);
            }
        }
        else if (element.ValueKind == JsonValueKind.Array)
        {
            foreach (JsonElement child in element.EnumerateArray()) VisitJson(child, tokens);
        }
    }

    private static IEnumerable<KeyValuePair<string, string>> ParseQuery(string query)
    {
        foreach (string item in query.Split('&', StringSplitOptions.RemoveEmptyEntries))
        {
            string[] pair = item.Split('=', 2);
            if (pair.Length == 2)
                yield return new(pair[0], Uri.UnescapeDataString(pair[1].Replace('+', ' ')));
        }
    }

    private static void AddIfUseful(string key, string value, List<string> tokens)
    {
        if (IdentityKeys.Contains(Uri.UnescapeDataString(key).Replace("_", ""))) AddToken(value, tokens);
    }

    private static void AddToken(string? value, List<string> tokens)
    {
        if (!string.IsNullOrWhiteSpace(value)) tokens.Add(value.Trim());
    }

    private static IReadOnlyList<string> DistinctTokens(IEnumerable<string> tokens) =>
        tokens.Where(t => !string.IsNullOrWhiteSpace(t)).Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
}
