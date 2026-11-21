using System.Buffers.Binary;
using System.Diagnostics;
using System.IO;
using System.IO.Ports;

namespace BmsTool.Windows;

// 独占Comm Tool PC串口。与现有BMS Modbus/BLE transport分离，禁止发送升级或参数命令。
internal sealed class CommToolCommandException(byte status, string message) : IOException(message)
{
    public byte Status { get; } = status;
}

internal sealed class CommToolOwcClient : IDisposable
{
    private readonly CommToolSerialConnection? _connection;
    private readonly Func<byte, byte[], CancellationToken, byte[]> _exchange;
    private OwcReceiveMode _mode = OwcReceiveMode.A133;
    public bool UsesExtendedSnapshots { get; private set; }
    internal CommToolOwcClient(Func<byte, byte[], CancellationToken, byte[]> exchange) => _exchange = exchange;
    public CommToolOwcClient(string port)
    {
        _connection = new CommToolSerialConnection(port, 1000);
        _exchange = _connection.Command;
    }

    public void Start(OwcReceiveMode mode, CancellationToken token)
    {
        if ((byte)mode > 2) throw new ArgumentOutOfRangeException(nameof(mode));
        _mode = mode;
        try
        {
            byte mask = CommToolOwcProtocol.DecodeCapabilities(Command(0x45, [], token));
            if ((mask & (1 << (byte)mode)) == 0)
                throw new IOException("Comm Tool不支持该接收模式，请核对PC10/11引脚配置或升级App");
            UsesExtendedSnapshots = true;
        }
        catch (CommToolCommandException ex) when (ex.Status == 2)
        {
            if (mode == OwcReceiveMode.LishenV111)
                throw new IOException("此Comm Tool仅支持A1.3.3；锂神模式需要升级Comm Tool App", ex);
            UsesExtendedSnapshots = false; _mode = OwcReceiveMode.A133;
        }
        SetEnabled(true, token);
    }

    public Version? TryReadFirmwareVersion(CancellationToken token) =>
        CommToolOwcProtocol.TryReadFirmwareVersion(_exchange, token);

    public void SetEnabled(bool enabled, CancellationToken token) =>
        Command(UsesExtendedSnapshots ? (byte)0x46 : (byte)0x43,
            UsesExtendedSnapshots ? [enabled ? (byte)1 : (byte)0, (byte)_mode] : [enabled ? (byte)1 : (byte)0], token);

    public Dictionary<byte, OwcSnapshot> ReadSnapshots(CancellationToken token)
    {
        for (int attempt = 0; attempt < 2; attempt++)
        {
            var snapshots = new Dictionary<byte, OwcSnapshot>();
            foreach (byte id in UsesExtendedSnapshots ? CommToolOwcProtocol.ExtendedPacketIds : CommToolOwcProtocol.PacketIds)
            {
                byte[] payload = Command(UsesExtendedSnapshots ? (byte)0x47 : (byte)0x44, [id], token);
                snapshots[id] = UsesExtendedSnapshots ? CommToolOwcProtocol.DecodeSnapshotV3(payload, id) :
                    CommToolOwcProtocol.DecodeSnapshot(payload, id);
            }
            if (CommToolOwcProtocol.Coherent(snapshots.Values)) return snapshots;
        }
        throw new IOException("读取期间设备会话变化，请重新开始监控");
    }

    private byte[] Command(byte command, byte[] payload, CancellationToken token)
    {
        token.ThrowIfCancellationRequested();
        return _exchange(command, payload, token);
    }
    public void Dispose() => _connection?.Dispose();
}

internal sealed class CommToolSerialConnection : IDisposable
{
    private readonly SerialPort _port;
    private readonly int timeoutMs;
    private ushort _sequence;

    public CommToolSerialConnection(string port, int timeoutMs)
    {
        this.timeoutMs = timeoutMs;
        _port = new SerialPort(port, 115200, Parity.None, 8, StopBits.One)
        { ReadTimeout = 50, WriteTimeout = 1000, Handshake = Handshake.None };
        try { _port.Open(); }
        catch { _port.Dispose(); throw; }
    }

    public byte[] Command(byte command, byte[] payload, CancellationToken token)
    {
        token.ThrowIfCancellationRequested();
        _sequence = unchecked((ushort)(_sequence + 1));
        if (_sequence == 0) _sequence = 1;
        byte[] request = CommToolOwcProtocol.EncodeCommand(_sequence, command, payload);
        var timer = Stopwatch.StartNew();
        _port.Write(request, 0, request.Length);
        bool prefix = false;
        while (timer.ElapsedMilliseconds < timeoutMs)
        {
            byte value = ReadExact(1, timer, token)[0];
            if (!prefix) { prefix = value == 0x55; continue; }
            if (value != 0xAA) { prefix = value == 0x55; continue; }
            prefix = false;
            byte[] header = new byte[10];
            header[0] = 0x55; header[1] = 0xAA;
            ReadExact(8, timer, token).CopyTo(header, 2);
            if (header[2] != 1) throw new IOException("Comm Tool外层协议版本非法");
            int length = BinaryPrimitives.ReadUInt16LittleEndian(header.AsSpan(8));
            if (length > 512) throw new IOException("Comm Tool响应超长");
            byte[] tail = ReadExact(length + 2, timer, token);
            byte[] frame = new byte[12 + length];
            header.CopyTo(frame, 0); tail.CopyTo(frame, 10);
            var response = CommToolOwcProtocol.DecodeFrame(frame);
            if (response.Sequence != _sequence || response.Command != command || (response.Flags & 1) == 0) continue;
            if (response.Status != 0) throw new CommToolCommandException(response.Status, $"Comm Tool拒绝命令0x{command:X2}，状态0x{response.Status:X2}");
            return response.Payload;
        }
        throw new TimeoutException($"等待Comm Tool命令0x{command:X2}超时");
    }

    private byte[] ReadExact(int count, Stopwatch timer, CancellationToken token)
    {
        byte[] data = new byte[count];
        int received = 0;
        while (received < count && timer.ElapsedMilliseconds < timeoutMs)
        {
            token.ThrowIfCancellationRequested();
            try { received += _port.Read(data, received, count - received); }
            catch (TimeoutException) { }
        }
        token.ThrowIfCancellationRequested();
        if (received != count) throw new TimeoutException("Comm Tool响应超时或不完整");
        return data;
    }

    public void Dispose() => _port.Dispose();
}
