using Plugin.BLE.Abstractions.Contracts;

namespace TelinkOta.Mobile.Ble;

public sealed class MobileBleDevice
{
    internal MobileBleDevice(IDevice native) => Native = native;

    internal IDevice Native { get; }
    public Guid Id => Native.Id;
    public string Name => string.IsNullOrWhiteSpace(Native.Name) ? "未命名 BLE 设备" : Native.Name;
    public int Rssi => Native.Rssi;
    public string Display => $"{Name}  {Id}  RSSI {Rssi} dBm";

    public override string ToString() => Display;
}
