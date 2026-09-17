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
        if (_transport is BmsBleTransport ble && (ble.NegotiatedMtu ?? 23) < request.Length + 3)
            throw new IOException("设备尚未支持此写入路径；请使用完整 AFE 参数事务或直连串口，无需修改 MTU。");

        byte[] rsp = await TransactAsync(request, ct);
        ModbusRtu.ValidateWriteMultipleAck(rsp, start, checked((ushort)values.Length));
    }

    public async Task WriteAfeProfileAsync(ushort[] values, AfeHardwareAccessSession session, CancellationToken ct=default)
    {
        if(values.Length!=35) throw new ArgumentException("AFE 参数必须为完整 35 words。");
        if(_transport is not BmsBleTransport ble || (ble.NegotiatedMtu ?? 23)>=82) {
            await WriteRegistersAsync(0x2500,values,ct);return;
        }
        if(session.ProtocolVersion<2)
            throw new IOException("当前固件不支持 MTU=23 的 AFE 分片事务，参数帧未发送。请升级配套固件或使用直连串口，无需修改 MTU。");
        byte[] raw=new byte[70];
        for(int i=0;i<35;i++) BinaryPrimitives.WriteUInt16BigEndian(raw.AsSpan(i*2,2),values[i]);
        byte[] frame=ModbusRtu.WriteMultiple(0x2500,raw);
        for(int offset=0;offset<frame.Length;offset+=11) {
            int count=Math.Min(11,frame.Length-offset);
            byte[] body=new byte[7+count];body[0]=1;body[1]=0x42;body[2]=5;
            BinaryPrimitives.WriteUInt16BigEndian(body.AsSpan(3,2),session.Token);
            body[5]=(byte)offset;body[6]=(byte)count;
            Array.Copy(frame,offset,body,7,count);
            byte[] ack=await TransactAsync(ModbusRtu.Frame(body),ct);
            ModbusRtu.ValidateAfeHardwareResponse(ack,5);
            if(ack.Length!=9 || BinaryPrimitives.ReadUInt16BigEndian(ack.AsSpan(4,2))!=session.Token || ack[6]!=offset+count)
                throw new IOException("AFE 分片确认不匹配；未提交参数，请重新读取设备。");
        }
        // No automatic retry: a lost COMMIT response has an unknown outcome.
        byte[] committed=await TransactAsync(ModbusRtu.AfeHardwareCommand(6,session.Token),ct);
        ModbusRtu.ValidateAfeHardwareResponse(committed,6);
        if(committed.Length!=6) throw new IOException("AFE 提交确认长度错误；请重新读取设备。");
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
