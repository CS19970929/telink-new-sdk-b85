using System.Text.RegularExpressions;

namespace BmsTool.Android;

// QR payloads contain only the Bluetooth MAC, never a command or a project ID.
internal static class BluetoothMacCode
{
    public static bool TryParse(string? text,out string mac)
    {
        mac=string.Empty;
        if(string.IsNullOrWhiteSpace(text)||text.Length>64)return false;
        text=text.Trim();
        if(!Regex.IsMatch(text,@"\A(?:[0-9A-Fa-f]{12}|(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}|(?:[0-9A-Fa-f]{2}-){5}[0-9A-Fa-f]{2})\z",RegexOptions.CultureInvariant))return false;
        string hex=text.Replace(":","").Replace("-","").ToUpperInvariant();
        if(hex is "000000000000" or "FFFFFFFFFFFF")return false;
        mac=string.Join(":",Enumerable.Range(0,6).Select(i=>hex.Substring(i*2,2)));
        return true;
    }
}
