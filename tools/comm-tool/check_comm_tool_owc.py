"""Exercise the production C decoder with synthetic edges; no board or BIN output."""
from __future__ import annotations

import ctypes
import io
import os
import random
import shutil
import struct
import subprocess
import tempfile
from dataclasses import replace
from pathlib import Path

from comm_tool_owc import decode_snapshot, decode_fields, read_snapshots, visible_fields, link_online
from comm_tool_host import encode_frame, crc16_modbus, read_frame

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "firmware/comm_tool_f103ret6/source/app"


def build_library(temp: Path, extra_sources=()) -> Path:
    exports = ("CtOwc_Init", "CtOwc_ResetDecoder", "CtOwc_FeedEdge", "CtOwc_RecordOverflow",
               "CtOwc_Poll", "CtOwc_EncodeSnapshot", "CtProtocol_Init", "CtProtocol_Feed", "CtProtocol_Encode",
               "CtOwc_SetMode", "CtOwc_GetMode", "CtOwc_EncodeSnapshotV3")
    if extra_sources:
        exports += ("ModelReset", "ModelEdge")
    dll = temp / "owc.dll"
    compiler = shutil.which("cl")
    env = dict(os.environ)
    if not compiler:
        vcvars = next(Path(os.environ.get("ProgramFiles", "C:/Program Files")).glob(
            "Microsoft Visual Studio/2022/*/VC/Auxiliary/Build/vcvars64.bat"))
        setup = temp / "msvc-env.cmd"
        setup.write_text(f'@echo off\ncall "{vcvars}" >nul\nif errorlevel 1 exit /b 1\nset\n', encoding="ascii")
        result = subprocess.run(["cmd", "/d", "/c", str(setup)],
                                capture_output=True, text=True, check=True)
        for line in result.stdout.splitlines():
            if "=" in line and not line.startswith("="):
                key, value = line.split("=", 1)
                env[key] = value
        compiler = shutil.which("cl", path=env.get("Path", env.get("PATH")))
    if not compiler:
        raise RuntimeError("MSVC cl.exe unavailable")
    subprocess.run([compiler, "/nologo", "/LD", "/W4", "/WX", "/TC", "/O2", "/utf-8",
                    f"/I{APP}", str(APP / "ct_owc.c"), str(APP / "ct_protocol.c"), str(APP / "ct_crc16.c"), *map(str,extra_sources),
                    f"/Fe{dll}", f"/Fo{temp}\\",
                    "/link", *[f"/EXPORT:{name}" for name in exports]],
                   env=env, cwd=temp, check=True)
    return dll


def main() -> int:
    parent = Path(os.environ["LOCALAPPDATA"]) / "CodexTemp/comm-tool"
    parent.mkdir(parents=True, exist_ok=True)
    # Keep the DLL until process exit, because Windows holds loaded DLL handles.
    temp = Path(tempfile.mkdtemp(prefix="owc-host-", dir=parent))
    lib = ctypes.CDLL(str(build_library(temp)))
    lib.CtOwc_FeedEdge.argtypes = [ctypes.c_uint32, ctypes.c_uint8, ctypes.c_uint32]
    lib.CtOwc_Poll.argtypes = [ctypes.c_uint32]
    lib.CtOwc_EncodeSnapshot.argtypes = [ctypes.c_uint8, ctypes.c_uint8, ctypes.c_uint32,
                                        ctypes.c_void_p, ctypes.c_uint16]
    lib.CtOwc_EncodeSnapshot.restype = ctypes.c_uint16
    clock_us = 0

    def edge(duration, level):
        nonlocal clock_us
        clock_us += duration
        lib.CtOwc_FeedEdge(duration, level, (clock_us // 1000) & 0xFFFFFFFF)

    def snapshot(packet_id=0):
        out = ctypes.create_string_buffer(512)
        n = lib.CtOwc_EncodeSnapshot(packet_id, 1, (clock_us // 1000) & 0xFFFFFFFF, out, len(out))
        return decode_snapshot(out.raw[:n], packet_id)

    def packet(packet_id, data, corrupt=False):
        raw = bytes([packet_id, 0x13]) + (b"" if packet_id == 1 else bytes([len(data)])) + data
        return raw + bytes([((sum(raw) & 255) ^ (1 if corrupt else 0))])

    def send(raw, jitter=0, stop=True, high_ms=51):
        nonlocal clock_us
        edge(100000, 0)
        edge(10000, 1)
        edge(1000, 0)
        for byte in raw:
            for bit in range(8):
                low = 500 if byte & (1 << bit) else 1500
                delta = random.randint(-jitter, jitter)
                edge(low + delta, 1)
                edge(2000 - low - delta, 0)
        if stop:
            edge(5000, 1)
            clock_us += high_ms * 1000
            lib.CtOwc_Poll((clock_us // 1000) & 0xFFFFFFFF)

    random.seed(20261012)
    lib.CtOwc_Init()
    send(packet(0x3C, b"BAD", corrupt=True))
    assert not snapshot().checksum_ok and snapshot().session == 0
    lib.CtOwc_Init()
    public = packet(1, bytes(range(17)))
    realtime_data = bytearray(29)
    realtime_data[0] = 160
    struct.pack_into("<H", realtime_data, 1, 520)
    struct.pack_into("<H", realtime_data, 3, 5100)
    realtime_data[5:8] = bytes([65, 60, 70])
    realtime = packet(0x3A, realtime_data)
    cells = packet(0x3B, struct.pack("<HHH", 3300, 61001, 0xFFFF))
    unique = packet(0x3C, b"BMS-123")
    for raw in (public, realtime, cells, unique):
        send(raw, jitter=100)
        assert snapshot(raw[0]).raw == raw
    assert snapshot().good == 4
    fields = dict((name, value) for name, value, _ in decode_fields(realtime))
    assert fields["SOC"] == "80" and fields["Voltage"] == "52"
    assert fields["Max temperature"] == "25"
    assert fields["Current"] == "10"
    # 标准 BMS 向量：-12.3A 充电、25/20/30℃、容量7.8Ah；无来源字段为协议无效值。
    corrected = bytearray(realtime_data)
    struct.pack_into("<H", corrected, 3, 4877)
    struct.pack_into("<H", corrected, 11, 123)
    struct.pack_into("<HH", corrected, 13, 3400, 3300)
    corrected[17:19] = bytes([16, 1])
    corrected[19:24] = b"\xff" * 5
    corrected[24] = 0  # key=0，未实现认证
    corrected_raw = packet(0x3A, corrected)
    send(corrected_raw)
    corrected_fields = dict((k, v) for k, v, _ in decode_fields(snapshot(0x3A).raw))
    assert corrected_fields["Current"] == "-12.3" and corrected_fields["MOS temperature"] == "30"
    assert corrected_fields["Cycles"] == "123" and corrected_fields["Max cell"] == "3400"
    for name in ("BMS status (raw)", "Charge status (raw)", "Allowed feedback current",
                 "Requested charge voltage", "Requested charge current"):
        assert corrected_fields[name] == "--", name
    corrected_public = bytearray(17)
    corrected_public[:3] = bytes([1, 0xFF, 1])
    struct.pack_into("<HH", corrected_public, 3, 0xFFFF, 78)
    corrected_public[7] = 160
    struct.pack_into("<HH", corrected_public, 8, 520, 4877)
    corrected_public[12:15] = bytes([65, 60, 70])
    corrected_public[16] = 1
    corrected_public_raw = packet(1, corrected_public)
    send(corrected_public_raw)
    public_fields = dict((k, v) for k, v, _ in decode_fields(snapshot(1).raw))
    assert public_fields["Rated capacity"] == "7.8" and public_fields["MOS temperature"] == "30"
    assert public_fields["Rated voltage"] == "--" and public_fields["Battery model"] == "--"
    assert not snapshot(0x3B).raw and not snapshot(0x3C).raw  # 新接入公有帧清除旧身份/单体。
    session = snapshot(1).session
    send(corrected_public_raw)
    assert snapshot(1).session == session  # 连续三帧公有报文属于同一接入。
    # 未知温度不能显示成 -40℃或有符号 int8 回绕。
    corrected[5:8] = b"\xff" * 3
    invalid_fields = dict((k, v) for k, v, _ in decode_fields(packet(0x3A, corrected)))
    assert all(invalid_fields[name] == "--" for name in ("Max temperature", "Min temperature", "MOS temperature"))
    assert [v for k, v, _ in decode_fields(cells) if k.startswith("Cell")] == ["3300", "--", "--"]
    # 电池编码沿用可打印 Config SN；无效字节不能渲染成替换字符或残留旧 SN。
    assert dict((k, v) for k, v, _ in decode_fields(unique))["Battery code"] == "BMS-123"
    for code, expected in ((b"A", "A"), (b"X" * 32, "X" * 32),
                           (b"\xff", "--"), (b" " * 32, "--"),
                           (b"A\xff", "--"), (b"A\x00B", "--"), (b"\x7f", "--")):
        raw = packet(0x3C, code)
        send(raw)
        assert snapshot(0x3C).raw == raw
        assert dict((k, v) for k, v, _ in decode_fields(snapshot(0x3C).raw))["Battery code"] == expected
    code_before_bad = snapshot(0x3C).raw
    session_before_bad = snapshot(0x3C).session
    send(packet(0x3C, b"BAD", corrupt=True))
    assert snapshot(0x3C).raw == code_before_bad and not snapshot().checksum_ok
    assert snapshot().session == session_before_bad
    checksum_errors = snapshot().checksum_errors

    send(cells)
    bad = packet(0x3B, b"\x01\x02", corrupt=True)
    send(bad)
    assert snapshot().checksum_errors == checksum_errors + 1 and not snapshot().checksum_ok
    assert snapshot(0x3B).raw == cells and decode_fields(bad) == []
    bad_sum = bytearray(cells); bad_sum[-1] = 0xAA
    send(bad_sum)
    assert snapshot().checksum_errors == checksum_errors + 2
    before = snapshot().good
    send(public, high_ms=49)
    assert snapshot().good == before  # 缺少完整的结束高电平，不发布。
    edge(0, 0)
    send(public)
    assert snapshot().good == before + 1
    before = snapshot().good
    send(public[:-1])
    assert snapshot().good == before
    send(public, stop=False)
    clock_us += 30000
    lib.CtOwc_Poll((clock_us // 1000) & 0xFFFFFFFF)
    assert snapshot().good == before
    lib.CtOwc_RecordOverflow()
    assert snapshot().overflows == 1
    for i in range(500):
        edge(random.randint(1, 30000), i & 1)
    send(realtime)
    assert snapshot(0x3A).raw == realtime
    for raw in (packet(0x3B, b"\x01"), packet(0x3A, b"\x00" * 26),
                packet(0x3A, b"\x00" * 30), packet(0x3C, b"")):
        before = snapshot().good
        send(raw)
        assert snapshot().good == before
    # 最大 len、超长报文、16 bit 定时器回绕和 32 bit 毫秒回绕。
    send(packet(0x3C, b"A" * 255))
    assert len(snapshot(0x3C).raw) == 259
    before = snapshot().good
    send(public + b"\x00")
    assert snapshot().good == before
    clock_us = (0xFFFFFFFF - 150) * 1000
    lib.CtOwc_ResetDecoder()
    send(public)
    assert snapshot(1).raw == public and snapshot(1).age_ms == 51
    assert snapshot(1).fresh
    clock_us += 80000000
    assert not snapshot(1).fresh
    # PC 外层帧仍为原 AA55 + seq/cmd + Modbus CRC16。
    encoded = encode_frame(7, 0x44, b"\x3a")
    assert encoded[:2] == b"\x55\xaa" and crc16_modbus(encoded[:-2]) == int.from_bytes(encoded[-2:], "little")
    parser = ctypes.create_string_buffer(4096)
    frame = ctypes.create_string_buffer(1024)
    lib.CtProtocol_Init.argtypes = [ctypes.c_void_p]
    lib.CtProtocol_Feed.argtypes = [ctypes.c_void_p, ctypes.c_uint8, ctypes.c_void_p]
    lib.CtProtocol_Feed.restype = ctypes.c_uint8
    lib.CtProtocol_Init(parser)
    complete = [lib.CtProtocol_Feed(parser, byte, frame) for byte in encoded]
    assert complete == [0] * (len(encoded) - 1) + [1]
    assert frame.raw[6:9] == b"\x01\x00\x3a"  # CtFrame.length + payload[0]
    snap = ctypes.create_string_buffer(512)
    n = lib.CtOwc_EncodeSnapshot(1, 1, (clock_us // 1000) & 0xFFFFFFFF, snap, 512)
    wire = ctypes.create_string_buffer(524)
    lib.CtProtocol_Encode.argtypes = [ctypes.c_uint16, ctypes.c_uint8, ctypes.c_uint8, ctypes.c_void_p,
                                     ctypes.c_uint16, ctypes.c_uint8, ctypes.c_void_p, ctypes.c_uint16]
    lib.CtProtocol_Encode.restype = ctypes.c_uint16
    wire_length = lib.CtProtocol_Encode(7, 0x44, 0, snap, n, 1, wire, 524)
    response = read_frame(io.BytesIO(wire.raw[:wire_length]), 1.0)
    assert response.seq == 7 and response.cmd == 0x44 and response.flags == 1
    assert decode_snapshot(response.payload, 1).raw == public
    # 失联后即使只收到实时帧，也不能继续展示前一会话的 SN 和单体。
    send(unique)
    previous_session = snapshot().session
    clock_us += 6000000
    send(realtime)
    assert snapshot().session != previous_session
    assert not snapshot(0x3C).raw and not snapshot(0x3B).raw
    # 跨五次查询的换设备：第一次读到旧会话，生产 read_snapshots 必须重读整组。
    class SwitchingClient:
        calls = 0

        def command(self, _command, payload):
            self.calls += 1
            if self.calls == 2:
                send(corrected_public_raw)
            out = ctypes.create_string_buffer(512)
            n = lib.CtOwc_EncodeSnapshot(payload[0], 1, (clock_us // 1000) & 0xFFFFFFFF, out, 512)
            return type("Response", (), {"payload": out.raw[:n]})()

    client = SwitchingClient()
    coherent = read_snapshots(client)
    assert client.calls == 10 and len({s.session for s in coherent.values()}) == 1
    # 即使 SUM 正确，超协议范围的物理值也不能显示为有效测量。
    out_of_range = bytearray(corrected)
    out_of_range[0] = 201
    struct.pack_into("<H", out_of_range, 3, 10001)
    out_of_range[5:8] = bytes([191, 254, 255])
    struct.pack_into("<H", out_of_range, 11, 60001)
    out_of_range[17:19] = bytes([0, 26])
    invalid = dict((k, v) for k, v, _ in decode_fields(packet(0x3A, out_of_range)))
    assert all(invalid[k] == "--" for k in ("SOC", "Current", "Max temperature", "Min temperature",
                                           "MOS temperature", "Cycles", "Max cell index (raw)", "Min cell index (raw)"))
    status = bytearray(corrected)
    status[9] = 0x21  # 充电 + 请求充电器应答。
    fields = dict((k, v) for k, v, _ in decode_fields(packet(0x3A, status)))
    assert fields["Work status"] == "充电" and fields["Response requested"] == "充电器"
    assert fields["Encryption / charging policy / mutual recognition"] == "未实现"
    # 实际 Tk 窗口烟雾验证：有效数据显示，过期与错误后只保留状态行。
    import comm_tool_upgrade_ui as ui
    app = ui.UpgradeUi("", 19200, ui.DEFAULT_BIN)
    app.withdraw()
    window = ui.OwcMonitorWindow(app, "TEST-NO-SERIAL")
    window.withdraw()
    window.snapshots = {1: replace(snapshot(1), age_ms=51, enabled=True)}
    window._render()
    assert len(window.tree.get_children()) > 5
    for code, expected in ((b"BMS-123", "BMS-123"), (b"\xff", "--")):
        raw = packet(0x3C, code)
        send(raw)
        window.snapshots = {0x3C: snapshot(0x3C)}
        window._render()
        assert any(window.tree.item(item, "values")[2] == expected for item in window.tree.get_children())
    window.snapshots = {0x3C: replace(snapshot(0x3C), age_ms=80000)}
    window._render()
    assert len(window.tree.get_children()) == 1
    window.snapshots = {1: replace(snapshot(1), age_ms=80000, enabled=True)}
    window._render()
    assert len(window.tree.get_children()) > 5  # 接入静态资料保留。
    assert not any(window.tree.item(item, "values")[1] == "总电压" for item in window.tree.get_children())
    # 长周期帧本身未过期，但实时链路已离线时不显示其旧测量。
    send(cells)
    aged = replace(snapshot(0x3B), age_ms=10000)
    assert aged.fresh and not link_online({0x3B: aged}) and not visible_fields(aged, {0x3B: aged})
    send(realtime)
    available = {0x3B: snapshot(0x3B), 0x3A: snapshot(0x3A)}
    assert visible_fields(available[0x3B], available)
    unavailable = bytearray(realtime_data)
    struct.pack_into("<H", unavailable, 1, 0xFFFF)
    send(packet(0x3A, unavailable))
    available[0x3A] = snapshot(0x3A)
    assert link_online(available) and not visible_fields(available[0x3B], available)
    assert not snapshot(0x3B).raw
    send(realtime)
    assert not snapshot(0x3B).raw  # 恢复不能复用失效前的旧单体。
    send(cells)
    assert snapshot(0x3B).raw == cells
    window.messages.put(("recovering", None))
    window._drain()
    assert not window.snapshots and not window.tree.get_children()
    window.messages.put(("error", "simulated link loss"))
    window._drain()
    assert not window.tree.get_children() and not window.snapshots
    app._close_main()
    print("OWC production C decoder + Python host tests OK", temp)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
