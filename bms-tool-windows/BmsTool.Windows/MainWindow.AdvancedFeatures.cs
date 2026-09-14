using System.Collections.ObjectModel;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;

namespace BmsTool.Windows;

public partial class MainWindow
{
    private readonly ObservableCollection<DeviceEventLogRow> _deviceEventLogs = new();
    private readonly List<LongTermMonitorRecord> _longTermRecords = new();

    private bool _advancedFeaturesInitialized;
    private TextBlock? _eventLogStatus;
    private TextBlock? _longTermStatus;
    private ComboBox? _longTermIntervalBox;
    private Button? _longTermStartButton;
    private Button? _longTermStopButton;
    private Button? _longTermExportButton;
    private DispatcherTimer? _advancedFeatureTimer;
    private bool _longTermMonitoring;
    private DateTime _longTermStartedAt;
    private DateTime _longTermLastRecordAt = DateTime.MinValue;
    private string _longTermLastUpdateToken = string.Empty;

    static MainWindow()
    {
        EventManager.RegisterClassHandler(
            typeof(MainWindow),
            FrameworkElement.LoadedEvent,
            new RoutedEventHandler(AdvancedFeatures_ClassLoaded),
            true);
    }

    private static void AdvancedFeatures_ClassLoaded(object sender, RoutedEventArgs e)
    {
        if (sender is not MainWindow window) return;
        window.Dispatcher.BeginInvoke(window.InitializeAdvancedFeatures, DispatcherPriority.Loaded);
    }

    private void InitializeAdvancedFeatures()
    {
        AddSh3520ParameterTab();
        AddAfeHardwareTab();
        if (_advancedFeaturesInitialized) return;
        _advancedFeaturesInitialized = true;

        AddLongTermMonitorToolbarToMainPage();
        AddEventLogTab();

        _advancedFeatureTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(500) };
        _advancedFeatureTimer.Tick += (_, _) => AdvancedFeatureTimer_Tick();
        _advancedFeatureTimer.Start();

        AppendLog(_afeHardwareEditorLaunchEnabled
            ? "客户版高级功能已启用；AFE硬件编辑器启动开关已打开，但仍需高级功能验证。"
            : "客户版高级功能已启用；AFE硬件编辑器默认隐藏。", "APP");
    }

    private async Task WaitForCommunicationIdleAsync()
    {
        if (_autoReconnectRunning)
            throw new InvalidOperationException("设备正在后台重连，请稍后再操作参数。");

        for (int i = 0; i < 50 && _polling; i++)
            await Task.Delay(40);

        if (_polling)
            throw new IOException("实时数据读取仍在进行，无法安全开始参数事务。");
    }
}

public sealed record DeviceEventLogRow(int Position, string EventName, string IntervalText, string StateText);
