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
    public void ParseLegacyWindow_ExtractsCellsAndStats()
    {
        // 63 寄存器完整窗口：32 单体 + 极值/位置/压差/总压 + 10 温度 + 充放电电流 + SOC/SOH/容量/循环 + 故障 + 均衡
        var regs = new ushort[63];
        for (int i = 0; i < 32; i++) regs[i] = (ushort)(4000 + i);   // 单体
        regs[32] = 4031;  // max
        regs[33] = 4000;  // min
        regs[34] = 32;    // max pos
        regs[35] = 1;     // min pos
        regs[36] = 31;    // delta
        regs[37] = 42000; // 总压 420.00V? *100 → 420.00V? 不，42000/100=420V... 用 4200 → 42.00V
        regs[37] = 4200;
        for (int i = 0; i < 10; i++) regs[38 + i] = (ushort)(250 + i); // (+40)*10 → -15℃ 起
        regs[48] = 300; // temp max → -10℃
        regs[49] = 250; // temp min → -15℃
        regs[50] = 1234; // Ichg 123.4A
        regs[51] = 0;    // Idischg 0
        regs[52] = 78;   // SOC
        regs[53] = 95;   // SOH
        regs[54] = 1000; // CapacityNow 10.00Ah
        regs[55] = 1100; // CapacityFull 11.00Ah
        regs[56] = 1150; // CapacityFactory 11.50Ah
        regs[57] = 123;  // cycles
        regs[58] = 0x0005; // fault1: CellOvp + CellUvp
        regs[61] = 0x0001; // balance1

        var data = BeWindow(regs);
        var snap = BatterySnapshot.ParseLegacyWindow(data);
        Assert.That(snap, Is.Not.Null);
        Assert.That(snap!.UsingStableWindow, Is.False);
        Assert.That(snap.CellVoltagesMv.Count, Is.EqualTo(32));
        Assert.That(snap.CellVoltagesMv[0], Is.EqualTo(4000));
        Assert.That(snap.CellVoltagesMv[31], Is.EqualTo(4031));
        Assert.That(snap.MaxCellMv, Is.EqualTo(4031));
        Assert.That(snap.MinCellMv, Is.EqualTo(4000));
        Assert.That(snap.MaxCellPosition, Is.EqualTo(32));
        Assert.That(snap.MinCellPosition, Is.EqualTo(1));
        Assert.That(snap.CellDeltaMv, Is.EqualTo(31));
        Assert.That(snap.PackVoltageV, Is.EqualTo(42.0).Within(0.001));
        Assert.That(snap.TemperaturesC.Count, Is.EqualTo(10));
        Assert.That(snap.TemperaturesC[0], Is.EqualTo(-15.0).Within(0.001));
        Assert.That(snap.MaxTempC, Is.EqualTo(-10.0).Within(0.001));
        Assert.That(snap.MinTempC, Is.EqualTo(-15.0).Within(0.001));
        Assert.That(snap.ChargeCurrentA, Is.EqualTo(123.4).Within(0.001));
        Assert.That(snap.SocPercent, Is.EqualTo(78));
        Assert.That(snap.SohPercent, Is.EqualTo(95));
        Assert.That(snap.CapacityNowAh, Is.EqualTo(10.0).Within(0.001));
        Assert.That(snap.CapacityFullAh, Is.EqualTo(11.0).Within(0.001));
        Assert.That(snap.CapacityFactoryAh, Is.EqualTo(11.5).Within(0.001));
        Assert.That(snap.CycleTimes, Is.EqualTo(123));
        Assert.That(snap.MdlFaultFirst, Is.EqualTo(0x0005));
        Assert.That(snap.BalanceFlag1, Is.EqualTo(0x0001));
        Assert.That(snap.PackCurrentA, Is.EqualTo(123.4).Within(0.001)); // 由充电电流推导
    }

    [Test]
    public void SystemStatusBits_Decode()
    {
        // bit0 StartUpBMS + bit2 MOS_CHG + bit8 Heat + ProjectVer=1 (bits20-23)
        uint status = 0x00100105;
        var names = SystemStatusBits.Decode(status);
        Assert.That(names, Does.Contain("StartUpBMS"));
        Assert.That(names, Does.Contain("MOS_CHG"));
        Assert.That(names, Does.Contain("Heat"));
        Assert.That(names, Does.Contain("ProjectVer=1"));
    }

    [Test]
    public void FaultBits_Decode()
    {
        var names = FaultBits.Decode(0x1003); // bit0 CellOvp + bit1 CellUvp + bit12 SocLow
        Assert.That(names, Does.Contain("CellOvp"));
        Assert.That(names, Does.Contain("CellUvp"));
        Assert.That(names, Does.Contain("SocLow"));
        Assert.That(FaultBits.Decode(0), Is.Empty);
    }

    [Test]
    public void ParseMac_FormatsColons()
    {
        var data = new byte[] { 0xA4, 0xC1, 0x38, 0x16, 0x02, 0x5A };
        Assert.That(BatterySnapshot.ParseMac(data), Is.EqualTo("A4:C1:38:16:02:5A"));
    }

    [Test]
    public void ParseSystemStatus_LowWordFirst()
    {
        var data = new byte[] { 0x12, 0x34, 0x56, 0x78 };
        Assert.That(BatterySnapshot.ParseSystemStatus(data), Is.EqualTo(0x56781234u));
        Assert.That(BatterySnapshot.ParseSystemStatus(new byte[2]), Is.Null);
    }

    [TestCase("BT_cs-0604", "cs-0604")]
    [TestCase("cs_0604", "cs_0604")]
    public void BluetoothName_NormalizeAndEncode(string input, string expectedSuffix)
    {
        Assert.That(BluetoothNameCodec.TryNormalize(input, out var suffix, out _, out var error), Is.True, error);
        Assert.That(suffix, Is.EqualTo(expectedSuffix));
        var bytes = BluetoothNameCodec.EncodeSuffix(suffix);
        Assert.That(bytes.Length % 2, Is.Zero);
        Assert.That(System.Text.Encoding.ASCII.GetString(bytes).TrimEnd('\0'), Is.EqualTo(expectedSuffix));
    }

    [TestCase("")]
    [TestCase("BT_")]
    [TestCase("bad name")]
    [TestCase("中文")]
    [TestCase("1234567890123456789012")]
    public void BluetoothName_InvalidSuffixRejected(string input)
    {
        Assert.That(BluetoothNameCodec.TryNormalize(input, out _, out _, out var error), Is.False);
        Assert.That(error, Is.Not.Empty);
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

    [Test]
    public async Task SppClient_DuplicateStaleFrame_ReturnsFreshData()
    {
        // 模拟迟到的上一帧重复通知污染下一轮累积器：第二轮必须返回新数据而非旧数据
        var t = new FakeSppTransport
        {
            ResponsePayload = new byte[] { 0x41, 0x41 }, // 第一轮 "AA"
            DuplicateLastResponse = true,                 // 第二轮先重发上一帧
        };
        using var client = new ModbusSppClient(t);

        var first = await client.ReadRegistersAsync(0xD120, 1, TimeSpan.FromSeconds(2), CancellationToken.None);
        Assert.That(first, Is.Not.Null);
        Assert.That(first!.Length, Is.EqualTo(2));

        // 第二轮换成 "BB"
        t.ResponsePayload = new byte[] { 0x42, 0x42 };
        var second = await client.ReadRegistersAsync(0xD120, 1, TimeSpan.FromSeconds(2), CancellationToken.None);
        Assert.That(second, Is.Not.Null);
        Assert.That(second!.Length, Is.EqualTo(2));
        Assert.That(second, Is.EqualTo(new byte[] { 0x42, 0x42 }), "不得返回迟到的旧帧数据");
    }

    [Test]
    public async Task SppClient_StaleFrameDifferentLength_DoesNotMatch()
    {
        // 上一轮是 11 寄存器（22 字节），本轮请求 1 寄存器（2 字节）——迟到帧长度不同必须被丢弃
        var t = new FakeSppTransport
        {
            ResponsePayload = new byte[22], // 第一轮 22 字节
            DuplicateLastResponse = true,
        };
        using var client = new ModbusSppClient(t);

        var first = await client.ReadRegistersAsync(0xD120, 11, TimeSpan.FromSeconds(2), CancellationToken.None);
        Assert.That(first, Is.Not.Null);

        t.ResponsePayload = new byte[] { 0x12, 0x34 };
        var second = await client.ReadRegistersAsync(0xD120, 1, TimeSpan.FromSeconds(2), CancellationToken.None);
        Assert.That(second, Is.Not.Null);
        Assert.That(second, Is.EqualTo(new byte[] { 0x12, 0x34 }));
    }

    private sealed class FakeSppTransport : IBleTransport
    {
        public byte[]? ResponsePayload;
        public bool Fragmented;
        public bool NoResponse;
        public bool WriteResult = true;

        /// <summary>true 时在发送本轮响应前先重发上一轮响应（模拟迟到重复帧）。</summary>
        public bool DuplicateLastResponse;

        private byte[]? _lastResponse;
        private readonly object _lock = new();

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
            byte[]? resp;
            byte[]? dup = null;
            lock (_lock)
            {
                if (!WriteResult || NoResponse || ResponsePayload is null)
                    return WriteResult;

                resp = frame[1] == ModbusRtu.FuncWriteMultiple
                    ? BuildWriteResponse(frame)
                    : BuildResponse(ResponsePayload);
                if (DuplicateLastResponse && _lastResponse is not null)
                {
                    // 先重发上一帧（完整），模拟迟到重复通知
                    dup = _lastResponse.ToArray();
                }
                _lastResponse = resp.ToArray();
            }

            if (dup is not null)
            {
                var d = dup;
                _ = Task.Run(async () =>
                {
                    await Task.Delay(1, ct);
                    SppNotifyReceived?.Invoke(d);
                });
            }

            if (Fragmented && resp is not null)
            {
                for (int off = 0; off < resp.Length; off += 20)
                {
                    int len = Math.Min(20, resp.Length - off);
                    var chunk = new byte[len];
                    Array.Copy(resp, off, chunk, 0, len);
                    SppNotifyReceived?.Invoke(chunk);
                    if (off + 20 < resp.Length) await Task.Delay(1, ct);
                }
            }
            else if (resp is not null)
            {
                SppNotifyReceived?.Invoke(resp);
            }
            return true;
        }

        private static byte[] BuildResponse(byte[] payload)
        {
            var resp = new byte[3 + payload.Length + 2];
            resp[0] = 0x01;
            resp[1] = 0x03;
            resp[2] = (byte)payload.Length;
            payload.CopyTo(resp, 3);
            ushort crc = Crc16.Compute(resp.AsSpan(0, 3 + payload.Length));
            resp[^2] = (byte)(crc & 0xFF);
            resp[^1] = (byte)(crc >> 8);
            return resp;
        }

        private static byte[] BuildWriteResponse(byte[] request)
        {
            var resp = request[..6];
            ushort crc = Crc16.Compute(resp);
            return resp.Concat(new[] { (byte)(crc & 0xFF), (byte)(crc >> 8) }).ToArray();
        }

        public Task DisconnectAsync() => Task.CompletedTask;
        public ValueTask DisposeAsync() => ValueTask.CompletedTask;
    }
}

