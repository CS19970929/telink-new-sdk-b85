using System.Buffers.Binary;
using System.Globalization;
using System.Text;

namespace BmsTool.Windows;

public enum OwcReceiveMode : byte { Auto = 0, A133 = 1, LishenV111 = 2 }
public enum OwcWireProtocol : byte { Unknown = 0, A133 = 1, LishenV111 = 2 }

public sealed record OwcSnapshot(bool Enabled, bool ChecksumOk, uint AgeMs, uint Good,
    uint ChecksumErrors, uint TimingErrors, uint Overflows, uint Sequence,
    byte[] Raw, ushort Session, byte Version)
{
    public OwcReceiveMode Mode { get; init; } = OwcReceiveMode.A133;
    public OwcWireProtocol Protocol { get; init; } = OwcWireProtocol.A133;
    public OwcWireProtocol FrameProtocol { get; init; } = OwcWireProtocol.A133;
    public OwcWireProtocol MismatchProtocol { get; init; }
    public bool Fresh => Enabled && ChecksumOk && Raw.Length > 0 &&
        AgeMs <= (FrameProtocol == OwcWireProtocol.A133 && Raw[0] is 0x3B or 0x3C ? 75000u : 5000u);
}

public sealed record OwcField(string Packet, string Name, string Value, string Unit);
public sealed record CommToolOwcFrame(byte Flags, ushort Sequence, byte Command, byte Status, byte[] Payload);

// 旧v1/v2仅解释A1.3.3；新命令v3显式携带协议，禁止按raw首字节猜协议。
public static class CommToolOwcProtocol
{
    public static readonly byte[] PacketIds = [1, 0x3A, 0x3B, 0x3C, 0];
    public static readonly byte[] ExtendedPacketIds = [1, 0x3A, 0x3B, 0x3C, 0x80, 0];
    public const byte LishenSelector = 0x80;
    public const int HeaderSize = 32;
    public const int ExtendedHeaderSize = 36;
    public static string ProtocolName(OwcWireProtocol protocol) => protocol switch
    {
        OwcWireProtocol.A133 => "A1.3.3",
        OwcWireProtocol.LishenV111 => "锂神 V1.1.1",
        _ => "正在识别"
    };
    public static byte DecodeCapabilities(ReadOnlySpan<byte> payload)
    {
        if (payload.Length != 8 || payload[0] != 1 || (payload[1] & ~7) != 0 ||
            payload[2] != 3 || payload[3] != 0 ||
            BinaryPrimitives.ReadUInt16LittleEndian(payload[4..]) != ExtendedHeaderSize ||
            BinaryPrimitives.ReadUInt16LittleEndian(payload[6..]) != 259)
            throw new FormatException("Comm Tool一线通能力响应非法或版本不支持");
        return payload[1];
    }


    public static ushort Crc16(ReadOnlySpan<byte> data)
    {
        ushort crc = 0xFFFF;
        foreach (byte value in data)
        {
            crc ^= value;
            for (int bit = 0; bit < 8; bit++)
                crc = (ushort)((crc & 1) != 0 ? (crc >> 1) ^ 0xA001 : crc >> 1);
        }
        return crc;
    }

    public static byte[] EncodeCommand(ushort sequence, byte command, ReadOnlySpan<byte> payload)
    {
        if (payload.Length > 512) throw new ArgumentException("Comm Tool payload超长");
        byte[] frame = new byte[12 + payload.Length];
        frame[0] = 0x55; frame[1] = 0xAA; frame[2] = 1;
        BinaryPrimitives.WriteUInt16LittleEndian(frame.AsSpan(4), sequence);
        frame[6] = command;
        BinaryPrimitives.WriteUInt16LittleEndian(frame.AsSpan(8), (ushort)payload.Length);
        payload.CopyTo(frame.AsSpan(10));
        BinaryPrimitives.WriteUInt16LittleEndian(frame.AsSpan(frame.Length - 2),
            Crc16(frame.AsSpan(0, frame.Length - 2)));
        return frame;
    }

    public static CommToolOwcFrame DecodeFrame(byte[] frame)
    {
        if (frame.Length < 12 || frame[0] != 0x55 || frame[1] != 0xAA || frame[2] != 1)
            throw new FormatException("Comm Tool外层帧头非法");
        int length = BinaryPrimitives.ReadUInt16LittleEndian(frame.AsSpan(8));
        if (length > 512 || frame.Length != length + 12) throw new FormatException("Comm Tool外层帧长度非法");
        if (Crc16(frame.AsSpan(0, frame.Length - 2)) != BinaryPrimitives.ReadUInt16LittleEndian(frame.AsSpan(frame.Length - 2)))
            throw new FormatException("Comm Tool响应CRC错误");
        return new(frame[3], BinaryPrimitives.ReadUInt16LittleEndian(frame.AsSpan(4)),
            frame[6], frame[7], frame[10..^2]);
    }

    public static OwcSnapshot DecodeSnapshot(ReadOnlySpan<byte> payload, byte requestedId) =>
        DecodeSnapshotCore(payload, requestedId, false);
    public static OwcSnapshot DecodeSnapshotV3(ReadOnlySpan<byte> payload, byte requestedId) =>
        DecodeSnapshotCore(payload, requestedId, true);

    private static OwcSnapshot DecodeSnapshotCore(ReadOnlySpan<byte> payload, byte requestedId, bool extended)
    {
        int header = extended ? ExtendedHeaderSize : HeaderSize;
        if (payload.Length < header) throw new FormatException("一线通快照头不完整");
        byte version = payload[0];
        if (extended ? version != 3 : version is not (1 or 2))
            throw new FormatException($"不支持的快照版本：{version}");
        if (!(extended ? ExtendedPacketIds : PacketIds).Contains(requestedId) ||
            payload[1] > 1 || payload[2] > 1 || payload[3] > 1)
            throw new FormatException("一线通快照头非法");
        var mode = OwcReceiveMode.A133;
        var protocol = OwcWireProtocol.A133;
        var frameProtocol = OwcWireProtocol.A133;
        var mismatch = OwcWireProtocol.Unknown;
        if (extended)
        {
            if (payload[32] > 2 || payload[33] > 2 || payload[34] > 2 || payload[35] > 2)
                throw new FormatException("快照协议标识非法");
            mode = (OwcReceiveMode)payload[32]; protocol = (OwcWireProtocol)payload[33];
            frameProtocol = (OwcWireProtocol)payload[34]; mismatch = (OwcWireProtocol)payload[35];
            if (mode != OwcReceiveMode.Auto && protocol != OwcWireProtocol.Unknown &&
                (byte)mode != (byte)protocol) throw new FormatException("固定接收模式与锁定协议冲突");
        }
        ushort session = BinaryPrimitives.ReadUInt16LittleEndian(payload[30..]);
        if (version == 1 && session != 0) throw new FormatException("v1保留字段必须为零");
        int length = BinaryPrimitives.ReadUInt16LittleEndian(payload[28..]);
        if (length != payload.Length - header || length > 259 || (payload[2] != 0) != (length != 0))
            throw new FormatException("一线通快照长度非法");
        byte[] raw = payload[header..].ToArray();
        bool valid = payload[3] != 0;
        if (raw.Length != 0)
        {
            if (frameProtocol == OwcWireProtocol.LishenV111)
            {
                ValidateLishen(raw);
                if (requestedId is not (0 or LishenSelector)) throw new FormatException("锂神快照selector不一致");
            }
            else if (frameProtocol == OwcWireProtocol.A133)
            {
                ValidatePacket(raw);
                if (requestedId == LishenSelector || (requestedId != 0 && raw[0] != requestedId))
                    throw new FormatException("一线通报文ID不一致");
            }
            else throw new FormatException("非空快照缺少报文协议");
            if (version >= 2 && valid && session == 0) throw new FormatException("有效报文缺少会话");
            if (extended && valid && protocol != frameProtocol) throw new FormatException("快照跨接收协议");
            if (valid != SumOk(raw)) throw new FormatException("一线通SUM与有效标志不一致");
            if (requestedId != 0 && !valid) throw new FormatException("有效缓存含坏SUM");
        }
        else if (valid || (extended && frameProtocol != OwcWireProtocol.Unknown))
            throw new FormatException("空快照标识非法");
        return new OwcSnapshot(payload[1] != 0, valid, U32(payload, 4), U32(payload, 8),
            U32(payload, 12), U32(payload, 16), U32(payload, 20), U32(payload, 24), raw, session, version)
            { Mode = mode, Protocol = protocol, FrameProtocol = frameProtocol, MismatchProtocol = mismatch };
    }

    private static void ValidateLishen(byte[] raw)
    {
        if (raw.Length != 6 || raw[1] > 100 ||
            !((raw[0] == 0 && raw[4] == 0) || IsLishenExtended(raw)))
            throw new FormatException("锂神V1.1.1报文格式非法");
    }

    public static bool IsLishenExtended(byte[] raw) =>
        raw.Length == 6 && (raw[4] & 0xC0) == 0xC0 && (raw[4] & 0x3F) <= 0x29;

    private static uint U32(ReadOnlySpan<byte> data, int offset) =>
        BinaryPrimitives.ReadUInt32LittleEndian(data[offset..]);
    private static ushort U16(byte[] data, int offset) =>
        BinaryPrimitives.ReadUInt16LittleEndian(data.AsSpan(offset));

    private static void ValidatePacket(byte[] raw)
    {
        if (raw.Length < 4 || !PacketIds.Contains(raw[0]) || raw[0] == 0 ||
            (raw[1] & 15) == 15 || (raw[1] >> 4) == 15 ||
            raw.Length != (raw[0] == 1 ? 20 : raw[2] + 4))
            throw new FormatException("一线通报文ID、版本或长度非法");
        if ((raw[0] == 0x3A && raw[2] is not (>= 27 and <= 29)) ||
            (raw[0] == 0x3B && (raw[2] == 0 || raw[2] % 2 != 0)) ||
            (raw[0] == 0x3C && raw[2] == 0))
            throw new FormatException("一线通私有报文长度非法");
    }

    private static bool SumOk(byte[] raw) => raw.Length > 0 &&
        (raw.Take(raw.Length - 1).Sum(value => (int)value) & 255) == raw[^1];

    public static bool Online(IReadOnlyDictionary<byte, OwcSnapshot> snapshots) =>
        snapshots.Values.Any(s => s.Enabled && s.ChecksumOk && s.Raw.Length != 0 && s.AgeMs <= 5000 &&
            s.Protocol != OwcWireProtocol.Unknown && s.Protocol == s.FrameProtocol && s.MismatchProtocol == OwcWireProtocol.Unknown);

    public static bool Coherent(IEnumerable<OwcSnapshot> snapshots) =>
        snapshots.Select(s => (s.Version, s.Session, s.Enabled, s.Mode, s.Protocol)).Distinct().Count() == 1;

    public static IReadOnlyList<OwcField> VisibleFields(OwcSnapshot snapshot,
        IReadOnlyDictionary<byte, OwcSnapshot> snapshots)
    {
        if (!snapshot.Enabled || !snapshot.ChecksumOk || snapshot.Raw.Length == 0 ||
            snapshot.MismatchProtocol != OwcWireProtocol.Unknown) return [];
        if (snapshot.FrameProtocol == OwcWireProtocol.LishenV111)
            return Online(snapshots) && snapshot.Fresh ? DecodeFields(snapshot.Raw, snapshot.FrameProtocol) : [];
        byte id = snapshot.Raw[0];
        if (id != 1 && (!Online(snapshots) || !snapshot.Fresh)) return [];
        if (id == 0x3B && snapshots.TryGetValue(0x3A, out var realtime) && realtime.Raw.Length != 0 &&
            (!realtime.Fresh || U16(realtime.Raw, 4) > 10000)) return [];
        var fields = DecodeFields(snapshot.Raw);
        // 接入公有帧只保留静态资料，实时测量以3A为准。
        if (id == 1)
            fields.RemoveAll(row => row.Name is not ("ID" or "协议" or "厂家代码" or "电池型号" or
                "材料代码" or "材料" or "额定电压" or "额定容量"));
        return fields;
    }

    public static List<OwcField> DecodeFields(byte[] raw, OwcWireProtocol protocol = OwcWireProtocol.A133)
    {
        var rows = new List<OwcField>();
        if (!SumOk(raw)) return rows;
        if (protocol == OwcWireProtocol.LishenV111)
        {
            ValidateLishen(raw);
            rows.Add(new("锂神", "协议", IsLishenExtended(raw) ? "锂神扩展（本项目）" : "锂神 V1.1.1", ""));
            rows.Add(new("锂神", "SOC", raw[1].ToString(CultureInfo.InvariantCulture), "%"));
            rows.Add(new("锂神", "总电压", raw[2].ToString(CultureInfo.InvariantCulture), "V"));
            rows.Add(new("锂神", "电池温度", unchecked((sbyte)raw[3]).ToString(CultureInfo.InvariantCulture), "℃"));
            if (IsLishenExtended(raw))
            {
                rows.Add(new("锂神", "电流", raw[0] == 0x80 ? "--" :
                    unchecked((sbyte)raw[0]).ToString(CultureInfo.InvariantCulture), "A"));
                byte fault = (byte)(raw[4] & 0x3F);
                rows.Add(new("锂神", "故障码", $"0x{fault:X2}", ""));
                rows.Add(new("锂神", "故障", FaultName(fault), ""));
            }
            return rows;
        }
        if (protocol != OwcWireProtocol.A133) throw new FormatException("未知一线通报文协议");
        ValidatePacket(raw);
        string packet = $"0x{raw[0]:X2}";
        void Add(string name, string value, string unit = "") => rows.Add(new(packet, name, value, unit));
        void Scaled(string name, int value, double scale, string unit, int maximum, double bias = 0) =>
            Add(name, value > maximum ? "--" : (value * scale + bias).ToString("0.###", CultureInfo.InvariantCulture), unit);
        Add("ID", packet); Add("协议", $"0x{raw[1]:X2}");
        if (raw[0] is 1 or 0x3A)
        {
            int start = raw[0] == 1 ? 9 : 3;
            Scaled("SOC", raw[start], 0.5, "%", 200);
            Scaled("总电压", U16(raw, start + 1), 0.1, "V", 10000);
            Scaled("电流", U16(raw, start + 3), 0.1, "A", 10000, -500);
            Scaled("最高温度", raw[start + 5], 1, "℃", 190, -40);
            Scaled("最低温度", raw[start + 6], 1, "℃", 190, -40);
            Scaled("MOS温度", raw[start + 7], 1, "℃", 190, -40);
            byte fault = raw[start + 8], work = raw[start + 9];
            Add("故障码", $"0x{fault:X2}"); Add("故障", FaultName(fault));
            bool workValid = work != 255 && (raw[0] == 1 ? work <= 2 : (work & 0x18) == 0);
            Add("工作状态原值", $"0x{work:X2}");
            Add("工作状态", workValid ? (work & 7) switch { 0 => "放电", 1 => "充电", 2 => "回馈", _ => "--" } : "--");
            if (raw[0] == 1)
            {
                Add("厂家代码", raw[2] == 255 ? "--" : $"0x{raw[2]:X2}");
                Add("电池型号", raw[3] > 15 ? "--" : raw[3].ToString());
                Add("材料代码", $"0x{raw[4]:X2}");
                Add("材料", raw[4] switch { 1 => "磷酸铁锂", 2 => "锰酸锂", 3 => "三元锂",
                    4 => "钴酸锂", 5 => "聚合锂", 6 => "钛酸锂", 7 => "铅酸", 8 => "镍氢", 9 => "钠", _ => "未定义/保留" });
                Scaled("额定电压", U16(raw, 5), 0.1, "V", 10000);
                Scaled("额定容量", U16(raw, 7), 0.1, "Ah", 10000);
            }
            else
            {
                Add("请求应答设备", workValid ? (work >> 5) switch { 0 => "不需要应答", 1 => "充电器", 2 => "ECU", 3 => "TBOX", _ => "--" } : "--");
                Add("BMS状态原值（需解码）", raw[27] == 0 ? "--" : $"0x{raw[13]:X2}");
                Add("加密/充电策略/互认", "未实现");
                Scaled("循环次数", U16(raw, 14), 1, "", 60000);
                Scaled("最高单体", U16(raw, 16), 1, "mV", 10000);
                Scaled("最低单体", U16(raw, 18), 1, "mV", 10000);
                Add("最高单体位置原值", raw[20] is >= 1 and <= 25 ? raw[20].ToString() : "--");
                Add("最低单体位置原值", raw[21] is >= 1 and <= 25 ? raw[21].ToString() : "--");
                Add("充电状态原值", raw[26] == 255 ? "--" : $"0x{raw[26]:X2}");
                Add("充电状态", raw[26] switch { 0 => "保留", 1 => "满充停止", 2 => "非法充电", 3 => "电池保护", 4 => "正在充电", _ => "--" });
                Scaled("允许回馈电流", raw[22], 0.1, "A", 254);
                Scaled("请求充电电压", U16(raw, 23), 0.1, "V", 10000);
                Scaled("请求充电电流", raw[25], 0.1, "A", 254);
            }
        }
        else if (raw[0] == 0x3B)
            for (int i = 0; i < raw[2] / 2; i++) Scaled($"单体 {i + 1}", U16(raw, 3 + i * 2), 1, "mV", 60000);
        else if (raw[0] == 0x3C)
        {
            byte[] code = raw[3..^1];
            bool valid = code.All(b => b is >= 32 and <= 126) && code.Any(b => b != 32);
            Add("电池编码", valid ? Encoding.ASCII.GetString(code) : "--");
        }
        return rows;
    }

    public static string FaultName(byte code) => code switch
    {
        0 => "无故障", 1 => "放电过流二级保护", 2 => "放电过流一级保护", 3 => "低温充电保护",
        4 => "充电高温保护", 5 => "放电高温保护", 6 => "欠压保护", 7 => "过压保护",
        8 => "充电过流保护", 9 => "放电低温保护", 0x0A => "充电MOS故障", 0x0B => "放电MOS故障",
        0x14 => "充电高温异常报警", 0x15 => "放电高温异常报警", 0x22 => "放电过流一级预警",
        0x23 => "低温充电预警", 0x24 => "充电高温预警", 0x25 => "放电高温预警",
        0x26 => "欠压预警", 0x27 => "过压预警", 0x28 => "充电过流预警",
        0x29 => "放电低温预警", _ => "未定义/保留"
    };
}
