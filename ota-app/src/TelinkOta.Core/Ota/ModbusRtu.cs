namespace TelinkOta.Core.Ota;

/// <summary>
/// 最小 Modbus RTU codec（仅用于升级后通过 SPP 读取固件版本复核，功能码 0x03）。
/// 依据 vendor/ble_sample 对接文档：设备地址 0x01，CRC16/MODBUS，低字节在前。
/// </summary>
public static class ModbusRtu
{
    public const byte DefaultSlaveAddr = 0x01;
    public const byte FuncReadHolding = 0x03;

    /// <summary>构建读保持寄存器请求：[addr][0x03][reg(2 LE)][qty(2 LE)][crc16(2 LE)]。</summary>
    public static byte[] BuildReadRequest(ushort startRegister, ushort quantity, byte slaveAddr = DefaultSlaveAddr)
    {
        var frame = new byte[8];
        frame[0] = slaveAddr;
        frame[1] = FuncReadHolding;
        frame[2] = (byte)(startRegister & 0xFF);
        frame[3] = (byte)(startRegister >> 8);
        frame[4] = (byte)(quantity & 0xFF);
        frame[5] = (byte)(quantity >> 8);
        ushort crc = Crc16.Compute(frame.AsSpan(0, 6));
        frame[6] = (byte)(crc & 0xFF);
        frame[7] = (byte)(crc >> 8);
        return frame;
    }

    /// <summary>
    /// 校验并解析 0x03 响应。
    /// 响应：[addr][0x03][byteCount][data...][crc16(2 LE)]。
    /// 返回数据区；帧不完整/CRC 错误返回 null。
    /// </summary>
    public static bool TryParseReadResponse(byte[] frame, out byte[]? data)
    {
        data = null;
        if (frame is null || frame.Length < 5)
            return false;
        if (frame[0] != DefaultSlaveAddr)
            return false;
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
}
