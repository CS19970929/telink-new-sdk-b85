#if BMS_FACTORY_APP
using System.Text;
using System.Windows.Threading;

namespace BmsTool.Windows;

public partial class MainWindow
{
    private const int UiLogMaxChars = 262144;
    private const int UiLogPendingMaxChars = 262144;
    private const int UiLogMaxLineChars = 16384;
    private readonly object _uiLogGate = new();
    private readonly Queue<string> _uiLogPending = new();
    private int _uiLogPendingChars;
    private bool _uiLogClosed;
    private DispatcherTimer? _uiLogTimer;

    private void InitializeUiLog()
    {
        LogBox.IsUndoEnabled = false;
        _uiLogTimer = new DispatcherTimer(DispatcherPriority.Background) { Interval = TimeSpan.FromMilliseconds(200) };
        _uiLogTimer.Tick += (_, _) => FlushUiLog();
        _uiLogTimer.Start();
        Closed += (_, _) => {
            _uiLogTimer.Stop();
            lock (_uiLogGate) { _uiLogClosed = true; _uiLogPending.Clear(); _uiLogPendingChars = 0; }
        };
    }

    // 完整原文已先写入 SessionLogger；此队列只供界面查看，可丢弃旧显示行。
    private void QueueUiLog(string text, string category)
    {
        if (text.Length > UiLogMaxLineChars) text = text[..UiLogMaxLineChars] + "…（完整内容见日志文件）";
        string line = $"{DateTime.Now:HH:mm:ss.fff} [{category}] {text}{Environment.NewLine}";
        lock (_uiLogGate) {
            if (_uiLogClosed) return;
            _uiLogPending.Enqueue(line); _uiLogPendingChars += line.Length;
            while (_uiLogPending.Count > 1000 || _uiLogPendingChars > UiLogPendingMaxChars)
                _uiLogPendingChars -= _uiLogPending.Dequeue().Length;
        }
    }

    private void FlushUiLog()
    {
        var batch = new StringBuilder();
        lock (_uiLogGate) {
            for (int i = 0; i < 100 && _uiLogPending.Count != 0; ++i) {
                string line = _uiLogPending.Dequeue(); _uiLogPendingChars -= line.Length;
                batch.Append(line);
            }
        }
        if (batch.Length == 0) return;
        string addition = batch.ToString();
        if (LogBox.Text.Length + addition.Length > UiLogMaxChars) {
            string tail = LogBox.Text + addition;
            int start = Math.Max(0, tail.Length - UiLogMaxChars * 3 / 4);
            int newline = tail.IndexOf('\n', start);
            LogBox.Text = tail[(newline >= 0 ? newline + 1 : start)..];
        }
        else LogBox.AppendText(addition);
        LogBox.ScrollToEnd();
    }
}
#endif
