using System.Buffers.Binary;
using BmsTool.Windows;

// 真实 ReadBatteryAsync / 接收拼帧，不需要设备或 UI 消息泵。
static class MonitorPollingTest
{
    static void Check(bool value, string why) { if (!value) throw new Exception(why); }
    public static async Task RunAsync()
    {
        await using var client = new BmsClient(new PollTransport());
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
        Console.WriteLine("PASS polling: coherent cell fields, existing realtime units, no UI-pump dependency");
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
            else throw new Exception($"Unexpected polling request {address:X4}");
            byte[] body = new byte[3+2*count]; body[0]=1; body[1]=3; body[2]=(byte)(2*count);
            for(int i=0;i<count;i++) BinaryPrimitives.WriteUInt16BigEndian(body.AsSpan(3+2*i,2),w[i]);
            var frame = ModbusRtu.Frame(body);
            await Task.Delay(1,ct).ConfigureAwait(false);
            DataReceived?.Invoke(frame.AsMemory(0,4)); DataReceived?.Invoke(frame.AsMemory(4));
        }
    }
}
