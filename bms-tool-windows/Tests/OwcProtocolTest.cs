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

    static byte[] Extended(byte[] raw, byte protocol = 2, byte mode = 0, uint age = 100, ushort session = 9)
    {
        byte[] old = Payload(2, raw, age, session);
        byte[] payload = new byte[36 + raw.Length];
        old.AsSpan(0, 32).CopyTo(payload);
        payload[0] = 3; payload[32] = mode; payload[33] = protocol;
        payload[34] = raw.Length == 0 ? (byte)0 : protocol;
        raw.CopyTo(payload, 36);
        return payload;
    }
    static void Expect<T>(Action action, string name) where T : Exception
    {
        try { action(); } catch (T) { checks++; return; }
        throw new Exception("应拒绝：" + name);
    }
    static void CheckExtended(byte[] realtime)
    {
        byte[] raw = [0, 80, 76, 236, 0, 136];
        var live = CommToolOwcProtocol.DecodeSnapshotV3(Extended(raw), 0x80);
        var snapshots = new Dictionary<byte, OwcSnapshot> { [0] = live, [0x80] = live };
        var fields = CommToolOwcProtocol.VisibleFields(live, snapshots).ToDictionary(f => f.Name, f => f.Value);
        Check(fields["SOC"] == "80" && fields["总电压"] == "76" && fields["电池温度"] == "-20", "锂神独立量纲及有符号温度");
        Check(!fields.ContainsKey("电流") && !fields.ContainsKey("MOS温度"), "不伪造未提供的测量");
        Check(live.Version == 3 && live.Protocol == OwcWireProtocol.LishenV111, "v3显式协议");
        snapshots[0] = snapshots[0x80] = live with { AgeMs = 5001 };
        Check(!CommToolOwcProtocol.Online(snapshots) &&
            CommToolOwcProtocol.VisibleFields(snapshots[0x80], snapshots).Count == 0, "锂神过期隐藏");
        snapshots[0] = snapshots[0x80] = live with { MismatchProtocol = OwcWireProtocol.A133 };
        Check(!CommToolOwcProtocol.Online(snapshots) &&
            CommToolOwcProtocol.VisibleFields(snapshots[0x80], snapshots).Count == 0, "协议不匹配隐藏旧测量");
        var waiting = CommToolOwcProtocol.DecodeSnapshotV3(Extended([], protocol: 0), 0);
        Check(!CommToolOwcProtocol.Online(new Dictionary<byte, OwcSnapshot> { [0] = waiting }), "自动确认前不在线");
        Reject(() => CommToolOwcProtocol.DecodeSnapshot(Extended(raw), 0), "旧解析入口不接受v3");
        Reject(() => CommToolOwcProtocol.DecodeSnapshotV3(Extended(raw), 0x3A), "锂神不能装作A1.3.3槽");
        byte[] invalid = Extended(raw); invalid[33] = 1;
        Reject(() => CommToolOwcProtocol.DecodeSnapshotV3(invalid, 0), "槽协议与锁定协议冲突");
        invalid = Extended(raw); invalid[32] = 3;
        Reject(() => CommToolOwcProtocol.DecodeSnapshotV3(invalid, 0), "未知接收模式");
        invalid = Extended([], protocol: 0); invalid[34] = 2;
        Reject(() => CommToolOwcProtocol.DecodeSnapshotV3(invalid, 0), "空槽不能带报文协议");
        byte[] bad = (byte[])raw.Clone(); bad[1] = 101;
        bad[^1] = (byte)(bad.Take(5).Sum(b => (int)b) & 255);
        Reject(() => CommToolOwcProtocol.DecodeSnapshotV3(Extended(bad), 0x80), "非法SOC");
        foreach (int current in new[] { -127, -60, -1, 0, 1, 59, 60, 127, -128 })
        {
            byte[] ext = [unchecked((byte)current), 73, 76, 31, 0xC4, 0];
            ext[5] = (byte)(ext.Take(5).Sum(x => (int)x) & 255);
            var sample = CommToolOwcProtocol.DecodeSnapshotV3(Extended(ext), 0x80);
            var values = CommToolOwcProtocol.DecodeFields(ext, OwcWireProtocol.LishenV111)
                .ToDictionary(x => x.Name, x => x.Value);
            Check(values["电流"] == (current == -128 ? "--" : current.ToString()), "扩展电流符号/无效值");
            Check(values["故障码"] == "0x04" && values["故障"] == "充电高温保护", "扩展故障沿用映射");
            Check(!(sample with { AgeMs = 5001 }).Fresh, "电流59/60不误用A133静态帧有效期");
        }
        byte[] capabilities = [1, 7, 3, 0, 36, 0, 3, 1];
        Check(CommToolOwcProtocol.DecodeCapabilities(capabilities) == 7, "能力合同");
        Reject(() => CommToolOwcProtocol.DecodeCapabilities([1, 7, 3]), "截断能力");
        var commands = new List<(byte Command, byte[] Payload)>();
        using var modern = new CommToolOwcClient((command, payload, _) =>
        {
            commands.Add((command, payload));
            return command switch
            {
                0x01 => [1, 0, 2, 9, ..new byte[24]],
                0x45 => capabilities,
                0x46 => [],
                0x47 => Extended(payload[0] is 0 or 0x80 ? raw : [], mode: 2),
                _ => throw new Exception("unexpected command")
            };
        });
        modern.Start(OwcReceiveMode.LishenV111, default);
        Check(modern.UsesExtendedSnapshots && commands[1].Command == 0x46 &&
            commands[1].Payload.SequenceEqual(new byte[] { 1, 2 }), "新设备按所选模式启动");
        Check(modern.TryReadFirmwareVersion(default) == new Version(0, 2, 9) &&
            commands[^1].Command == 0x01 && commands[^1].Payload.Length == 0, "只读 GET_INFO 解析 Comm Tool 固件版本");
        Check(modern.ReadSnapshots(default).Count == 6, "新设备查询明确selector并验证整组");
        modern.SetEnabled(false, default);
        Check(commands[^1].Command == 0x46 && commands[^1].Payload.SequenceEqual(new byte[] { 0, 2 }), "停止不丢失所选模式");
        commands.Clear();
        using var legacy = new CommToolOwcClient((command, payload, _) =>
        {
            commands.Add((command, payload));
            if (command is 0x01 or 0x45) throw new CommToolCommandException(2, "UNSUPPORTED");
            return command == 0x43 ? [] : Payload(1, payload[0] is 0 or 0x3A ? realtime : []);
        });
        legacy.Start(OwcReceiveMode.Auto, default);
        Check(!legacy.UsesExtendedSnapshots && commands[^1].Command == 0x43, "旧设备自动选项明确回退A1.3.3");
        Check(legacy.TryReadFirmwareVersion(default) is null, "不应答 GET_INFO 的旧设备可继续监控");
        Check(legacy.ReadSnapshots(default).Count == 5, "旧设备保留五ID及v1快照");
        commands.Clear();
        Expect<System.IO.IOException>(() => legacy.Start(OwcReceiveMode.LishenV111, default), "旧设备拒绝锂神模式");
        Check(commands.Count == 1, "旧设备错误选择不会启动接收");
        using var timeout = new CommToolOwcClient((_, _, _) => throw new TimeoutException());
        Expect<TimeoutException>(() => timeout.Start(OwcReceiveMode.Auto, default), "超时不能伪装旧固件");
        using var malformed = new CommToolOwcClient((_, _, _) => [0]);
        Expect<FormatException>(() => malformed.Start(OwcReceiveMode.Auto, default), "非法能力不能回退");
        Check(malformed.TryReadFirmwareVersion(default) is null && timeout.TryReadFirmwareVersion(default) is null,
            "不显示截断或超时的固件版本");
        using var pins = new CommToolOwcClient((_, _, _) => [1, 0, 3, 0, 36, 0, 3, 1]);
        Expect<System.IO.IOException>(() => pins.Start(OwcReceiveMode.Auto, default), "无接收引脚能力时禁止启用");
        Expect<OperationCanceledException>(() => modern.Start(OwcReceiveMode.Auto, new CancellationToken(true)), "初始化尊重取消");
        Check(!CommToolOwcProtocol.Coherent(new[] { live, live with { Protocol = OwcWireProtocol.A133 } }), "整组不能跨协议");
        Check(!CommToolOwcProtocol.Coherent(new[] { live, live with { Mode = OwcReceiveMode.A133 } }), "整组不能跨模式");
    }

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
        CheckExtended(realtime);
        Console.WriteLine($"PASS OWC protocol: {checks} checks, no serial/firmware access");
        return 0;
    }
}
