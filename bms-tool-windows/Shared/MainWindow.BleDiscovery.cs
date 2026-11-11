using System.Windows;
using System.Windows.Threading;
namespace BmsTool.Windows;

public partial class MainWindow
{
    private readonly LatestConnectionOperation _connectionOperations = new();
    private readonly DispatcherTimer _bleListTimer = new() { Interval = TimeSpan.FromSeconds(1) };
    private CancellationTokenSource? _manualConnectCts;
    private bool _manualConnectionInProgress, _connectionDesired, _bleWindowClosed, _bleScanRunning;
    private long _bleScanGeneration;
    private int _bleRefreshQueued;
    private string? _bleScanProblem;

    private void InitializeBleDiscoveryUi()
    {
        var view = System.Windows.Data.CollectionViewSource.GetDefaultView(_devices);
        view.GroupDescriptions.Add(new System.Windows.Data.PropertyGroupDescription(nameof(DiscoveredDevice.Category)));
        _bleListTimer.Tick += (_, _) => RefreshBleDeviceList();
        Loaded += (_, _) => _bleListTimer.Start();
        Closed += (_, _) =>
        {
            _bleWindowClosed = true; _bleListTimer.Stop(); ++_bleScanGeneration;
            _watcher?.Dispose(); _manualConnectCts?.Cancel(); _connectionOperations.Cancel();
        };
    }
    private void StartBleDiscovery()
    {
        if (_manualConnectionInProgress || _connectionOperations.IsBusy)
        {
            BleDiscoveryStatusText.Text = "连接操作尚未结束，请先取消或等待完成。"; return;
        }
        long generation = ++_bleScanGeneration;
        _watcher?.Dispose();
        _devices.Clear(); _deviceMap.Clear();
        _bleScanProblem = null; _bleScanRunning = true;
        var scanner = new BmsBleScanner(_ => { }, msg => AppendLog(msg, "SCAN"));
        _watcher = scanner;
        scanner.DevicesChanged += () => QueueBleRefresh(generation);
        scanner.ScanFailed += message => Dispatcher.BeginInvoke(() =>
        {
            if (generation != _bleScanGeneration || _bleWindowClosed) return;
            _bleScanProblem = message; RefreshBleDeviceList();
        });
        scanner.Start();
        RefreshBleDeviceList();
    }
    private void StopBleDiscovery()
    {
        ++_bleScanGeneration; _bleScanRunning = false;
        _watcher?.Stop();
        RefreshBleDeviceList();
    }
    private void QueueBleRefresh(long generation)
    {
        if (_bleWindowClosed || Interlocked.Exchange(ref _bleRefreshQueued, 1) != 0) return;
        Dispatcher.BeginInvoke(() =>
        {
            Interlocked.Exchange(ref _bleRefreshQueued, 0);
            if (generation == _bleScanGeneration && !_bleWindowClosed) RefreshBleDeviceList();
        });
    }
    private void RefreshBleDeviceList()
    {
        if (_bleWindowClosed || !IsInitialized || BleDiscoveryStatusText is null) return;
        bool serial = IsSerialConnectionMode;
        BleDiscoveryStatusText.Visibility = ShowHistoryDevicesBox.Visibility = serial ? Visibility.Collapsed : Visibility.Visible;
        if (serial) return;
        var all = _watcher?.Snapshot(true) ?? Array.Empty<DiscoveredDevice>();
        var visible = all.Where(d => d.IsNearby || ShowHistoryDevicesBox.IsChecked == true).ToList();
        if (_connectionMode == ConnectionMode.Ble && _connectedAddress is ulong active && _bmsTransport?.IsConnected == true)
        {
            visible.RemoveAll(d => d.Address == active);
            visible.Insert(0, new(active, _connectedName, 0) { Source = BleDiscoverySource.Connected, LastSeenUtc = null, LastSeenTimestamp = null });
        }
        ulong? selected = (DeviceList.SelectedItem as DiscoveredDevice)?.Address;
        foreach (var item in _devices.ToArray())
            if (!visible.Any(d => d.Address == item.Address)) { _devices.Remove(item); _deviceMap.Remove(item.Address); }
        foreach (var item in visible) UpsertDevice(item);
        if (selected is ulong address && _deviceMap.TryGetValue(address, out var restored)) DeviceList.SelectedItem = restored;
        int nearby = all.Count(d => d.IsNearby);
        int history = all.Count - nearby;
        string state = _bleScanRunning && _watcher?.IsDiscoveryAvailable == true
            ? (_watcher.IsBroadcastAvailable ? "正在搜索附近设备" : "正在通过系统广播更新搜索（原始广播接口不可用）")
            : _bleScanProblem ?? "搜索已停止";
        BleDiscoveryStatusText.Text = $"{state}\n最近12秒确认发现：{nearby}台；历史记录：{history}台。历史记录不代表在线。";
        BleDiscoveryStatusText.ToolTip = BleDiscoveryStatusText.Text;
    }
    private void ShowHistoryDevices_Changed(object sender, RoutedEventArgs e) => RefreshBleDeviceList();
    private async void CancelConnection_Click(object sender, RoutedEventArgs e)
    {
        CancelConnectionButton.IsEnabled = false;
        _connectionDesired = false; _pollTimer.Stop();
        _manualConnectCts?.Cancel(); _connectionOperations.Cancel();
        ConnectionText.Text = "正在取消，等待当前Windows蓝牙查询结束…";
        try
        {
            await DisposeBmsAsync();
            _connectedAddress = null; _connectedSerialPort = null;
            ConnectionText.Text = "连接已取消";
        }
        catch (OperationCanceledException) { }
        catch (Exception ex) { if (!_bleWindowClosed) ShowError("取消连接失败", ex); }
        RefreshBleDeviceList();
    }
    private async Task<bool> ConnectBmsInternalAsync(ulong address, CancellationToken ct = default, bool allowIapOnly = false)
    {
        _connectionDesired = true;
        CancelConnectionButton.IsEnabled = true;
        try { return await _connectionOperations.RunAsync(token => ConnectBmsOnceAsync(address, token, allowIapOnly), ct); }
        finally { if (!_manualConnectionInProgress && !_connectionOperations.IsBusy) CancelConnectionButton.IsEnabled = false; }
    }
    private Task ConnectSerialInternalAsync(string portName, int baudRate, CancellationToken ct = default)
    {
        _connectionDesired = true;
        return _connectionOperations.RunAsync(async token => { await ConnectSerialOnceAsync(portName, baudRate, token); return true; }, ct);
    }
    private Task DisposeBmsAsync()
    {
        _connectionDesired = false; _manualConnectCts?.Cancel();
        return _connectionOperations.RunAsync(async _ => { await DisposeBmsCoreAsync(); return true; });
    }
}
