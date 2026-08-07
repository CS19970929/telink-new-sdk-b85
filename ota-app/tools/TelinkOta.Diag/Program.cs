using System.Runtime.InteropServices.WindowsRuntime;
using TelinkOta.App.Wpf.Ble;
using TelinkOta.Core.Bms;
using TelinkOta.Core.Ota;
using Windows.Devices.Bluetooth;
using Windows.Devices.Bluetooth.GenericAttributeProfile;

namespace TelinkOta.Diag;

/// <summary>
/// SPP Modbus 诊断工具 v3：
///  - WriteWithResponse（实测 WNR 被设备丢弃）；
///  - 全从机地址扫描 0x03 读（解析器不再限定地址），定位设备实际 MB_ADDR；
///  - 寄存器/数量按固件大端编码。
/// </summary>
internal static class Program
{
    private static async Task Main(string[] args)
    {
        Console.WriteLine("=== Telink SPP Modbus 诊断工具 v3 ===");
        string filter = args.Length > 0 ? args[0] : "A4C13816025A";

        ulong? address = await ScanForDeviceAsync(filter);
        if (address is null)
        {
            Console.WriteLine($"未扫描到目标设备（过滤 '{filter}'）。");
            return;
        }
        Console.WriteLine($"目标设备地址: {address:X12}");

        await TestReadAsync(address.Value);
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
            var _lastAcc = Array.Empty<byte>();
            notify.ValueChanged += (_, args) =>
            {
                var data = args.CharacteristicValue.ToArray();
                acc.AddRange(data);
            };

            var cccd = await notify.WriteClientCharacteristicConfigurationDescriptorAsync(
                GattClientCharacteristicConfigurationDescriptorValue.Notify);
            Console.WriteLine($"  CCCD: {cccd}");

            // ---- 全从机地址扫描 0x03 读（每地址 400ms）----
            Console.WriteLine("\n--- 从机地址扫描（0x01~0xFF，0xD120 qty=11）---");
            for (int addr = 0x01; addr <= 0xFF; addr++)
            {
                acc.Clear();
                var req = BuildRead((byte)addr, 0xD120, 11);
                var w = await write.WriteValueWithResultAsync(req.AsBuffer(), GattWriteOption.WriteWithResponse);
                var deadline = DateTime.UtcNow.AddMilliseconds(400);
                while (DateTime.UtcNow < deadline)
                {
                    await Task.Delay(20);
                    if (ModbusRtu.TryParseReadResponse(acc.ToArray(), out var payload, out byte rspAddr))
                    {
                        Console.WriteLine($"  ✅ 从机地址 = 0x{rspAddr:X2}（请求 0x{addr:X2}）数据 {payload!.Length} 字节");
                        DumpPayload(payload);
                        goto scanned;
                    }
                }
            }
            Console.WriteLine("  ✗ 0x01~0xFF 均无 0x03 响应");
        scanned:
            Console.WriteLine("  地址扫描结束");

            // ---- 寄存器扫描：0x0000..0xEFFF 每页取首寄存器（qty=1，60ms/个）----
            Console.WriteLine("\n--- 寄存器页扫描（qty=1，找设备实际支持的寄存器范围）---");
            int hitCount = 0;
            for (int page = 0x0000; page <= 0xEFFF; page += 0x10)
            {
                acc.Clear();
                var req3 = BuildRead(0x01, (ushort)page, 1);
                var w3 = await write.WriteValueWithResultAsync(req3.AsBuffer(), GattWriteOption.WriteWithResponse);
                var deadline = DateTime.UtcNow.AddMilliseconds(60);
                while (DateTime.UtcNow < deadline)
                {
                    await Task.Delay(15);
                    if (ModbusRtu.TryParseReadResponse(acc.ToArray(), out var payload, out byte rspAddr))
                    {
                        Console.WriteLine($"  ✅ 0x{page:X4} 响应({rspAddr:X2}): {Convert.ToHexString(payload!)}");
                        hitCount++;
                        break;
                    }
                }
            }
            Console.WriteLine($"  寄存器扫描完成，命中 {hitCount} 处");

            // ---- 0x06 写单寄存器探针（0x1102 调试寄存器，值 0，安全）----
            acc.Clear();
            var w6 = new byte[] { 0x01, 0x06, 0x11, 0x02, 0x00, 0x00, 0x2D, 0x36 };
            Console.WriteLine($"\n--- 0x06 写单寄存器探针（应回显请求）---\n  请求: {Convert.ToHexString(w6)}");
            var wr = await write.WriteValueWithResultAsync(w6.AsBuffer(), GattWriteOption.WriteWithResponse);
            Console.WriteLine($"  写入: {wr.Status}");
            await Task.Delay(1500);
            Console.WriteLine($"  收到: {Convert.ToHexString(acc.ToArray())}");

            // ---- 单请求精确抓包：1 个实时窗口请求，3 秒内打印每条通知 ----
            Console.WriteLine("\n--- 单请求通知流（0xD120 qty=11，重复 5 次观察投递模式）---");
            for (int i = 0; i < 5; i++)
            {
                acc.Clear();
                var rr = BuildRead(0x01, BmsRegisters.RealtimeBase, BmsRegisters.RealtimeCount);
                Console.WriteLine($"\n  请求#{i + 1}: {Convert.ToHexString(rr)}");
                await write.WriteValueWithResultAsync(rr.AsBuffer(), GattWriteOption.WriteWithResponse);
                var end = DateTime.UtcNow.AddSeconds(3);
                var chunks = new List<string>();
                while (DateTime.UtcNow < end)
                {
                    await Task.Delay(50);
                    var cur = acc.ToArray();
                    if (cur.Length > 0 && (chunks.Count == 0 || !cur.SequenceEqual(_lastAcc)))
                    {
                        chunks.Add(Convert.ToHexString(cur));
                        _lastAcc = cur;
                    }
                    if (ModbusRtu.TryParseReadResponse(cur, out _, out _))
                        break;
                }
                foreach (var c in chunks)
                    Console.WriteLine($"    [{DateTime.Now:HH:mm:ss.fff}] {c}");
                Console.WriteLine($"    累计 {acc.Count} 字节");
                await Task.Delay(500);
            }

            // ---- 连续轮询测试：0xD120(11) + 0xD000(63) + 0xD115(2) 循环 20 次 ----
            Console.WriteLine("\n--- 连续轮询测试（20 轮，观察大窗口成功率与内容新鲜度）---");
            int okRealtime = 0, okFull = 0, okStatus = 0;
            byte[]? lastRealtime = null;
            int staleCount = 0;
            for (int i = 0; i < 20; i++)
            {
                acc.Clear();
                var r1 = BuildRead(0x01, BmsRegisters.RealtimeBase, BmsRegisters.RealtimeCount);
                await write.WriteValueWithResultAsync(r1.AsBuffer(), GattWriteOption.WriteWithResponse);
                var d1 = DateTime.UtcNow.AddSeconds(2);
                while (DateTime.UtcNow < d1)
                {
                    await Task.Delay(30);
                    if (ModbusRtu.TryParseReadResponse(acc.ToArray(), out var p, out _))
                    {
                        okRealtime++;
                        if (lastRealtime is not null && p!.SequenceEqual(lastRealtime))
                            staleCount++;
                        lastRealtime = p;
                        break;
                    }
                }

                acc.Clear();
                var r2 = BuildRead(0x01, BmsRegisters.CellsBase, BmsRegisters.CellsCount);
                await write.WriteValueWithResultAsync(r2.AsBuffer(), GattWriteOption.WriteWithResponse);
                var d2 = DateTime.UtcNow.AddSeconds(3);
                while (DateTime.UtcNow < d2)
                {
                    await Task.Delay(30);
                    if (ModbusRtu.TryParseReadResponse(acc.ToArray(), out _, out _)) { okFull++; break; }
                }

                acc.Clear();
                var r3 = BuildRead(0x01, BmsRegisters.SystemStatusBase, BmsRegisters.SystemStatusCount);
                await write.WriteValueWithResultAsync(r3.AsBuffer(), GattWriteOption.WriteWithResponse);
                var d3 = DateTime.UtcNow.AddSeconds(2);
                while (DateTime.UtcNow < d3)
                {
                    await Task.Delay(30);
                    if (ModbusRtu.TryParseReadResponse(acc.ToArray(), out _, out _)) { okStatus++; break; }
                }

                if (i % 5 == 0)
                    Console.WriteLine($"  轮 {i + 1}: realtime={okRealtime} full={okFull} status={okStatus}");
            }
            Console.WriteLine($"  结果: realtime {okRealtime}/20, full {okFull}/20, status {okStatus}/20, 连续相同内容 {staleCount} 次");

            // ---- Echo 链路确认 ----
            acc.Clear();
            var echo = new byte[] { 0x01, 0x7F, 0x12, 0x34, 0x56, 0x78, 0x6F, 0x34 };
            Console.WriteLine($"\n--- Echo 链路检查 ---\n  请求: {Convert.ToHexString(echo)}");
            var w2 = await write.WriteValueWithResultAsync(echo.AsBuffer(), GattWriteOption.WriteWithResponse);
            Console.WriteLine($"  写入: {w2.Status}");
            await Task.Delay(1000);
            Console.WriteLine($"  收到: {Convert.ToHexString(acc.ToArray())}");

            await notify.WriteClientCharacteristicConfigurationDescriptorAsync(
                GattClientCharacteristicConfigurationDescriptorValue.None);
            device.Dispose();
        }
        catch (Exception ex)
        {
            Console.WriteLine($"  异常: {ex.Message}");
        }
    }

    private static void DumpPayload(byte[] payload)
    {
        if (payload.Length >= 22 && BatterySnapshot.ParseRealtime(payload) is { } snap)
        {
            Console.WriteLine($"  总压={snap.PackVoltageV:F2}V 电流={snap.PackCurrentA:F2}A SOC={snap.SocPercent}% " +
                              $"T={snap.MaxTempC:F1}/{snap.MinTempC:F1}/{snap.MosTempC:F1}C " +
                              $"单体={snap.MaxCellMv}/{snap.MinCellMv}/{snap.CellDeltaMv}mV");
        }
        else
        {
            Console.WriteLine($"  原始: {Convert.ToHexString(payload)}");
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
