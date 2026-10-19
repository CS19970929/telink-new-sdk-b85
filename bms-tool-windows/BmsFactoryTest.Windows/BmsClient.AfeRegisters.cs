using System.Buffers.Binary;
using System.IO;

namespace BmsTool.Windows;

public sealed record AfeRegisterDeviceInfo(ushort Model, bool Enabled, bool Held,
    string CatalogId, ushort ShuntElementMicroOhm, byte ParallelCount)
{
    public string ModelName => Model switch { 0x1124 => "DVC1124-2", 0x3510 => "SH3673510", 0x3520 => "SH3673520", _ => $"0x{Model:X4}" };
    public string ShuntText => ParallelCount == 0 ? "未知" :
        $"{ShuntElementMicroOhm}/{ParallelCount} µΩ = {(decimal)ShuntElementMicroOhm / ParallelCount / 1000m:0.#########} mΩ";
}

public sealed partial class BmsClient
{
    private async Task<byte[]> AfeRegisterCommandAsync(byte command, ushort token, byte[] data,
        int payloadBytes, CancellationToken ct = default)
    {
        int prefix = command == 0x10 ? 3 : 5;
        byte[] body = new byte[prefix + data.Length];
        body[0] = 1; body[1] = 0x42; body[2] = command;
        if (prefix == 5) BinaryPrimitives.WriteUInt16BigEndian(body.AsSpan(3, 2), token);
        Array.Copy(data, 0, body, prefix, data.Length);
        // 一次请求一次响应；丢失应答可能已经写入，不能自动重试。
        byte[] response = await TransactAsync(ModbusRtu.Frame(body), ct);
        ModbusRtu.ValidateFrame(response);
        if (response.Length < 6 || response[0] != 1 || response[1] != 0x42 || response[2] != command)
            throw new IOException("AFE 寄存器响应头不匹配；请重新读取设备。");
        if (response[3] != 0)
            throw new IOException(response[3] switch {
                1 => "寄存器请求格式不合法。", 2 => "AFE 授权已过期，请重新读取设备打开会话。",
                3 => "固件未启用寄存器调试，或不支持此命令。", 5 => "该地址/位为只读、保留、命令或有副作用字段。",
                6 => "寄存器在编辑后已变化，CAS 拒绝写入；请重新读取。",
                7 => "AFE 通信、写后读回或恢复未确认；结果可能不确定，请重新读取，设备保持冻结。",
                8 => "请先进入调试冻结；进入前需要完整有效测量且电流在 ±200mA 内。",
                _ => $"AFE 寄存器状态 0x{response[3]:X2}。" });
        if (response.Length != payloadBytes + 6) throw new IOException("AFE 寄存器响应长度不匹配。");
        byte[] result = new byte[payloadBytes]; Array.Copy(response, 4, result, 0, result.Length);
        return result;
    }

    public async Task<AfeRegisterDeviceInfo> ReadAfeRegisterInfoAsync(CancellationToken ct = default)
    {
        byte[] p = await AfeRegisterCommandAsync(0x10, 0, Array.Empty<byte>(), 12, ct);
        ushort model = BinaryPrimitives.ReadUInt16BigEndian(p.AsSpan(1, 2));
        ushort element = BinaryPrimitives.ReadUInt16BigEndian(p.AsSpan(9, 2));
        if (p[0] != 1 || p[3] > 1 || p[4] > 1 || p[11] == 0 || p[11] > 16 || element == 0 ||
            (model != 0x1124 && model != 0x3510 && model != 0x3520))
            throw new IOException("未知 AFE 寄存器接口版本或比例。");
        return new AfeRegisterDeviceInfo(model, p[3] != 0,
            p[4] != 0, BinaryPrimitives.ReadUInt32BigEndian(p.AsSpan(5, 4)).ToString("x8"),
            element, p[11]);
    }

    public async Task<byte> ReadAfeRegisterAsync(ushort token, byte address, bool consumeFlags = false, CancellationToken ct = default)
    {
        byte[] p = await AfeRegisterCommandAsync(0x11, token, new[] { address, consumeFlags ? (byte)1 : (byte)0 }, 2, ct);
        if (p[0] != address) throw new IOException("寄存器读回地址不匹配。");
        return p[1];
    }

    public async Task<byte> WriteAfeRegisterAsync(ushort token, byte address, byte mask, byte expected, byte value, CancellationToken ct = default)
    {
        byte[] p = await AfeRegisterCommandAsync(0x13, token, new[] { address, mask, expected, value }, 2, ct);
        if (p[0] != address || (p[1] & mask) != (value & mask)) throw new IOException("寄存器地址/目标字段读回不一致。");
        return p[1];
    }

    public Task BeginAfeRegisterDebugAsync(ushort token, CancellationToken ct = default) =>
        AfeRegisterCommandAsync(0x12, token, Array.Empty<byte>(), 0, ct);

    public Task RestoreAfeRegisterDebugAsync(ushort token, CancellationToken ct = default) =>
        AfeRegisterCommandAsync(0x14, token, Array.Empty<byte>(), 0, ct);
}
