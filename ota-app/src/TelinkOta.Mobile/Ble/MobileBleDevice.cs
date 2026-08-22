using Plugin.BLE.Abstractions.Contracts;

namespace TelinkOta.Mobile.Ble;

public sealed class MobileBleDevice
{
    internal MobileBleDevice(IDevice native) => Native = native;

    internal IDevice Native { get; }
    public Guid Id => Native.Id;
    public string Name => string.IsNullOrWhiteSpace(Native.Name) ? "未命名 BLE 设备" : Native.Name;
    public int Rssi => Native.Rssi;
    public string? BluetoothAddress
    {
        get
        {
#if ANDROID
            return (Native.NativeDevice as Android.Bluetooth.BluetoothDevice)?.Address;
#else
            return null;
#endif
        }
    }

    public string Display => string.IsNullOrWhiteSpace(BluetoothAddress)
        ? $"{Name}  {Id}  RSSI {Rssi} dBm"
        : $"{Name}  {BluetoothAddress}  RSSI {Rssi} dBm";

    public bool MatchesQr(IReadOnlyCollection<string> tokens)
    {
        string[] candidates = { Name, Id.ToString(), BluetoothAddress ?? string.Empty };
        return tokens.Any(token => candidates.Any(candidate => SameIdentity(token, candidate)));
    }

    private static bool SameIdentity(string left, string right)
    {
        string a = NormalizeIdentity(left);
        string b = NormalizeIdentity(right);
        return a.Length > 0 && a == b;
    }

    private static string NormalizeIdentity(string value) =>
        new string(value.Where(char.IsLetterOrDigit).ToArray()).ToUpperInvariant();

    public override string ToString() => Display;
}
