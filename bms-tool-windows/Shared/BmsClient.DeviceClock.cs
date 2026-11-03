using System.Diagnostics;
using System.Buffers.Binary;
using System.IO;

namespace BmsTool.Windows;

public sealed record DeviceClockReading(bool Running, DateTimeOffset? UtcTime, bool Synchronized);

public sealed partial class BmsClient
{
    private bool _deviceClockUnsupported;
    public const ushort DeviceClockBase = 0x2E70;
    public const int DeviceClockSyncThresholdSeconds = 300;

    // UTC在线上传输；显示时才转换为电脑本地时区，夏令时不改写板内日历。
    public async Task<DeviceClockReading?> ReadAndSynchronizeDeviceClockAsync(CancellationToken ct = default)
    {
        if (!IsD015 || _deviceClockUnsupported) return null;
        DeviceClockReading? reading;
        try { reading = await ReadDeviceClockAsync(ct).ConfigureAwait(false); }
        catch (BmsModbusException ex) when (ex.Code is 1 or 2)
        {
            _deviceClockUnsupported = true; // 旧D015每次连接只探测一次，不持续产生非法地址请求。
            return null;
        }
        if (reading is null) { _deviceClockUnsupported = true; return null; }
        if (!reading.Running || !DeviceClockNeedsSync(reading.UtcTime, DateTimeOffset.UtcNow)) return reading;

        DateTimeOffset target = DateTimeOffset.UtcNow;
        if (target.Year is < 2000 or > 2099) throw new IOException("电脑日期超出板载RTC范围2000～2099年，请先修正电脑时间。");
        ushort[] words = { (ushort)target.Year, (ushort)(target.Month << 8 | target.Day),
            (ushort)(target.Hour << 8 | target.Minute), (ushort)target.Second };
        long sent = Stopwatch.GetTimestamp();
        byte[] raw = new byte[8];
        for (int i = 0; i < words.Length; i++) BinaryPrimitives.WriteUInt16BigEndian(raw.AsSpan(i * 2), words[i]);
        byte[] request = ModbusRtu.WriteMultiple(DeviceClockBase + 3, raw);
        ModbusRtu.ValidateWriteMultipleAck(await TransactAsync(request, ct).ConfigureAwait(false), DeviceClockBase + 3, 4);
        DeviceClockReading? verified = await ReadDeviceClockAsync(ct).ConfigureAwait(false);
        DateTimeOffset expected = target.Add(Stopwatch.GetElapsedTime(sent));
        if (verified?.UtcTime is not DateTimeOffset actual || (actual - expected).Duration() > TimeSpan.FromSeconds(10))
            throw new IOException("板载时间校时后回读不一致，将在后续轮询重新读取。");
        return verified with { Synchronized = true };
    }

    public static bool DeviceClockNeedsSync(DateTimeOffset? boardUtc, DateTimeOffset computerUtc) =>
        boardUtc is null || (boardUtc.Value - computerUtc).Duration() > TimeSpan.FromSeconds(DeviceClockSyncThresholdSeconds);

    private async Task<DeviceClockReading?> ReadDeviceClockAsync(CancellationToken ct)
    {
        ushort[] words = await ReadRegistersAsync(DeviceClockBase, 7, ct).ConfigureAwait(false);
        if (words[0] != 0x5254 || words[1] != 1 || (words[2] & ~3) != 0) return null;
        if ((words[2] & 1) == 0) return new(false, null, false);
        if ((words[2] & 2) == 0) return new(true, null, false);
        try
        {
            if (words[3] is < 2000 or > 2099 || words[6] > 59) throw new ArgumentOutOfRangeException();
            return new(true, new DateTimeOffset(words[3], words[4] >> 8, words[4] & 255,
                words[5] >> 8, words[5] & 255, words[6], TimeSpan.Zero), false);
        }
        catch (ArgumentOutOfRangeException) { return new(true, null, false); }
    }
}
