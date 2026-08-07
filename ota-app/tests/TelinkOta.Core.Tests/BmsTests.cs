using NUnit.Framework;
using TelinkOta.Core.Bms;
using TelinkOta.Core.Ota;

namespace TelinkOta.Core.Tests;

public class BmsTests
{
    private static byte[] BeWindow(params ushort[] regs)
    {
        var data = new byte[regs.Length * 2];
        for (int i = 0; i < regs.Length; i++)
        {
            data[i * 2] = (byte)(regs[i] >> 8);
            data[i * 2 + 1] = (byte)(regs[i] & 0xFF);
        }
        return data;
    }

    [Test]
    public void ParseRealtime_StableWindow_Conversions()
    {
        // 0xD120 窗口：magic=0x4253, ver=1, 电压4200(=42.00V), 电流-1230(=-12.3A放电), SOC=78,
        // 温度 250(= -15.0℃), 320(= -8.0℃), 300(=-10.0℃), 单体 4200/4100/100
        var data = BeWindow(0x4253, 0x0001, 4200, 0xFB32, 78, 250, 320, 300, 4200, 4100, 100);
        var snap = BatterySnapshot.ParseRealtime(data);
        Assert.That(snap, Is.Not.Null);
        Assert.That(snap!.IsValid, Is.True);
        Assert.That(snap.UsingStableWindow, Is.True);
        Assert.That(snap.PackVoltageV, Is.EqualTo(42.0).Within(0.001));
        Assert.That(snap.PackCurrentA, Is.EqualTo(-123.0).Within(0.001)); // int16 有符号，A*10
        Assert.That(snap.SocPercent, Is.EqualTo(78));
        Assert.That(snap.MaxTempC, Is.EqualTo(-15.0).Within(0.001)); // /10 - 40
        Assert.That(snap.MinTempC, Is.EqualTo(-8.0).Within(0.001));
        Assert.That(snap.MosTempC, Is.EqualTo(-10.0).Within(0.001));
        Assert.That(snap.MaxCellMv, Is.EqualTo(4200));
        Assert.That(snap.MinCellMv, Is.EqualTo(4100));
        Assert.That(snap.CellDeltaMv, Is.EqualTo(100));
    }

    [Test]
    public void ParseRealtime_ChargePositiveCurrent()
    {
        var data = BeWindow(0x4253, 0x0001, 0, 1234, 0, 0, 0, 0, 0, 0, 0);
        var snap = BatterySnapshot.ParseRealtime(data);
        Assert.That(snap!.PackCurrentA, Is.EqualTo(123.4).Within(0.001));
    }

    [Test]
    public void ParseRealtime_WrongMagic_ReturnsNull()
    {
        var data = BeWindow(0x1234, 0x0001, 4200, 0, 78, 250, 320, 300, 4200, 4100, 100);
        Assert.That(BatterySnapshot.ParseRealtime(data), Is.Null);
    }

    [Test]
    public void ParseRealtime_ShortData_ReturnsNull()
    {
        Assert.That(BatterySnapshot.ParseRealtime(new byte[10]), Is.Null);
        Assert.That(BatterySnapshot.ParseRealtime(Array.Empty<byte>()), Is.Null);
    }

    [Test]
    public void ParseLegacyCells_ExtractsCellsAndVbat()
    {
        var regs = new ushort[32];
        for (int i = 0; i < 29; i++) regs[i] = (ushort)(4000 + i);
        regs[31] = 42000; // Vbat_mv = 42.000V
        var data = BeWindow(regs);
        var snap = BatterySnapshot.ParseLegacyCells(data);
        Assert.That(snap, Is.Not.Null);
        Assert.That(snap!.UsingStableWindow, Is.False);
        Assert.That(snap.CellVoltagesMv.Count, Is.EqualTo(29));
        Assert.That(snap.CellVoltagesMv[0], Is.EqualTo(4000));
        Assert.That(snap.CellVoltagesMv[28], Is.EqualTo(4028));
        Assert.That(snap.PackVoltageV, Is.EqualTo(42.0).Within(0.001));
    }

    [Test]
    public void ParseSystemStatus_FirstWord()
    {
        var data = new byte[] { 0x12, 0x34, 0x56, 0x78 };
        Assert.That(BatterySnapshot.ParseSystemStatus(data), Is.EqualTo((ushort)0x1234));
        Assert.That(BatterySnapshot.ParseSystemStatus(new byte[2]), Is.Null);
    }

    [Test]
    public void ParseAsciiRegs_TrimsAtZero()
    {
        var data = System.Text.Encoding.ASCII.GetBytes("D3PRO-12345678\0\0\0");
        Assert.That(BatterySnapshot.ParseAsciiRegs(data), Is.EqualTo("D3PRO-12345678"));
        Assert.That(BatterySnapshot.ParseAsciiRegs(Array.Empty<byte>()), Is.EqualTo(""));
    }

    // ============ ModbusSppClient（FakeTransport） ============

    [Test]
    public async Task SppClient_ReadRegisters_Ok()
    {
        var t = new FakeSppTransport
        {
            ResponsePayload = new byte[] { 0x42, 0x53, 0x00, 0x01, 0x10, 0x68 },
        };
        using var client = new ModbusSppClient(t);
        var payload = await client.ReadRegistersAsync(0xD120, 3, TimeSpan.FromSeconds(2), CancellationToken.None);
        Assert.That(payload, Is.Not.Null);
        Assert.That(payload!.Length, Is.EqualTo(6));
        Assert.That(payload[0], Is.EqualTo(0x42));
    }

    [Test]
    public async Task SppClient_FragmentedNotify_Reassembles()
    {
        var t = new FakeSppTransport
        {
            ResponsePayload = new byte[22], // 11 寄存器，按 20 字节分片发两次
            Fragmented = true,
        };
        using var client = new ModbusSppClient(t);
        var payload = await client.ReadRegistersAsync(0xD120, 11, TimeSpan.FromSeconds(2), CancellationToken.None);
        Assert.That(payload, Is.Not.Null);
        Assert.That(payload!.Length, Is.EqualTo(22));
    }

    [Test]
    public async Task SppClient_Timeout_ReturnsNull()
    {
        var t = new FakeSppTransport { NoResponse = true };
        using var client = new ModbusSppClient(t);
        var payload = await client.ReadRegistersAsync(0xD120, 3, TimeSpan.FromMilliseconds(100), CancellationToken.None);
        Assert.That(payload, Is.Null);
    }

    [Test]
    public async Task SppClient_WriteFail_ReturnsNull()
    {
        var t = new FakeSppTransport { WriteResult = false };
        using var client = new ModbusSppClient(t);
        var payload = await client.ReadRegistersAsync(0xD120, 3, TimeSpan.FromSeconds(1), CancellationToken.None);
        Assert.That(payload, Is.Null);
    }

    private sealed class FakeSppTransport : IBleTransport
    {
        public byte[]? ResponsePayload;
        public bool Fragmented;
        public bool NoResponse;
        public bool WriteResult = true;

        public event Action<byte[]>? OtaNotifyReceived { add { } remove { } }
        public event Action<byte[]>? SppNotifyReceived;
        public event Action? ConnectionLost { add { } remove { } }
        public ulong DeviceAddress => 0;
        public int MaxWriteLength => 20;

        public Task<bool> ConnectAsync(TimeSpan timeout, CancellationToken ct) => Task.FromResult(true);
        public Task<bool> DiscoverOtaServiceAsync(TimeSpan timeout, CancellationToken ct) => Task.FromResult(true);
        public Task<bool> DiscoverSppServiceAsync(TimeSpan timeout, CancellationToken ct) => Task.FromResult(true);
        public Task<bool> EnableOtaNotificationsAsync(TimeSpan timeout, CancellationToken ct) => Task.FromResult(true);
        public Task<bool> EnableSppNotificationsAsync(TimeSpan timeout, CancellationToken ct) => Task.FromResult(true);
        public Task<int> NegotiateMtuAsync(TimeSpan timeout, CancellationToken ct) => Task.FromResult(23);
        public Task<bool> WriteWithoutResponseAsync(byte[] data, CancellationToken ct) => Task.FromResult(true);
        public Task<bool> WaitForTxQueueDrainedAsync(TimeSpan timeout, CancellationToken ct) => Task.FromResult(true);

        public async Task<bool> WriteSppAsync(byte[] frame, CancellationToken ct)
        {
            if (!WriteResult || NoResponse || ResponsePayload is null)
                return WriteResult;
            // 组 Modbus 0x03 响应并（可选）按 20 字节分片
            var resp = new byte[3 + ResponsePayload.Length + 2];
            resp[0] = 0x01;
            resp[1] = 0x03;
            resp[2] = (byte)ResponsePayload.Length;
            ResponsePayload.CopyTo(resp, 3);
            ushort crc = Crc16.Compute(resp.AsSpan(0, 3 + ResponsePayload.Length));
            resp[^2] = (byte)(crc & 0xFF);
            resp[^1] = (byte)(crc >> 8);

            if (Fragmented)
            {
                for (int off = 0; off < resp.Length; off += 20)
                {
                    int len = Math.Min(20, resp.Length - off);
                    var chunk = new byte[len];
                    Array.Copy(resp, off, chunk, 0, len);
                    SppNotifyReceived?.Invoke(chunk);
                    await Task.Delay(1, ct);
                }
            }
            else
            {
                SppNotifyReceived?.Invoke(resp);
            }
            return true;
        }

        public Task DisconnectAsync() => Task.CompletedTask;
        public ValueTask DisposeAsync() => ValueTask.CompletedTask;
    }
}
