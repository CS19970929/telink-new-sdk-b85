using System.Buffers.Binary;
using System.Diagnostics;
using System.IO;
using System.IO.Ports;

namespace BmsTool.Windows;

// 独占Comm Tool PC串口。与现有BMS Modbus/BLE transport分离，禁止发送升级或参数命令。
internal sealed class CommToolOwcClient : IDisposable
{
    private readonly SerialPort _port;
    private ushort _sequence;

    public CommToolOwcClient(string port)
    {
        _port = new SerialPort(port, 115200, Parity.None, 8, StopBits.One)
        { ReadTimeout = 50, WriteTimeout = 1000, Handshake = Handshake.None };
        try { _port.Open(); }
        catch { _port.Dispose(); throw; }
    }

    public void SetEnabled(bool enabled, CancellationToken token) =>
        Command(0x43, [enabled ? (byte)1 : (byte)0], token);

    public Dictionary<byte, OwcSnapshot> ReadSnapshots(CancellationToken token)
    {
        for (int attempt = 0; attempt < 2; attempt++)
        {
            var snapshots = new Dictionary<byte, OwcSnapshot>();
            foreach (byte id in CommToolOwcProtocol.PacketIds)
                snapshots[id] = CommToolOwcProtocol.DecodeSnapshot(Command(0x44, [id], token), id);
            if (CommToolOwcProtocol.Coherent(snapshots.Values)) return snapshots;
        }
        throw new IOException("读取期间设备会话变化，请重新开始监控");
    }

    private byte[] Command(byte command, byte[] payload, CancellationToken token)
    {
        token.ThrowIfCancellationRequested();
        _sequence = unchecked((ushort)(_sequence + 1));
        if (_sequence == 0) _sequence = 1;
        byte[] request = CommToolOwcProtocol.EncodeCommand(_sequence, command, payload);
        var timer = Stopwatch.StartNew();
        _port.Write(request, 0, request.Length);
        bool prefix = false;
        while (timer.ElapsedMilliseconds < 1000)
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
            if (response.Status != 0) throw new IOException($"Comm Tool拒绝命令0x{command:X2}，状态0x{response.Status:X2}");
            return response.Payload;
        }
        throw new TimeoutException($"等待Comm Tool命令0x{command:X2}超时");
    }

    private byte[] ReadExact(int count, Stopwatch timer, CancellationToken token)
    {
        byte[] data = new byte[count];
        int received = 0;
        while (received < count && timer.ElapsedMilliseconds < 1000)
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
