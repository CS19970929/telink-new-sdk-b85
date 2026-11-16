"""A1.3.3 public fields; encryption, charging policy and mutual recognition are unsupported."""
from __future__ import annotations

import struct
from dataclasses import dataclass
from comm_tool_host import CommToolCommandError

CMD_OWC_CONTROL = 0x43
CMD_OWC_SNAPSHOT = 0x44
CMD_OWC_INFO = 0x45
CMD_OWC_CONFIG = 0x46
CMD_OWC_FRAME = 0x47
LISHEN_SELECTOR = 0x80
OWC_IDS = (0, 0x01, 0x3A, 0x3B, 0x3C)
SNAPSHOT_HEADER = struct.Struct("<BBBBIIIIIIHH")
PUBLIC_STATIC_FIELDS = {"ID", "Protocol", "Manufacturer", "Battery model", "Chemistry code",
                        "Chemistry", "Rated voltage", "Rated capacity"}
FAULT_NAMES = {
    0x00: "无故障", 0x01: "放电过流二级保护", 0x02: "放电过流一级保护",
    0x03: "低温充电保护", 0x04: "充电高温保护", 0x05: "放电高温保护",
    0x06: "欠压保护", 0x07: "过压保护", 0x08: "充电过流保护", 0x09: "放电低温保护",
    0x0A: "充电MOS故障", 0x0B: "放电MOS故障",
    0x14: "充电高温异常报警", 0x15: "放电高温异常报警",
    0x22: "放电过流一级预警", 0x23: "低温充电预警", 0x24: "充电高温预警",
    0x25: "放电高温预警", 0x26: "欠压预警", 0x27: "过压预警",
    0x28: "充电过流预警", 0x29: "放电低温预警",
}
CHEMISTRY_NAMES = {1: "磷酸铁锂", 2: "锰酸锂", 3: "三元锂", 4: "钴酸锂",
                   5: "聚合锂", 6: "钛酸锂", 7: "铅酸", 8: "镍氢", 9: "钠"}


@dataclass(frozen=True)
class OwcSnapshot:
    enabled: bool
    checksum_ok: bool
    age_ms: int
    good: int
    checksum_errors: int
    timing_errors: int
    overflows: int
    sequence: int
    raw: bytes
    session: int
    snapshot_version: int = 2
    mode: int = 1
    protocol: int = 1
    frame_protocol: int = 1
    mismatch_protocol: int = 0

    @property
    def fresh(self) -> bool:
        if not self.raw or not self.enabled or not self.checksum_ok:
            return False
        return self.age_ms <= (75000 if self.raw[0] in (0x3B, 0x3C) else 5000)


def decode_snapshot(payload: bytes, requested_id: int = 0, extended: bool = False) -> OwcSnapshot:
    header_size = 36 if extended else SNAPSHOT_HEADER.size
    if len(payload) < header_size:
        raise ValueError("OWC snapshot header truncated")
    version, enabled, available, valid, age, good, bad, timing, lost, seq, length, session = SNAPSHOT_HEADER.unpack_from(payload)
    if version not in ((3,) if extended else (1, 2)):
        raise ValueError(f"Unsupported OWC snapshot version: {version}")
    # v1 与 v2 的头长及 raw 布局相同；v1 的末两字节是保留值，不是会话号。
    if version == 1 and session:
        raise ValueError("OWC v1 reserved field must be zero")
    if enabled > 1 or available > 1 or valid > 1 or requested_id not in (OWC_IDS + (LISHEN_SELECTOR,) if extended else OWC_IDS):
        raise ValueError("OWC snapshot header invalid")
    mode, protocol, frame_protocol, mismatch = payload[32:36] if extended else (1, 1, 1, 0)
    if any(v > 2 for v in (mode, protocol, frame_protocol, mismatch)):
        raise ValueError("OWC protocol marker invalid")
    if mode and protocol and mode != protocol:
        raise ValueError("OWC mode/protocol mismatch")
    raw = payload[header_size:]
    if length != len(raw) or length > 259 or bool(available) != bool(length):
        raise ValueError("OWC snapshot length invalid")
    if raw:
        _validate_shape(raw, frame_protocol)
        if frame_protocol == 2:
            if requested_id not in (0, LISHEN_SELECTOR):
                raise ValueError("Lishen selector mismatch")
        elif requested_id == LISHEN_SELECTOR or (requested_id and raw[0] != requested_id):
            raise ValueError("OWC packet ID invalid")
        if extended and valid and protocol != frame_protocol:
            raise ValueError("OWC cross-protocol snapshot")
        if version >= 2 and valid and not session:
            raise ValueError("OWC packet has no session")
        if bool(valid) != ((sum(raw[:-1]) & 0xFF) == raw[-1]):
            raise ValueError("OWC checksum flag inconsistent")
        if requested_id and not valid:
            raise ValueError("OWC valid cache contains bad checksum")
    elif valid or (extended and frame_protocol):
        raise ValueError("OWC empty snapshot marker invalid")
    return OwcSnapshot(bool(enabled), bool(valid), age, good, bad, timing, lost, seq, raw, session, version, mode, protocol, frame_protocol, mismatch)


def _validate_shape(raw: bytes, protocol: int = 1) -> None:
    if protocol == 2:
        if len(raw) != 6 or raw[0] or raw[1] > 100 or raw[4]:
            raise ValueError("Lishen V1.1.1 packet invalid")
        return
    if protocol != 1:
        raise ValueError("Unknown OWC wire protocol")
    if len(raw) < 2 or raw[0] not in OWC_IDS[1:]:
        raise ValueError("OWC packet ID invalid")
    if (raw[1] & 0x0F) == 0x0F or (raw[1] >> 4) == 0x0F:
        raise ValueError("OWC packet version invalid")
    expected = 20 if raw[0] == 1 else (raw[2] + 4 if len(raw) >= 3 else 0)
    if len(raw) != expected:
        raise ValueError("OWC packet length invalid")
    if raw[0] == 0x3A and not 27 <= raw[2] <= 29:
        raise ValueError("OWC realtime encryption field length invalid")
    if raw[0] == 0x3B and (not raw[2] or raw[2] % 2):
        raise ValueError("OWC cell pairs invalid")
    if raw[0] == 0x3C and not raw[2]:
        raise ValueError("OWC battery code empty")


def decode_fields(raw: bytes, protocol: int = 1) -> list[tuple[str, str, str]]:
    """Only checksum-valid data is eligible for physical-value decoding."""
    if not raw or (sum(raw[:-1]) & 0xFF) != raw[-1]:
        return []
    _validate_shape(raw, protocol)
    if protocol == 2:
        temperature = raw[3] if raw[3] < 128 else raw[3] - 256
        return [("Protocol", "Lishen V1.1.1", ""), ("SOC", str(raw[1]), "%"),
                ("Voltage", str(raw[2]), "V"), ("Battery temperature", str(temperature), "℃")]
    rows = [("ID", f"0x{raw[0]:02X}", ""), ("Protocol", f"0x{raw[1]:02X}", "")]

    def u16(offset: int) -> int:
        return struct.unpack_from("<H", raw, offset)[0]

    def scaled(name: str, value: int, scale: float, unit: str, maximum: int,
               invalid: int = 0xFFFF, bias: float = 0) -> None:
        rows.append((name, "--" if value == invalid or value > maximum else f"{value * scale + bias:g}", unit))

    if raw[0] in (1, 0x3A):
        start = 9 if raw[0] == 1 else 3
        scaled("SOC", raw[start], 0.5, "%", 200, 0xFF)
        scaled("Voltage", u16(start + 1), 0.1, "V", 10000)
        scaled("Current", u16(start + 3), 0.1, "A", 10000, bias=-500)
        for name, offset in (("Max temperature", 5), ("Min temperature", 6), ("MOS temperature", 7)):
            value = raw[start + offset]
            display = "--" if value > 190 else str(value - 40)
            rows.append((name, display, "C"))
        work = raw[start + 9]
        work_valid = work != 0xFF and (work <= 2 if raw[0] == 1 else not (work & 0x18))
        rows += [("Fault code", f"0x{raw[start+8]:02X}", ""),
                 ("Fault", FAULT_NAMES.get(raw[start+8], "未定义/保留"), ""),
                 ("Work status (raw)", f"0x{work:02X}", ""),
                 ("Work status", {0: "放电", 1: "充电", 2: "回馈"}.get(work & 7, "--") if work_valid else "--", "")]
        if raw[0] == 1:
            rows += [("Manufacturer", "--" if raw[2] == 0xFF else f"0x{raw[2]:02X}", ""),
                     ("Battery model", str(raw[3]) if raw[3] <= 15 else "--", ""),
                     ("Chemistry code", f"0x{raw[4]:02X}", ""),
                     ("Chemistry", CHEMISTRY_NAMES.get(raw[4], "未定义/保留"), "")]
            scaled("Rated voltage", u16(5), 0.1, "V", 10000)
            scaled("Rated capacity", u16(7), 0.1, "Ah", 10000)
        else:
            # 本地 BMS key=0 明确未实现认证；状态字需要解码，不能按普通位图解释。
            rows += [("Response requested", {0: "不需要应答", 1: "充电器", 2: "ECU", 3: "TBOX"}.get(work >> 5, "--") if work_valid else "--", ""),
                     ("BMS status (raw)", "--" if raw[27] == 0 else f"0x{raw[13]:02X}", ""),
                     ("Encryption / charging policy / mutual recognition", "未实现", "")]
            scaled("Cycles", u16(14), 1, "", 60000)
            scaled("Max cell", u16(16), 1, "mV", 10000)
            scaled("Min cell", u16(18), 1, "mV", 10000)
            rows += [("Max cell index (raw)", str(raw[20]) if 1 <= raw[20] <= 25 else "--", ""),
                     ("Min cell index (raw)", str(raw[21]) if 1 <= raw[21] <= 25 else "--", ""),
                     ("Charge status (raw)", "--" if raw[26] == 0xFF else f"0x{raw[26]:02X}", ""),
                     ("Charge status", {0: "保留", 1: "满充停止", 2: "非法充电", 3: "电池保护", 4: "正在充电"}.get(raw[26], "--"), "")]
            # 8 bit / 0.1 A 的表示上限为 25.4 A；不把字段误当成 1 A 精度。
            scaled("Allowed feedback current", raw[22], 0.1, "A", 254, 0xFF)
            scaled("Requested charge voltage", u16(23), 0.1, "V", 10000)
            scaled("Requested charge current", raw[25], 0.1, "A", 254, 0xFF)
    elif raw[0] == 0x3B:
        if raw[2] == 0 or raw[2] % 2:
            raise ValueError("OWC cell pairs invalid")
        for i in range(raw[2] // 2):
            value = u16(3 + i * 2)
            rows.append((f"Cell {i+1}", "--" if value > 60000 else str(value), "mV"))
    elif raw[0] == 0x3C:
        code = raw[3:-1]
        # Config SN 是可打印 ASCII；0xFF、空白或非法字符不能显示成真实编码。
        valid = bool(code) and all(0x20 <= value <= 0x7E for value in code) and any(value != 0x20 for value in code)
        rows.append(("Battery code", code.decode("ascii") if valid else "--", ""))
    return rows


def link_online(snapshots: dict[int, OwcSnapshot]) -> bool:
    return any(s.enabled and s.checksum_ok and s.raw and s.age_ms <= 5000 and
               s.protocol and s.protocol == s.frame_protocol and not s.mismatch_protocol
               for s in snapshots.values())


def visible_fields(snapshot: OwcSnapshot, snapshots: dict[int, OwcSnapshot]) -> list[tuple[str, str, str]]:
    if not snapshot.enabled or not snapshot.checksum_ok or not snapshot.raw or snapshot.mismatch_protocol:
        return []
    if snapshot.frame_protocol == 2:
        return decode_fields(snapshot.raw, 2) if link_online(snapshots) and snapshot.fresh else []
    if snapshot.raw[0] == 1:
        # 接入静态资料不随 75 s 消失；电压/电流等只从实时帧显示。
        return [row for row in decode_fields(snapshot.raw) if row[0] in PUBLIC_STATIC_FIELDS]
    if not link_online(snapshots) or not snapshot.fresh:
        return []
    if snapshot.raw[0] == 0x3B:
        realtime = snapshots.get(0x3A)
        # AFE 失效已由实时帧报告时，不等下一次60s单体帧才隐藏旧单体。
        if realtime and realtime.raw:
            if not realtime.fresh or struct.unpack_from("<H", realtime.raw, 4)[0] > 10000:
                return []
    return decode_fields(snapshot.raw)


def read_snapshots(client, extended: bool = False) -> dict[int, OwcSnapshot]:
    # 五次查询可能跨越接入边界。会话或 enable 不一致时重读，不能拼接两块电池。
    for _ in range(2):
        snapshots = {packet_id: decode_snapshot(client.command(CMD_OWC_FRAME if extended else CMD_OWC_SNAPSHOT,
                                  bytes([packet_id])).payload, packet_id, extended)
                     for packet_id in ((1, 0x3A, 0x3B, 0x3C, LISHEN_SELECTOR, 0) if extended else (1, 0x3A, 0x3B, 0x3C, 0))}
        if len({(s.snapshot_version, s.session, s.enabled, s.mode, s.protocol) for s in snapshots.values()}) == 1:
            return snapshots
    raise ValueError("OWC session changed during snapshot read; restart monitoring")


def control_receiver(client, enabled: bool, mode: int, extended: bool):
    client.command(CMD_OWC_CONFIG if extended else CMD_OWC_CONTROL,
                   bytes([int(enabled), mode]) if extended else bytes([int(enabled)]))


def start_receiver(client, mode: int = 0) -> tuple[bool, int]:
    if mode not in (0, 1, 2):
        raise ValueError("OWC mode invalid")
    try:
        p = client.command(CMD_OWC_INFO).payload
    except CommToolCommandError as exc:
        if exc.status != 2:
            raise
        if mode == 2:
            raise ValueError("此Comm Tool仅支持A1.3.3；锂神接收需要升级Comm Tool App")
        control_receiver(client, True, 1, False)
        return False, 1
    if len(p) != 8 or p[0] != 1 or p[1] & ~7 or p[2:4] != bytes([3, 0]) or struct.unpack_from("<HH", p, 4) != (36, 259):
        raise ValueError("OWC capability response invalid")
    if not p[1] & (1 << mode):
        raise ValueError("Comm Tool接收模式不可用，请核对PC10/11配置或升级App")
    control_receiver(client, True, mode, True)
    return True, mode
