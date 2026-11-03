using BmsTool.Windows;
using System.Buffers.Binary;
using System.IO;

static class Program
{
    static void Check(bool ok, string message) { if (!ok) throw new Exception(message); }
    static async Task Main(string[] args)
    {
        DateTimeOffset now = DateTimeOffset.UtcNow;
        foreach (int difference in new[] { -301, -300, 0, 300, 301 })
            Check(BmsClient.DeviceClockNeedsSync(now.AddSeconds(difference), now) == (Math.Abs(difference) > 300), "严格大于5分钟");
        Check(BmsClient.DeviceClockNeedsSync(null, now), "未校时需要同步");
        Check(!BmsClient.DeviceClockNeedsSync(now.ToOffset(TimeSpan.FromHours(8)), now), "时区变化不改UTC");

        foreach (int unsupported in new[] { 0, 1, 2 })
        {
            await using var transport = new ClockTransport { UnsupportedCode = (byte)unsupported };
            await using var client = new BmsClient(transport);
            Check(await client.ReadAndSynchronizeDeviceClockAsync() is null && transport.Reads == 0, "其他产品不探测时钟");
            await client.ReadRegistersAsync(0x2500, 2); // 通过现有硬件能力识别D015。
            var reading = await client.ReadAndSynchronizeDeviceClockAsync();
            if (unsupported != 0)
            {
                Check(reading is null && transport.Writes == 0, "旧固件不写入");
                await client.ReadAndSynchronizeDeviceClockAsync();
                Check(transport.Reads == 1, "旧固件每连接只探测一次");
            }
            else
            {
                Check(reading?.Synchronized == true && transport.Writes == 1, "初次自动校时并回读");
                Check(reading!.UtcTime?.Offset == TimeSpan.Zero && (reading.UtcTime.Value - DateTimeOffset.UtcNow).Duration().TotalSeconds < 3, "读取真实UTC");
                await client.ReadAndSynchronizeDeviceClockAsync();
                Check(transport.Writes == 1, "正常轮询不重复写");
                transport.Utc = DateTimeOffset.UtcNow.AddMinutes(-10);
                Check((await client.ReadAndSynchronizeDeviceClockAsync())?.Synchronized == true && transport.Writes == 2, "慢10分钟自动校时");
                transport.Utc = DateTimeOffset.UtcNow.AddMinutes(10);
                await client.ReadAndSynchronizeDeviceClockAsync();
                Check(transport.Writes == 3, "快10分钟自动校时");
                transport.Unavailable = true;
                Check((await client.ReadAndSynchronizeDeviceClockAsync())?.Running == false && transport.Writes == 3, "RTC失败不反复写");
                transport.Unavailable = false;
                transport.BadCalendar = true;
                await client.ReadAndSynchronizeDeviceClockAsync();
                Check(transport.Writes == 4, "非法日历触发校时");
                transport.Utc = DateTimeOffset.UtcNow.AddHours(-1); transport.LoseAck = true;
                bool failed = false;
                try { await client.ReadAndSynchronizeDeviceClockAsync(); } catch (IOException) { failed = true; }
                Check(failed && transport.Writes == 5, "丢ACK时本次失败");
                await client.ReadAndSynchronizeDeviceClockAsync();
                Check(transport.Writes == 5, "恢复后先读，已生效时间不重复写");
                transport.Utc = DateTimeOffset.UtcNow.AddHours(-1); transport.IgnoreWrite = true;
                failed = false;
                try { await client.ReadAndSynchronizeDeviceClockAsync(); } catch (IOException) { failed = true; }
                Check(failed, "ACK不等于校时成功，验证实际回读");
            }
            Check(transport.MaxRequest <= 20, "读写单帧兼容MTU23");
        }
        if (args.Length == 1)
        {
            await using var transport = new FirmwareTransport(args[0]);
            await using var client = new BmsClient(transport);
            await client.ReadIdentityAsync("", "D015 clock firmware interop");
            var reading = await client.ReadAndSynchronizeDeviceClockAsync();
            Check(reading?.Synchronized == true, "实际C固件首次校时");
            Check((await client.ReadAndSynchronizeDeviceClockAsync())?.Synchronized == false, "实际C固件回读后不重复写");
            bool rejected = false;
            try { await client.WriteSingleRegisterAsync(0x2E73, 2027); }
            catch (IOException ex) when (ex.Message.Contains("code=0x03", StringComparison.Ordinal)) { rejected = true; }
            Check(rejected, "实际C固件拒绝半组时间写入");
            Console.WriteLine("PASS actual D015 C firmware clock read/write/verify and rejected partial write");
        }
        Console.WriteLine("PASS device clock: UTC, strict 300s boundary, cold-start, drift, unavailable/invalid, old firmware, lost ACK, verified write, MTU23");
    }
}

sealed class ClockTransport : IBmsTransport
{
    public bool IsConnected => true;
    public string DiscoveryDescription => "clock test";
    public event Action<ReadOnlyMemory<byte>>? DataReceived;
    public event Action<string>? ConnectionProgress { add { } remove { } }
    public Task ReconnectAsync(CancellationToken ct = default) => Task.CompletedTask;
    public DateTimeOffset? Utc;
    public byte UnsupportedCode;
    public bool Unavailable, BadCalendar, LoseAck, IgnoreWrite;
    public int Reads, Writes, MaxRequest;
    public Task WriteAsync(ReadOnlyMemory<byte> bytes, CancellationToken ct)
    {
        byte[] frame = bytes.ToArray();
        MaxRequest = Math.Max(MaxRequest, frame.Length);
        ushort reg = BinaryPrimitives.ReadUInt16BigEndian(frame.AsSpan(2));
        byte[] response;
        if (reg == 0x2500) response = ModbusRtu.Frame(new byte[] { 1, 3, 4, 0, 1, 0x35, 0x20 });
        else if (frame[1] == 3)
        {
            Reads++;
            if (UnsupportedCode != 0) response = ModbusRtu.Frame(new byte[] { 1, 0x83, UnsupportedCode });
            else
            {
                DateTimeOffset date = Utc ?? new DateTimeOffset(2000, 1, 1, 0, 0, 0, TimeSpan.Zero);
                ushort[] words = { 0x5254, 1, (ushort)(Unavailable ? 0 : Utc is null ? 1 : 3),
                    (ushort)date.Year, (ushort)(BadCalendar ? 0x0230 : date.Month << 8 | date.Day),
                    (ushort)(date.Hour << 8 | date.Minute), (ushort)date.Second };
                byte[] body = new byte[17]; body[0] = 1; body[1] = 3; body[2] = 14;
                for (int i = 0; i < 7; i++) BinaryPrimitives.WriteUInt16BigEndian(body.AsSpan(3 + i * 2), words[i]);
                response = ModbusRtu.Frame(body);
            }
        }
        else
        {
            Writes++;
            if (!IgnoreWrite)
            {
                Utc = new DateTimeOffset(BinaryPrimitives.ReadUInt16BigEndian(frame.AsSpan(7)), frame[9], frame[10],
                    frame[11], frame[12], frame[14], TimeSpan.Zero);
                BadCalendar = false;
            }
            if (LoseAck) { LoseAck = false; throw new IOException("模拟写入生效但ACK丢失"); }
            response = ModbusRtu.Frame(frame.AsSpan(0, 6));
        }
        DataReceived?.Invoke(response);
        return Task.CompletedTask;
    }
    public ValueTask DisposeAsync() => ValueTask.CompletedTask;
}
