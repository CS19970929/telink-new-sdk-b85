using System.IO;
using System.IO.Ports;

namespace BmsTool.Windows;

public sealed record SerialPortEndpoint(string PortName)
{
    public override string ToString() => $"{PortName}    8N1";
}

/// <summary>
/// 直连 STM32/BMS UART 的 Modbus RTU 通道。
/// 固件 Sci 初始化为 19200、8N1、无硬件流控；串口层不拆包，BmsClient 负责 Modbus 帧组包。
/// </summary>
public sealed class BmsSerialTransport : IBmsTransport
{
    public const int DefaultBaudRate = 19200;
    private readonly SemaphoreSlim _writeGate = new(1, 1);
    private readonly object _portLock = new();
    private SerialPort? _port;
    private string? _lastPortName;
    private int _lastBaudRate = DefaultBaudRate;

    public bool IsConnected
    {
        get { lock (_portLock) return _port?.IsOpen == true; }
    }

    public string DiscoveryDescription { get; private set; } = "串口未连接";
    public int BaudRate { get; private set; } = DefaultBaudRate;
    public event Action<ReadOnlyMemory<byte>>? DataReceived;
    public event Action<string>? ConnectionProgress;

    public Task ConnectAsync(string portName, int baudRate = DefaultBaudRate, CancellationToken ct = default)
    {
        ct.ThrowIfCancellationRequested();
        if (string.IsNullOrWhiteSpace(portName))
            throw new ArgumentException("串口号不能为空。", nameof(portName));
        if (baudRate <= 0)
            throw new ArgumentOutOfRangeException(nameof(baudRate), "波特率必须为正数。");

        DisposeConnection();
        var port = new SerialPort(portName.Trim(), baudRate, Parity.None, 8, StopBits.One)
        {
            Handshake = Handshake.None,
            DtrEnable = false,
            RtsEnable = false,
            ReadTimeout = 1000,
            WriteTimeout = 3000,
            ReadBufferSize = 8192,
            WriteBufferSize = 4096
        };
        port.DataReceived += OnDataReceived;
        try
        {
            port.Open();
            lock (_portLock)
            {
                _port = port;
                _lastPortName = portName.Trim();
                _lastBaudRate = baudRate;
                BaudRate = baudRate;
            }
            DiscoveryDescription = $"串口已连接 · {_lastPortName} · {BaudRate} 8N1";
            ConnectionProgress?.Invoke($"[SERIAL] OPEN_OK port={_lastPortName}; baud={BaudRate}; format=8N1; flow=None");
            return Task.CompletedTask;
        }
        catch
        {
            port.DataReceived -= OnDataReceived;
            port.Dispose();
            throw;
        }
    }

    public async Task ReconnectAsync(CancellationToken ct = default)
    {
        string portName = _lastPortName ?? throw new IOException("没有可用于重连的串口。 ");
        ConnectionProgress?.Invoke($"[SERIAL] RECONNECT_BEGIN port={portName}");
        DisposeConnection();
        await Task.Delay(150, ct);
        await ConnectAsync(portName, _lastBaudRate, ct);
        ConnectionProgress?.Invoke($"[SERIAL] RECONNECT_OK port={portName}");
    }

    public async Task WriteAsync(ReadOnlyMemory<byte> data, CancellationToken ct = default)
    {
        if (data.Length == 0) return;
        await _writeGate.WaitAsync(ct);
        try
        {
            SerialPort port;
            lock (_portLock) port = _port ?? throw new IOException("串口未连接。");
            if (!port.IsOpen) throw new IOException("串口已关闭。");
            byte[] bytes = data.ToArray();
            await Task.Run(() => port.Write(bytes, 0, bytes.Length), ct);
            ConnectionProgress?.Invoke($"[SERIAL] WRITE_OK len={bytes.Length}");
        }
        catch (Exception ex) when (ex is IOException or InvalidOperationException or TimeoutException)
        {
            ConnectionProgress?.Invoke($"[SERIAL] WRITE_FAIL len={data.Length}; type={ex.GetType().Name}; message={ex.Message}");
            throw;
        }
        finally
        {
            _writeGate.Release();
        }
    }

    private void OnDataReceived(object sender, SerialDataReceivedEventArgs e)
    {
        try
        {
            SerialPort? port;
            lock (_portLock) port = _port;
            if (port is null || !port.IsOpen) return;
            while (port.BytesToRead > 0)
            {
                byte[] buffer = new byte[Math.Min(Math.Max(port.BytesToRead, 1), 4096)];
                int count = port.Read(buffer, 0, buffer.Length);
                if (count <= 0) break;
                ConnectionProgress?.Invoke($"[SERIAL] RX_FRAGMENT len={count}");
                DataReceived?.Invoke(buffer.AsMemory(0, count));
            }
        }
        catch (Exception ex) when (ex is IOException or InvalidOperationException or TimeoutException)
        {
            ConnectionProgress?.Invoke($"[SERIAL] READ_FAIL type={ex.GetType().Name}; message={ex.Message}");
        }
    }

    public ValueTask DisposeAsync()
    {
        DisposeConnection();
        _writeGate.Dispose();
        return ValueTask.CompletedTask;
    }

    private void DisposeConnection()
    {
        SerialPort? port;
        lock (_portLock)
        {
            port = _port;
            _port = null;
        }
        if (port is null) return;
        port.DataReceived -= OnDataReceived;
        try { if (port.IsOpen) port.Close(); } catch { }
        port.Dispose();
        DiscoveryDescription = "串口未连接";
    }
}
