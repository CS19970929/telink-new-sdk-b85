using System.Diagnostics;

namespace BmsTool.Windows;

public partial class MainWindow
{
    private BmsClient? _deviceClockClient;
    private long? _deviceClockReadTick;
    private int _deviceClockReadIntervalSeconds = 5;
    private bool _deviceClockBusy;

    private async Task RefreshDeviceClockAsync(BmsClient client, CancellationToken ct = default)
    {
        if (_deviceClockBusy) return;
        if (ReferenceEquals(client, _deviceClockClient) && _deviceClockReadTick is long last &&
            Stopwatch.GetElapsedTime(last).TotalSeconds < _deviceClockReadIntervalSeconds) return;
        _deviceClockBusy = true;
        _deviceClockClient = client;
        try
        {
            var reading = await client.ReadAndSynchronizeDeviceClockAsync(ct);
            if (!ReferenceEquals(client, _bms)) return;
            _deviceClockReadIntervalSeconds = 5;
            string text = reading is null ? "当前固件不支持板载时间" : !reading.Running ? "RTC不可用" :
                reading.UtcTime is DateTimeOffset utc ? $"{utc.ToLocalTime():yyyy-MM-dd HH:mm:ss zzz}" : "时间未同步";
            DeviceClockText.Text = "板载时间：" + text;
            CustomerClockText.Text = DeviceClockText.Text;
            if (reading?.Synchronized == true)
                AppendLog("板载时间已自动同步到电脑UTC时间，回读验证通过。", "CLOCK");
        }
        catch (OperationCanceledException) when (ct.IsCancellationRequested) { throw; }
        catch (Exception ex)
        {
            if (!ReferenceEquals(client, _bms)) return;
            _deviceClockReadIntervalSeconds = 30; // 失败不阻塞电池轮询，退避后先读再判断是否需要写。
            DeviceClockText.Text = CustomerClockText.Text = "板载时间：读取或校时失败，稍后重试";
            AppendLog("板载时间读取/校时失败：" + ex.Message, "CLOCK");
        }
        finally { _deviceClockReadTick = Stopwatch.GetTimestamp(); _deviceClockBusy = false; }
    }
}
