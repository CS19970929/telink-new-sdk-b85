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
        Check(snapshot.LowPowerFault is null && legacyTransport.LowPowerReads == 0,
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
        Console.WriteLine("PASS polling: coherent readings, no UI-pump dependency and optional RTC fault reporting");
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
            }
            else if (address == 0xD115) w[0]=12;
            else if (address == 0xD120) {
                w[0]=0x4253; w[1]=1; w[2]=5341; w[4]=76;
                w[8]=3347; w[9]=3336; w[10]=11; // 较晚窗口已经恢复。
            }
            else if (address == 0x2AF0) {
                if (AdvertisesLowPowerFault) { w[0] = 0x424D; w[1] = 1; w[9] = 0x0020; }
            }
            else if (address == 0xD04F && count == 1 && AdvertisesLowPowerFault) {
                ++LowPowerReads;
                w[0] = LowPowerWord;
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
