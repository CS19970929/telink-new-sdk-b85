using System.Buffers.Binary;
using System.IO;

namespace BmsTool.Windows;

internal sealed class CommToolCanTransport(string portName) : IBmsTransport
{
    private readonly SemaphoreSlim _gate = new(1);
    private CommToolSerialConnection? _connection;
    private bool _disposed;
    public bool IsConnected => _connection is not null;
    public Version? FirmwareVersion { get; private set; }
    public string DiscoveryDescription => $"{portName} · Comm Tool · CAN 250 kbit/s · 地址 0";
    public event Action<ReadOnlyMemory<byte>>? DataReceived;
    public event Action<string>? ConnectionProgress;

    public async Task ReconnectAsync(CancellationToken ct = default)
    {
        await _gate.WaitAsync(ct).ConfigureAwait(false);
        try
        {
            ObjectDisposedException.ThrowIf(_disposed, this);
            FirmwareVersion = null;
            _connection?.Dispose(); _connection = null;
            ConnectionProgress?.Invoke($"正在连接 {DiscoveryDescription}");
            await Task.Run(() =>
            {
                var channel = new CommToolSerialConnection(portName, 9000);
                try
                {
                    Configure(channel, ct);
                    ConnectionProgress?.Invoke("Comm Tool 已回应，正在等待 BMS 的 CAN 应答");
                    ProbeBms(channel.Command, ct);
                    var firmwareVersion = CommToolOwcProtocol.TryReadFirmwareVersion(channel.Command, ct);
                    ct.ThrowIfCancellationRequested();
                    _connection = channel;
                    FirmwareVersion = firmwareVersion;
                }
                catch { channel.Dispose(); throw; }
            }, ct).ConfigureAwait(false);
        }
        finally { _gate.Release(); }
    }

    internal static void ProbeBms(Func<byte, byte[], CancellationToken, byte[]> command, CancellationToken ct)
    {
        byte[] request = ModbusRtu.ReadHolding(BmsRegisters.Legacy, 4);
        byte[] response;
        try { response = Exchange(request, command, ct); }
        catch (TimeoutException ex) when (ex.InnerException is CommToolCommandException { Status: 6 })
        {
            // CAN 的首帧可能只产生 RTC 唤醒沿，给接收器恢复时间后重试一次只读探测。
            ct.WaitHandle.WaitOne(250);
            ct.ThrowIfCancellationRequested();
            response = Exchange(request, command, ct);
        }
        _ = ModbusRtu.ParseRead(response, 4);
    }

    internal static void Configure(CommToolSerialConnection channel, CancellationToken ct)
    {
        byte[] config = new byte[8];
        BinaryPrimitives.WriteUInt32LittleEndian(config, 250000);
        config[4] = 1;
        if (channel.Command(0x02, config, ct).Length != 0)
            throw new IOException("Comm Tool CAN 设置应答长度错误");
    }

    internal async Task<byte[]> ReadFeidaoAsync(CancellationToken token)
    {
        await _gate.WaitAsync(token).ConfigureAwait(false);
        try
        {
            var channel = _connection ?? throw new IOException("Comm Tool 已断开");
            return await Task.Run(() => channel.Command(0x17, [], token), token).ConfigureAwait(false);
        }
        finally { _gate.Release(); }
    }

    public async Task WriteAsync(ReadOnlyMemory<byte> data, CancellationToken ct = default)
    {
        await _gate.WaitAsync(ct).ConfigureAwait(false);
        try
        {
            var channel = _connection ?? throw new IOException("Comm Tool 已断开");
            byte[] response = await Task.Run(() => Exchange(data.ToArray(), channel.Command, ct), ct).ConfigureAwait(false);
            ct.ThrowIfCancellationRequested();
            DataReceived?.Invoke(response);
        }
        finally { _gate.Release(); }
    }

    internal static byte[] Exchange(byte[] frame, Func<byte, byte[], CancellationToken, byte[]> command,
        CancellationToken ct)
    {
        if (frame.Length < 8 || frame[0] != BmsRegisters.DeviceAddress ||
            ModbusRtu.Crc16(frame.AsSpan(0, frame.Length-2)) != BinaryPrimitives.ReadUInt16LittleEndian(frame.AsSpan(frame.Length-2)))
            throw new InvalidDataException("CAN 请求的从站、长度或 CRC 错误");
        byte function = frame[1];
        if (function is not (3 or 6 or 16)) throw new NotSupportedException("CAN 转接支持 FC03、FC06 和 FC10");
        int count = function == 6 ? 1 : BinaryPrimitives.ReadUInt16BigEndian(frame.AsSpan(4));
        ushort address = BinaryPrimitives.ReadUInt16BigEndian(frame.AsSpan(2));
        if (count is < 1 or > 120 || address + count > 65536 ||
            frame.Length != (function == 16 ? 9 + 2*count : 8) || (function == 16 && frame[6] != 2*count))
            throw new InvalidDataException("CAN 寄存器数量、地址范围或报文长度错误");

        byte[] payload = new byte[4 + (function == 3 ? 0 : 2*count)];
        BinaryPrimitives.WriteUInt16LittleEndian(payload, address);
        BinaryPrimitives.WriteUInt16LittleEndian(payload.AsSpan(2), (ushort)count);
        for (int i=0; function != 3 && i<count; ++i)
        {
            int offset = function == 6 ? 4 : 7 + 2*i;
            payload[4+2*i] = frame[offset+1]; payload[5+2*i] = frame[offset];
        }
        byte opcode = function switch { 3 => 0x10, 6 => 0x11, _ => 0x16 };
        byte[] reply;
        try { reply = command(opcode, payload, ct); }
        catch (CommToolCommandException ex) when (ex.Status == 6)
        {
            string message = function == 3 ? "CAN 等待 BMS 的读取应答超时" :
                "CAN 写入结果未确认，请重新读取目标值";
            throw new TimeoutException(message, ex);
        }
        catch (CommToolCommandException ex) when (ex.Status == 2 && function == 16)
        { throw new NotSupportedException("整组写入需要支持 0x16 的 Comm Tool 固件和对应 BMS 固件", ex); }
        catch (CommToolCommandException ex) when (ex.Status == 7 && function == 3)
        {
            // 转接器仅提供综合错误；上层可跳过不可用的可选寄存器。
            throw new BmsModbusException(function, 4);
        }
        catch (CommToolCommandException ex) when (function != 3)
        { throw new IOException("CAN 写入未确认，请重新读取目标寄存器。"+ex.Message, ex); }
        if (reply.Length != (function == 3 ? count*2 : 0)) throw new InvalidDataException("CAN 寄存器应答长度错误");
        byte[] response = new byte[function == 3 ? 5+2*count : 8];
        if (function == 3)
        {
            response[0] = frame[0]; response[1] = function; response[2] = (byte)reply.Length;
            for (int i=0; i<count; ++i)
            {
                response[3+2*i] = reply[2*i+1]; response[4+2*i] = reply[2*i];
            }
        }
        else Array.Copy(frame, response, 6);
        BinaryPrimitives.WriteUInt16LittleEndian(response.AsSpan(response.Length-2), ModbusRtu.Crc16(response.AsSpan(0,response.Length-2)));
        return response;
    }

    public async ValueTask DisposeAsync()
    {
        await _gate.WaitAsync().ConfigureAwait(false);
        try
        {
            _disposed = true;
            FirmwareVersion = null;
            _connection?.Dispose();
            _connection = null;
        }
        finally { _gate.Release(); }
    }
}
