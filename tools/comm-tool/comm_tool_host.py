#!/usr/bin/env python3
"""PC host tool for the UART-to-CAN BMS comm tool."""

from __future__ import annotations

import argparse
import math
import struct
import sys
import time
import zlib
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Optional


MAGIC = 0xAA55
VERSION = 1
FLAG_ACK = 0x01
MAX_PAYLOAD = 512
FW_DATA_MAX_CHUNK = MAX_PAYLOAD - 4
FW_DATA_DEFAULT_CHUNK = 496

BMS_APP_BASE_ADDR = 0x08004800
COMM_TOOL_APP_BASE_ADDR = 0x08008000
APP_BASE_ADDR = BMS_APP_BASE_ADDR
IAP_BASE_ADDR = 0x08000000
BMS_APP_FLASH_LIMIT = 0x0801F800
COMM_TOOL_APP_FLASH_LIMIT = 0x08018000
SRAM_BASE = 0x20000000
SRAM_LIMIT = 0x20010000

CMD_GET_INFO = 0x01
CMD_SET_CAN = 0x02
CMD_BMS_READ = 0x10
CMD_BMS_WRITE = 0x11
CMD_BMS_AGING_CTRL = 0x12
CMD_BMS_AGING_STATUS = 0x13
CMD_BMS_AGING_SET_HOURS = 0x14
CMD_SET_BMS_UART = 0x15
CMD_FW_BEGIN = 0x20
CMD_FW_DATA = 0x21
CMD_FW_END = 0x22
CMD_FW_INFO = 0x23
CMD_ENTER_IAP = 0x30
CMD_UPGRADE = 0x31
CMD_UPGRADE_STATUS = 0x32
CMD_UPGRADE_ABORT = 0x33
CMD_RAW_CAN_TX = 0x40
CMD_CAN_DIAG = 0x41
CMD_DEBUG_LOG = 0x42

APP_SET_ONCE_SOC_ADDR = 0x1005
APP_AGING_ACTION_START = 0x51
APP_AGING_ACTION_STOP = 0x50
APP_AGING_ACTION_RESET_TIME = 0x5A

STATUS_TEXT = {
    0x00: "OK",
    0x01: "CRC_ERROR",
    0x02: "UNSUPPORTED",
    0x03: "BAD_PARAM",
    0x04: "BAD_STATE",
    0x05: "FLASH_ERROR",
    0x06: "CAN_TIMEOUT",
    0x07: "BMS_ERROR(板端拒绝/地址无效/参数越界/写权限关闭)",
}

DEBUG_LOG_MODULES = {
    1: "APP",
    2: "UART",
    3: "CAN",
    4: "FLASH",
    5: "UPGRADE",
    6: "PROTOCOL",
}

DEBUG_LOG_EVENTS = {
    1: "BOOT",
    2: "CMD_RX",
    3: "CMD_TX",
    4: "BAD_FRAME",
    5: "CAN_SET",
    6: "CAN_TX_FAIL",
    7: "CAN_TX_TIMEOUT",
    8: "FW_BEGIN",
    9: "FW_END",
    10: "UPGRADE_START",
    11: "UPGRADE_PHASE",
    12: "UPGRADE_ERROR",
    13: "UPGRADE_ABORT",
}

HEADER_STRUCT = struct.Struct("<HBBHBBH")
HEADER_SIZE = HEADER_STRUCT.size


@dataclass
class Frame:
    version: int
    flags: int
    seq: int
    cmd: int
    status: int
    payload: bytes


def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for value in data:
        crc ^= value
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc & 0xFFFF


def require_pyserial():
    try:
        import serial  # type: ignore
    except ImportError as exc:
        raise SystemExit(
            "当前 Python 环境缺少 pyserial。\n"
            "请执行: py -3.9 -m pip install pyserial\n"
            f"当前解释器: {sys.executable}"
        ) from exc
    return serial


def encode_frame(seq: int, cmd: int, payload: bytes = b"", *, status: int = 0, flags: int = 0) -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ValueError(f"payload too large: {len(payload)} > {MAX_PAYLOAD}")
    header = HEADER_STRUCT.pack(MAGIC, VERSION, flags & 0xFF, seq & 0xFFFF, cmd & 0xFF, status & 0xFF, len(payload))
    body = header + payload
    return body + struct.pack("<H", crc16_modbus(body))


def read_frame(ser, timeout: float) -> Frame:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        first = ser.read(1)
        if first != b"\x55":
            continue
        second = ser.read(1)
        if second != b"\xAA":
            continue
        rest = ser.read(HEADER_SIZE - 2)
        if len(rest) != HEADER_SIZE - 2:
            break
        header = b"\x55\xAA" + rest
        magic, version, flags, seq, cmd, status, length = HEADER_STRUCT.unpack(header)
        if magic != MAGIC:
            continue
        if version != VERSION:
            raise RuntimeError(f"协议版本不匹配: device={version} host={VERSION}")
        if length > MAX_PAYLOAD:
            raise RuntimeError(f"payload 长度异常: {length}")
        payload_crc = ser.read(length + 2)
        if len(payload_crc) != length + 2:
            break
        payload = payload_crc[:length]
        expect_crc = struct.unpack("<H", payload_crc[length:])[0]
        actual_crc = crc16_modbus(header + payload)
        if expect_crc != actual_crc:
            raise RuntimeError(f"响应 CRC 错误: expect=0x{expect_crc:04X} actual=0x{actual_crc:04X}")
        return Frame(version, flags, seq, cmd, status, payload)
    raise TimeoutError("等待 comm tool 响应超时")


class CommToolCommandError(RuntimeError):
    def __init__(self, status: int):
        self.status = status
        super().__init__(f"comm tool 返回错误: {STATUS_TEXT.get(status, f'0x{status:02X}')}")


class CommToolClient:
    def __init__(self, port: str, baud: int, timeout: float):
        serial = require_pyserial()
        self._ser = serial.Serial(port=port, baudrate=baud, timeout=0.05, write_timeout=timeout)
        self._timeout = timeout
        self._seq = 0

    def close(self) -> None:
        self._ser.close()

    def command(self, cmd: int, payload: bytes = b"", timeout: Optional[float] = None) -> Frame:
        self._seq = (self._seq + 1) & 0xFFFF
        if self._seq == 0:
            self._seq = 1
        frame = encode_frame(self._seq, cmd, payload)
        self._ser.write(frame)
        self._ser.flush()

        deadline = time.monotonic() + (timeout if timeout is not None else self._timeout)
        while time.monotonic() < deadline:
            resp = read_frame(self._ser, max(0.05, deadline - time.monotonic()))
            if resp.seq != self._seq:
                continue
            if resp.cmd != cmd:
                continue
            if (resp.flags & FLAG_ACK) == 0:
                continue
            if resp.status != 0:
                raise CommToolCommandError(resp.status)
            return resp
        raise TimeoutError(f"等待命令 0x{cmd:02X} 响应超时")

    def __enter__(self) -> "CommToolClient":
        return self

    def __exit__(self, _exc_type, _exc, _tb) -> None:
        self.close()


def format_hex(data: bytes) -> str:
    return " ".join(f"{value:02X}" for value in data)


def decode_can_esr(esr: int) -> str:
    lec_text = {
        0: "none",
        1: "stuff",
        2: "form",
        3: "ack",
        4: "bit-recessive",
        5: "bit-dominant",
        6: "crc",
        7: "software",
    }
    lec = (esr >> 4) & 0x07
    tec = (esr >> 16) & 0xFF
    rec = (esr >> 24) & 0xFF
    flags = []
    if esr & 0x01:
        flags.append("EWGF")
    if esr & 0x02:
        flags.append("EPVF")
    if esr & 0x04:
        flags.append("BOFF")
    flag_text = ",".join(flags) if flags else "none"
    return f"TEC={tec} REC={rec} LEC={lec_text.get(lec, str(lec))} flags={flag_text}"


def load_image(path: Path, app_address: int) -> bytes:
    if app_address == IAP_BASE_ADDR:
        raise SystemExit("拒绝 App 地址 0x08000000，该地址是 IAP/Bootloader 起始地址。")
    limit = image_limit(app_address)
    if limit is None:
        raise SystemExit(
            f"App 地址只允许 BMS 0x{BMS_APP_BASE_ADDR:08X} 或 comm tool 0x{COMM_TOOL_APP_BASE_ADDR:08X}，"
            f"实际为 0x{app_address:08X}。"
        )
    if not path.exists():
        raise SystemExit(f"找不到 bin 文件: {path}")
    image = path.read_bytes()
    if len(image) < 8:
        raise SystemExit(f"bin 文件太小，缺少向量表: {path}")
    if app_address + len(image) > limit:
        raise SystemExit(
            f"bin 超出 App 区: end=0x{app_address + len(image):08X}, limit=0x{limit:08X}"
        )
    msp, reset, msp_ok, reset_thumb_ok = vector_summary(image)
    if (not msp_ok) or (not reset_thumb_ok) or (reset < app_address) or (reset >= limit):
        raise SystemExit(
            f"App 向量表非法: MSP=0x{msp:08X}, Reset=0x{reset:08X}, "
            f"App区=0x{app_address:08X}..0x{limit:08X}"
        )
    return image


def image_limit(app_address: int) -> Optional[int]:
    if app_address == BMS_APP_BASE_ADDR:
        return BMS_APP_FLASH_LIMIT
    if app_address == COMM_TOOL_APP_BASE_ADDR:
        return COMM_TOOL_APP_FLASH_LIMIT
    return None


def vector_summary(image: bytes) -> tuple[int, int, bool, bool]:
    if len(image) < 8:
        return 0, 0, False, False
    msp, reset = struct.unpack_from("<II", image, 0)
    msp_ok = SRAM_BASE <= msp < SRAM_LIMIT
    reset_ok = (reset & 1) == 1
    return msp, reset, msp_ok, reset_ok


def print_image_plan(path: Path, image: bytes, app_address: int, chunk_size: int) -> None:
    if chunk_size <= 0 or chunk_size > FW_DATA_MAX_CHUNK:
        raise ValueError(f"chunk-size 必须在 1..{FW_DATA_MAX_CHUNK} 之间")
    crc16 = crc16_modbus(image)
    crc32 = zlib.crc32(image) & 0xFFFFFFFF
    limit = image_limit(app_address)
    msp, reset, msp_ok, reset_thumb_ok = vector_summary(image)
    reset_ok = reset_thumb_ok and (limit is not None) and (app_address <= reset < limit)
    chunks = math.ceil(len(image) / chunk_size)
    print("comm tool 固件下载 dry-run")
    print(f"  bin: {path}")
    print(f"  App 地址: 0x{app_address:08X}")
    print(f"  大小: {len(image)} bytes")
    print(f"  分块: {chunks} x {chunk_size} bytes")
    print(f"  CRC16-Modbus: 0x{crc16:04X}")
    print(f"  CRC32: 0x{crc32:08X}")
    print(f"  初始 MSP: 0x{msp:08X} {'OK' if msp_ok else 'BAD'}")
    print(f"  ResetHandler: 0x{reset:08X} {'OK' if reset_ok else 'BAD'}")
    print("  结果: 仅检查文件和分块，未写入 comm tool。")


def iter_chunks(image: bytes, chunk_size: int) -> Iterable[tuple[int, bytes]]:
    for offset in range(0, len(image), chunk_size):
        yield offset, image[offset : offset + chunk_size]


def open_client(args) -> CommToolClient:
    return CommToolClient(args.port, args.baud, args.timeout)


def cmd_list_ports(_args) -> int:
    require_pyserial()
    from serial.tools import list_ports  # type: ignore

    ports = list(list_ports.comports())
    if not ports:
        print("未发现串口。")
        return 0
    for port in ports:
        print(f"{port.device}\t{port.description}")
    return 0


def cmd_info(args) -> int:
    with open_client(args) as client:
        resp = client.command(CMD_GET_INFO)
    payload = resp.payload
    print(f"GET_INFO raw: {format_hex(payload)}")
    if len(payload) >= 20:
        proto, fw_major, fw_minor, fw_patch = struct.unpack_from("<BBBB", payload, 0)
        bitrate, cache_base, cache_size, flags = struct.unpack_from("<IIII", payload, 4)
        print(f"  协议版本: {proto}")
        print(f"  固件版本: {fw_major}.{fw_minor}.{fw_patch}")
        print(f"  CAN 波特率: {bitrate}")
        print(f"  缓存区: 0x{cache_base:08X} + {cache_size} bytes")
        print(f"  flags: 0x{flags:08X}")
        if len(payload) >= 22:
            print(f"  IAP 节点: {payload[20]}")
            print(f"  BMS App CAN 地址: {payload[21]}")
        if len(payload) >= 28:
            bms_uart_baud = struct.unpack_from("<I", payload, 24)[0]
            print(f"  BMS UART/USART2 波特率: {bms_uart_baud}")
    return 0


def cmd_set_can(args) -> int:
    if args.node_id <= 0 or args.node_id > 0x7F:
        raise SystemExit("node-id 必须在 1..127 之间")
    if args.app_can_addr < 0 or args.app_can_addr > 0x0F:
        raise SystemExit("app-can-addr 必须在 0..15 之间")
    payload = struct.pack("<IBBH", args.can_bitrate, args.node_id & 0xFF, args.app_can_addr & 0x0F, 0)
    with open_client(args) as client:
        client.command(CMD_SET_CAN, payload, timeout=args.long_timeout)
    print(f"CAN 参数已设置: bitrate={args.can_bitrate} node_id={args.node_id} app_can_addr={args.app_can_addr}")
    return 0


def cmd_set_bms_uart(args) -> int:
    if args.bms_baud < 1200 or args.bms_baud > 460800:
        raise SystemExit("--bms-baud 必须在 1200..460800 之间")
    payload = struct.pack("<I", args.bms_baud)
    with open_client(args) as client:
        resp = client.command(CMD_SET_BMS_UART, payload, timeout=args.long_timeout)
    applied = args.bms_baud
    if len(resp.payload) >= 4:
        applied = struct.unpack_from("<I", resp.payload, 0)[0]
    print(f"comm tool USART2/BMS 波特率已设置: {applied}")
    return 0


def cmd_fw_dry_run(args) -> int:
    path = Path(args.bin).resolve()
    image = load_image(path, args.app_address)
    print_image_plan(path, image, args.app_address, args.chunk_size)
    return 0


def cmd_fw_download(args) -> int:
    expected = f"0x{args.app_address:08X}"
    if args.confirm_app_address != expected:
        raise SystemExit(f"真实下载必须显式确认 App 地址: -ConfirmAppAddress {expected}")

    path = Path(args.bin).resolve()
    image = load_image(path, args.app_address)
    print_image_plan(path, image, args.app_address, args.chunk_size)

    crc16 = crc16_modbus(image)
    crc32 = zlib.crc32(image) & 0xFFFFFFFF
    begin_payload = struct.pack("<IIHI", args.app_address, len(image), crc16, crc32)

    with open_client(args) as client:
        start_time = time.monotonic()
        client.command(CMD_FW_BEGIN, begin_payload, timeout=args.long_timeout)
        total = math.ceil(len(image) / args.chunk_size)
        for index, (offset, chunk) in enumerate(iter_chunks(image, args.chunk_size), 1):
            client.command(CMD_FW_DATA, struct.pack("<I", offset) + chunk, timeout=args.long_timeout)
            if index == total or index % args.progress_every == 0:
                print(f"  下载进度: {index}/{total} offset=0x{offset:08X}")
        client.command(CMD_FW_END, struct.pack("<IHI", len(image), crc16, crc32), timeout=args.long_timeout)
    elapsed = max(0.001, time.monotonic() - start_time)
    print(f"固件已写入 comm tool 缓存并完成校验。耗时 {elapsed:.1f}s，速度 {len(image) / 1024.0 / elapsed:.1f} KiB/s")
    return 0


def cmd_fw_info(args) -> int:
    with open_client(args) as client:
        resp = client.command(CMD_FW_INFO)
    payload = resp.payload
    print(f"FW_INFO raw: {format_hex(payload)}")
    if len(payload) >= 15:
        app_addr, size, crc16, crc32, valid = struct.unpack_from("<IIHIB", payload, 0)
        print(f"  App 地址: 0x{app_addr:08X}")
        print(f"  大小: {size} bytes")
        print(f"  CRC16: 0x{crc16:04X}")
        print(f"  CRC32: 0x{crc32:08X}")
        print(f"  有效: {valid}")
    return 0


def cmd_bms_read(args) -> int:
    payload = struct.pack("<HH", args.address, args.count)
    with open_client(args) as client:
        resp = client.command(CMD_BMS_READ, payload, timeout=args.long_timeout)
    if len(resp.payload) % 2 != 0:
        raise RuntimeError("BMS_READ 响应长度不是 2 字节对齐")
    words = list(struct.unpack("<" + "H" * (len(resp.payload) // 2), resp.payload))
    for index, value in enumerate(words):
        print(f"0x{args.address + index:04X}: 0x{value:04X} ({value})")
    return 0


def cmd_bms_write(args) -> int:
    words = [int(value, 0) & 0xFFFF for value in args.values]
    payload = struct.pack("<HH", args.address, len(words))
    payload += struct.pack("<" + "H" * len(words), *words)
    with open_client(args) as client:
        client.command(CMD_BMS_WRITE, payload, timeout=args.long_timeout)
    print(f"已写入 BMS: addr=0x{args.address:04X} words={len(words)}")
    return 0


def cmd_bms_write_soc(args) -> int:
    if args.soc < 0 or args.soc > 100:
        raise SystemExit("soc 必须在 0..100 之间")
    payload = struct.pack("<HHH", APP_SET_ONCE_SOC_ADDR, 1, args.soc)
    with open_client(args) as client:
        client.command(CMD_BMS_WRITE, payload, timeout=args.long_timeout)
    print(f"已写入 BMS SOC={args.soc}%")
    return 0


def aging_state_name(value: int) -> str:
    return {
        0: "停止",
        1: "运行",
        2: "完成",
    }.get(value, f"未知({value})")


def format_remaining_minutes(minutes: int) -> str:
    hours, mins = divmod(minutes, 60)
    if hours:
        return f"{hours}h{mins:02d}min"
    return f"{mins}min"


def cmd_bms_aging(args) -> int:
    action_map = {
        "start": APP_AGING_ACTION_START,
        "stop": APP_AGING_ACTION_STOP,
        "reset-time": APP_AGING_ACTION_RESET_TIME,
    }
    action = action_map[args.action]
    with open_client(args) as client:
        resp = client.command(CMD_BMS_AGING_CTRL, bytes([action]), timeout=args.long_timeout)
    if len(resp.payload) < 2:
        raise RuntimeError("BMS_AGING_CTRL 响应长度不足")
    state = resp.payload[0]
    remaining_hours = resp.payload[1]
    print(f"老化动作完成: action={args.action} state={aging_state_name(state)} remaining≈{remaining_hours}h")
    return 0


def cmd_bms_aging_status(args) -> int:
    with open_client(args) as client:
        resp = client.command(CMD_BMS_AGING_STATUS, timeout=args.long_timeout)
    if len(resp.payload) < 3:
        raise RuntimeError("BMS_AGING_STATUS 响应长度不足")
    state = resp.payload[0]
    remaining_minutes = struct.unpack_from("<H", resp.payload, 1)[0]
    print(
        "老化时间: "
        f"state={aging_state_name(state)} "
        f"remaining={format_remaining_minutes(remaining_minutes)} "
        f"({remaining_minutes}min)"
    )
    return 0


def cmd_bms_aging_set_hours(args) -> int:
    if args.hours < 1 or args.hours > 168:
        raise SystemExit("--hours 必须在 1..168 范围内")
    payload = struct.pack("<H", args.hours)
    with open_client(args) as client:
        resp = client.command(CMD_BMS_AGING_SET_HOURS, payload, timeout=args.long_timeout)
    if len(resp.payload) < 4:
        raise RuntimeError("BMS_AGING_SET_HOURS 响应长度不足")
    state = resp.payload[0]
    remaining_hours = resp.payload[1]
    applied_hours = struct.unpack_from("<H", resp.payload, 2)[0]
    print(
        "老化时长修改完成: "
        f"duration={applied_hours}h "
        f"state={aging_state_name(state)} "
        f"remaining≈{remaining_hours}h"
    )
    return 0


def cmd_enter_iap(args) -> int:
    if not args.confirm_enter_iap:
        raise SystemExit("进入 IAP 会让 BMS App 复位，请添加 -ConfirmEnterIap。")
    with open_client(args) as client:
        client.command(CMD_ENTER_IAP, timeout=args.long_timeout)
    print("BMS 已接受进入 IAP 请求。")
    return 0


def cmd_upgrade(args) -> int:
    if not args.confirm_upgrade:
        raise SystemExit("一键升级会擦写 BMS App，请添加 -ConfirmUpgrade。")
    with open_client(args) as client:
        client.command(CMD_UPGRADE, timeout=args.long_timeout)
        last_percent = -1
        deadline = time.monotonic() + args.long_timeout
        while time.monotonic() < deadline:
            resp = client.command(CMD_UPGRADE_STATUS, timeout=2.0)
            payload = resp.payload
            if len(payload) < 13:
                raise RuntimeError("UPGRADE_STATUS 响应长度不足")
            state, percent, error = struct.unpack_from("<BBB", payload, 0)
            written, total, expect_seq = struct.unpack_from("<IIH", payload, 3)
            if percent != last_percent or state != 1:
                print(
                    f"  升级进度: state={state} percent={percent}% "
                    f"written={written}/{total} expect_seq={expect_seq} error=0x{error:02X}"
                )
                last_percent = percent
            if state == 2 and error == 0:
                print("comm tool 已完成 CAN 升级。")
                return 0
            if state in (3, 4) or error != 0:
                raise RuntimeError(
                    f"升级失败: state={state} percent={percent}% error=0x{error:02X} "
                    f"written={written}/{total} expect_seq={expect_seq}"
                )
            time.sleep(0.25)
    raise TimeoutError("等待升级完成超时")


def cmd_upgrade_status(args) -> int:
    with open_client(args) as client:
        resp = client.command(CMD_UPGRADE_STATUS)
    payload = resp.payload
    print(f"UPGRADE_STATUS raw: {format_hex(payload)}")
    if len(payload) >= 12:
        state, percent, error = struct.unpack_from("<BBB", payload, 0)
        written, total, expect_seq = struct.unpack_from("<IIH", payload, 3)
        print(f"  state={state} percent={percent}% error=0x{error:02X}")
        print(f"  written={written} total={total} expect_seq={expect_seq}")
    return 0


def cmd_can_diag(args) -> int:
    payload = b"\x01" if args.clear else b"\x00"
    with open_client(args) as client:
        resp = client.command(CMD_CAN_DIAG, payload)
    data = resp.payload
    print(f"CAN_DIAG raw: {format_hex(data)}")
    if len(data) >= 62:
        (
            tx_count,
            tx_ok,
            tx_fail,
            tx_timeout,
            rx_count,
            rx_drop,
            last_esr,
            last_tsr,
            last_msr,
            last_rf0r,
            last_tx_id,
            last_rx_id,
        ) = struct.unpack_from("<IIIIIIIIIIII", data, 0)
        last_tx_ide, last_tx_dlc, last_tx_status, last_rx_ide, last_rx_dlc = struct.unpack_from("<BBBBB", data, 48)
        last_rx_data = data[53:61]
        node_id = data[61]
        print(f"  tx: count={tx_count} ok={tx_ok} fail={tx_fail} timeout={tx_timeout}")
        print(f"  rx: count={rx_count} drop={rx_drop}")
        print(f"  regs: ESR=0x{last_esr:08X} TSR=0x{last_tsr:08X} MSR=0x{last_msr:08X} RF0R=0x{last_rf0r:08X}")
        print(f"  esr decoded: {decode_can_esr(last_esr)}")
        print(
            f"  last tx: id=0x{last_tx_id:08X} ide={last_tx_ide} dlc={last_tx_dlc} "
            f"status=0x{last_tx_status:02X}"
        )
        print(
            f"  last rx: id=0x{last_rx_id:08X} ide={last_rx_ide} dlc={last_rx_dlc} "
            f"data={format_hex(last_rx_data)}"
        )
        print(f"  node_id={node_id}")
        if len(data) >= 63:
            print(f"  app_can_addr={data[62]}")
    return 0


def cmd_debug_log(args) -> int:
    payload = bytes([args.count & 0xFF, 1 if args.clear else 0])
    with open_client(args) as client:
        resp = client.command(CMD_DEBUG_LOG, payload)
    data = resp.payload
    print(f"DEBUG_LOG raw: {format_hex(data)}")
    if len(data) < 6:
        raise RuntimeError("DEBUG_LOG response too short")

    enabled = data[0]
    count = data[1]
    capacity = data[2]
    entry_size = data[3]
    dropped = struct.unpack_from("<H", data, 4)[0]
    print(f"  enabled={enabled} count={count} capacity={capacity} entry_size={entry_size} dropped={dropped}")
    if enabled == 0:
        print("  debug log is disabled in this firmware build")
        return 0
    if entry_size != 12:
        raise RuntimeError(f"unsupported DEBUG_LOG entry size: {entry_size}")

    offset = 6
    for _ in range(count):
        if offset + entry_size > len(data):
            break
        seq = struct.unpack_from("<H", data, offset)[0]
        tick_ms = struct.unpack_from("<I", data, offset + 2)[0]
        module = data[offset + 6]
        event = data[offset + 7]
        value0 = struct.unpack_from("<H", data, offset + 8)[0]
        value1 = struct.unpack_from("<H", data, offset + 10)[0]
        module_text = DEBUG_LOG_MODULES.get(module, f"MOD_{module}")
        event_text = DEBUG_LOG_EVENTS.get(event, f"EVT_{event}")
        print(
            f"  #{seq:05d} {tick_ms:10d}ms {module_text:<8} {event_text:<16} "
            f"v0=0x{value0:04X} v1=0x{value1:04X}"
        )
        offset += entry_size
    return 0


def cmd_abort(args) -> int:
    with open_client(args) as client:
        client.command(CMD_UPGRADE_ABORT)
    print("已发送升级终止命令。")
    return 0


def add_serial_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--port", required=True, help="comm tool 串口，如 COM4")
    parser.add_argument("--baud", type=int, default=115200, help="串口波特率")
    parser.add_argument("--timeout", type=float, default=1.0, help="普通命令超时秒数")
    parser.add_argument("--long-timeout", type=float, default=5.0, help="Flash/CAN 长命令超时秒数")


def add_fw_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--bin", required=True, help="BMS App 或 comm tool App bin 文件")
    parser.add_argument("--app-address", type=lambda value: int(value, 0), default=BMS_APP_BASE_ADDR)
    parser.add_argument("--chunk-size", type=int, default=FW_DATA_DEFAULT_CHUNK)


def cmd_owc_monitor(args) -> int:
    from comm_tool_owc import CMD_OWC_CONTROL, read_snapshots, link_online, visible_fields
    with CommToolClient(args.port, args.baud, args.timeout) as client:
        client.command(CMD_OWC_CONTROL, b"\x01")
        try:
            while True:
                snapshots = read_snapshots(client)
                last = snapshots[0]
                if not last.enabled:
                    print("OWC receiver disabled; starting a new session")
                    time.sleep(2.0)
                    client.command(CMD_OWC_CONTROL, b"\x01")
                    time.sleep(1)
                    continue
                online = link_online(snapshots)
                print(f"good={last.good} checksum_errors={last.checksum_errors} "
                      f"timing_errors={last.timing_errors} overflows={last.overflows} snapshot=v{last.snapshot_version} session={last.session or '--'} online={online}")
                for packet_id, snapshot in snapshots.items():
                    print(f"ID=0x{packet_id:02X} age_ms={snapshot.age_ms} "
                          f"checksum_ok={snapshot.checksum_ok} fresh={snapshot.fresh} raw={snapshot.raw.hex(' ')}")
                    if packet_id:
                        for name, value, unit in visible_fields(snapshot, snapshots):
                            print(f"  {name}: {value} {unit}")
                time.sleep(1)
        except KeyboardInterrupt:
            return 0
        finally:
            client.command(CMD_OWC_CONTROL, b"\x00")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="PC 串口 comm tool 上位机")
    sub = parser.add_subparsers(dest="command", required=True)

    p_ports = sub.add_parser("list-ports", help="列出串口")
    p_ports.set_defaults(func=cmd_list_ports)

    p_info = sub.add_parser("info", help="读取 comm tool 信息")
    add_serial_args(p_info)
    p_info.set_defaults(func=cmd_info)

    p_owc = sub.add_parser("owc-monitor", help="解析 Comm Tool 一线通报文（Ctrl+C 停止）")
    add_serial_args(p_owc)
    p_owc.set_defaults(func=cmd_owc_monitor)

    p_set_can = sub.add_parser("set-can", help="设置 comm tool CAN 参数和目标 BMS 地址")
    add_serial_args(p_set_can)
    p_set_can.add_argument("--can-bitrate", type=int, default=250000)
    p_set_can.add_argument("--node-id", type=lambda value: int(value, 0), default=1)
    p_set_can.add_argument("--app-can-addr", type=lambda value: int(value, 0), default=0)
    p_set_can.set_defaults(func=cmd_set_can)

    p_set_bms_uart = sub.add_parser("set-bms-uart", help="设置 comm tool USART2/BMS 侧波特率")
    add_serial_args(p_set_bms_uart)
    p_set_bms_uart.add_argument("--bms-baud", type=int, required=True)
    p_set_bms_uart.set_defaults(func=cmd_set_bms_uart)

    p_dry = sub.add_parser("fw-dry-run", help="检查 BMS App bin 和分块")
    add_fw_args(p_dry)
    p_dry.set_defaults(func=cmd_fw_dry_run)

    p_download = sub.add_parser("fw-download", help="下载 BMS App 到 comm tool 缓存")
    add_serial_args(p_download)
    add_fw_args(p_download)
    p_download.add_argument("--confirm-app-address", default="")
    p_download.add_argument("--progress-every", type=int, default=16)
    p_download.set_defaults(func=cmd_fw_download)

    p_fw_info = sub.add_parser("fw-info", help="查询 comm tool 缓存固件")
    add_serial_args(p_fw_info)
    p_fw_info.set_defaults(func=cmd_fw_info)

    p_read = sub.add_parser("bms-read", help="通过 comm tool 读取 BMS 寄存器")
    add_serial_args(p_read)
    p_read.add_argument("--address", type=lambda value: int(value, 0), required=True)
    p_read.add_argument("--count", type=lambda value: int(value, 0), required=True)
    p_read.set_defaults(func=cmd_bms_read)

    p_write = sub.add_parser("bms-write", help="通过 comm tool 写 BMS 寄存器")
    add_serial_args(p_write)
    p_write.add_argument("--address", type=lambda value: int(value, 0), required=True)
    p_write.add_argument("values", nargs="+")
    p_write.set_defaults(func=cmd_bms_write)

    p_write_soc = sub.add_parser("bms-write-soc", help="常用功能：写入一次 BMS SOC")
    add_serial_args(p_write_soc)
    p_write_soc.add_argument("--soc", type=int, required=True)
    p_write_soc.set_defaults(func=cmd_bms_write_soc)

    p_aging = sub.add_parser("bms-aging", help="常用功能：开启/提前结束/重置 BMS 老化模式")
    add_serial_args(p_aging)
    p_aging.add_argument("action", choices=("start", "stop", "reset-time"))
    p_aging.set_defaults(func=cmd_bms_aging)

    p_aging_status = sub.add_parser("bms-aging-status", help="读取 BMS 老化模式剩余时间广播")
    add_serial_args(p_aging_status)
    p_aging_status.set_defaults(func=cmd_bms_aging_status)

    p_aging_set_hours = sub.add_parser("bms-aging-set-hours", help="常用功能：修改 BMS 老化时长，单位小时，并重置老化时间")
    add_serial_args(p_aging_set_hours)
    p_aging_set_hours.add_argument("--hours", type=int, required=True)
    p_aging_set_hours.set_defaults(func=cmd_bms_aging_set_hours)

    p_enter = sub.add_parser("enter-iap", help="让 BMS App 进入 IAP")
    add_serial_args(p_enter)
    p_enter.add_argument("--confirm-enter-iap", action="store_true")
    p_enter.set_defaults(func=cmd_enter_iap)

    p_upgrade = sub.add_parser("upgrade", help="使用 comm tool 缓存固件一键升级 BMS")
    add_serial_args(p_upgrade)
    p_upgrade.add_argument("--confirm-upgrade", action="store_true")
    p_upgrade.set_defaults(func=cmd_upgrade)

    p_status = sub.add_parser("upgrade-status", help="读取升级状态")
    add_serial_args(p_status)
    p_status.set_defaults(func=cmd_upgrade_status)

    p_abort = sub.add_parser("upgrade-abort", help="终止升级")
    add_serial_args(p_abort)
    p_abort.set_defaults(func=cmd_abort)

    p_diag = sub.add_parser("can-diag", help="读取 comm tool CAN 诊断计数")
    add_serial_args(p_diag)
    p_diag.add_argument("--clear", action="store_true", help="读取前先清空诊断计数")
    p_diag.set_defaults(func=cmd_can_diag)

    p_debug_log = sub.add_parser("debug-log", help="read comm tool debug ring log")
    add_serial_args(p_debug_log)
    p_debug_log.add_argument("--count", type=int, default=32, help="max latest records to read")
    p_debug_log.add_argument("--clear", action="store_true", help="clear debug log after reading")
    p_debug_log.set_defaults(func=cmd_debug_log)

    return parser


def main(argv: Optional[list[str]] = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    return int(args.func(args))


if __name__ == "__main__":
    raise SystemExit(main())
