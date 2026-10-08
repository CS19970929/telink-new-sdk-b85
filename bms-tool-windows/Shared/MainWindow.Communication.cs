using System.Windows;
using System.Windows.Media;

namespace BmsTool.Windows;

public partial class MainWindow
{
    private readonly BmsCommunicationStatistics _communicationStatistics = new();
    private long _communicationReconnectAttempts;
    private DateTime _communicationNextLogUtc;
    private long _communicationLoggedFailures;

    private void UpdateCommunicationStatus()
    {
        var s = _communicationStatistics.Snapshot();
        string rtt = s.LastResponseMs is double ms ? $"{ms:F1}ms" : "—";
        CommunicationCountersText.Text = $"通信统计：TX {s.TxFrames}帧 / RX {s.RxFrames}帧 · 超时 {s.Timeouts} · CRC {s.CrcErrors} · 异常 {s.InvalidResponses + s.ExceptionResponses} · 发送失败 {s.SendFailures} · 重连 {_communicationReconnectAttempts}";
        string detail = $"本窗口累计，断开和重连后保留。TX 为 transport 写入成功的请求；RX 为 CRC、地址、功能和长度校验通过的响应（包含异常应答）。\n发送 {s.TxBytes}B / 原始接收 {s.RxBytes}B；最近响应 {rtt}；取消 {s.Cancellations}。\n最近异常：{(string.IsNullOrEmpty(s.LastError) ? "无" : s.LastError)}";
        CommunicationCountersText.ToolTip = detail;
        CustomerCommStatusBorder.Visibility = Visibility.Visible;
        string state;
        Brush color;
        if (_otaRunning) { state = "OTA中"; color = Brushes.LightGray; }
        else if (_autoReconnectRunning) { state = "重连中"; color = Brushes.Orange; }
        else if (_bms is null) { state = "未连接"; color = Brushes.LightGray; }
        else if (s.LastResponseAgeMs is double age && age < 3200) { state = "正在通讯"; color = LampOnBrush; }
        else if (_diagBusy || _eventLogReadInProgress || _shBusy || ShFactoryBusy()) { state = "业务读取中"; color = Brushes.Orange; }
        else { state = _polling ? "等待响应" : "数据未更新"; color = Brushes.Orange; }
        CommunicationStatusText.Text = "通信：" + state;
        CustomerCommStatusBorder.Background = color;
        CustomerCommStatusBorder.ToolTip = detail + $"\n电池快照：{(_customerLastDataUtc == DateTime.MinValue ? "未读取" : _customerLastDataUtc.ToLocalTime().ToString("HH:mm:ss.fff"))}。";
        long failures = s.Timeouts + s.CrcErrors + s.InvalidResponses + s.ExceptionResponses + s.SendFailures;
        if (failures != _communicationLoggedFailures || DateTime.UtcNow >= _communicationNextLogUtc) {
            _communicationLoggedFailures = failures;
            _communicationNextLogUtc = DateTime.UtcNow.AddSeconds(30);
            AppendLog($"TX={s.TxFrames}/{s.TxBytes}B; RX={s.RxFrames}/{s.RxBytes}B; timeout={s.Timeouts}; crc={s.CrcErrors}; invalid={s.InvalidResponses}; exception={s.ExceptionResponses}; sendFail={s.SendFailures}; cancelled={s.Cancellations}; reconnect={_communicationReconnectAttempts}; lastRtt={rtt}; lastError={s.LastError}", "COMM_STATS");
        }
    }
}
