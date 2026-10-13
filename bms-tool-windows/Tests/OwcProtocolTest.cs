using System.Buffers.Binary;
using System.Text;
using BmsTool.Windows;

static class OwcProtocolTest
{
    static int checks;
    static void Check(bool condition, string name)
    {
        if (!condition) throw new Exception(name);
        checks++;
    }
    static void Reject(Action action, string name)
    {
        try { action(); } catch (FormatException) { checks++; return; }
        throw new Exception("应拒绝：" + name);
    }
    static byte[] Packet(byte id, byte[] data)
    {
        byte[] raw = new byte[data.Length + (id == 1 ? 3 : 4)];
        raw[0] = id; raw[1] = 0x13;
        int offset = id == 1 ? 2 : 3;
        if (id != 1) raw[2] = (byte)data.Length;
        data.CopyTo(raw, offset);
        raw[^1] = (byte)(raw.Take(raw.Length - 1).Sum(b => (int)b) & 255);
        return raw;
    }
    static void U16(byte[] data, int offset, ushort value) =>
        BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(offset), value);
    static byte[] Payload(byte version, byte[] raw, uint age = 100, ushort session = 0, bool valid = true)
    {
        byte[] payload = new byte[32 + raw.Length];
        payload[0] = version; payload[1] = 1;
        payload[2] = (byte)(raw.Length == 0 ? 0 : 1);
        payload[3] = (byte)(raw.Length != 0 && valid ? 1 : 0);
        BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(4), age);
        BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(8), 10);
        U16(payload, 28, (ushort)raw.Length); U16(payload, 30, session);
        raw.CopyTo(payload, 32); return payload;
    }
    static Dictionary<string, string> Fields(byte[] raw) =>
        CommToolOwcProtocol.DecodeFields(raw).ToDictionary(f => f.Name, f => f.Value);

    static int Main()
    {
        byte[] data = Enumerable.Repeat((byte)255, 29).ToArray();
        data[0] = 160; U16(data, 1, 520); U16(data, 3, 4877);
        data[5] = 65; data[6] = 60; data[7] = 70; data[8] = 0x26; data[9] = 0x21;
        U16(data, 11, 123); U16(data, 13, 3400); U16(data, 15, 3300);
        data[17] = 16; data[18] = 1; data[24] = 0;
        byte[] realtime = Packet(0x3A, data);
        foreach (byte version in new byte[] { 1, 2 })
        {
            var snapshot = CommToolOwcProtocol.DecodeSnapshot(Payload(version, realtime, session: (ushort)(version == 1 ? 0 : 7)), 0x3A);
            var fields = CommToolOwcProtocol.VisibleFields(snapshot, new Dictionary<byte, OwcSnapshot> { [0x3A] = snapshot }).ToDictionary(f => f.Name, f => f.Value);
            Check(fields["SOC"] == "80" && fields["总电压"] == "52" && fields["电流"] == "-12.3", "v1/v2标准量纲");
            Check(fields["最高温度"] == "25" && fields["MOS温度"] == "30", "温度偏移");
            Check(fields["循环次数"] == "123" && fields["最高单体"] == "3400" && fields["最高单体位置原值"] == "16", "周期及极值");
            Check(fields["工作状态"] == "充电" && fields["请求应答设备"] == "充电器", "工作状态位域");
            Check(fields["BMS状态原值（需解码）"] == "--" && fields["加密/充电策略/互认"] == "未实现", "未实现业务");
            Check(fields["请求充电电流"] == "--" && fields["充电状态"] == "--", "无效充电参数");
            Check(snapshot.Version == version && snapshot.Fresh, "快照版本");
        }
        byte[] cellsData = new byte[6];
        U16(cellsData, 0, 3300); U16(cellsData, 2, 61001); U16(cellsData, 4, 65535);
        byte[] cells = Packet(0x3B, cellsData);
        Check(Fields(cells)["单体 1"] == "3300" && Fields(cells)["单体 2"] == "--" && Fields(cells)["单体 3"] == "--", "无效槽位");
        foreach (var (code, expected) in new[] { ("BMS-123", "BMS-123"), (new string('X', 32), new string('X', 32)), ("   ", "--") })
            Check(Fields(Packet(0x3C, Encoding.ASCII.GetBytes(code)))["电池编码"] == expected, "电池编码");
        Check(Fields(Packet(0x3C, [255]))["电池编码"] == "--", "无效编码");
        var live = CommToolOwcProtocol.DecodeSnapshot(Payload(1, realtime), 0x3A);
        var single = CommToolOwcProtocol.DecodeSnapshot(Payload(1, cells, 10000), 0x3B);
        var snapshots = new Dictionary<byte, OwcSnapshot> { [0x3A] = live, [0x3B] = single };
        Check(CommToolOwcProtocol.VisibleFields(single, snapshots).Count > 0, "在线长周期数据");
        snapshots[0x3A] = live with { AgeMs = 6000 };
        Check(!CommToolOwcProtocol.Online(snapshots) && CommToolOwcProtocol.VisibleFields(single, snapshots).Count == 0, "离线隐藏");
        byte[] invalidData = (byte[])data.Clone(); U16(invalidData, 1, 65535);
        snapshots[0x3A] = CommToolOwcProtocol.DecodeSnapshot(Payload(1, Packet(0x3A, invalidData)), 0x3A);
        Check(CommToolOwcProtocol.VisibleFields(single, snapshots).Count == 0, "AFE无效隐藏旧单体");
        Check(CommToolOwcProtocol.VisibleFields(live with { Enabled = false }, snapshots).Count == 0, "接收关闭");
        byte[] publicData = new byte[17];
        U16(publicData, 3, 65535); U16(publicData, 5, 78);
        var info = CommToolOwcProtocol.DecodeSnapshot(Payload(1, Packet(1, publicData), 80000), 1);
        var staticFields = CommToolOwcProtocol.VisibleFields(info, new Dictionary<byte, OwcSnapshot>());
        Check(staticFields.Any(f => f.Name == "额定容量" && f.Value == "7.8") && !staticFields.Any(f => f.Name == "总电压"), "离线静态接入信息");
        Check(!CommToolOwcProtocol.Coherent(new[] { live, live with { Version = 2 } }), "跨快照版本");
        Check(!CommToolOwcProtocol.Coherent(new[] { live, live with { Enabled = false } }), "跨enable");
        Check(!CommToolOwcProtocol.Coherent(new[] { live with { Version = 2, Session = 1 }, live with { Version = 2, Session = 2 } }), "跨会话");
        Reject(() => CommToolOwcProtocol.DecodeSnapshot(Payload(1, realtime, session: 1), 0), "v1保留字段");
        Reject(() => CommToolOwcProtocol.DecodeSnapshot(Payload(2, realtime), 0), "v2会话缺失");
        Reject(() => CommToolOwcProtocol.DecodeSnapshot(Payload(3, realtime), 0), "未知快照版本");
        Reject(() => CommToolOwcProtocol.DecodeSnapshot(Payload(1, realtime), 0x3C), "ID错配");
        Reject(() => CommToolOwcProtocol.DecodeSnapshot(Payload(1, realtime)[..^1], 0), "截断");
        foreach (byte[] bad in new[] { Packet(0x3B, [1]), Packet(0x3C, []), Packet(0x3A, new byte[26]), Packet(0x3A, new byte[30]) })
            Reject(() => CommToolOwcProtocol.DecodeSnapshot(Payload(1, bad), 0), "非法私有长度");
        byte[] badSum = (byte[])realtime.Clone(); badSum[^1] ^= 1;
        Reject(() => CommToolOwcProtocol.DecodeSnapshot(Payload(1, badSum), 0), "SUM与标志冲突");
        var badSnapshot = CommToolOwcProtocol.DecodeSnapshot(Payload(1, badSum, valid: false), 0);
        Check(CommToolOwcProtocol.VisibleFields(badSnapshot, snapshots).Count == 0, "坏SUM不显示");
        byte[] frame = CommToolOwcProtocol.EncodeCommand(7, 0x44, [0x3A]);
        Check(Convert.ToHexString(frame) == "55AA01000700440001003A6A07", "外层协议向量");
        var decoded = CommToolOwcProtocol.DecodeFrame(frame);
        Check(decoded.Sequence == 7 && decoded.Command == 0x44 && decoded.Payload.SequenceEqual(new byte[] { 0x3A }), "外层帧解析");
        frame[^1] ^= 1; Reject(() => CommToolOwcProtocol.DecodeFrame(frame), "外层CRC");
        Console.WriteLine($"PASS OWC protocol: {checks} checks, no serial/firmware access");
        return 0;
    }
}
