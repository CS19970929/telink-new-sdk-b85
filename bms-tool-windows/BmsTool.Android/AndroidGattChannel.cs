using Android.Bluetooth;
using Android.Content;
using Java.Util;
using System.IO;
using System.Runtime.Versioning;

namespace BmsTool.Android;

internal sealed class AndroidGattChannel : BluetoothGattCallback, IAsyncDisposable
{
    private static readonly TimeSpan ReadySettleDelay = TimeSpan.FromMilliseconds(300);
    private static readonly UUID CccdUuid = UUID.FromString("00002902-0000-1000-8000-00805f9b34fb")!;
    private readonly Context _context;
    private readonly string _mac;
    private readonly UUID _serviceUuid;
    private readonly UUID _writeUuid;
    private readonly UUID _notifyUuid;
    private readonly SemaphoreSlim _writeGate = new(1, 1);
    private BluetoothGatt? _gatt;
    private BluetoothGattCharacteristic? _writeCharacteristic;
    private BluetoothGattCharacteristic? _notifyCharacteristic;
    private TaskCompletionSource<bool>? _connectedTcs;
    private TaskCompletionSource<bool>? _readyTcs;
    private TaskCompletionSource<int>? _writeTcs;
    private TaskCompletionSource<int>? _descriptorTcs;
    private bool _disposed;

    public bool IsConnected { get; private set; }
    public bool NotificationsEnabled { get; private set; }
    public int NegotiatedMtu { get; private set; } = 23;
    public event Action<ReadOnlyMemory<byte>>? NotificationReceived;
    public event Action<string>? Log;

    public AndroidGattChannel(Context context, string mac, Guid serviceUuid, Guid writeUuid, Guid notifyUuid)
    {
        _context = context;
        _mac = mac;
        _serviceUuid = UUID.FromString(serviceUuid.ToString())!;
        _writeUuid = UUID.FromString(writeUuid.ToString())!;
        _notifyUuid = UUID.FromString(notifyUuid.ToString())!;
    }

    public async Task ConnectAsync(CancellationToken ct)
    {
        const int maxAttempts = 3;
        for (int attempt = 1; attempt <= maxAttempts; attempt++)
        {
            try
            {
                await ConnectOnceAsync(ct);
                return;
            }
            catch (Exception ex) when (attempt < maxAttempts && ex is IOException or TimeoutException)
            {
                int delayMs = 500 * attempt;
                Log?.Invoke($"[GATT] CONNECT_RETRY attempt={attempt}/{maxAttempts} type={ex.GetType().Name} message={ex.Message} delay={delayMs}ms");
                await Task.Delay(delayMs, ct);
            }
        }
    }

    private async Task ConnectOnceAsync(CancellationToken ct)
    {
        ThrowIfDisposed();
        await DisconnectAsync();
        BluetoothManager manager = (BluetoothManager?)_context.GetSystemService(Context.BluetoothService)
            ?? throw new IOException("Android BluetoothManager is unavailable.");
        BluetoothAdapter adapter = manager.Adapter ?? throw new IOException("Android BluetoothAdapter is unavailable.");
        if (!adapter.IsEnabled) throw new IOException("Android Bluetooth is disabled.");

        BluetoothDevice device = adapter.GetRemoteDevice(_mac)
            ?? throw new IOException($"Bluetooth device {_mac} is unavailable.");
        _connectedTcs = NewTcs<bool>();
        _readyTcs = NewTcs<bool>();
        Log?.Invoke($"[GATT] CONNECT mac={_mac}");
        _gatt = device.ConnectGatt(_context, false, this, BluetoothTransports.Le)
            ?? throw new IOException("BluetoothDevice.ConnectGatt returned null.");

        await WaitWithTimeoutAsync(_connectedTcs.Task, TimeSpan.FromSeconds(20), ct, "BLE connect timeout.");
        await WaitWithTimeoutAsync(_readyTcs.Task, TimeSpan.FromSeconds(20), ct, "GATT service discovery timeout.");
        Log?.Invoke($"[GATT] READY_SETTLE delay={ReadySettleDelay.TotalMilliseconds:F0}ms");
        await Task.Delay(ReadySettleDelay, ct);
    }

    public async Task WriteAsync(ReadOnlyMemory<byte> data, bool withResponse, CancellationToken ct)
    {
        await _writeGate.WaitAsync(ct);
        try
        {
            BluetoothGatt gatt = _gatt ?? throw new IOException("GATT is not connected.");
            BluetoothGattCharacteristic characteristic = _writeCharacteristic ?? throw new IOException("GATT write characteristic is unavailable.");
            GattWriteType writeType = withResponse && characteristic.Properties.HasFlag(GattProperty.Write)
                ? GattWriteType.Default
                : GattWriteType.NoResponse;

            const int maxCongestionRetries = 6;
            for (int attempt = 0; attempt <= maxCongestionRetries; attempt++)
            {
                ct.ThrowIfCancellationRequested();
                _writeTcs = NewTcs<int>();
                bool queued;
                if (OperatingSystem.IsAndroidVersionAtLeast(33))
                {
                    queued = gatt.WriteCharacteristic(characteristic, data.ToArray(), (int)writeType) == (int)CurrentBluetoothStatusCodes.Success;
                }
                else
                {
                    characteristic.WriteType = writeType;
                    queued = characteristic.SetValue(data.ToArray()) && gatt.WriteCharacteristic(characteristic);
                }

                if (!queued)
                {
                    _writeTcs = null;
                    if (attempt == maxCongestionRetries)
                        throw new IOException("Android rejected the GATT write before queueing it.");
                    await Task.Delay(20 * (attempt + 1), ct);
                    continue;
                }

                int status = await WaitWithTimeoutAsync(_writeTcs.Task, TimeSpan.FromSeconds(8), ct, "GATT write callback timeout.");
                _writeTcs = null;
                if (status == 0) return;
                if (status == 143 && attempt < maxCongestionRetries)
                {
                    int backoffMs = 30 * (attempt + 1);
                    Log?.Invoke($"[GATT] CONGESTED status=143 retry={attempt + 1}/{maxCongestionRetries} backoff={backoffMs}ms");
                    await Task.Delay(backoffMs, ct);
                    continue;
                }
                throw new IOException($"GATT write failed: status={status}.");
            }
        }
        finally
        {
            _writeGate.Release();
        }
    }

    public override void OnConnectionStateChange(BluetoothGatt? gatt, GattStatus status, ProfileState newState)
    {
        base.OnConnectionStateChange(gatt, status, newState);
        Log?.Invoke($"[GATT] STATE status={(int)status} state={newState}");
        if (gatt is null) return;
        if ((int)status == 0 && newState == ProfileState.Connected)
        {
            IsConnected = true;
            _connectedTcs?.TrySetResult(true);
            gatt.RequestConnectionPriority(GattConnectionPriority.High);
            if (!gatt.RequestMtu(247))
                gatt.DiscoverServices();
            return;
        }

        IsConnected = false;
        var error = new IOException($"GATT disconnected: status={(int)status}, state={newState}.");
        if (newState == ProfileState.Disconnected)
        {
            _connectedTcs?.TrySetException(error);
            _readyTcs?.TrySetException(error);
            _writeTcs?.TrySetException(error);
        }
    }

    public override void OnMtuChanged(BluetoothGatt? gatt, int mtu, GattStatus status)
    {
        base.OnMtuChanged(gatt, mtu, status);
        if ((int)status == 0) NegotiatedMtu = mtu;
        Log?.Invoke($"[GATT] MTU status={(int)status} mtu={mtu}");
        gatt?.DiscoverServices();
    }

    public override void OnServicesDiscovered(BluetoothGatt? gatt, GattStatus status)
    {
        base.OnServicesDiscovered(gatt, status);
        if (gatt is null || (int)status != 0)
        {
            _readyTcs?.TrySetException(new IOException($"GATT service discovery failed: status={(int)status}."));
            return;
        }

        BluetoothGattService? service = gatt.GetService(_serviceUuid);
        _writeCharacteristic = service?.GetCharacteristic(_writeUuid);
        _notifyCharacteristic = service?.GetCharacteristic(_notifyUuid);
        if (_writeCharacteristic is null || _notifyCharacteristic is null)
        {
            _readyTcs?.TrySetException(new IOException($"Required GATT service/characteristic not found: service={_serviceUuid}."));
            return;
        }

        Log?.Invoke($"[GATT] READY mtu={NegotiatedMtu} writeProps={_writeCharacteristic.Properties} notifyProps={_notifyCharacteristic.Properties}");
        if (!_notifyCharacteristic.Properties.HasFlag(GattProperty.Notify))
        {
            _readyTcs?.TrySetException(new IOException("GATT notify characteristic does not support notifications."));
            return;
        }

        if (!gatt.SetCharacteristicNotification(_notifyCharacteristic, true))
        {
            _readyTcs?.TrySetException(new IOException("Android rejected local notification enable."));
            return;
        }

        BluetoothGattDescriptor? descriptor = _notifyCharacteristic.GetDescriptor(CccdUuid);
        if (descriptor is null)
        {
            _readyTcs?.TrySetException(new IOException("CCCD descriptor was not found."));
            return;
        }

        _descriptorTcs = NewTcs<int>();
        byte[] enableNotification = { 0x01, 0x00 };
        bool descriptorQueued;
        if (OperatingSystem.IsAndroidVersionAtLeast(33))
            descriptorQueued = gatt.WriteDescriptor(descriptor, enableNotification) == (int)CurrentBluetoothStatusCodes.Success;
        else
            descriptorQueued = descriptor.SetValue(enableNotification) && gatt.WriteDescriptor(descriptor);
        if (!descriptorQueued)
            _readyTcs?.TrySetException(new IOException("Android rejected CCCD write."));
    }

    public override void OnDescriptorWrite(BluetoothGatt? gatt, BluetoothGattDescriptor? descriptor, GattStatus status)
    {
        base.OnDescriptorWrite(gatt, descriptor, status);
        _descriptorTcs?.TrySetResult((int)status);
        if ((int)status == 0)
        {
            NotificationsEnabled = true;
            _readyTcs?.TrySetResult(true);
        }
        else
        {
            _readyTcs?.TrySetException(new IOException($"CCCD write failed: status={(int)status}."));
        }
    }

    public override void OnCharacteristicWrite(BluetoothGatt? gatt, BluetoothGattCharacteristic? characteristic, GattStatus status)
    {
        base.OnCharacteristicWrite(gatt, characteristic, status);
        _writeTcs?.TrySetResult((int)status);
    }

    [SupportedOSPlatform("android33.0")]
    public override void OnCharacteristicChanged(BluetoothGatt? gatt, BluetoothGattCharacteristic? characteristic, byte[] value)
    {
        NotificationReceived?.Invoke(value);
    }

    [UnsupportedOSPlatform("android33.0")]
    public override void OnCharacteristicChanged(BluetoothGatt? gatt, BluetoothGattCharacteristic? characteristic)
    {
        byte[]? value = characteristic?.GetValue();
        if (value is not null)
            NotificationReceived?.Invoke(value);
    }

    private static TaskCompletionSource<T> NewTcs<T>() =>
        new(TaskCreationOptions.RunContinuationsAsynchronously);

    private static async Task<T> WaitWithTimeoutAsync<T>(Task<T> task, TimeSpan timeout, CancellationToken ct, string timeoutMessage)
    {
        using var timeoutCts = CancellationTokenSource.CreateLinkedTokenSource(ct);
        Task delay = Task.Delay(timeout, timeoutCts.Token);
        Task winner = await Task.WhenAny(task, delay);
        if (winner == task)
        {
            timeoutCts.Cancel();
            return await task;
        }
        ct.ThrowIfCancellationRequested();
        throw new TimeoutException(timeoutMessage);
    }

    public async Task DisconnectAsync()
    {
        await _writeGate.WaitAsync();
        try
        {
            IsConnected = false;
            NotificationsEnabled = false;
            _writeCharacteristic = null;
            _notifyCharacteristic = null;
            BluetoothGatt? gatt = _gatt;
            _gatt = null;
            if (gatt is not null)
            {
                try { gatt.Disconnect(); } catch { }
                gatt.Close();
                gatt.Dispose();
            }
        }
        finally
        {
            _writeGate.Release();
        }
    }

    private void ThrowIfDisposed()
    {
        if (_disposed) throw new ObjectDisposedException(nameof(AndroidGattChannel));
    }

    public async ValueTask DisposeAsync()
    {
        if (_disposed) return;
        await DisconnectAsync();
        _disposed = true;
        _writeGate.Dispose();
    }
}
