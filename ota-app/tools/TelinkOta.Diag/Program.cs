using System.Runtime.InteropServices.WindowsRuntime;
using TelinkOta.App.Wpf.Ble;
using TelinkOta.Core.Bms;
using TelinkOta.Core.Ota;
using Windows.Devices.Bluetooth;
using Windows.Devices.Bluetooth.GenericAttributeProfile;

namespace TelinkOta.Diag;

/// <summary>
/// SPP Modbus 诊断工具 v2：
/// 只使用 WriteWithResponse（实测 WNR 被设备丢弃），
/// 先读 0xD120 稳定窗口，再读产品信息，并持续记录所有通知。
/// </summary>
internal static class Program
{
    private static async Task Main(string[] args)
    {
        Console.WriteLine("=== Telink SPP Modbus 诊断工具 v2 ===");
        string filter = args.Length > 0 ? args[0] : "A4C13816025A";

        ulong? address = await ScanForDeviceAsync(filter);
        if (address is null)
        {
            Console.WriteLine($"未扫描到目标设备（过滤 '{filter}'）。");
            return;
        }
        Console.WriteLine($"目标设备地址: {address:X12}");

        for (int attempt = 1; attempt <= 3; attempt++)
        {
            Console.WriteLine($"\n========== 尝试 #{attempt} ==========");
            await TestReadAsync(address.Value);
            if (attempt < 3)
                await Task.Delay(3000);
        }
        Console.WriteLine("\n诊断完成。");
    }

    private static async Task TestReadAsync(ulong address)
    {
        try
        {
            var device = await BluetoothLEDevice.FromBluetoothAddressAsync(address);
            if (device is null) { Console.WriteLine("  设备句柄获取失败"); return; }
            var services = await device.GetGattServicesAsync(BluetoothCacheMode.Uncached);

            GattCharacteristic? write = null, notify = null;
            foreach (var svc in services.Services)
            {
                if (svc.Uuid == OtaConstants.SppServiceUuid)
                {
                    var chars = await svc.GetCharacteristicsAsync(BluetoothCacheMode.Uncached);
                    foreach (var ch in chars.Characteristics)
                    {
                        if (ch.Uuid == OtaConstants.SppWriteUuid) write = ch;
                        if (ch.Uuid == OtaConstants.SppNotifyUuid) notify = ch;
                    }
                    break;
                }
            }
            if (write is null || notify is null)
            {
                Console.WriteLine("  SPP 特征不完整！");
                return;
            }
            Console.WriteLine($"  写 handle=0x{write.AttributeHandle:X4} 通知 handle=0x{notify.AttributeHandle:X4}");

            var acc = new List<byte>();
            notify.ValueChanged += (_, args) =>
            {
                var data = args.CharacteristicValue.ToArray();
                acc.AddRange(data);
                Console.WriteLine($"    [NOTIFY {DateTime.Now:HH:mm:ss.fff}] len={data.Length} {Convert.ToHexString(data)}");
            };

            var cccd = await notify.WriteClientCharacteristicConfigurationDescriptorAsync(
                GattClientCharacteristicConfigurationDescriptorValue.Notify);
            Console.WriteLine($"  CCCD: {cccd}");

            var tasks = new List<(string Name, byte[] Req)>();
            // 用不同从机地址扫描 0x03 读（设备固件 MB_ADDR 可能不是 0x01：
            // 0x7F/0x84 异常不检查地址都能响应，只有 0x03 被地址检查拦截）
            for (byte addr = 0x01; addr <= 0x10; addr++)
            {
                tasks.Add(($"0x03 addr=0x{addr:X2} 0xD120 qty=11", BuildRead(addr, 0xD120, 11)));
            }
            tasks.Add(("0x04 读输入寄存器（应回异常0x84）", new byte[] { 0x01, 0x04, 0x20, 0xD1, 0x00, 0x01, 0x6A, 0x33 }));
            tasks.Add(("Echo 链路检查(0x7F)", new byte[] { 0x01, 0x7F, 0x12, 0x34, 0x56, 0x78, 0x6F, 0x34 }));

            foreach (var (name, req) in tasks)
            {
                acc.Clear();
                Console.WriteLine($"\n  --- {name} ---");
                Console.WriteLine($"  请求: {Convert.ToHexString(req)}");
                var w = await write.WriteValueWithResultAsync(req.AsBuffer(), GattWriteOption.WriteWithResponse);
                Console.WriteLine($"  写入: {w.Status} protErr=0x{w.ProtocolError:X2}");

                var deadline = DateTime.UtcNow.AddSeconds(10);
                while (DateTime.UtcNow < deadline)
                {
                    await Task.Delay(200);
                    if (ModbusRtu.TryParseReadResponse(acc.ToArray(), out var payload))
                    {
                        Console.WriteLine($"  ✅ 响应 {payload!.Length} 字节: {Convert.ToHexString(payload)}");
                        if (name.StartsWith("0xD120") && payload.Length >= 22 &&
                            BatterySnapshot.ParseRealtime(payload) is { } snap)
                        {
                            Console.WriteLine($"  总压={snap.PackVoltageV:F2}V 电流={snap.PackCurrentA:F2}A SOC={snap.SocPercent}% " +
                                              $"T={snap.MaxTempC:F1}/{snap.MinTempC:F1}/{snap.MosTempC:F1}C " +
                                              $"单体={snap.MaxCellMv}/{snap.MinCellMv}/{snap.CellDeltaMv}mV");
                        }
                        else if (name.StartsWith("0xC022"))
                        {
                            Console.WriteLine($"  软件版本: \"{BatterySnapshot.ParseAsciiRegs(payload)}\"");
                        }
                        else if (name.StartsWith("Echo"))
                        {
                            Console.WriteLine("  链路正常 ✓");
                        }
                        break;
                    }
                }
                if (!ModbusRtu.TryParseReadResponse(acc.ToArray(), out _))
                {
                    Console.WriteLine($"  ✗ 10s 内未收到有效响应（累计 {acc.Count} 字节）");
                }
            }

            await notify.WriteClientCharacteristicConfigurationDescriptorAsync(
                GattClientCharacteristicConfigurationDescriptorValue.None);
            device.Dispose();
        }
        catch (Exception ex)
        {
            Console.WriteLine($"  异常: {ex.Message}");
        }
    }

    private static byte[] BuildRead(byte addr, ushort start, ushort qty)
    {
        var frame = new byte[8];
        frame[0] = addr;
        frame[1] = 0x03;
        frame[2] = (byte)(start >> 8);
        frame[3] = (byte)(start & 0xFF);
        frame[4] = (byte)(qty >> 8);
        frame[5] = (byte)(qty & 0xFF);
        ushort crc = TelinkOta.Core.Ota.Crc16.Compute(frame.AsSpan(0, 6));
        frame[6] = (byte)(crc & 0xFF);
        frame[7] = (byte)(crc >> 8);
        return frame;
    }

    private static async Task<ulong?> ScanForDeviceAsync(string filter)
    {
        using var scanner = new BleScanner();
        ulong? found = null;
        var tcs = new TaskCompletionSource<ulong?>();
        var cts = new CancellationTokenSource(TimeSpan.FromSeconds(20));

        scanner.Start(info =>
        {
            if (found is null &&
                (info.Name.Contains(filter, StringComparison.OrdinalIgnoreCase) ||
                 info.AddressHex.Contains(filter, StringComparison.OrdinalIgnoreCase)))
            {
                found = info.Address;
                Console.WriteLine($"发现设备: '{info.Name}' ({info.AddressHex}) RSSI={info.Rssi}");
                tcs.TrySetResult(info.Address);
            }
        });

        try
        {
            return await tcs.Task.WaitAsync(cts.Token);
        }
        catch (Exception ex) when (ex is TimeoutException or TaskCanceledException or OperationCanceledException)
        {
            return null;
        }
        finally
        {
            scanner.Stop();
        }
    }
}
