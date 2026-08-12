namespace TelinkOta.Core.Ota;

/// <summary>
/// 最小 Modbus RTU codec（用于 BMS 寄存器读写，功能码 0x03 等）。
/// 依据 vendor/ble_sample modbus_rtu.c 实码：**大端 u16**（u16be/put_u16be）、CRC16/MODBUS 低字节在前。
/// </summary>
public static class ModbusRtu
{
    public const byte DefaultSlaveAddr = 0x01;
    public const byte FuncReadHolding = 0x03;
    public const byte FuncWriteMultiple = 0x10;

    /// <summary>构建读保持寄存器请求：[addr][0x03][reg(2 BE)][qty(2 BE)][crc16(2 LE)]。</summary>
    public static byte[] BuildReadRequest(ushort startRegister, ushort quantity, byte slaveAddr = DefaultSlaveAddr)
    {
        if (quantity == 0 || quantity > 0x7D)
            throw new ArgumentOutOfRangeException(nameof(quantity), "0x03 一次最多 125 寄存器");
        var frame = new byte[8];
        frame[0] = slaveAddr;
        frame[1] = FuncReadHolding;
        frame[2] = (byte)(startRegister >> 8);   // 大端：高字节在前（固件 u16be）
        frame[3] = (byte)(startRegister & 0xFF);
        frame[4] = (byte)(quantity >> 8);
        frame[5] = (byte)(quantity & 0xFF);
        ushort crc = Crc16.Compute(frame.AsSpan(0, 6));
        frame[6] = (byte)(crc & 0xFF);
        frame[7] = (byte)(crc >> 8);
        return frame;
    }

    /// <summary>
    /// 构建写多个保持寄存器请求：
    /// [addr][0x10][reg(2 BE)][qty(2 BE)][byteCount][data...][crc16(2 LE)]。
    /// data 必须是按 Modbus 线序排列的偶数字节数组。
    /// </summary>
    public static byte[] BuildWriteMultipleRequest(ushort startRegister, ReadOnlySpan<byte> data,
        byte slaveAddr = DefaultSlaveAddr)
    {
        if (data.Length == 0 || (data.Length & 1) != 0)
            throw new ArgumentException("0x10 数据长度必须是非零偶数", nameof(data));

        int quantity = data.Length / 2;
        if (quantity > 0x7B)
            throw new ArgumentOutOfRangeException(nameof(data), "0x10 一次最多写 123 个寄存器");

        var frame = new byte[9 + data.Length];
        frame[0] = slaveAddr;
        frame[1] = FuncWriteMultiple;
        frame[2] = (byte)(startRegister >> 8);
        frame[3] = (byte)startRegister;
        frame[4] = (byte)(quantity >> 8);
        frame[5] = (byte)quantity;
        frame[6] = (byte)data.Length;
        data.CopyTo(frame.AsSpan(7));
        ushort crc = Crc16.Compute(frame.AsSpan(0, frame.Length - 2));
        frame[^2] = (byte)crc;
        frame[^1] = (byte)(crc >> 8);
        return frame;
    }

    /// <summary>
    /// 校验并解析 0x03 响应。
    /// 响应：[addr][0x03][byteCount][data...][crc16(2 LE)]。
    /// 返回响应中的从机地址（data 随帧返回）；帧不完整/CRC 错误返回 null。
    /// </summary>
    public static bool TryParseReadResponse(byte[] frame, out byte[]? data, out byte slaveAddr)
    {
        data = null;
        slaveAddr = 0;
        if (frame is null || frame.Length < 5)
            return false;
        slaveAddr = frame[0];
        if (frame[1] == (byte)(FuncReadHolding | 0x80))
        {
            // 异常响应：[addr][0x83][exception] — 长度 5
            return false;
        }
        if (frame[1] != FuncReadHolding)
            return false;

        int byteCount = frame[2];
        int expected = 3 + byteCount + 2;
        if (frame.Length < expected)
            return false; // 分片未收齐

        ushort crc = Crc16.Compute(frame.AsSpan(0, expected - 2));
        ushort stored = (ushort)(frame[expected - 2] | (frame[expected - 1] << 8));
        if (crc != stored)
            return false;

        data = new byte[byteCount];
        Array.Copy(frame, 3, data, 0, byteCount);
        return true;
    }

    /// <summary>校验 0x10 回包中的从机地址、起始寄存器、数量和 CRC。</summary>
    public static bool TryParseWriteMultipleResponse(byte[] frame, ushort expectedStartRegister,
        ushort expectedQuantity, byte expectedSlaveAddr = DefaultSlaveAddr)
    {
        if (frame is null || frame.Length < 8 ||
            frame[0] != expectedSlaveAddr || frame[1] != FuncWriteMultiple)
            return false;

        ushort crc = Crc16.Compute(frame.AsSpan(0, 6));
        ushort stored = (ushort)(frame[6] | (frame[7] << 8));
        if (crc != stored)
            return false;

        ushort start = (ushort)((frame[2] << 8) | frame[3]);
        ushort quantity = (ushort)((frame[4] << 8) | frame[5]);
        return start == expectedStartRegister && quantity == expectedQuantity;
    }

    /// <summary>兼容重载：按默认从机地址 0x01 校验。</summary>
    public static bool TryParseReadResponse(byte[] frame, out byte[]? data)
    {
        bool ok = TryParseReadResponse(frame, out data, out byte addr);
        return ok && addr == DefaultSlaveAddr;
    }
}
