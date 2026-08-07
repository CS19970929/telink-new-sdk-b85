namespace TelinkOta.App.Wpf.Ble;

/// <summary>扫描到的 BLE 设备。</summary>
public sealed class BleDeviceInfo
{
    public required ulong Address { get; init; }
    public string Name { get; init; } = "";
    public string? LocalName { get; init; }
    public short Rssi { get; init; }
    public bool Connectable { get; init; }
    public IReadOnlyList<Guid> ServiceUuids { get; init; } = Array.Empty<Guid>();
    public DateTime FirstSeen { get; init; } = DateTime.Now;

    public string AddressHex => Address.ToString("X12");

    public override string ToString() =>
        $"{Name,-24} {AddressHex} RSSI={Rssi}dBm {(Connectable ? "conn" : "noconn")}";
}
