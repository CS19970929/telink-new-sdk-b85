using System.Buffers.Binary;
using BmsTool.Windows;

// 真实 ReadBatteryAsync / 接收拼帧，不需要设备或 UI 消息泵。
static class MonitorPollingTest
{
    static void Check(bool value, string why) { if (!value) throw new Exception(why); }
    public static async Task RunAsync()
    {
        var legacyTransport = new PollTransport();
        await using var client = new BmsClient(legacyTransport);
        var snapshot = await client.ReadBatteryAsync();
        Check(snapshot.CellMillivolts[15] == 0 && snapshot.MinCellMv == 0 &&
            snapshot.MaxCellMv == 3342 && snapshot.CellDeltaMv == 3342 && snapshot.MinCellPosition == 16,
            "cell extrema and positions must use the same D000 response as cell array");
        Check(snapshot.PackVoltageV == 53.41 && snapshot.SocPercent == 76,
            "D120 pack and SOC retain existing semantics");
        Check(snapshot.CellDataWindow=="D000" && snapshot.ReadStartedUtc<=snapshot.CellsReadUtc &&
            snapshot.CellsReadUtc<=snapshot.StatusReadUtc && snapshot.StatusReadUtc<=snapshot.RealtimeReadUtc,
            "PC read-window timestamps preserve provenance without claiming atomic measurement");
        // 调用者的 UI 上下文不调度回调，协议层仍须能够收齐、解析并返回。
        var previous = SynchronizationContext.Current;
        Task<BatterySnapshot> read;
        try { SynchronizationContext.SetSynchronizationContext(new NoPumpContext()); read = client.ReadBatteryAsync(); }
        finally { SynchronizationContext.SetSynchronizationContext(previous); }
        await read.WaitAsync(TimeSpan.FromSeconds(3));
        Check(snapshot.LowPowerFault is null && legacyTransport.LowPowerReads == 0 &&
            legacyTransport.HardwareProtectionReads == 0,
            "old devices must not get an extra low-power status request");

        var capableTransport = new PollTransport { AdvertisesLowPowerFault = true, LowPowerWord = 0x0001 };
        await using var capableClient = new BmsClient(capableTransport);
        var healthy = await capableClient.ReadBatteryAsync();
        Check(healthy.LowPowerFault == false && !healthy.HasProtection,
            "cell imbalance error byte must not be decoded as a low-power fault");

        capableTransport.LowPowerWord = 0x0100;
        var faulted = await capableClient.ReadBatteryAsync();
        Check(faulted.LowPowerFault == true && faulted.HasProtection &&
            faulted.ProtectionLevel3Text.Contains("RTC／低功耗故障") && faulted.ProtectionLevel3Raw == 0,
            "high-byte low-power fault must appear in the monitor without changing the raw protection bits");

        capableTransport.LowPowerWord = 0;
        var cleared = await capableClient.ReadBatteryAsync();
        Check(cleared.LowPowerFault == false && !cleared.HasProtection && capableTransport.LowPowerReads == 3,
            "low-power status must refresh on each capable device read");
        var hardwareTransport = new PollTransport { AdvertisesHardwareProtection = true, LegacyThird = 0x20, HardwareProtection = 2 };
        await using var hardwareClient = new BmsClient(hardwareTransport);
        var shorted = await hardwareClient.ReadBatteryAsync();
        Check(shorted.HasProtection && shorted.ProtectionLevel3Raw == 0x20 && shorted.ProtectionLevel3Text == "放电短路",
            "C060 short-circuit must be shown by name without the legacy overcurrent alias");

        hardwareTransport.SoftwareThird = 0x10;
        hardwareTransport.LegacyThird = 0x33;
        hardwareTransport.HardwareProtection = 0x0d;
        var combined = await hardwareClient.ReadBatteryAsync();
        Check(combined.ProtectionLevel3Text == "充电过流、硬件放电过流、硬件单体过压、硬件单体欠压",
            "C060 hardware and software trips must coexist across the same status read");

        hardwareTransport.SoftwareThird = 0;
        hardwareTransport.HardwareProtection = 0;
        var recovered = await hardwareClient.ReadBatteryAsync();
        Check(!recovered.HasProtection && recovered.ProtectionLevel3Text == "无" && hardwareTransport.HardwareProtectionReads == 3,
            "a fresh C060 status read must clear an earlier merged protection snapshot");

        hardwareTransport.RejectHardwareRead = true;
        hardwareTransport.LegacyThird = 0x20;
        var fallback = await hardwareClient.ReadBatteryAsync();
        Check(fallback.HasProtection && fallback.HardwareProtectionStateUnknown &&
            fallback.ProtectionLevel3Text == "放电过流、硬件保护详情未确认",
            "a failed C060 status read must fall back to the legacy protection word");
        Console.WriteLine("PASS polling: coherent readings, no UI-pump dependency, optional RTC and C060 hardware protection reporting");
    }
    sealed class NoPumpContext : SynchronizationContext
    {
        public override void Post(SendOrPostCallback callback, object? state) { }
    }
    sealed class PollTransport : IBmsTransport
    {
        public bool IsConnected => true;
        public bool RequiresSerialWakeup => false;
        public string DiscoveryDescription => "offline polling";
        public bool AdvertisesLowPowerFault { get; init; }
        public ushort LowPowerWord { get; set; }
        public int LowPowerReads { get; private set; }
        public bool AdvertisesHardwareProtection { get; init; }
        public bool RejectHardwareRead { get; set; }
        public ushort LegacyThird { get; set; }
        public ushort SoftwareThird { get; set; }
        public ushort HardwareProtection { get; set; }
        public int HardwareProtectionReads { get; private set; }
        public event Action<ReadOnlyMemory<byte>>? DataReceived;
        public event Action<string>? ConnectionProgress { add { } remove { } }
        public Task ReconnectAsync(CancellationToken ct = default) => Task.CompletedTask;
        public ValueTask DisposeAsync() => ValueTask.CompletedTask;
        public async Task WriteAsync(ReadOnlyMemory<byte> request, CancellationToken ct = default)
        {
            byte[] data = request.ToArray();
            ushort address = BinaryPrimitives.ReadUInt16BigEndian(data.AsSpan(2,2));
            ushort count = BinaryPrimitives.ReadUInt16BigEndian(data.AsSpan(4,2));
            var w = new ushort[count];
            if (address == 0xD000) {
                Array.Fill(w,(ushort)61001,0,32); Array.Fill(w,(ushort)3337,0,16); w[15]=0;
                w[32]=3342; w[33]=0; w[34]=9; w[35]=16; w[36]=3342;
                w[60]=LegacyThird;
            }
            else if (address == 0xD115) w[0]=12;
            else if (address == 0xD120) {
                w[0]=0x4253; w[1]=1; w[2]=5341; w[4]=76;
                w[8]=3347; w[9]=3336; w[10]=11; // 较晚窗口已经恢复。
            }
            else if (address == 0x2AF0) {
                if (AdvertisesLowPowerFault || AdvertisesHardwareProtection)
                    w = [0x424D, 1, 0xC060, 0x0020, 0x7693, 2, 20, 20, 20, 0];
                if (AdvertisesLowPowerFault) w[9] |= 0x0020;
                if (AdvertisesHardwareProtection) w[9] |= 0x0040;
            }
            else if (address == 0xD04F && count == 1 && AdvertisesLowPowerFault) {
                ++LowPowerReads;
                w[0] = LowPowerWord;
            }
            else if (address == 0x2D10 && count == 2 && AdvertisesHardwareProtection) {
                ++HardwareProtectionReads;
                if (RejectHardwareRead) {
                    await Task.Delay(1,ct).ConfigureAwait(false);
                    DataReceived?.Invoke(ModbusRtu.Frame(new byte[] { 1, 0x83, 2 }));
                    return;
                }
                w[0] = SoftwareThird;
                w[1] = HardwareProtection;
            }
            else throw new Exception($"Unexpected polling request {address:X4}");
            byte[] body = new byte[3+2*count]; body[0]=1; body[1]=3; body[2]=(byte)(2*count);
            for(int i=0;i<count;i++) BinaryPrimitives.WriteUInt16BigEndian(body.AsSpan(3+2*i,2),w[i]);
            var frame = ModbusRtu.Frame(body);
            await Task.Delay(1,ct).ConfigureAwait(false);
            DataReceived?.Invoke(frame.AsMemory(0,4)); DataReceived?.Invoke(frame.AsMemory(4));
        }
    }
}
