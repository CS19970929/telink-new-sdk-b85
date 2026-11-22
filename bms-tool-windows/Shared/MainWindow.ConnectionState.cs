namespace BmsTool.Windows;

public partial class MainWindow
{
    // A new connection, including reconnecting the same endpoint, invalidates
    // every editable snapshot. Backend model is not a device identity.
    private long _connectionGeneration;
    private CancellationTokenSource _connectionLifetime = new();
    private long _afeSnapshotGeneration;
    private BmsClient? _protectionSnapshotClient;
    private long _protectionSnapshotGeneration;

    private void InvalidateConnectionState()
    {
        _connectionGeneration++;
        var previous = _connectionLifetime;
        _connectionLifetime = new CancellationTokenSource();
        _afeHardwareSource = null;
        _protectionSnapshotClient = null;
        _parameterCapture = null;
        _parameterProbedClient = null;
        ClearC073ProtectionView();
        _c073ReadClient = null;
        UpdateProtectionPage(false);
        _afeHardwareModel.ClearReadback();
        if (_afeHardwareStatus is not null) _afeHardwareStatus.Text = "连接已改变，请重新读取AFE参数";
        if (_afeHardwareDeviceInfo is not null) _afeHardwareDeviceInfo.Text = "设备：未读取";
        previous.Cancel();
        previous.Dispose();
    }

    private void EnsureCurrentConnection(BmsClient client, long generation)
    {
        if (generation != _connectionGeneration || !ReferenceEquals(client, _bms) || _bmsTransport?.IsConnected != true)
            throw new OperationCanceledException("连接已改变或断开，请重新读取当前设备。");
    }

    private void EnsureAfeSnapshot(BmsClient client, long generation)
    {
        EnsureCurrentConnection(client, generation);
        if (!ReferenceEquals(client, _afeHardwareSource) || generation != _afeSnapshotGeneration)
            throw new InvalidOperationException("请先重新读取当前连接的AFE参数，再执行保存。");
    }

    private void EnsureProtectionSnapshot(BmsClient client, long generation)
    {
        EnsureCurrentConnection(client, generation);
        if (!ReferenceEquals(client, _protectionSnapshotClient) || generation != _protectionSnapshotGeneration)
            throw new InvalidOperationException("请先重新读取当前连接的软件保护参数。");
    }
}
