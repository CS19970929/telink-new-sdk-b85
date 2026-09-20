using Microsoft.Win32;
using System.Windows;
using System.Windows.Controls;

namespace BmsTool.Windows;

public partial class MainWindow
{
    private BmsDiagnosticSession? _observationSession;
    private string? _observationEnd;
    private BmsClient? _observationClient;

    private void AddObservationControls(Panel controls)
    {
        _diagAuto.Unchecked += (_, _) => {
            if (_observationSession is not null && _observationEnd is null)
                ObservationGap("paused_by_user");
        };
        var start = new Button { Content = "开始诊断会话（写盘）", Margin = new Thickness(4) };
        start.Click += async (_, _) =>
        {
            if (_diagBusy || _observationSession is not null) return;
            if (_bms is null) { _diagStatus.Text = "请先连接指定设备"; return; }
            var folder = new OpenFolderDialog { Title = "选择诊断会话保存目录" };
            if (folder.ShowDialog(this) != true) return;
            try
            {
                _observationSession = new BmsDiagnosticSession(folder.FolderName, ConnectionText.Text);
                _observationEnd = null;
                _observationClient = _bms;
                _diagAuto.IsChecked = true;
                await CaptureDiagnosticsAsync(false);
            }
            catch (Exception ex) { _diagStatus.Text = "会话创建失败：" + ex.Message; }
        };
        var stop = new Button { Content = "结束诊断会话", Margin = new Thickness(4) };
        stop.Click += (_, _) => StopObservationSession("cancelled");
        controls.Children.Add(start); controls.Children.Add(stop);
    }

    private void StopObservationSession(string outcome)
    {
        if (_observationSession is null) return;
        _observationEnd ??= outcome;
        _diagAuto.IsChecked = false;
        if (_diagBusy) { _diagCts?.Cancel(); return; }
        FinishObservationSession();
    }

    private void FinishObservationSession()
    {
        if (_observationSession is null || _observationEnd is null) return;
        var session = _observationSession;
        _observationSession = null;
        _observationClient = null;
        try
        {
            session.Finish(_observationEnd);
            _diagStatus.Text = $"会话已保存：{session.DirectoryPath} · 完整 {session.CompleteSamples}/{session.Samples} · 缺口 {session.Gaps}";
        }
        catch (Exception ex) { _diagStatus.Text = "会话保存失败，已有 JSONL 保留：" + ex.Message; }
        finally { _observationEnd = null; }
    }

    private void ObservationGap(string reason)
    {
        try { _observationSession?.NoteGap(reason); }
        catch (Exception ex)
        {
            StopObservationSession("storage_failed");
            _diagStatus.Text = "会话写盘失败：" + ex.Message;
        }
    }
}
