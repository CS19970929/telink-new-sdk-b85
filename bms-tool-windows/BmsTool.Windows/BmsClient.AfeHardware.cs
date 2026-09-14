using System.Buffers.Binary;
using System.IO;

namespace BmsTool.Windows;

public sealed record AfeHardwareAccessSession(
    ushort Token,
    ushort TimeoutSeconds,
    byte ProtocolVersion,
    ushort BackendModel);

public sealed partial class BmsClient
{
    public async Task WriteRegistersAsync(ushort start, ushort[] values, CancellationToken ct = default)
    {
        ArgumentNullException.ThrowIfNull(values);
        if (values.Length is < 1 or > 123)
            throw new ArgumentOutOfRangeException(nameof(values), "Modbus 0x10 quantity must be 1..123 words.");

        byte[] raw = new byte[values.Length * 2];
        for (int i = 0; i < values.Length; i++)
            BinaryPrimitives.WriteUInt16BigEndian(raw.AsSpan(i * 2, 2), values[i]);

        byte[] request = ModbusRtu.WriteMultiple(start, raw);
        if (_transport is BmsBleTransport ble)
        {
            int mtu = ble.NegotiatedMtu ?? 23;
            if (mtu < request.Length + 3)
                throw new IOException($"当前 BLE MTU={mtu} 无法承载 {request.Length} 字节 AFE 原子写入。请使用直连串口，或使用 MTU ≥ {request.Length + 3} 的透明 BLE 通道；硬件保护参数禁止拆帧写入。");
        }

        byte[] rsp = await TransactAsync(request, ct);
        ModbusRtu.ValidateWriteMultipleAck(rsp, start, checked((ushort)values.Length));
    }

    public async Task<AfeHardwareAccessSession> OpenAfeHardwareAccessAsync(CancellationToken ct = default)
    {
        byte[] rsp = await TransactAsync(ModbusRtu.AfeHardwareOpen(), ct);
        ModbusRtu.ValidateAfeHardwareResponse(rsp, 0x01);
        if (rsp.Length != 13) throw new IOException("AFE hardware OPEN response length mismatch.");
        var session = new AfeHardwareAccessSession(
            BinaryPrimitives.ReadUInt16BigEndian(rsp.AsSpan(4, 2)),
            BinaryPrimitives.ReadUInt16BigEndian(rsp.AsSpan(6, 2)),
            rsp[8],
            BinaryPrimitives.ReadUInt16BigEndian(rsp.AsSpan(9, 2)));
        if (session.Token == 0) throw new IOException("AFE hardware OPEN returned a zero session token.");
        return session;
    }

    public async Task<ushort> AfeHardwareHeartbeatAsync(ushort token, CancellationToken ct = default)
    {
        byte[] rsp = await TransactAsync(ModbusRtu.AfeHardwareCommand(0x02, token), ct);
        ModbusRtu.ValidateAfeHardwareResponse(rsp, 0x02);
        if (rsp.Length != 10 || BinaryPrimitives.ReadUInt16BigEndian(rsp.AsSpan(4, 2)) != token)
            throw new IOException("AFE hardware heartbeat response mismatch.");
        return BinaryPrimitives.ReadUInt16BigEndian(rsp.AsSpan(6, 2));
    }

    public async Task<ushort> AfeHardwareStatusAsync(ushort token, CancellationToken ct = default)
    {
        byte[] rsp = await TransactAsync(ModbusRtu.AfeHardwareCommand(0x04, token), ct);
        ModbusRtu.ValidateAfeHardwareResponse(rsp, 0x04);
        if (rsp.Length != 10 || BinaryPrimitives.ReadUInt16BigEndian(rsp.AsSpan(4, 2)) != token)
            throw new IOException("AFE hardware status response mismatch.");
        return BinaryPrimitives.ReadUInt16BigEndian(rsp.AsSpan(6, 2));
    }

    public async Task CloseAfeHardwareAccessAsync(ushort token, CancellationToken ct = default)
    {
        byte[] rsp = await TransactAsync(ModbusRtu.AfeHardwareCommand(0x03, token), ct);
        ModbusRtu.ValidateAfeHardwareResponse(rsp, 0x03);
        if (rsp.Length != 6) throw new IOException("AFE hardware CLOSE response length mismatch.");
    }

    public async Task TryCloseAfeHardwareAccessAsync(ushort token, CancellationToken ct = default)
    {
        try
        {
            await CloseAfeHardwareAccessAsync(token, ct);
        }
        catch (Exception ex) when (ex is IOException or TimeoutException or OperationCanceledException)
        {
            // A successful profile commit deliberately closes the firmware-side
            // AFE session before the PC reaches this finally block.
            Log?.Invoke($"[AFE_HW] close ignored after transaction: {ex.GetType().Name}: {ex.Message}");
        }
    }
}
