using Windows.Devices.Bluetooth;

namespace TelinkOta.App.Wpf.Ble;

/// <summary>扫描到的 BLE 设备。</summary>
public sealed class BleDeviceInfo
{
    public required ulong Address { get; init; }
    /// <summary>Windows 设备枚举返回的稳定连接入口；为空时退回地址连接。</summary>
    public string? DeviceId { get; init; }
    /// <summary>最近一次实时广播报告的地址类型；不得根据地址高位猜测。</summary>
    public BluetoothAddressType? AddressType { get; init; }
    public string Name { get; init; } = "";
    public string? LocalName { get; init; }
    public short Rssi { get; init; }
    public bool Connectable { get; init; }
    public IReadOnlyList<Guid> ServiceUuids { get; init; } = Array.Empty<Guid>();
    public DateTime FirstSeen { get; init; } = DateTime.Now;

    public string AddressHex => Address.ToString("X12");
    public string DisplayName => string.IsNullOrWhiteSpace(Name) ? "（名称未广播）" : Name;
    public string AvailabilityText => AddressType is null
        ? "Windows 缓存（需实连确认）"
        : $"实时广播 / {AddressType}";

    public override string ToString() =>
        $"{DisplayName,-24} {AddressHex} RSSI={Rssi}dBm {(Connectable ? "conn" : "noconn")}";
}
