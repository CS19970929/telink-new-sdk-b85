#!/usr/bin/env python3
"""User-facing GUI for comm tool BMS CAN upgrade."""

from __future__ import annotations

import argparse
import csv
import math
import queue
import struct
import sys
import threading
import time
import zlib
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Callable, Optional

import tkinter as tk
from tkinter import filedialog, messagebox, simpledialog, ttk

from comm_tool_upgrade_ui_i18n import LANG_EN, LANG_ZH, contains_chinese, install_ui_language, tr
from comm_tool_owc import read_snapshots, link_online, visible_fields, start_receiver, control_receiver

from comm_tool_host import (
    BMS_APP_BASE_ADDR as APP_BASE_ADDR,
    BMS_APP_FLASH_LIMIT as APP_FLASH_LIMIT,
    CMD_BMS_READ,
    CMD_BMS_WRITE,
    CMD_BMS_AGING_CTRL,
    CMD_BMS_AGING_STATUS,
    CMD_BMS_AGING_SET_HOURS,
    CMD_SET_BMS_UART,
    CMD_CAN_DIAG,
    CMD_FW_BEGIN,
    CMD_FW_DATA,
    CMD_FW_END,
    CMD_FW_INFO,
    CMD_ENTER_IAP,
    CMD_GET_INFO,
    CMD_SET_CAN,
    CMD_UPGRADE,
    CMD_UPGRADE_STATUS,
    CommToolClient,
    APP_SET_ONCE_SOC_ADDR,
    APP_AGING_ACTION_START,
    APP_AGING_ACTION_STOP,
    APP_AGING_ACTION_RESET_TIME,
    crc16_modbus,
    require_pyserial,
    vector_summary,
)


def resolve_repo_root() -> Path:
    return Path(sys.executable).resolve().parent if getattr(sys, "frozen", False) else Path(__file__).resolve().parents[2]


REPO_ROOT = resolve_repo_root()
# 导入镜像由用户显式选择，不再预选另一项目的BMS固件。
DEFAULT_BIN = Path()
DEFAULT_LOG_DIR = Path.home() / "Documents/CodexOutputs/comm-tool/logs"
DEFAULT_LONG_TIMEOUT = 180.0
DEFAULT_CHUNK_SIZE = 496
POST_UPGRADE_APP_READY_TIMEOUT = 15.0
POST_UPGRADE_APP_READY_INTERVAL = 1.0
LOG_MIN_INTERVAL_SECONDS = 2.0
LOG_MAX_INTERVAL_SECONDS = 3600.0

COMM_MODE_COMM_TOOL = "comm_tool"
COMM_MODE_DIRECT_BMS = "direct_bms"
COMM_MODE_LABELS = {
    COMM_MODE_COMM_TOOL: "comm tool/CAN桥",
    COMM_MODE_DIRECT_BMS: "BMS直连串口",
}
COMM_MODE_FROM_LABEL = {label: mode for mode, label in COMM_MODE_LABELS.items()}
UI_EDITION_CUSTOMER = "customer"
UI_EDITION_INTERNAL = "internal"
CUSTOMER_MAINTENANCE_PASSWORD = "hs456"
COMM_TOOL_PC_BAUD = 115200
BMS_UART_DEFAULT_BAUD = 19200
BMS_UART_BAUD_OPTIONS = ("9600", "19200", "38400", "57600", "115200")
BMS_DIRECT_DEFAULT_BAUD = BMS_UART_DEFAULT_BAUD
BMS_DIRECT_DEFAULT_SLAVE = 1
BMS_DIRECT_IAP_SLAVE = 1
BMS_DIRECT_IAP_BLOCK_SIZE = 1024
BMS_DIRECT_IAP_APP_LIMIT = 0x0801F800
BMS_DIRECT_IAP_ACK_TIMEOUT = 3.0
BMS_DIRECT_IAP_RESET_WAIT = 1.0
BMS_DIRECT_IAP_READY_RETRIES = 5
BMS_DIRECT_IAP_READY_RETRY_DELAY = 0.5

C073_APP_BASE = 0x08003000
C073_PACKAGE_MAGIC = 0x32553044
C073_LEGACY_BOARD = 0xC073000D
C073_GUARDED_BOARD = 0xC073002C
C073_ID_OFFSET = 0x104
C073_PAGE_BYTES = 1024
C073_CACHE_CHUNK_BYTES = 48


@dataclass(frozen=True)
class CanUpgradeImage:
    data: bytes
    address: int
    board: int = 0
    version: int = 0
    capacity: int = 0

    @property
    def crc32(self) -> int:
        return zlib.crc32(self.data)

    def running_identity(self) -> list[int]:
        return [0x5550, 1, self.board >> 16, self.board & 0xFFFF, 2, 2,
                self.address >> 16, self.address & 0xFFFF, 0, self.capacity, 0, 0]


def load_can_upgrade_image(path: Path) -> CanUpgradeImage:
    file_data = path.read_bytes()
    data = file_data
    identity = None
    if path.suffix.lower() == ".c073" or file_data[:4] == struct.pack("<I", C073_PACKAGE_MAGIC):
        if len(file_data) < 32:
            raise ValueError("C073 升级包缺少格式 2 包头")
        magic, version, header, board, address, size, build, crc, header_crc = struct.unpack_from("<IHH6I", file_data)
        data = file_data[32:]
        if ((magic, version, header) != (C073_PACKAGE_MAGIC, 2, 32) or size != len(data)
                or header_crc != zlib.crc32(file_data[:28]) or crc != zlib.crc32(data)
                or board not in (C073_LEGACY_BOARD, C073_GUARDED_BOARD)):
            raise ValueError("C073 升级包头、板号、长度或 CRC32 错误")
        identity = board, address, build
    if len(data) < 8:
        raise ValueError("BIN 太小，缺少向量表")
    stack, entry, stack_ok, thumb = vector_summary(data)
    reset = entry & ~1
    if len(data) >= C073_ID_OFFSET + 16:
        board, address, version, actual = struct.unpack_from("<4I", data, C073_ID_OFFSET)
        if identity is not None or board in (C073_LEGACY_BOARD, C073_GUARDED_BOARD):
            if identity is not None and identity != (board, address, version):
                raise ValueError("C073 包头与 APP 内嵌身份不一致")
            family = version >> 16
            capacity = 0
            if board == C073_GUARDED_BOARD and family == 2:
                capacity = 44 * C073_PAGE_BYTES
            elif board == C073_LEGACY_BOARD:
                capacity = {0: 46, 1: 46, 2: 44}.get(family, 0) * C073_PAGE_BYTES
            if (address != C073_APP_BASE or len(data) > capacity or not thumb
                    or not 0x200000D0 <= stack <= 0x20005000 or stack & 7
                    or not C073_ID_OFFSET + 16 < actual <= len(data)
                    or (actual + C073_PAGE_BYTES - 1) // C073_PAGE_BYTES * C073_PAGE_BYTES != len(data)
                    or not address + C073_ID_OFFSET + 16 <= reset < address + actual):
                raise ValueError("C073 APP 的分区、版本族、向量或页对齐无效")
            return CanUpgradeImage(data, address, board, version, capacity)
    if identity is not None:
        raise ValueError("C073 APP 缺少内嵌身份")
    if APP_BASE_ADDR + len(data) > APP_FLASH_LIMIT or not stack_ok or not thumb or not APP_BASE_ADDR <= reset < APP_BASE_ADDR + len(data):
        raise ValueError(f"旧版 APP 的大小或向量无效，支持区间 0x{APP_BASE_ADDR:08X}–0x{APP_FLASH_LIMIT:08X}")
    return CanUpgradeImage(data, APP_BASE_ADDR)


BMS_OVERVIEW_ADDR = 0xD000
BMS_OVERVIEW_WORDS = 63
BMS_LIVE_WORDS = 88
BMS_LIVE_STATUS_ADDR = 0xD115
BMS_LIVE_STATUS_WORDS = 4
BMS_LIVE_STATUS_RETRY_SECONDS = 30.0
BMS_STATUS_CHARGE_MOS_BIT = 2
BMS_STATUS_DISCHARGE_MOS_BIT = 3
BMS_STATUS_HEAT_BIT = 8
BMS_STATUS_COOL_BIT = 9
BMS_LIVE_TEMP_START_INDEX = 38
BMS_LIVE_TEMP_COUNT = 10
BMS_LIVE_TEMP_MOS_OFFSET = 9
CELL_VOLTAGE_NOT_PRESENT = 61001
BMS_EVENT_RECORD_ADDR = 0xC008
BMS_EVENT_RECORD_WORDS = 100
BMS_EVENT_RECORD_CHUNK_WORDS = 20
BMS_RESET_EVENT_RECORD_ADDR = 0x1007
BMS_RESET_EVENT_RECORD_VALUE = 0x0001
BMS_PRODUCT_INFO_ADDR = 0xC002
BMS_PRODUCT_INFO_FIELD_LEN = 32
BMS_PRODUCT_INFO_WORDS = (BMS_PRODUCT_INFO_FIELD_LEN * 3) // 2
BMS_DIRECT_ENTER_IAP_ADDR = 0xFFFD
BMS_DIRECT_IAP_DATA_ADDR = 0xFFFE
BMS_DIRECT_IAP_COMPLETE_ADDR = 0xFFFF
BMS_DIRECT_AGING_FUNCTION_ID = 7
BMS_DIRECT_FUNCTION_ON_ADDR = 0x1102
BMS_DIRECT_FUNCTION_OFF_ADDR = 0x1103
BMS_DIRECT_AGING_STATUS_ADDR = 0xC080
BMS_DIRECT_AGING_STATUS_WORDS = 5
BMS_DIRECT_AGING_RESET_TIME_ADDR = 0x1008
BMS_DIRECT_AGING_SET_HOURS_ADDR = 0x1009
BMS_DIRECT_AGING_RESET_GUARD = 0x005A
PRODUCT_INFO_REFRESH_SECONDS = 30.0
BMS_READ_RETRY_COUNT = 3
BMS_READ_RETRY_DELAY_SECONDS = 0.3
SH309_AFE_PARAM_ADDR = 0x2400
SH309_AFE_PARAM_WORDS = 24
SH309_TMOS_PARAM_ADDR = 0x2132
SH309_TMOS_PARAM_WORDS = 5
SH309_RESET_PROTECT_ADDR = 0x1002
SH309_RESET_AFE_ADDR = 0x1006
SH309_OV_UV_DELAY_MS = [100, 200, 300, 400, 600, 800, 1000, 2000, 3000, 4000, 6000, 8000, 10000, 20000, 30000, 40000]
SH309_CHG_SECOND_DELAY_MS = [10, 20, 40, 60, 80, 100, 200, 400, 600, 800, 1000, 2000, 4000, 8000, 10000, 20000]
SH309_DSG_SECOND_DELAY_MS = [50, 100, 200, 400, 600, 800, 1000, 2000, 4000, 6000, 8000, 10000, 15000, 20000, 30000, 40000]
SH309_SHORT_DELAY_US = [0, 64, 128, 192, 256, 320, 384, 448, 512, 576, 640, 704, 768, 832, 896, 960]
SH309_AFE_OCD1V_OCCV_MV = [20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 160, 180, 200]
SH309_AFE_SCV_MV = [50, 80, 110, 140, 170, 200, 230, 260, 290, 320, 350, 400, 500, 600, 800, 1000]
SH309_DEFAULT_CS_RES_MOHM = 2
SH309_DEFAULT_CS_RES_NUM = 4
SH309_SYS_CS_RES_KEY = "系统参数/采样电阻"
SH309_SYS_CS_RES_NUM_KEY = "系统参数/采样电阻数"
SH309_SECOND_CURRENT_KEYS = {
    "SH309/二级充电过流(A)",
    "SH309/二级放电过流(A)",
}
SH309_SHORT_CURRENT_KEY = "SH309/短路电流(A)"
SH309_HIGH_TEMP_KEYS = {
    "SH309/充电高温(℃)",
    "SH309/充电高温恢复(℃)",
    "SH309/放电高温(℃)",
    "SH309/放电高温恢复(℃)",
}
SH309_LOW_CHG_TEMP_KEY = "SH309/充电低温(℃)"
SH309_LOW_CHG_TEMP_RCV_KEY = "SH309/充电低温恢复(℃)"
SH309_LOW_DSG_TEMP_KEY = "SH309/放电低温(℃)"
SH309_LOW_DSG_TEMP_RCV_KEY = "SH309/放电低温恢复(℃)"
SH309_TMOS_DEFAULT_DISPLAY = {
    "SH309/MOS过温1(℃)": "75",
    "SH309/MOS过温2(℃)": "85",
    "SH309/MOS过温3(℃)": "95",
    "SH309/MOS恢复(℃)": "80",
    "SH309/延时(10ms)": "100",
}
BMS_EVENT_NAMES = [
    "NA",
    "BMS开机",
    "BMS休眠",
    "均衡开启",
    "保留4",
    "保留5",
    "单节过压保护",
    "总压过压保护",
    "充电过流保护",
    "单节低压保护",
    "总压低压保护",
    "放电过流保护",
    "充电低温保护",
    "放电低温保护",
    "充电高温保护",
    "放电高温保护",
    "压差过大保护",
    "短路保护",
    "AFE1报错",
    "AFE2报错",
    "EEPROM报错",
]

FAULT_BIT_NAMES = [
    "单体过压",
    "单体欠压",
    "总压过压",
    "总压欠压",
    "充电过流",
    "放电过流",
    "充电高温",
    "放电高温",
    "充电低温",
    "放电低温",
    "压差过大",
    "温差过大",
    "SOC低",
    "MOS过温",
    "保留1",
    "保留2",
]


@dataclass(frozen=True)
class BmsParamDef:
    key: str
    group: str
    name: str
    addr: int
    unit: str
    kind: str = "u16"


def _build_bms_param_defs() -> list[BmsParamDef]:
    defs: list[BmsParamDef] = []
    protect_groups = [
        ("单体过压", "mV", "u16"),
        ("单体欠压", "mV", "u16"),
        ("总压过压", "mV", "u16"),
        ("总压欠压", "mV", "u16"),
        ("充电过流", "A", "u16"),
        ("放电过流", "A", "u16"),
        ("充电高温", "℃", "temp"),
        ("充电低温", "℃", "temp"),
        ("放电高温", "℃", "temp"),
        ("放电低温", "℃", "temp"),
        ("MOS高温", "℃", "temp"),
        ("压差保护", "mV", "u16"),
        ("SOC低电", "%", "u16"),
    ]
    fields = [("一级阈值", 0), ("二级阈值", 1), ("三级阈值", 2), ("恢复阈值", 3), ("延时", 4)]
    for group_index, (group, unit, kind) in enumerate(protect_groups):
        base = 0x2100 + group_index * 5
        for field_name, offset in fields:
            defs.append(
                BmsParamDef(
                    key=f"{group}/{field_name}",
                    group=group,
                    name=field_name,
                    addr=base + offset,
                    unit=unit if offset < 4 else "raw",
                    kind=kind if offset < 4 else "u16",
                )
            )

    other_defs = [
        ("均衡参数", "开启电压", 0x2300, "mV", "u16"),
        ("均衡参数", "开启压差", 0x2301, "mV", "u16"),
        ("均衡参数", "关闭压差", 0x2302, "mV", "u16"),
        ("短路参数", "充电短路范围", 0x2308, "A", "x10"),
        ("短路参数", "放电短路范围", 0x2309, "A", "x10"),
        ("短路参数", "短路延时", 0x230A, "ms", "x10"),
        ("短路参数", "短路电流", 0x230B, "A", "x10"),
        ("限流参数", "SOC曲线选择", 0x230C, "raw", "u16"),
        ("限流参数", "永久密码", 0x230D, "raw", "u16"),
        ("限流参数", "限流压差", 0x230E, "mV", "u16"),
        ("限流参数", "限流电流", 0x230F, "A", "x10"),
        ("休眠参数", "正常休眠电压", 0x2310, "mV", "u16"),
        ("休眠参数", "正常休眠时间", 0x2311, "min", "u16"),
        ("休眠参数", "过放休眠电压", 0x2312, "mV", "u16"),
        ("休眠参数", "过放休眠时间", 0x2313, "min", "u16"),
        ("休眠参数", "充电电流过滤", 0x2314, "A", "x10"),
        ("休眠参数", "放电电流过滤", 0x2315, "A", "x10"),
        ("休眠参数", "RTC唤醒时间", 0x2316, "min", "u16"),
        ("休眠参数", "RTC休眠时间", 0x2317, "min", "u16"),
        ("SOC参数", "容量", 0x2318, "Ah", "x10"),
        ("SOC参数", "循环次数", 0x2319, "次", "u16"),
        ("SOC参数", "SOC_100电压", 0x231A, "mV", "u16"),
        ("SOC参数", "SOC_0电压", 0x231B, "mV", "u16"),
        ("系统参数", "电池串数", 0x231C, "串", "u16"),
        ("系统参数", "采样电阻", 0x231D, "mΩ", "u16"),
        ("系统参数", "采样电阻数", 0x231E, "个", "u16"),
        ("系统参数", "预充时间", 0x231F, "s", "u16"),
    ]
    for group, name, addr, unit, kind in other_defs:
        defs.append(BmsParamDef(f"{group}/{name}", group, name, addr, unit, kind))
    return defs


def _build_sh309_param_defs() -> list[BmsParamDef]:
    afe_fields = [
        ("单节过压(mv)", 0, "mV", "u16"),
        ("过压恢复(mv)", 1, "mV", "u16"),
        ("过压延时(ms)", 2, "ms", "ms10"),
        ("单节低压(mv)", 3, "mV", "u16"),
        ("低压恢复(mv)", 4, "mV", "u16"),
        ("低压延时(ms)", 5, "ms", "ms10"),
        ("一级充电过流(A)", 6, "A", "x10"),
        ("一级充电过流延时(ms)", 7, "ms", "ms10"),
        ("二级充电过流(A)", 8, "A", "x10"),
        ("二级充电过流延时(ms)", 9, "ms", "ms10"),
        ("一级放电过流(A)", 10, "A", "x10"),
        ("一级放电过流延时(ms)", 11, "ms", "ms10"),
        ("二级放电过流(A)", 12, "A", "x10"),
        ("二级放电过流延时(ms)", 13, "ms", "ms10"),
        ("充电高温(℃)", 14, "℃", "temp"),
        ("充电高温恢复(℃)", 15, "℃", "temp"),
        ("充电低温(℃)", 16, "℃", "temp"),
        ("充电低温恢复(℃)", 17, "℃", "temp"),
        ("放电高温(℃)", 18, "℃", "temp"),
        ("放电高温恢复(℃)", 19, "℃", "temp"),
        ("放电低温(℃)", 20, "℃", "temp"),
        ("放电低温恢复(℃)", 21, "℃", "temp"),
        ("短路电流(A)", 22, "A", "u16"),
        ("短路延时(us)", 23, "us", "u16"),
    ]
    tmos_fields = [
        ("MOS过温1(℃)", 0, "℃", "temp"),
        ("MOS过温2(℃)", 1, "℃", "temp"),
        ("MOS过温3(℃)", 2, "℃", "temp"),
        ("MOS恢复(℃)", 3, "℃", "temp"),
        ("延时(10ms)", 4, "10ms", "u16"),
    ]
    defs = [
        BmsParamDef(f"SH309/{name}", "SH309", name, SH309_AFE_PARAM_ADDR + offset, unit, kind)
        for name, offset, unit, kind in afe_fields
    ]
    defs.extend(
        BmsParamDef(f"SH309/{name}", "SH309", name, SH309_TMOS_PARAM_ADDR + offset, unit, kind)
        for name, offset, unit, kind in tmos_fields
    )
    return defs


BMS_PARAM_DEFS = _build_bms_param_defs()
SH309_PARAM_DEFS = _build_sh309_param_defs()
SH309_AFE_PARAM_KEYS = {
    param.key for param in SH309_PARAM_DEFS if SH309_AFE_PARAM_ADDR <= param.addr < SH309_AFE_PARAM_ADDR + SH309_AFE_PARAM_WORDS
}
SH309_TMOS_PARAM_KEYS = {
    param.key for param in SH309_PARAM_DEFS if SH309_TMOS_PARAM_ADDR <= param.addr < SH309_TMOS_PARAM_ADDR + SH309_TMOS_PARAM_WORDS
}
ALL_PARAM_DEFS = BMS_PARAM_DEFS + SH309_PARAM_DEFS
BMS_PARAM_BY_KEY = {param.key: param for param in ALL_PARAM_DEFS}
BMS_PARAM_ADDR_TO_KEY = {param.addr: param.key for param in ALL_PARAM_DEFS}
BMS_PARAM_PRESETS = {param.key: param.addr for param in ALL_PARAM_DEFS}

SH309_VOLTAGE_PARAM_ROWS = (
    (
        "单体过压",
        ("SH309/单节过压(mv)", "SH309/过压恢复(mv)", "SH309/过压延时(ms)"),
    ),
    (
        "单体欠压",
        ("SH309/单节低压(mv)", "SH309/低压恢复(mv)", "SH309/低压延时(ms)"),
    ),
)
SH309_CURRENT_PARAM_ROWS = (
    ("充电过流一级", ("SH309/一级充电过流(A)", "SH309/一级充电过流延时(ms)")),
    ("充电过流二级", ("SH309/二级充电过流(A)", "SH309/二级充电过流延时(ms)")),
    ("放电过流一级", ("SH309/一级放电过流(A)", "SH309/一级放电过流延时(ms)")),
    ("放电过流二级", ("SH309/二级放电过流(A)", "SH309/二级放电过流延时(ms)")),
)
SH309_TEMPERATURE_PARAM_ROWS = (
    ("充电高温", ("SH309/充电高温(℃)", "SH309/充电高温恢复(℃)")),
    ("充电低温", ("SH309/充电低温(℃)", "SH309/充电低温恢复(℃)")),
    ("放电高温", ("SH309/放电高温(℃)", "SH309/放电高温恢复(℃)")),
    ("放电低温", ("SH309/放电低温(℃)", "SH309/放电低温恢复(℃)")),
)
SH309_MOS_PARAM_ROWS = (
    (
        "MOS过温",
        (
            "SH309/MOS过温1(℃)",
            "SH309/MOS过温2(℃)",
            "SH309/MOS过温3(℃)",
            "SH309/MOS恢复(℃)",
            "SH309/延时(10ms)",
        ),
    ),
)
SH309_SHORT_PARAM_ROWS = (
    ("AFE短路", ("SH309/短路电流(A)", "SH309/短路延时(us)")),
)

SH309_PARAM_UI_KEY_SEQUENCE = tuple(
    key
    for rows in (
        SH309_VOLTAGE_PARAM_ROWS,
        SH309_CURRENT_PARAM_ROWS,
        SH309_TEMPERATURE_PARAM_ROWS,
        SH309_MOS_PARAM_ROWS,
        SH309_SHORT_PARAM_ROWS,
    )
    for _label, keys in rows
    for key in keys
)
PARAM_UI_KEYS = set(SH309_PARAM_UI_KEY_SEQUENCE)
PARAM_UI_KEYS.update(param.key for param in BMS_PARAM_DEFS if 0x2300 <= param.addr < 0x2400)


class UiEvent:
    def __init__(self, kind: str, payload=None):
        self.kind = kind
        self.payload = payload


def _temp_c(raw: int) -> float:
    return raw / 10.0 - 40.0


def _is_valid_cell_voltage(value: int) -> bool:
    return value != 0 and value != CELL_VOLTAGE_NOT_PRESENT


def _valid_cell_items(cells: list[int]) -> list[tuple[int, int]]:
    return [(index, value) for index, value in enumerate(cells) if _is_valid_cell_voltage(value)]


def _live_temp_words(words: list[int]) -> list[int]:
    return words[BMS_LIVE_TEMP_START_INDEX:BMS_LIVE_TEMP_START_INDEX + BMS_LIVE_TEMP_COUNT]


def _live_temp_label(index: int) -> str:
    return "MOS温度" if index == BMS_LIVE_TEMP_MOS_OFFSET else f"温度{index + 1}"


def _zero_live_words() -> list[int]:
    return [0] * BMS_LIVE_WORDS


def _is_zero_live_snapshot(words: list[int]) -> bool:
    return len(words) >= BMS_OVERVIEW_WORDS and all(value == 0 for value in words[:BMS_OVERVIEW_WORDS])


def _zero_bms_overview_text() -> str:
    return (
        "SOC 0%  SOH 0%  总压 0.00V  充电 0.0A  放电 0.0A\n"
        "单体: max --  min --  压差 --  有效串数 0\n"
        "温度: max 0℃  min 0℃  容量 0.00/0.00Ah  出厂 0.00Ah  循环 0\n"
        "故障字: 0x0000 0x0000 0x0000  均衡: 0x0000 0x0000\n"
        "单体电压(mV): 无有效单体电压"
    )


def _cell_stat_text(valid_cells: list[tuple[int, int]]) -> tuple[str, str, str]:
    if not valid_cells:
        return "--", "--", "--"
    max_index, max_mv = max(valid_cells, key=lambda item: item[1])
    min_index, min_mv = min(valid_cells, key=lambda item: item[1])
    return f"{max_mv}mV({max_index + 1})", f"{min_mv}mV({min_index + 1})", f"{max_mv - min_mv}mV"


def _fault_text(word: int) -> str:
    names = [name for index, name in enumerate(FAULT_BIT_NAMES) if (word & (1 << index)) != 0]
    return "、".join(names) if names else "无"


def _onoff_text(enabled: bool | None) -> str:
    if enabled is None:
        return "--"
    return "on" if enabled else "off"


def _format_number(value: float) -> str:
    if math.isfinite(value) and abs(value - round(value)) < 0.0001:
        return str(int(round(value)))
    return f"{value:.1f}".rstrip("0").rstrip(".")


def _param_display_value(param: BmsParamDef, raw: int) -> str:
    if param.kind == "temp":
        return _format_number(_temp_c(raw))
    if param.kind == "x10":
        return _format_number(raw / 10.0)
    if param.kind == "ms10":
        return str(raw * 10)
    return str(raw)


def _param_parse_display_value(param: BmsParamDef, text: str) -> int:
    stripped = text.strip()
    if not stripped:
        raise ValueError(f"{param.key} 不能为空")
    if param.kind == "temp":
        raw = int(round((float(stripped) + 40.0) * 10.0))
    elif param.kind == "x10":
        raw = int(round(float(stripped) * 10.0))
    elif param.kind == "ms10":
        raw = int(round(float(stripped) / 10.0))
    else:
        raw = int(stripped, 0)
    if raw < 0 or raw > 0xFFFF:
        raise ValueError(f"{param.key} 超出 0..65535")
    return raw


def _event_interval_text(delta: int) -> str:
    if delta == 0:
        return "NA"
    if delta == 171:
        return "1min以内"
    if delta <= 24:
        return f"{delta}h"
    if delta <= 168:
        return f"{delta // 24}d_{delta % 24}h"
    return "溢出"


def _modbus_crc_frame(frame: bytes) -> bytes:
    crc = crc16_modbus(frame)
    return frame + bytes((crc & 0xFF, (crc >> 8) & 0xFF))


def _u16be(value: int) -> bytes:
    return bytes(((value >> 8) & 0xFF, value & 0xFF))


def _build_direct_iap_write_frame(slave: int, addr: int, declared_length: int, payload: bytes, byte_count: int) -> bytes:
    if slave <= 0 or slave > 247:
        raise ValueError("Modbus slave 必须是 1..247")
    if addr not in (BMS_DIRECT_ENTER_IAP_ADDR, BMS_DIRECT_IAP_DATA_ADDR, BMS_DIRECT_IAP_COMPLETE_ADDR):
        raise ValueError(f"不支持的串口 IAP 地址: 0x{addr:04X}")
    if declared_length <= 0 or declared_length > 0xFFFF:
        raise ValueError("串口 IAP 长度必须是 1..65535")
    if byte_count < 0 or byte_count > 0xFF:
        raise ValueError("串口 IAP byte_count 超出 0..255")
    if byte_count == 0:
        if len(payload) != declared_length:
            raise ValueError("串口 IAP 原始块长度与声明长度不一致")
    elif byte_count != len(payload) or declared_length * 2 != byte_count:
        raise ValueError("串口 IAP Modbus 字节数与寄存器数不一致")

    frame = bytes((slave, 0x10)) + _u16be(addr) + _u16be(declared_length) + bytes((byte_count,)) + payload
    return _modbus_crc_frame(frame)


def _validate_modbus_write_response(frame: bytes, slave: int, expected_func: int, expected_echo: bytes) -> None:
    if len(frame) not in (5, 8):
        raise RuntimeError(f"Modbus 写响应长度错误: {len(frame)}")
    expected_crc = crc16_modbus(frame[:-2])
    actual_crc = frame[-2] | (frame[-1] << 8)
    if expected_crc != actual_crc:
        raise RuntimeError(f"Modbus CRC 错误: expect=0x{expected_crc:04X} actual=0x{actual_crc:04X}")
    if frame[0] != slave:
        raise RuntimeError(f"Modbus slave 不匹配: expect={slave} actual={frame[0]}")
    if frame[1] & 0x80:
        if len(frame) != 5:
            raise RuntimeError(f"Modbus 异常响应长度错误: {len(frame)}")
        raise RuntimeError(f"Modbus 异常: func=0x{frame[1]:02X} code=0x{frame[2]:02X}")
    if len(frame) != 8:
        raise RuntimeError(f"Modbus 正常写响应长度错误: {len(frame)}")
    if frame[1] != expected_func:
        raise RuntimeError(f"Modbus 写功能码不匹配: 0x{frame[1]:02X}")
    if frame[2:6] != expected_echo:
        raise RuntimeError("Modbus 写响应回显不匹配")


class DirectBmsModbusClient:
    def __init__(self, port: str, baud: int, timeout: float, slave: int):
        if slave <= 0 or slave > 247:
            raise ValueError("Modbus slave 必须是 1..247")
        serial = require_pyserial()
        self._ser = serial.Serial(port=port, baudrate=baud, timeout=timeout, write_timeout=timeout)
        self._slave = slave & 0xFF

    def close(self) -> None:
        self._ser.close()

    def __enter__(self) -> "DirectBmsModbusClient":
        return self

    def __exit__(self, _exc_type, _exc, _tb) -> None:
        self.close()

    def _verify_crc(self, frame: bytes) -> None:
        if len(frame) < 4:
            raise RuntimeError("Modbus 响应长度不足")
        expected = crc16_modbus(frame[:-2])
        actual = frame[-2] | (frame[-1] << 8)
        if expected != actual:
            raise RuntimeError(f"Modbus CRC 错误: expect=0x{expected:04X} actual=0x{actual:04X}")

    def _read_exact(self, size: int, label: str) -> bytes:
        data = self._ser.read(size)
        if len(data) != size:
            raise TimeoutError(f"等待 Modbus {label} 超时")
        return data

    def set_slave(self, slave: int) -> None:
        if slave <= 0 or slave > 247:
            raise ValueError("Modbus slave 必须是 1..247")
        self._slave = slave & 0xFF

    def set_timeout(self, timeout: float) -> None:
        if timeout <= 0:
            raise ValueError("串口超时必须大于 0")
        self._ser.timeout = timeout
        self._ser.write_timeout = timeout

    def reset_input_buffer(self) -> None:
        self._ser.reset_input_buffer()

    def _read_write_response(self, req: bytes, expected_func: int) -> None:
        head = self._read_exact(2, "写响应头")
        if head[1] & 0x80:
            frame = head + self._read_exact(3, "写异常响应")
        else:
            frame = head + self._read_exact(6, "写正常响应")
        _validate_modbus_write_response(frame, self._slave, expected_func, req[2:6])

    def read_words(self, addr: int, count: int) -> list[int]:
        if count <= 0 or count > 120:
            raise ValueError("读取数量必须是 1..120")
        req = _modbus_crc_frame(bytes((self._slave, 0x03)) + _u16be(addr) + _u16be(count))
        self._ser.reset_input_buffer()
        self._ser.write(req)
        self._ser.flush()
        head = self._read_exact(3, "读响应头")
        if head[0] != self._slave:
            raise RuntimeError(f"Modbus slave 不匹配: expect={self._slave} actual={head[0]}")
        if head[1] & 0x80:
            tail = self._read_exact(2, "异常响应")
            self._verify_crc(head + tail)
            raise RuntimeError(f"Modbus 异常: func=0x{head[1]:02X} code=0x{head[2]:02X}")
        if head[1] != 0x03:
            raise RuntimeError(f"Modbus 功能码不匹配: 0x{head[1]:02X}")
        byte_count = head[2]
        if byte_count != count * 2:
            raise RuntimeError(f"Modbus 读长度错误: {byte_count} != {count * 2}")
        body = self._read_exact(byte_count + 2, "读响应数据")
        frame = head + body
        self._verify_crc(frame)
        payload = frame[3 : 3 + byte_count]
        return [(payload[i] << 8) | payload[i + 1] for i in range(0, len(payload), 2)]

    def write_words(self, addr: int, words: list[int]) -> None:
        if not words:
            raise ValueError("写入数量必须大于 0")
        if len(words) > 120:
            raise ValueError("一次最多写入 120 个寄存器")
        clean_words = [word & 0xFFFF for word in words]
        if len(clean_words) == 1 and addr < 0x2000:
            expected_func = 0x06
            req = _modbus_crc_frame(bytes((self._slave, 0x06)) + _u16be(addr) + _u16be(clean_words[0]))
        else:
            expected_func = 0x10
            payload = b"".join(_u16be(word) for word in clean_words)
            req = _modbus_crc_frame(
                bytes((self._slave, 0x10)) + _u16be(addr) + _u16be(len(clean_words)) + bytes((len(payload),)) + payload
            )
        self._ser.reset_input_buffer()
        self._ser.write(req)
        self._ser.flush()
        self._read_write_response(req, expected_func)

    def _write_direct_iap_command(self, addr: int, declared_length: int, payload: bytes, byte_count: int) -> None:
        req = _build_direct_iap_write_frame(self._slave, addr, declared_length, payload, byte_count)
        self._ser.reset_input_buffer()
        self._ser.write(req)
        self._ser.flush()
        self._read_write_response(req, 0x10)

    def iap_connect(self) -> None:
        self._write_direct_iap_command(BMS_DIRECT_ENTER_IAP_ADDR, 1, b"\x00\x00", 2)

    def iap_write_block(self, payload: bytes) -> None:
        if not payload or len(payload) > BMS_DIRECT_IAP_BLOCK_SIZE:
            raise ValueError(f"串口 IAP 数据块必须是 1..{BMS_DIRECT_IAP_BLOCK_SIZE} 字节")
        self._write_direct_iap_command(BMS_DIRECT_IAP_DATA_ADDR, len(payload), payload, 0)

    def iap_complete(self) -> None:
        self._write_direct_iap_command(BMS_DIRECT_IAP_COMPLETE_ADDR, 1, b"\x00\x00", 2)


def _read_bms_words_once(port: str, baud: int, addr: int, count: int) -> list[int]:
    payload = struct.pack("<HH", addr, count)
    with CommToolClient(port, baud, timeout=1.0) as client:
        resp = client.command(CMD_BMS_READ, payload, timeout=max(10.0, count * 0.08 + 3.0))
    if len(resp.payload) != count * 2:
        raise RuntimeError(f"BMS_READ 响应长度错误: {len(resp.payload)}")
    return list(struct.unpack("<" + "H" * count, resp.payload))


def _words_to_be_bytes(words: list[int]) -> bytes:
    data = bytearray()
    for word in words:
        data.append((word >> 8) & 0xFF)
        data.append(word & 0xFF)
    return bytes(data)


def _decode_product_field(data: bytes) -> str:
    text = data.rstrip(b"\x00\xff ").decode("ascii", errors="ignore").strip()
    return text if text else "--"


def _decode_product_info_words(words: list[int]) -> dict[str, str]:
    if len(words) < BMS_PRODUCT_INFO_WORDS:
        raise RuntimeError("BMS 产品信息长度不足")
    data = _words_to_be_bytes(words[:BMS_PRODUCT_INFO_WORDS])
    field_len = BMS_PRODUCT_INFO_FIELD_LEN
    return {
        "serial": _decode_product_field(data[0:field_len]),
        "hardware": _decode_product_field(data[field_len : field_len * 2]),
        "software": _decode_product_field(data[field_len * 2 : field_len * 3]),
    }


def _format_product_info(info: dict[str, str]) -> str:
    return (
        f"软件版本: {info.get('software', '--')}    "
        f"硬件版本: {info.get('hardware', '--')}    "
        f"BMS序列号: {info.get('serial', '--')}"
    )


def _read_product_info_once(port: str, baud: int) -> dict[str, str]:
    words = _read_bms_words_once(port, baud, BMS_PRODUCT_INFO_ADDR, BMS_PRODUCT_INFO_WORDS)
    return _decode_product_info_words(words)


class BmsMonitorWindow(tk.Toplevel):
    def __init__(self, parent: "UpgradeUi"):
        super().__init__(parent)
        self.parent = parent
        self.title(tr("BMS 实时监控"))
        self.geometry("900x660")
        self.minsize(820, 580)
        self.protocol("WM_DELETE_WINDOW", self._on_close)

        self.events: "queue.Queue[UiEvent]" = queue.Queue()
        self.worker: threading.Thread | None = None
        self.running = False
        self.closed = False
        self.parent_paused = False
        self.interval_var = tk.StringVar(value="2.0")
        self.state_var = tk.StringVar(value="未开始")
        self.summary_var = tk.StringVar(value="未读取")
        self.detail_var = tk.StringVar(value="")
        self.product_info_var = tk.StringVar(value=_format_product_info({}))

        self._build_ui()
        self.after(80, self._after_events)

    def _build_ui(self) -> None:
        root = ttk.Frame(self, padding=12)
        root.pack(fill=tk.BOTH, expand=True)
        root.columnconfigure(0, weight=2)
        root.columnconfigure(1, weight=1)
        root.rowconfigure(2, weight=1)

        toolbar = ttk.Frame(root)
        toolbar.grid(row=0, column=0, columnspan=2, sticky="ew", pady=(0, 8))
        ttk.Button(toolbar, text="开始", command=self.start).pack(side=tk.LEFT, padx=(0, 8))
        ttk.Button(toolbar, text="暂停", command=self.stop).pack(side=tk.LEFT, padx=(0, 8))
        ttk.Button(toolbar, text="刷新一次", command=self.refresh_once).pack(side=tk.LEFT, padx=(0, 16))
        ttk.Label(toolbar, text="间隔(s)").pack(side=tk.LEFT)
        ttk.Combobox(toolbar, textvariable=self.interval_var, values=["0.5", "1.0", "2.0", "5.0"], width=8).pack(
            side=tk.LEFT, padx=(6, 16)
        )
        ttk.Label(toolbar, textvariable=self.state_var).pack(side=tk.LEFT)

        ttk.Label(root, textvariable=self.summary_var, justify=tk.LEFT).grid(
            row=1, column=0, columnspan=2, sticky="ew", pady=(0, 8)
        )

        cell_box = ttk.LabelFrame(root, text="单体电压")
        cell_box.grid(row=2, column=0, sticky="nsew", padx=(0, 8))
        cell_box.rowconfigure(0, weight=1)
        cell_box.columnconfigure(0, weight=1)
        self.cell_tree = ttk.Treeview(cell_box, columns=("mv",), show="headings", height=18)
        self.cell_tree.heading("mv", text="mV")
        self.cell_tree.column("mv", anchor=tk.CENTER, width=100)
        self.cell_tree.grid(row=0, column=0, sticky="nsew")
        cell_scroll = ttk.Scrollbar(cell_box, orient=tk.VERTICAL, command=self.cell_tree.yview)
        cell_scroll.grid(row=0, column=1, sticky="ns")
        self.cell_tree.configure(yscrollcommand=cell_scroll.set)

        right = ttk.Frame(root)
        right.grid(row=2, column=1, sticky="nsew")
        right.rowconfigure(0, weight=1)
        right.rowconfigure(1, weight=1)
        right.columnconfigure(0, weight=1)

        temp_box = ttk.LabelFrame(right, text="温度")
        temp_box.grid(row=0, column=0, sticky="nsew", pady=(0, 8))
        temp_box.rowconfigure(0, weight=1)
        temp_box.columnconfigure(0, weight=1)
        self.temp_tree = ttk.Treeview(temp_box, columns=("value",), show="headings", height=10)
        self.temp_tree.heading("value", text="℃")
        self.temp_tree.column("value", anchor=tk.CENTER, width=100)
        self.temp_tree.grid(row=0, column=0, sticky="nsew")

        detail_box = ttk.LabelFrame(right, text="故障/均衡/容量")
        detail_box.grid(row=1, column=0, sticky="nsew")
        detail_box.columnconfigure(0, weight=1)
        ttk.Label(detail_box, textvariable=self.detail_var, justify=tk.LEFT).grid(
            row=0, column=0, sticky="nw", padx=10, pady=10
        )

        product_box = ttk.LabelFrame(root, text="BMS 版本/序列号")
        product_box.grid(row=3, column=0, columnspan=2, sticky="ew", pady=(8, 0))
        product_box.columnconfigure(0, weight=1)
        ttk.Label(product_box, textvariable=self.product_info_var, justify=tk.LEFT).grid(
            row=0, column=0, sticky="w", padx=10, pady=8
        )

        for index in range(32):
            self.cell_tree.insert("", tk.END, iid=f"cell{index}", values=(f"{index + 1:02d}: --",))
        for index in range(BMS_LIVE_TEMP_COUNT):
            self.temp_tree.insert("", tk.END, iid=f"temp{index}", values=(f"{_live_temp_label(index)}: --",))

    def set_paused_by_parent(self, paused: bool) -> None:
        self.parent_paused = paused
        if paused:
            self.state_var.set("主任务执行中，监控暂停")
        elif self.running:
            self.state_var.set("监控中")
            self._schedule_poll(100)

    def start(self) -> None:
        self.running = True
        self.state_var.set("监控中")
        self._schedule_poll(10)

    def stop(self) -> None:
        self.running = False
        self.state_var.set("已暂停")

    def refresh_once(self) -> None:
        self._schedule_poll(10, force=True)

    def _schedule_poll(self, delay_ms: int, force: bool = False) -> None:
        if self.closed:
            return
        self.after(delay_ms, lambda: self._poll_if_needed(force))

    def _poll_if_needed(self, force: bool = False) -> None:
        if self.closed or self.parent_paused:
            return
        if not force and not self.running:
            return
        if self.parent.live_running:
            if self.parent.live_snapshot_cache is not None:
                self._show_snapshot(self.parent.live_snapshot_cache)
                self.state_var.set("监控中(复用主界面)")
            if self.parent.product_info_cache is not None:
                self.product_info_var.set(_format_product_info(self.parent.product_info_cache))
            if self.running:
                self._schedule_poll(self._interval_ms(1.0))
            return
        if self.worker and self.worker.is_alive():
            return
        self.worker = threading.Thread(target=self._worker_poll, daemon=True)
        self.worker.start()

    def _worker_poll(self) -> None:
        try:
            if not self.parent.serial_lock.acquire(blocking=False):
                self.events.put(UiEvent("busy", "串口忙，等待主任务结束"))
                return
            try:
                self.parent._capture_runtime_settings(include_bin=False)
                with self.parent._open_client() as client:
                    self.parent._set_can_target(client)
                    words = self.parent._read_live_words(client)
                    try:
                        product_info = self.parent._read_bms_product_info_cached(client)
                    except Exception as exc:
                        product_info = {"error": str(exc)}
            finally:
                self.parent.serial_lock.release()
            self.events.put(UiEvent("snapshot", words))
            self.events.put(UiEvent("product_info", product_info))
        except Exception as exc:
            self.events.put(UiEvent("error", str(exc)))

    def _interval_ms(self, minimum: float = 0.5) -> int:
        try:
            seconds = float(self.interval_var.get())
        except ValueError:
            seconds = 1.0
            self.interval_var.set("1.0")
        return int(max(minimum, seconds) * 1000)

    def _after_events(self) -> None:
        while True:
            try:
                event = self.events.get_nowait()
            except queue.Empty:
                break
            if event.kind == "snapshot":
                self._show_snapshot(event.payload)
                self.state_var.set("监控中" if self.running else "刷新完成")
                if self.running and not self.parent_paused:
                    self._schedule_poll(self._interval_ms(0.5))
            elif event.kind == "product_info":
                if isinstance(event.payload, dict) and "error" in event.payload:
                    self.product_info_var.set(f"软件版本: --    硬件版本: --    BMS序列号: --    读取失败: {event.payload['error']}")
                else:
                    self.product_info_var.set(_format_product_info(event.payload))
            elif event.kind == "error":
                self._clear_snapshot()
                self.state_var.set(f"读取失败: {event.payload}")
                if self.running and not self.parent_paused:
                    self._schedule_poll(self._interval_ms(1.0))
            elif event.kind == "busy":
                self.state_var.set(str(event.payload))
                if self.running and not self.parent_paused:
                    self._schedule_poll(self._interval_ms(1.0))
        if not self.closed:
            self.after(80, self._after_events)

    def _clear_snapshot(self) -> None:
        self.summary_var.set(
            "SOC 0%   SOH 0%   总压 0.00V   充电 0.0A   放电 0.0A\n"
            "单体 max --   min --   压差 --   有效串数 0   温度 max 0℃   min 0℃"
        )
        self.detail_var.set(
            "容量: 0.00/0.00Ah\n"
            "出厂容量: 0.00Ah\n"
            "循环次数: 0\n"
            "故障字: 0x0000  0x0000  0x0000\n"
            "均衡: 0x0000  0x0000"
        )
        for item in self.cell_tree.get_children():
            self.cell_tree.delete(item)
        for index in range(BMS_LIVE_TEMP_COUNT):
            self.temp_tree.item(f"temp{index}", values=(f"{_live_temp_label(index)}: 0",))

    def _show_snapshot(self, words: list[int]) -> None:
        if _is_zero_live_snapshot(words):
            self._clear_snapshot()
            return

        cells = words[0:32]
        valid_cells = _valid_cell_items(cells)
        max_cell_text, min_cell_text, delta_cell_text = _cell_stat_text(valid_cells)
        total_v = words[37] / 100.0
        ichg = words[50] / 10.0
        idsg = words[51] / 10.0
        soc = words[52]
        soh = words[53]
        capacity_now = words[54] / 100.0
        capacity_full = words[55] / 100.0

        self.summary_var.set(
            f"SOC {soc}%   SOH {soh}%   总压 {total_v:.2f}V   "
            f"充电 {ichg:.1f}A   放电 {idsg:.1f}A\n"
            f"单体 max {max_cell_text}   min {min_cell_text}   "
            f"压差 {delta_cell_text}   有效串数 {len(valid_cells)}   "
            f"温度 max {_temp_c(words[48]):.1f}℃   min {_temp_c(words[49]):.1f}℃"
        )
        self.detail_var.set(
            f"容量: {capacity_now:.2f}/{capacity_full:.2f}Ah\n"
            f"出厂容量: {words[56] / 100.0:.2f}Ah\n"
            f"循环次数: {words[57]}\n"
            f"故障字: 0x{words[58]:04X}  0x{words[59]:04X}  0x{words[60]:04X}\n"
            f"均衡: 0x{words[61]:04X}  0x{words[62]:04X}"
        )

        for item in self.cell_tree.get_children():
            self.cell_tree.delete(item)
        for index, value in valid_cells:
            self.cell_tree.insert("", tk.END, iid=f"cell{index}", values=(f"{index + 1:02d}: {value} mV",))
        for index, raw in enumerate(_live_temp_words(words)):
            self.temp_tree.item(f"temp{index}", values=(f"{_live_temp_label(index)}: {_temp_c(raw):.1f}",))

    def _on_close(self) -> None:
        self.running = False
        self.closed = True
        self.parent.monitor_window = None
        self.destroy()


class BmsLogWindow(tk.Toplevel):
    def __init__(self, parent: "UpgradeUi", records: list[tuple[int, int]]):
        super().__init__(parent)
        self.title(tr("BMS 日志"))
        self.geometry("620x620")
        self.minsize(560, 480)

        root = ttk.Frame(self, padding=12)
        root.pack(fill=tk.BOTH, expand=True)
        root.columnconfigure(0, weight=1)
        root.rowconfigure(0, weight=1)

        self.tree = ttk.Treeview(root, columns=("event", "interval", "raw"), show="headings", height=22)
        self.tree.heading("event", text="事件")
        self.tree.heading("interval", text="与上次间隔")
        self.tree.heading("raw", text="原始值")
        self.tree.column("event", width=240, anchor=tk.W)
        self.tree.column("interval", width=120, anchor=tk.CENTER)
        self.tree.column("raw", width=120, anchor=tk.CENTER)
        self.tree.grid(row=0, column=0, sticky="nsew")

        scrollbar = ttk.Scrollbar(root, orient=tk.VERTICAL, command=self.tree.yview)
        scrollbar.grid(row=0, column=1, sticky="ns")
        self.tree.configure(yscrollcommand=scrollbar.set)

        for index, (event, delta) in enumerate(records, start=1):
            if event == 0 and delta == 0:
                continue
            name = BMS_EVENT_NAMES[event] if event < len(BMS_EVENT_NAMES) else f"未知事件{event}"
            self.tree.insert(
                "",
                tk.END,
                values=(f"{index:03d}. {name}", _event_interval_text(delta), f"0x{event:02X} 0x{delta:02X}"),
            )


class OwcMonitorWindow(tk.Toplevel):
    """独占选定串口的只读监控；工作线程只通过 queue 更新 Tk。"""
    def __init__(self, parent, port: str):
        super().__init__(parent)
        self.parent = parent
        self.port = port
        self.title(tr("一线通监控"))
        self.geometry("1020x700")
        self.stop_event = threading.Event()
        self.messages = queue.Queue()
        self.thread = None
        self.closing = False
        self.status_var = tk.StringVar(value=tr("未连接"))
        self.snapshots = {}
        top = ttk.Frame(self, padding=8)
        top.pack(fill=tk.X)
        ttk.Label(top, text=f"{port} / Comm Tool / 115200 8N1").pack(side=tk.LEFT)
        self.mode_box = ttk.Combobox(top, width=18, state="readonly",
                                    values=[tr("自动识别"), "A1.3.3", tr("锂神 V1.1.1")])
        self.mode_box.current(0)
        self.mode_box.pack(side=tk.LEFT, padx=6)
        self.restart_pending = False
        self.mode_box.bind("<<ComboboxSelected>>", self._mode_changed)
        ttk.Button(top, text="开始监控", command=self._start).pack(side=tk.LEFT, padx=6)
        ttk.Button(top, text="停止监控", command=self._stop).pack(side=tk.LEFT)
        ttk.Label(self, textvariable=self.status_var, wraplength=980).pack(fill=tk.X, padx=8, pady=4)
        ttk.Label(self, text=tr("接收模式不会修改BMS协议；标准锂神提供三项，本项目扩展增加电流和故障。"), wraplength=980).pack(fill=tk.X, padx=8)
        ttk.Label(self, text=tr("校验失败和过期数据不显示为实时测量值。监控期间串口被独占；停止后恢复其它操作。"), wraplength=980).pack(fill=tk.X, padx=8, pady=4)
        body = ttk.Frame(self, padding=8)
        body.pack(fill=tk.BOTH, expand=True)
        self.tree = ttk.Treeview(body, columns=("packet", "field", "value", "unit"), show="headings")
        for key, title, width in (("packet", "报文", 190), ("field", "项目", 270), ("value", "数值", 250), ("unit", "单位", 70)):
            self.tree.heading(key, text=title)
            self.tree.column(key, width=width)
        self.tree.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        scroll = ttk.Scrollbar(body, command=self.tree.yview)
        scroll.pack(side=tk.RIGHT, fill=tk.Y)
        self.tree.configure(yscrollcommand=scroll.set)
        self.raw_text = tk.Text(self, height=8, wrap=tk.WORD)
        self.raw_text.pack(fill=tk.X, padx=8, pady=8)
        self.protocol("WM_DELETE_WINDOW", self._close)
        self.after(100, self._drain)

    def _start(self):
        if self.thread and self.thread.is_alive():
            return
        if ((self.parent.worker and self.parent.worker.is_alive()) or self.parent.live_running or
                (self.parent.monitor_window and self.parent.monitor_window.winfo_exists())):
            self.status_var.set(tr("请先停止其它监控并等待当前任务完成"))
            return
        if not self.parent.serial_lock.acquire(blocking=False):
            self.status_var.set(tr("串口忙"))
            return
        self.snapshots = {}
        self._render()
        self.stop_event.clear()
        self.thread = threading.Thread(target=self._worker, args=(self.mode_box.current(),), daemon=True)
        self.thread.start()

    def _mode_changed(self, _event=None):
        self.snapshots = {}
        self._render()
        if self.thread and self.thread.is_alive():
            self.restart_pending = True
            self.stop_event.set()

    def _stop(self):
        self.restart_pending = False
        self.stop_event.set()
        self.snapshots = {}
        self._render()

    def _worker(self, mode):
        try:
            with CommToolClient(self.port, COMM_TOOL_PC_BAUD, timeout=1.0) as client:
                extended, active_mode = start_receiver(client, mode)
                try:
                    while not self.stop_event.is_set():
                        snapshots = read_snapshots(client, extended)
                        if not snapshots[0].enabled:
                            self.messages.put(("recovering", None))
                            # 留出当前 BMS 帧结束与空闲检测时间，使其重新发送接入资料。
                            if self.stop_event.wait(2.0):
                                break
                            control_receiver(client, True, active_mode, extended)
                        else:
                            self.messages.put(("data", snapshots))
                        self.stop_event.wait(1.0)
                finally:
                    control_receiver(client, False, active_mode, extended)
        except Exception as exc:
            self.messages.put(("error", str(exc)))
        finally:
            self.parent.serial_lock.release()
            self.messages.put(("stopped", None))

    def _render(self):
        self.tree.delete(*self.tree.get_children())
        raw_lines = []
        online = link_online(self.snapshots)
        latest = self.snapshots.get(0)
        for packet_id, snapshot in self.snapshots.items():
            if latest and latest.protocol == 2 and packet_id not in (0, 0x80):
                continue
            if latest and latest.protocol != 2 and packet_id == 0x80:
                continue
            label = tr("最近完整帧") if packet_id == 0 else tr("锂神实时") if packet_id == 0x80 else f"0x{packet_id:02X}"
            fields = visible_fields(snapshot, self.snapshots) if packet_id and not (latest and latest.mismatch_protocol) else []
            if not snapshot.enabled:
                state = tr("接收器关闭")
            elif not snapshot.raw:
                state = tr("等待或过期")
            elif not snapshot.checksum_ok:
                state = tr("校验失败")
            elif packet_id == 1:
                state = tr("接入信息") if online else tr("接入信息（离线）")
            elif not online:
                state = tr("已失联")
            elif packet_id == 0x3B and snapshot.fresh and not fields:
                state = tr("测量不可用")
            else:
                state = tr("有效") if snapshot.fresh else tr("等待或过期")
            age = "--" if not snapshot.raw else f"{snapshot.age_ms / 1000:g}s"
            raw_lines.append(f"{label} | {state} | age={age} | snapshot=v{snapshot.snapshot_version} | session={snapshot.session or '--'} | seq={snapshot.sequence}\n{snapshot.raw.hex(' ')}")
            if packet_id:
                self.tree.insert("", tk.END, values=(label, tr("状态"), state, age))
                if fields:
                    field_names = {"Protocol": "协议版本", "Voltage": "总电压", "Current": "电流",
                                   "Battery temperature": "电池温度", "Max temperature": "最高温度",
                                   "Min temperature": "最低温度", "MOS temperature": "MOS温度", "Fault code": "故障码",
                                   "Fault": "故障说明", "Work status": "工作状态", "Work status (raw)": "工作状态原值",
                                   "Response requested": "请求应答设备", "Rated voltage": "额定电压", "Rated capacity": "额定容量",
                                   "Manufacturer": "厂家代码", "Battery model": "电池型号", "Chemistry code": "材料代码", "Chemistry": "电芯材料",
                                   "Allowed feedback current": "允许回馈电流", "Requested charge voltage": "请求充电电压",
                                   "Requested charge current": "请求充电电流",
                                   "BMS status (raw)": "BMS状态原值", "Cycles": "循环次数", "Max cell": "最高单体电压",
                                   "Min cell": "最低单体电压", "Max cell index (raw)": "最高单体位置原值",
                                   "Min cell index (raw)": "最低单体位置原值", "Charge status (raw)": "充电状态原值",
                                   "Charge status": "充电状态", "Encryption / charging policy / mutual recognition": "加密/充电策略/互认",
                                   "Battery code": "电池编码"}
                    for field, value, unit in fields:
                        display_name = (tr("单体") + " " + field[5:]) if field.startswith("Cell ") else tr(field_names.get(field, field))
                        self.tree.insert("", tk.END, values=(label, display_name, value, unit))
        if latest and latest.protocol == 2:
            self.tree.insert("", tk.END, values=(tr("锂神 V1.1.1"), tr("协议范围"), tr("标准三项；扩展按实际报文提供电流和故障"), ""))
        self.raw_text.configure(state="normal")
        self.raw_text.delete("1.0", tk.END)
        self.raw_text.insert("1.0", "\n".join(raw_lines))
        self.raw_text.configure(state="disabled")

    def _drain(self):
        while not self.messages.empty():
            kind, payload = self.messages.get_nowait()
            if kind == "data" and not self.stop_event.is_set():
                self.snapshots = payload
                latest = payload[0]
                self.status_var.set(f"{tr('在线') if link_online(payload) else tr('已失联')}  snapshot=v{latest.snapshot_version}  session={latest.session or '--'}  {tr('接收启用')}={latest.enabled}  {tr('有效帧')}={latest.good}  {tr('校验失败')}={latest.checksum_errors}  {tr('时序错误')}={latest.timing_errors}  {tr('队列溢出')}={latest.overflows}")
                if latest.snapshot_version < 3:
                    self.status_var.set(self.status_var.get() + tr("；旧Comm Tool仅支持A1.3.3"))
                elif latest.mismatch_protocol:
                    self.status_var.set(tr("接收协议不匹配，请切换模式"))
                elif not latest.protocol:
                    self.status_var.set(tr("正在识别协议，等待连续两帧"))
                if latest.protocol:
                    self.status_var.set(("A1.3.3" if latest.protocol == 1 else tr("锂神 V1.1.1")) + " · " + self.status_var.get())
                self._render()
            elif kind == "recovering":
                self.snapshots = {}
                self._render()
                self.status_var.set(tr("接收租约已到期，已清空缓存，正在重新启用；等待 BMS 数据。"))
            elif kind == "error":
                self.status_var.set(tr("通信异常，测量值已清空；上拉将在失联超时后释放：") + tr(payload))
                self.snapshots = {}
                self._render()
            elif kind == "stopped":
                self.snapshots = {}
                self._render()
                if not self.status_var.get().startswith(tr("通信异常")):
                    self.status_var.set(tr("监控已停止"))
        if self.restart_pending and not self.closing and (not self.thread or not self.thread.is_alive()):
            self.restart_pending = False
            self._start()
        if self.closing and (not self.thread or not self.thread.is_alive()):
            self.destroy()
            return
        self.after(100, self._drain)

    def _close(self):
        self.closing = True
        self._stop()


class UpgradeUi(tk.Tk):
    def __init__(
        self,
        port: str,
        baud: int,
        bin_path: Path,
        mode: str = COMM_MODE_COMM_TOOL,
        slave: int = BMS_DIRECT_DEFAULT_SLAVE,
        edition: str = UI_EDITION_INTERNAL,
    ):
        super().__init__()
        self.title(tr("BMS_V1.13.1 - CAN用户上位机"))
        self.geometry("1180x760")
        self.minsize(1100, 700)
        self.edition = edition if edition in (UI_EDITION_CUSTOMER, UI_EDITION_INTERNAL) else UI_EDITION_INTERNAL
        self.customer_unlocked = self.edition == UI_EDITION_INTERNAL
        if mode not in COMM_MODE_LABELS:
            mode = COMM_MODE_COMM_TOOL

        self.events: "queue.Queue[UiEvent]" = queue.Queue()
        self.worker: threading.Thread | None = None
        self.serial_lock = threading.Lock()

        self.port_var = tk.StringVar(value=port)
        self.baud_var = tk.StringVar(value=str(baud))
        self.connection_mode_var = tk.StringVar(value=COMM_MODE_LABELS[mode])
        self.modbus_slave_var = tk.StringVar(value=str(slave))
        self.bin_var = tk.StringVar(value=str(bin_path) if bin_path.is_file() else "")
        self.info_var = tk.StringVar(value="未连接")
        self.cache_var = tk.StringVar(value="未读取")
        self.image_var = tk.StringVar(value="未选择")
        self.result_var = tk.StringVar(value="等待操作")
        self.can_bitrate_var = tk.StringVar(value="250000")
        self.node_id_var = tk.StringVar(value="1")
        self.app_can_addr_var = tk.StringVar(value="0")
        self.upgrade_stage_var = tk.StringVar(value="升级进度: --")
        self.bms_addr_var = tk.StringVar(value="0xD000")
        self.bms_count_var = tk.StringVar(value="2")
        self.bms_values_var = tk.StringVar(value="")
        self.bms_result_var = tk.StringVar(value="未读取")
        self.bms_soc_write_var = tk.StringVar(value="80")
        self.bms_aging_hours_var = tk.StringVar(value="72")
        self.bms_aging_var = tk.StringVar(value="老化状态: 未读取")
        self.product_info_var = tk.StringVar(value=_format_product_info({}))
        self.bms_info_var = tk.StringVar(value="未读取")
        self.param_key_var = tk.StringVar(value=next(iter(BMS_PARAM_PRESETS)))
        self.param_value_var = tk.StringVar(value="")
        self.param_current_var = tk.StringVar(value="未读取")
        self.param_selected_var = tk.StringVar(value="未选择")
        self.param_edit_var = tk.StringVar(value="")
        self.param_dirty_var = tk.StringVar(value="未修改")
        self.param_write_button_var = tk.StringVar(value="写入修改")
        self.comm_state_var = tk.StringVar(value="通信: 未连接")
        self.live_interval_var = tk.StringVar(value="2.0")
        self.live_button_var = tk.StringVar(value="开始监控")
        self.log_interval_var = tk.StringVar(value="5")
        self.log_file_var = tk.StringVar(value="")
        self.log_status_var = tk.StringVar(value="记录: 已停止")
        self.log_button_var = tk.StringVar(value="开始记录")
        self.progress_var = tk.DoubleVar(value=0.0)
        self.active_port = port
        self.active_baud = baud
        self.active_connection_mode = mode
        self.active_modbus_slave = slave
        self.active_bin = bin_path
        self.active_can_bitrate = 250000
        self.active_node_id = 1
        self.active_app_can_addr = 0
        self.active_bms_addr = 0xD000
        self.active_bms_count = 2
        self.active_bms_words: list[int] = []
        self.active_soc_write = 80
        self.active_aging_hours = 72
        self.active_aging_action = 0
        self.active_aging_name = ""
        self.param_values: dict[str, int] = {}
        self.active_param_key = ""
        self.active_param_raw = 0
        self.active_param_values: dict[str, int] = {}
        self.active_param_range = ""
        self.monitor_window: BmsMonitorWindow | None = None
        self.owc_window: OwcMonitorWindow | None = None
        self.live_running = False
        self.live_after_id: str | None = None
        self.live_worker: threading.Thread | None = None
        self.log_running = False
        self.log_path: Path | None = None
        self.log_count = 0
        self.log_started_monotonic = 0.0
        self.log_next_due_monotonic = 0.0
        self.param_entry_vars: dict[str, tk.StringVar] = {}
        self.param_widgets: dict[str, tk.Widget] = {}
        self.param_dirty: set[str] = set()
        self.param_loading = False
        self.log_tree: ttk.Treeview | None = None
        self.applied_target: tuple[object, ...] | None = None
        self.applied_bms_uart_target: tuple[object, ...] | None = None
        self.live_word_count: int | None = None
        self.live_snapshot_cache: list[int] | None = None
        self.live_status_words_cache: list[int] | None = None
        self.live_status_retry_due_monotonic = 0.0
        self.product_info_cache: dict[str, str] | None = None
        self.product_info_cache_target: tuple[object, ...] | None = None
        self.product_info_next_due_monotonic = 0.0

        self._build_ui()
        self._refresh_ports()
        self._after_events()
        self._describe_selected_bin()
        self.protocol("WM_DELETE_WINDOW", self._close_main)

    def _close_main(self) -> None:
        if self.owc_window and self.owc_window.winfo_exists():
            self.owc_window._close()
            if self.owc_window.thread and self.owc_window.thread.is_alive():
                self.after(100, self._close_main)
                return
        self.destroy()

    def _build_ui(self) -> None:
        self._configure_styles()

        root = ttk.Frame(self, padding=4)
        root.pack(fill=tk.BOTH, expand=True)
        root.columnconfigure(0, weight=1)
        root.rowconfigure(0, weight=1)

        self.main_notebook = ttk.Notebook(root)
        self.main_notebook.grid(row=0, column=0, sticky="nsew")

        monitor_tab = ttk.Frame(self.main_notebook, padding=6)
        data_tab = ttk.Frame(self.main_notebook, padding=6)
        storage_tab = ttk.Frame(self.main_notebook, padding=6)
        params_tab = ttk.Frame(self.main_notebook, padding=6)
        system_tab = ttk.Frame(self.main_notebook, padding=6)
        other_tab = ttk.Frame(self.main_notebook, padding=6)
        self.main_tab_specs = (
            (monitor_tab, "实时监控", False),
            (data_tab, "实时数据", True),
            (storage_tab, "存储信息", False),
            (params_tab, "参数设置", True),
            (system_tab, "系统状态", True),
            (other_tab, "其它功能", True),
        )

        self._build_monitor_tab(monitor_tab)
        self._build_data_tab(data_tab)
        self._build_storage_tab(storage_tab)
        self._build_params_tab(params_tab)
        self._build_system_tab(system_tab)
        self._build_other_tab(other_tab)
        self._refresh_main_tabs()

        bottom = ttk.Frame(root)
        bottom.grid(row=1, column=0, sticky="ew", pady=(4, 0))
        bottom.columnconfigure(1, weight=1)
        ttk.Label(bottom, textvariable=self.result_var).grid(row=0, column=0, sticky="w")
        ttk.Label(bottom, textvariable=self.comm_state_var).grid(row=0, column=1, sticky="e", padx=(8, 0))
        if self.edition == UI_EDITION_CUSTOMER:
            self.access_button_var = tk.StringVar(value="锁定维护功能" if self.customer_unlocked else "维护解锁")
            ttk.Button(bottom, textvariable=self.access_button_var, command=self._toggle_customer_access).grid(
                row=0, column=2, sticky="e", padx=(12, 0)
            )

    def _refresh_main_tabs(self) -> None:
        visible = {str(tab) for tab in self.main_notebook.tabs()}
        selected = self.main_notebook.select()
        for tab, _title, _restricted in self.main_tab_specs:
            if str(tab) in visible:
                self.main_notebook.forget(tab)
        for tab, title, restricted in self.main_tab_specs:
            if not restricted or self.customer_unlocked:
                self.main_notebook.add(tab, text=title)
        if selected and selected in self.main_notebook.tabs():
            self.main_notebook.select(selected)
        elif self.main_notebook.tabs():
            self.main_notebook.select(self.main_notebook.tabs()[0])

    def _toggle_customer_access(self) -> None:
        if self.edition != UI_EDITION_CUSTOMER:
            return
        if self.customer_unlocked:
            self.customer_unlocked = False
            self._refresh_main_tabs()
            self.access_button_var.set("维护解锁")
            self.result_var.set("维护功能已锁定")
            return
        password = simpledialog.askstring(
            tr("维护密码"),
            tr("请输入维护密码"),
            show="*",
            parent=self,
        )
        if password is None:
            return
        if password != CUSTOMER_MAINTENANCE_PASSWORD:
            messagebox.showerror("密码错误", "维护密码不正确。")
            return
        self.customer_unlocked = True
        self._refresh_main_tabs()
        self.access_button_var.set("锁定维护功能")
        self.result_var.set("维护功能已解锁")

    def _configure_styles(self) -> None:
        self.style = ttk.Style(self)
        try:
            self.style.theme_use("clam")
        except tk.TclError:
            pass
        self.style.configure("TNotebook.Tab", padding=(12, 4))
        self.style.configure("Small.TLabel", font=("TkDefaultFont", 8))
        self.style.configure("Value.TLabel", font=("TkDefaultFont", 10, "bold"))
        self.style.configure("ParameterHeader.TLabel", font=("TkDefaultFont", 9, "bold"), foreground="#27445d")
        self.style.configure("Dirty.TEntry", fieldbackground="#fff0b3")
        self.style.configure("Dirty.TCombobox", fieldbackground="#fff0b3")

    def _build_monitor_tab(self, tab: ttk.Frame) -> None:
        tab.columnconfigure(0, weight=3)
        tab.columnconfigure(1, weight=2)
        tab.columnconfigure(2, weight=3)
        tab.rowconfigure(0, weight=1)
        tab.rowconfigure(1, weight=0)

        left = ttk.LabelFrame(tab, text="单体电压(mV)")
        left.grid(row=0, column=0, sticky="nsew", padx=(0, 6))
        left.columnconfigure(0, weight=1)
        left.rowconfigure(1, weight=1)

        stat = ttk.Frame(left)
        stat.grid(row=0, column=0, sticky="ew", padx=8, pady=(6, 2))
        for col in range(4):
            stat.columnconfigure(col, weight=1)
        self.cell_stat_vars = {
            "max": tk.StringVar(value="最高电压 --"),
            "min": tk.StringVar(value="最低电压 --"),
            "avg": tk.StringVar(value="平均电压 --"),
            "delta": tk.StringVar(value="最大压差 --"),
        }
        ttk.Label(stat, textvariable=self.cell_stat_vars["max"], foreground="red").grid(row=0, column=0, sticky="w")
        ttk.Label(stat, textvariable=self.cell_stat_vars["min"], foreground="green").grid(row=0, column=1, sticky="w")
        ttk.Label(stat, textvariable=self.cell_stat_vars["avg"]).grid(row=1, column=0, sticky="w", pady=(3, 0))
        ttk.Label(stat, textvariable=self.cell_stat_vars["delta"]).grid(row=1, column=1, sticky="w", pady=(3, 0))

        cells_frame = ttk.Frame(left)
        cells_frame.grid(row=1, column=0, sticky="nsew", padx=8, pady=6)
        for col in range(4):
            cells_frame.columnconfigure(col, weight=1)
        self.cell_slots: list[tuple[ttk.Frame, int, int]] = []
        self.cell_value_vars: list[tk.StringVar] = []
        for index in range(32):
            row = index % 16
            col = (index // 16) * 2
            cell = ttk.Frame(cells_frame, borderwidth=1, relief="ridge", padding=(2, 2))
            number = tk.Label(cell, text=f"{index + 1:02d}", width=4, bg="#32f020", fg="red")
            number.grid(row=0, column=0, padx=(0, 4))
            value_var = tk.StringVar(value="--")
            ttk.Label(cell, textvariable=value_var, width=7, anchor="e").grid(row=0, column=1)
            cell.grid(row=row, column=col, sticky="w", padx=(0, 14), pady=3)
            self.cell_slots.append((cell, row, col))
            self.cell_value_vars.append(value_var)

        center = ttk.Frame(tab)
        center.grid(row=0, column=1, sticky="nsew", padx=6)
        center.columnconfigure(0, weight=1)
        center.rowconfigure(1, weight=1)

        basic = ttk.LabelFrame(center, text="基础信息")
        basic.grid(row=0, column=0, sticky="ew")
        basic.columnconfigure(1, weight=1)
        self.basic_vars = {
            "total_v": tk.StringVar(value="--"),
            "current": tk.StringVar(value="--"),
            "soh": tk.StringVar(value="--"),
            "cap_now": tk.StringVar(value="--"),
            "cap_full": tk.StringVar(value="--"),
            "cap_factory": tk.StringVar(value="--"),
            "cycle": tk.StringVar(value="--"),
            "soc": tk.StringVar(value="SOC:--"),
            "state": tk.StringVar(value="--"),
        }
        for row, (label, key, unit) in enumerate(
            [
                ("总压", "total_v", "V"),
                ("电流", "current", "A"),
                ("SOH", "soh", "%"),
                ("剩余容量", "cap_now", "mAh"),
                ("满电容量", "cap_full", "mAh"),
                ("出厂容量", "cap_factory", "mAh"),
                ("循环次数", "cycle", ""),
            ]
        ):
            ttk.Label(basic, text=label).grid(row=row, column=0, sticky="w", padx=(12, 6), pady=7)
            ttk.Label(basic, textvariable=self.basic_vars[key], style="Value.TLabel").grid(row=row, column=1, sticky="w", pady=7)
            ttk.Label(basic, text=unit).grid(row=row, column=2, sticky="w", padx=(4, 10), pady=7)
        ttk.Label(basic, textvariable=self.basic_vars["soc"], font=("TkDefaultFont", 13, "bold")).grid(
            row=0, column=3, columnspan=2, sticky="s", padx=(12, 12)
        )
        self.battery_canvas = tk.Canvas(basic, width=84, height=170, highlightthickness=0)
        self.battery_canvas.grid(row=1, column=3, rowspan=6, padx=(10, 14), pady=(4, 10))
        ttk.Label(basic, textvariable=self.basic_vars["state"]).grid(row=7, column=3, sticky="n", pady=(0, 8))
        self._draw_battery(0)

        temp = ttk.LabelFrame(center, text="温度(℃)")
        temp.grid(row=1, column=0, sticky="nsew", pady=(8, 0))
        temp.columnconfigure(1, weight=1)
        self.temp_vars = {
            "max": tk.StringVar(value="--"),
            "min": tk.StringVar(value="--"),
            "t1": tk.StringVar(value="--"),
            "t2": tk.StringVar(value="--"),
            "mos": tk.StringVar(value="--"),
        }
        for row, (label, key, color) in enumerate(
            [("最高温度", "max", "red"), ("最低温度", "min", "green"), ("温度1", "t1", ""), ("温度2", "t2", ""), ("MOS温度", "mos", "")]
        ):
            ttk.Label(temp, text=label).grid(row=row, column=0, sticky="w", padx=(12, 8), pady=13)
            value_label = ttk.Label(temp, textvariable=self.temp_vars[key])
            if color:
                value_label.configure(foreground=color)
            value_label.grid(row=row, column=1, sticky="w", pady=13)
            ttk.Label(temp, text="℃").grid(row=row, column=2, sticky="w", padx=(4, 12), pady=13)

        right = ttk.Frame(tab)
        right.grid(row=0, column=2, sticky="nsew", padx=(6, 0))
        right.columnconfigure(0, weight=1)
        right.rowconfigure(3, weight=1)
        right.rowconfigure(4, weight=1)
        right.rowconfigure(5, weight=1)

        comm = ttk.LabelFrame(right, text="通信设置")
        comm.grid(row=0, column=0, sticky="ew")
        self._build_connection_panel(comm)

        sys_box = ttk.LabelFrame(right, text="系统状态")
        sys_box.grid(row=1, column=0, sticky="ew", pady=(8, 0))
        self.status_badges: dict[str, tk.Label] = {}
        for col, name in enumerate(["充电MOS", "放电MOS", "加热", "冷凝"]):
            ttk.Label(sys_box, text=name).grid(row=0, column=col, sticky="n", padx=7, pady=(7, 2))
            badge = tk.Label(sys_box, text="--", width=8, relief="ridge", bg="#b0b0b0", fg="white")
            badge.grid(row=1, column=col, padx=7, pady=(0, 7))
            self.status_badges[name] = badge

        self.fault_vars = {
            "first": tk.StringVar(value="--"),
            "second": tk.StringVar(value="--"),
            "third": tk.StringVar(value="--"),
            "monitor": tk.StringVar(value="--"),
        }
        for row, (title, key) in enumerate([("一级告警", "first"), ("二级告警", "second"), ("三级保护", "third"), ("系统监控信息", "monitor")], start=2):
            box = ttk.LabelFrame(right, text=title)
            box.grid(row=row, column=0, sticky="nsew", pady=(8, 0))
            box.columnconfigure(0, weight=1)
            ttk.Label(box, textvariable=self.fault_vars[key], justify=tk.LEFT, wraplength=360).grid(
                row=0, column=0, sticky="nw", padx=8, pady=8
            )

        product_box = ttk.LabelFrame(tab, text="BMS 版本/序列号")
        product_box.grid(row=1, column=0, columnspan=3, sticky="ew", pady=(8, 0))
        product_box.columnconfigure(0, weight=1)
        ttk.Label(product_box, textvariable=self.product_info_var, justify=tk.LEFT).grid(
            row=0, column=0, sticky="w", padx=10, pady=8
        )

    def _build_connection_panel(self, parent: ttk.LabelFrame) -> None:
        parent.columnconfigure(1, weight=1)
        ttk.Label(parent, text="通信方式").grid(row=0, column=0, padx=(10, 6), pady=(8, 4), sticky="w")
        self.connection_mode_combo = ttk.Combobox(
            parent,
            textvariable=self.connection_mode_var,
            values=list(COMM_MODE_LABELS.values()),
            width=14,
            state="readonly",
        )
        self.connection_mode_combo.grid(row=0, column=1, sticky="w", pady=(8, 4))
        self.connection_mode_combo.bind("<<ComboboxSelected>>", self._on_connection_mode_changed)
        ttk.Label(parent, text="Modbus地址").grid(row=0, column=2, padx=6, pady=(8, 4), sticky="w")
        ttk.Entry(parent, textvariable=self.modbus_slave_var, width=6).grid(row=0, column=3, sticky="w", pady=(8, 4))
        ttk.Label(parent, text="串口").grid(row=1, column=0, padx=(10, 6), pady=(4, 4), sticky="w")
        self.port_combo = ttk.Combobox(parent, textvariable=self.port_var, width=12)
        self.port_combo.grid(row=1, column=1, sticky="w", pady=(4, 4))
        ttk.Button(parent, text="刷新", command=self._refresh_ports).grid(row=1, column=2, padx=6, pady=(4, 4))
        ttk.Label(parent, text="BMS/串口2波特率").grid(row=2, column=0, padx=(10, 6), pady=(4, 8), sticky="w")
        ttk.Combobox(parent, textvariable=self.baud_var, values=BMS_UART_BAUD_OPTIONS, width=10).grid(
            row=2, column=1, sticky="w", pady=(4, 8)
        )
        ttk.Button(parent, textvariable=self.live_button_var, command=self._toggle_live_monitor).grid(
            row=2, column=2, padx=6, pady=(4, 8)
        )
        ttk.Label(parent, text="间隔(s)").grid(row=2, column=3, sticky="w", padx=(0, 4), pady=(4, 8))
        ttk.Entry(parent, textvariable=self.live_interval_var, width=5).grid(row=2, column=4, sticky="w", padx=(0, 10), pady=(4, 8))
        ttk.Label(parent, text="记录间隔(s)").grid(row=3, column=0, padx=(10, 6), pady=(0, 4), sticky="w")
        ttk.Entry(parent, textvariable=self.log_interval_var, width=10).grid(row=3, column=1, sticky="w", pady=(0, 4))
        ttk.Button(parent, text="选择文件", command=self._choose_log_file).grid(row=3, column=2, padx=6, pady=(0, 4))
        ttk.Button(parent, textvariable=self.log_button_var, command=self._toggle_data_log).grid(
            row=3, column=3, padx=(0, 10), pady=(0, 4)
        )
        ttk.Label(parent, textvariable=self.log_status_var).grid(
            row=4, column=0, columnspan=5, sticky="w", padx=10, pady=(0, 8)
        )
        ttk.Button(parent, text="一线通监控", command=self._open_owc_monitor).grid(
            row=5, column=0, columnspan=2, sticky="w", padx=10, pady=(0, 8)
        )

    def _open_owc_monitor(self) -> None:
        if self.owc_window and self.owc_window.winfo_exists():
            self.owc_window.lift()
            return
        port = self.port_var.get().strip()
        if not port:
            messagebox.showerror("一线通监控", "未选择串口")
            return
        self.owc_window = OwcMonitorWindow(self, port)

    def _build_data_tab(self, tab: ttk.Frame) -> None:
        tab.columnconfigure(0, weight=1)
        tab.rowconfigure(1, weight=1)
        top = ttk.Frame(tab)
        top.grid(row=0, column=0, sticky="ew", pady=(0, 6))
        ttk.Button(top, text="读取一次", command=self._read_bms_overview).grid(row=0, column=0, padx=(0, 8))
        ttk.Button(top, textvariable=self.live_button_var, command=self._toggle_live_monitor).grid(row=0, column=1, padx=(0, 8))
        ttk.Label(top, textvariable=self.bms_info_var).grid(row=0, column=2, sticky="w")

        self.data_tree = ttk.Treeview(tab, columns=("name", "value", "unit"), show="headings")
        self.data_tree.heading("name", text="项目")
        self.data_tree.heading("value", text="数值")
        self.data_tree.heading("unit", text="单位")
        self.data_tree.column("name", width=220, anchor=tk.W)
        self.data_tree.column("value", width=140, anchor=tk.CENTER)
        self.data_tree.column("unit", width=80, anchor=tk.CENTER)
        self.data_tree.grid(row=1, column=0, sticky="nsew")
        scroll = ttk.Scrollbar(tab, orient=tk.VERTICAL, command=self.data_tree.yview)
        scroll.grid(row=1, column=1, sticky="ns")
        self.data_tree.configure(yscrollcommand=scroll.set)

    def _build_storage_tab(self, tab: ttk.Frame) -> None:
        tab.columnconfigure(0, weight=1)
        tab.rowconfigure(1, weight=1)
        actions = ttk.Frame(tab)
        actions.grid(row=0, column=0, sticky="ew", pady=(0, 6))
        ttk.Button(actions, text="读取BMS日志", command=self._read_bms_log).grid(row=0, column=0, padx=(0, 8))
        ttk.Button(actions, text="重置100条日志", command=self._reset_bms_log).grid(row=0, column=1, padx=(0, 8))
        ttk.Label(actions, text="显示板端存储的事件记录").grid(row=0, column=2, sticky="w")

        self.log_tree = ttk.Treeview(tab, columns=("index", "event", "interval", "raw"), show="headings")
        for col, title, width in [
            ("index", "序号", 70),
            ("event", "事件", 260),
            ("interval", "距离上次", 120),
            ("raw", "原始值", 120),
        ]:
            self.log_tree.heading(col, text=title)
            self.log_tree.column(col, width=width, anchor=tk.CENTER if col != "event" else tk.W)
        self.log_tree.grid(row=1, column=0, sticky="nsew")
        scroll = ttk.Scrollbar(tab, orient=tk.VERTICAL, command=self.log_tree.yview)
        scroll.grid(row=1, column=1, sticky="ns")
        self.log_tree.configure(yscrollcommand=scroll.set)

    def _build_params_tab(self, tab: ttk.Frame) -> None:
        tab.columnconfigure(0, weight=1)
        tab.rowconfigure(1, weight=1)
        actions = ttk.Frame(tab)
        actions.grid(row=0, column=0, sticky="ew", pady=(0, 6))
        actions.columnconfigure(5, weight=1)
        ttk.Label(actions, text="参数配置", style="ParameterHeader.TLabel").grid(row=0, column=0, padx=(2, 18))
        ttk.Button(actions, text="读取当前页", command=self._read_current_param_page).grid(row=0, column=1, padx=(0, 8))
        ttk.Button(actions, text="全部读取", command=self._read_all_params).grid(row=0, column=2, padx=(0, 16))
        ttk.Button(actions, textvariable=self.param_write_button_var, command=self._write_dirty_params).grid(
            row=0, column=3, padx=(0, 8)
        )
        ttk.Button(actions, text="放弃修改", command=self._clear_param_dirty).grid(row=0, column=4, padx=(0, 12))
        ttk.Label(actions, textvariable=self.param_dirty_var, foreground="#9a4d00").grid(row=0, column=5, sticky="w")

        self.param_tabs = ttk.Notebook(tab)
        self.param_tabs.grid(row=1, column=0, sticky="nsew")
        self.param_page_ranges: dict[str, str] = {}
        page_specs = [
            ("保护参数", "protect", self._build_protect_param_page),
            ("均衡/限流", "other", self._build_balance_limit_param_page),
            ("休眠", "other", self._build_sleep_param_page),
            ("SOC/系统", "other", self._build_soc_system_param_page),
        ]
        for title, range_name, builder in page_specs:
            page = ttk.Frame(self.param_tabs)
            self.param_tabs.add(page, text=title)
            self.param_page_ranges[str(page)] = range_name
            builder(self._make_scroll_area(page))

    def _build_system_tab(self, tab: ttk.Frame) -> None:
        tab.columnconfigure(0, weight=1)
        tab.columnconfigure(1, weight=1)
        tab.rowconfigure(1, weight=1)
        ttk.Button(tab, text="读取BMS信息", command=self._read_bms_overview).grid(row=0, column=0, sticky="w", pady=(0, 8))
        ttk.Button(tab, text="CAN诊断", command=self._can_diag).grid(row=0, column=1, sticky="w", pady=(0, 8))
        cards = ttk.Frame(tab)
        cards.grid(row=1, column=0, columnspan=2, sticky="nsew")
        cards.columnconfigure(0, weight=1)
        cards.columnconfigure(1, weight=1)
        self._status_card(cards, "comm tool", self.info_var, 0)
        self._status_card(cards, "comm tool 缓存", self.cache_var, 1)

    def _build_other_tab(self, tab: ttk.Frame) -> None:
        tab.columnconfigure(0, weight=1)
        tab.rowconfigure(6, weight=1)

        file_box = ttk.LabelFrame(tab, text="升级文件")
        file_box.grid(row=0, column=0, sticky="ew", pady=(0, 8))
        file_box.columnconfigure(1, weight=1)
        ttk.Label(file_box, text="BMS App bin").grid(row=0, column=0, padx=(10, 6), pady=8)
        ttk.Entry(file_box, textvariable=self.bin_var).grid(row=0, column=1, sticky="ew", pady=8)
        ttk.Button(file_box, text="选择", command=self._choose_bin).grid(row=0, column=2, padx=6, pady=8)
        ttk.Button(file_box, text="校验文件", command=self._describe_selected_bin).grid(row=0, column=3, padx=(0, 10), pady=8)

        status = ttk.Frame(tab)
        status.grid(row=1, column=0, sticky="ew", pady=(0, 8))
        status.columnconfigure(0, weight=1)
        status.columnconfigure(1, weight=1)
        status.columnconfigure(2, weight=1)
        self._status_card(status, "comm tool", self.info_var, 0)
        self._status_card(status, "当前文件", self.image_var, 1)
        self._status_card(status, "comm tool 缓存", self.cache_var, 2)

        target = ttk.LabelFrame(tab, text="目标设备")
        target.grid(row=2, column=0, sticky="ew", pady=(0, 8))
        target.columnconfigure(8, weight=1)
        ttk.Label(target, text="CAN波特率").grid(row=0, column=0, padx=(10, 6), pady=8)
        ttk.Combobox(
            target,
            textvariable=self.can_bitrate_var,
            values=("250000", "125000", "500000"),
            width=10,
            state="normal",
        ).grid(row=0, column=1, sticky="w", pady=8)
        ttk.Label(target, text="BMS地址").grid(row=0, column=2, padx=(14, 6), pady=8)
        ttk.Entry(target, textvariable=self.app_can_addr_var, width=8).grid(row=0, column=3, sticky="w", pady=8)
        ttk.Label(target, text="IAP节点").grid(row=0, column=4, padx=(14, 6), pady=8)
        ttk.Entry(target, textvariable=self.node_id_var, width=8).grid(row=0, column=5, sticky="w", pady=8)
        ttk.Button(target, text="应用设置", command=self._apply_can_settings).grid(row=0, column=6, padx=(14, 8), pady=8)
        ttk.Label(
            target,
            text="多设备时必须保证目标 BMS 地址唯一；多个 BMS 同地址不要同时升级。",
            foreground="#9a4d00",
        ).grid(row=0, column=7, columnspan=2, sticky="w", padx=(6, 10), pady=8)

        common = ttk.LabelFrame(tab, text="常用功能")
        common.grid(row=3, column=0, sticky="ew", pady=(0, 8))
        common.columnconfigure(9, weight=1)
        ttk.Label(common, text="SOC(%)").grid(row=0, column=0, padx=(10, 6), pady=8)
        ttk.Entry(common, textvariable=self.bms_soc_write_var, width=8).grid(row=0, column=1, sticky="w", pady=8)
        ttk.Button(common, text="写SOC", command=self._write_bms_soc).grid(row=0, column=2, padx=(8, 16), pady=8)
        ttk.Button(common, text="开启老化模式", command=self._aging_start).grid(row=0, column=3, padx=(0, 8), pady=8)
        ttk.Button(common, text="关闭老化模式", command=self._aging_stop).grid(row=0, column=4, padx=(0, 8), pady=8)
        ttk.Button(common, text="重置老化时间", command=self._aging_reset_time).grid(row=0, column=5, padx=(0, 12), pady=8)
        ttk.Button(common, text="读取老化时间", command=self._read_aging_status).grid(row=0, column=6, padx=(0, 12), pady=8)
        ttk.Label(common, text="老化时长(h)").grid(row=1, column=0, padx=(10, 6), pady=(0, 8))
        ttk.Entry(common, textvariable=self.bms_aging_hours_var, width=8).grid(row=1, column=1, sticky="w", pady=(0, 8))
        ttk.Button(common, text="修改老化时间", command=self._aging_set_hours).grid(row=1, column=2, padx=(8, 16), pady=(0, 8))
        ttk.Label(common, textvariable=self.bms_aging_var, foreground="#004b8d").grid(
            row=1, column=3, columnspan=7, sticky="w", padx=(0, 10), pady=(0, 8)
        )

        actions = ttk.Frame(tab)
        actions.grid(row=4, column=0, sticky="ew", pady=(0, 8))
        ttk.Button(actions, text="读取缓存", command=self._read_cache).grid(row=0, column=0, padx=(0, 8))
        ttk.Button(actions, text="写入缓存", command=self._download_only).grid(row=0, column=1, padx=(0, 8))
        ttk.Button(actions, text="一键升级", command=self._upgrade_selected).grid(row=0, column=2, padx=(0, 8))
        ttk.Button(actions, text="使用缓存升级", command=self._upgrade_cached_selected).grid(row=0, column=3, padx=(0, 8))
        ttk.Button(actions, text="读取BMS状态", command=self._read_bms_status).grid(row=0, column=4, padx=(0, 8))
        ttk.Button(actions, text="CAN诊断", command=self._can_diag).grid(row=0, column=5, padx=(0, 8))
        ttk.Button(actions, text="进入IAP", command=self._enter_iap).grid(row=0, column=6, padx=(0, 8))

        advanced = ttk.LabelFrame(tab, text="高级寄存器")
        advanced.grid(row=5, column=0, sticky="ew", pady=(0, 8))
        advanced.columnconfigure(6, weight=1)
        ttk.Label(advanced, text="地址").grid(row=0, column=0, padx=(10, 6), pady=8)
        ttk.Entry(advanced, textvariable=self.bms_addr_var, width=12).grid(row=0, column=1, sticky="w", pady=8)
        ttk.Label(advanced, text="数量").grid(row=0, column=2, padx=(12, 6), pady=8)
        ttk.Entry(advanced, textvariable=self.bms_count_var, width=8).grid(row=0, column=3, sticky="w", pady=8)
        ttk.Button(advanced, text="读取", command=self._read_bms_regs).grid(row=0, column=4, padx=(12, 8), pady=8)
        ttk.Label(advanced, text="写入值").grid(row=0, column=5, padx=(12, 6), pady=8)
        ttk.Entry(advanced, textvariable=self.bms_values_var).grid(row=0, column=6, sticky="ew", pady=8)
        ttk.Button(advanced, text="写入", command=self._write_bms_regs).grid(row=0, column=7, padx=(8, 10), pady=8)
        ttk.Label(advanced, textvariable=self.bms_result_var, justify=tk.LEFT).grid(
            row=1, column=0, columnspan=8, sticky="ew", padx=10, pady=(0, 8)
        )

        progress = ttk.Frame(tab)
        progress.grid(row=6, column=0, sticky="nsew")
        progress.columnconfigure(0, weight=1)
        progress.rowconfigure(3, weight=1)
        ttk.Progressbar(progress, variable=self.progress_var, maximum=100).grid(row=0, column=0, sticky="ew", pady=(0, 6))
        ttk.Label(progress, textvariable=self.upgrade_stage_var).grid(row=1, column=0, sticky="w", pady=(0, 4))
        ttk.Label(progress, text="运行日志").grid(row=2, column=0, sticky="w")
        self.log_text = tk.Text(progress, height=16, wrap="word")
        self.log_text.grid(row=3, column=0, sticky="nsew")
        self.log_text.configure(state="disabled")
        scrollbar = ttk.Scrollbar(progress, orient=tk.VERTICAL, command=self.log_text.yview)
        scrollbar.grid(row=3, column=1, sticky="ns")
        self.log_text.configure(yscrollcommand=scrollbar.set)

    def _make_scroll_area(self, parent: ttk.Frame) -> ttk.Frame:
        parent.columnconfigure(0, weight=1)
        parent.rowconfigure(0, weight=1)
        canvas = tk.Canvas(parent, highlightthickness=0)
        scrollbar = ttk.Scrollbar(parent, orient=tk.VERTICAL, command=canvas.yview)
        inner = ttk.Frame(canvas)
        window_id = canvas.create_window((0, 0), window=inner, anchor="nw")

        def _on_configure(_event=None) -> None:
            canvas.configure(scrollregion=canvas.bbox("all"))
            canvas.itemconfigure(window_id, width=canvas.winfo_width())

        inner.bind("<Configure>", _on_configure)
        canvas.bind("<Configure>", _on_configure)
        canvas.configure(yscrollcommand=scrollbar.set)
        canvas.grid(row=0, column=0, sticky="nsew")
        scrollbar.grid(row=0, column=1, sticky="ns")
        return inner

    def _build_param_groups(self, parent: ttk.Frame, params: list[BmsParamDef], columns: int) -> None:
        grouped: dict[str, list[BmsParamDef]] = {}
        for param in params:
            grouped.setdefault(param.group, []).append(param)
        for col in range(columns):
            parent.columnconfigure(col, weight=1)
        for index, (group, group_params) in enumerate(grouped.items()):
            frame = ttk.LabelFrame(parent, text=group)
            frame.grid(row=index // columns, column=index % columns, sticky="nsew", padx=6, pady=6)
            frame.columnconfigure(1, weight=1)
            for row, param in enumerate(group_params):
                ttk.Label(frame, text=param.name).grid(row=row, column=0, sticky="w", padx=(8, 6), pady=3)
                var = self.param_entry_vars.get(param.key)
                if var is None:
                    var = tk.StringVar(value="")
                    var.trace_add("write", lambda *_args, key=param.key: self._mark_param_dirty(key))
                    self.param_entry_vars[param.key] = var
                widget = ttk.Entry(frame, textvariable=var, width=12)
                self.param_widgets[param.key] = widget
                widget.grid(row=row, column=1, sticky="ew", pady=3)
                ttk.Label(frame, text=param.unit, width=5).grid(row=row, column=2, sticky="w", padx=(4, 8), pady=3)

    def _build_param_matrix(
        self,
        parent: ttk.Frame,
        title: str,
        headers: tuple[str, ...],
        rows: tuple[tuple[str, tuple[str, ...]], ...],
        grid_row: int = 0,
        grid_column: int = 0,
    ) -> None:
        parent.columnconfigure(grid_column, weight=1)
        frame = ttk.LabelFrame(parent, text=title)
        frame.grid(row=grid_row, column=grid_column, sticky="nsew", padx=6, pady=6)
        frame.columnconfigure(0, minsize=130)
        ttk.Label(frame, text="参数项", style="ParameterHeader.TLabel").grid(
            row=0, column=0, sticky="w", padx=(12, 8), pady=(8, 6)
        )
        for column, header in enumerate(headers, start=1):
            frame.columnconfigure(column, weight=1, minsize=145)
            ttk.Label(frame, text=header, style="ParameterHeader.TLabel", anchor="center").grid(
                row=0, column=column, sticky="ew", padx=6, pady=(8, 6)
            )
        for row_index, (label, keys) in enumerate(rows, start=1):
            ttk.Label(frame, text=label).grid(row=row_index, column=0, sticky="w", padx=(12, 8), pady=7)
            for column, key in enumerate(keys, start=1):
                self._add_param_matrix_cell(frame, row_index, column, key)

    def _add_param_matrix_cell(self, parent: ttk.Frame, row: int, column: int, key: str) -> None:
        param = BMS_PARAM_BY_KEY[key]
        var = self.param_entry_vars.get(key)
        if var is None:
            var = tk.StringVar(value="")
            var.trace_add("write", lambda *_args, item_key=key: self._mark_param_dirty(item_key))
            self.param_entry_vars[key] = var
        cell = ttk.Frame(parent)
        cell.grid(row=row, column=column, sticky="ew", padx=6, pady=4)
        cell.columnconfigure(0, weight=1)
        choices = self._sh309_field_choices(key)
        if choices:
            widget = ttk.Combobox(cell, textvariable=var, values=choices, width=12, state="readonly")
        else:
            widget = ttk.Entry(cell, textvariable=var, width=12)
        self.param_widgets[key] = widget
        widget.grid(row=0, column=0, sticky="ew")
        ttk.Label(cell, text=param.unit, width=5).grid(row=0, column=1, sticky="w", padx=(5, 0))
        ttk.Label(cell, text=f"0x{param.addr:04X}", style="Small.TLabel").grid(row=1, column=0, sticky="w", pady=(2, 0))

    def _build_param_form(
        self,
        parent: ttk.Frame,
        title: str,
        fields: tuple[tuple[str, str], ...],
        grid_row: int,
        columns: int = 2,
    ) -> None:
        frame = ttk.LabelFrame(parent, text=title)
        frame.grid(row=grid_row, column=0, sticky="ew", padx=6, pady=6)
        for column in range(columns):
            frame.columnconfigure(column * 2 + 1, weight=1)
        for index, (label, key) in enumerate(fields):
            row = index // columns
            column = (index % columns) * 2
            ttk.Label(frame, text=label).grid(row=row, column=column, sticky="w", padx=(10, 6), pady=6)
            self._add_param_matrix_cell(frame, row, column + 1, key)

    def _build_protect_param_page(self, parent: ttk.Frame) -> None:
        parent.columnconfigure(0, weight=1)
        parent.columnconfigure(1, weight=1)
        parent.rowconfigure(1, weight=1)
        self._build_param_matrix(
            parent,
            "SH309 电压保护",
            ("保护值", "恢复值", "延时"),
            SH309_VOLTAGE_PARAM_ROWS,
            grid_row=0,
            grid_column=0,
        )
        self._build_param_matrix(
            parent,
            "SH309 充放电过流",
            ("保护值", "延时"),
            SH309_CURRENT_PARAM_ROWS,
            grid_row=0,
            grid_column=1,
        )
        self._build_param_matrix(
            parent,
            "SH309 充放电温度",
            ("保护值", "恢复值"),
            SH309_TEMPERATURE_PARAM_ROWS,
            grid_row=1,
            grid_column=0,
        )

        right = ttk.Frame(parent)
        right.grid(row=1, column=1, sticky="nsew")
        right.columnconfigure(0, weight=1)
        self._build_param_form(
            right,
            "MOS 温度保护",
            (
                ("一级", "SH309/MOS过温1(℃)"),
                ("二级", "SH309/MOS过温2(℃)"),
                ("三级", "SH309/MOS过温3(℃)"),
                ("恢复", "SH309/MOS恢复(℃)"),
                ("延时", "SH309/延时(10ms)"),
            ),
            grid_row=0,
        )
        self._build_param_form(
            right,
            "AFE 短路保护",
            (("短路电流", "SH309/短路电流(A)"), ("短路延时", "SH309/短路延时(us)")),
            grid_row=1,
        )
        actions = ttk.Frame(right)
        actions.grid(row=2, column=0, sticky="ew", padx=6, pady=(4, 10))
        ttk.Button(actions, text="MOS过温重置", command=self._reset_sh309_tmos_defaults).grid(row=0, column=0, padx=(0, 8))
        ttk.Button(actions, text="保护点重置", command=self._reset_sh309_protect_params).grid(row=0, column=1)

    def _build_balance_limit_param_page(self, parent: ttk.Frame) -> None:
        groups = {"均衡参数", "短路参数", "限流参数"}
        self._build_param_groups(parent, [param for param in BMS_PARAM_DEFS if param.group in groups], columns=3)

    def _build_sleep_param_page(self, parent: ttk.Frame) -> None:
        sleep_rows = (
            ("正常休眠", ("休眠参数/正常休眠电压", "休眠参数/正常休眠时间")),
            ("过放休眠", ("休眠参数/过放休眠电压", "休眠参数/过放休眠时间")),
        )
        filter_rows = (("电流过滤", ("休眠参数/充电电流过滤", "休眠参数/放电电流过滤")),)
        rtc_rows = (("RTC调度", ("休眠参数/RTC唤醒时间", "休眠参数/RTC休眠时间")),)
        self._build_param_matrix(parent, "休眠条件", ("电压", "时间"), sleep_rows)
        self._build_param_matrix(parent, "休眠电流过滤", ("充电电流", "放电电流"), filter_rows, grid_row=1)
        self._build_param_matrix(parent, "RTC 时间", ("唤醒时间", "休眠时间"), rtc_rows, grid_row=2)

    def _build_soc_system_param_page(self, parent: ttk.Frame) -> None:
        groups = {"SOC参数", "系统参数"}
        self._build_param_groups(parent, [param for param in BMS_PARAM_DEFS if param.group in groups], columns=2)

    def _sh309_field_choices(self, key: str) -> list[str]:
        if key in {"SH309/单节过压(mv)", "SH309/过压恢复(mv)"}:
            return [str(value) for value in range(1000, 5001, 5)]
        if key in {"SH309/单节低压(mv)", "SH309/低压恢复(mv)"}:
            return [str(value) for value in range(1000, 5001, 20)]
        if key in {"SH309/过压延时(ms)", "SH309/低压延时(ms)"}:
            return [str(value) for value in SH309_OV_UV_DELAY_MS]
        if key in {"SH309/二级充电过流延时(ms)"}:
            return [str(value) for value in SH309_CHG_SECOND_DELAY_MS]
        if key in {"SH309/二级放电过流延时(ms)"}:
            return [str(value) for value in SH309_DSG_SECOND_DELAY_MS]
        if key in SH309_SECOND_CURRENT_KEYS:
            return self._sh309_current_choices(SH309_AFE_OCD1V_OCCV_MV)
        if key == SH309_SHORT_CURRENT_KEY:
            return self._sh309_current_choices(SH309_AFE_SCV_MV)
        if key == "SH309/短路延时(us)":
            return [str(value) for value in SH309_SHORT_DELAY_US]
        if key in SH309_HIGH_TEMP_KEYS:
            return [str(value) for value in range(40, 81)]
        if key == SH309_LOW_CHG_TEMP_KEY:
            return [str(value) for value in range(-20, 11)]
        if key == SH309_LOW_CHG_TEMP_RCV_KEY:
            return [str(value) for value in range(-20, 16)]
        if key == SH309_LOW_DSG_TEMP_KEY:
            return [str(value) for value in range(-40, 11)]
        if key == SH309_LOW_DSG_TEMP_RCV_KEY:
            return [str(value) for value in range(-40, 16)]
        return []

    def _sh309_cs_factor(self) -> float:
        return self._sh309_cs_factor_from_values(self.param_values)

    def _sh309_cs_factor_from_values(self, values: dict[str, int]) -> float:
        cs_res = values.get(SH309_SYS_CS_RES_KEY, SH309_DEFAULT_CS_RES_MOHM)
        cs_num = values.get(SH309_SYS_CS_RES_NUM_KEY, SH309_DEFAULT_CS_RES_NUM)
        if cs_res <= 0 or cs_num <= 0:
            cs_res = SH309_DEFAULT_CS_RES_MOHM
            cs_num = SH309_DEFAULT_CS_RES_NUM
        return float(cs_num) * 1000.0 / float(cs_res)

    def _sh309_current_choices(self, threshold_mv: list[int]) -> list[str]:
        factor = self._sh309_cs_factor()
        values: list[int] = []
        for mv in threshold_mv:
            current = int(mv * factor / 1000.0)
            if current not in values:
                values.append(current)
        return [str(value) for value in values]

    def _refresh_sh309_dynamic_choices(self) -> None:
        for key in SH309_SECOND_CURRENT_KEYS | {SH309_SHORT_CURRENT_KEY}:
            widget = self.param_widgets.get(key)
            if isinstance(widget, ttk.Combobox):
                widget.configure(values=self._sh309_field_choices(key))

    def _mark_param_dirty(self, key: str) -> None:
        if self.param_loading:
            return
        if key in BMS_PARAM_BY_KEY:
            self.param_dirty.add(key)
            self._set_param_dirty_style(key, True)
            self._update_param_dirty_label()

    def _set_param_dirty_style(self, key: str, dirty: bool) -> None:
        widget = self.param_widgets.get(key)
        if isinstance(widget, ttk.Combobox):
            widget.configure(style="Dirty.TCombobox" if dirty else "TCombobox")
        elif isinstance(widget, ttk.Entry):
            widget.configure(style="Dirty.TEntry" if dirty else "TEntry")

    def _update_param_dirty_label(self) -> None:
        if not self.param_dirty:
            self.param_dirty_var.set("未修改")
            self.param_write_button_var.set("写入修改")
            return
        self.param_dirty_var.set(f"已修改 {len(self.param_dirty)} 项，未写入")
        self.param_write_button_var.set(f"写入修改({len(self.param_dirty)})")

    def _clear_param_dirty(self) -> None:
        dirty_keys = list(self.param_dirty)
        self.param_loading = True
        try:
            for key in dirty_keys:
                var = self.param_entry_vars.get(key)
                if var is None:
                    continue
                raw = self.param_values.get(key)
                var.set(_param_display_value(BMS_PARAM_BY_KEY[key], raw) if raw is not None else "")
        finally:
            self.param_loading = False
        for key in dirty_keys:
            self._set_param_dirty_style(key, False)
        self.param_dirty.clear()
        self._update_param_dirty_label()
        if dirty_keys:
            self.param_current_var.set(f"已放弃 {len(dirty_keys)} 项未写入修改")

    def _validate_sh309_param_values(self, changed: dict[str, int]) -> None:
        if not changed:
            return
        needs_afe = any(key in SH309_AFE_PARAM_KEYS for key in changed)
        needs_tmos = any(key in SH309_TMOS_PARAM_KEYS for key in changed)
        if not (needs_afe or needs_tmos):
            return
        if needs_afe and ({SH309_SYS_CS_RES_KEY, SH309_SYS_CS_RES_NUM_KEY} & set(changed)):
            raise ValueError("采样电阻/采样电阻数不能和 SH309 AFE 过流、短路参数同批写入；请先写系统参数，再重新读取保护参数")

        merged = dict(self.param_values)
        merged.update(changed)
        missing: list[str] = []
        if needs_afe:
            missing.extend(key for key in SH309_AFE_PARAM_KEYS if key not in merged)
            if SH309_SYS_CS_RES_KEY not in merged:
                missing.append(SH309_SYS_CS_RES_KEY)
            if SH309_SYS_CS_RES_NUM_KEY not in merged:
                missing.append(SH309_SYS_CS_RES_NUM_KEY)
        if needs_tmos:
            missing.extend(key for key in SH309_TMOS_PARAM_KEYS if key not in merged)
        if missing:
            raise ValueError("请先点击“读取”读取完整 SH309 参数后再写入，缺少: " + "、".join(sorted(set(missing))[:6]))

        checks = [
            ("单节过压必须大于等于过压恢复", "SH309/单节过压(mv)", "SH309/过压恢复(mv)", ">="),
            ("单节低压必须小于等于低压恢复", "SH309/单节低压(mv)", "SH309/低压恢复(mv)", "<="),
            ("充电高温必须大于等于充电高温恢复", "SH309/充电高温(℃)", "SH309/充电高温恢复(℃)", ">="),
            ("充电低温必须小于等于充电低温恢复", "SH309/充电低温(℃)", "SH309/充电低温恢复(℃)", "<="),
            ("放电高温必须大于等于放电高温恢复", "SH309/放电高温(℃)", "SH309/放电高温恢复(℃)", ">="),
            ("放电低温必须小于等于放电低温恢复", "SH309/放电低温(℃)", "SH309/放电低温恢复(℃)", "<="),
        ]
        for message, left, right, op in checks:
            if op == ">=" and merged[left] < merged[right]:
                raise ValueError(message)
            if op == "<=" and merged[left] > merged[right]:
                raise ValueError(message)

        factor = self._sh309_cs_factor_from_values(merged)
        second_raw_choices = {int(mv * factor / 1000.0) * 10 for mv in SH309_AFE_OCD1V_OCCV_MV}
        short_raw_choices = {int(mv * factor / 1000.0) for mv in SH309_AFE_SCV_MV}
        for key in SH309_SECOND_CURRENT_KEYS:
            if key in changed and changed[key] not in second_raw_choices:
                param = BMS_PARAM_BY_KEY[key]
                raise ValueError(f"{param.name} 必须从当前采样电阻换算列表中选择")
        if SH309_SHORT_CURRENT_KEY in changed and changed[SH309_SHORT_CURRENT_KEY] not in short_raw_choices:
            raise ValueError("短路电流必须从当前采样电阻换算列表中选择")

    def _write_dirty_params(self) -> None:
        if not self.param_dirty:
            messagebox.showinfo("参数设置", "没有检测到已修改的参数。")
            return
        try:
            values: dict[str, int] = {}
            for key in sorted(self.param_dirty, key=lambda item: BMS_PARAM_BY_KEY[item].addr):
                param = BMS_PARAM_BY_KEY[key]
                var = self.param_entry_vars.get(key)
                if var is None:
                    continue
                values[key] = _param_parse_display_value(param, var.get())
            self._validate_sh309_param_values(values)
        except Exception as exc:
            messagebox.showerror("参数错误", str(exc))
            return
        preview = "\n".join(
            f"{BMS_PARAM_BY_KEY[key].group}/{BMS_PARAM_BY_KEY[key].name}: "
            f"{self.param_entry_vars[key].get().strip()} {BMS_PARAM_BY_KEY[key].unit}"
            for key in list(values)[:8]
        )
        if len(values) > 8:
            preview += f"\n... 共 {len(values)} 项"
        if not messagebox.askyesno("确认写入参数", f"确认写入以下已修改参数？\n\n{preview}"):
            return
        self.active_param_values = values
        self._run_worker("写入修改参数", self._worker_write_dirty_params)

    def _reset_sh309_tmos_defaults(self) -> None:
        if not messagebox.askyesno(
            "确认重置MOS过温",
            "将把 MOS 过温1/2/3、MOS恢复和延时写回当前项目默认值。\n\n确认继续？",
        ):
            return
        values: dict[str, int] = {}
        for key, display in SH309_TMOS_DEFAULT_DISPLAY.items():
            param = BMS_PARAM_BY_KEY[key]
            values[key] = _param_parse_display_value(param, display)
            var = self.param_entry_vars.get(key)
            if var is not None:
                var.set(display)
        self.active_param_values = values
        self._run_worker("MOS过温重置", self._worker_write_dirty_params)

    def _reset_sh309_protect_params(self) -> None:
        if not messagebox.askyesno(
            "确认保护点重置",
            "将同时执行 AFE 参数重置(0x1006)和保护参数重置(0x1002)，然后重新读取 SH309 参数。\n\n确认继续？",
        ):
            return
        self._run_worker("保护点重置", self._worker_reset_sh309_protect_params)

    def _toggle_live_monitor(self) -> None:
        if self.live_running:
            self._stop_live_monitor()
        else:
            try:
                self._capture_runtime_settings(include_bin=False)
            except Exception as exc:
                messagebox.showerror("参数错误", str(exc))
                return
            self.live_running = True
            self.live_button_var.set("停止监控")
            self.comm_state_var.set("通信: 监控中")
            self._schedule_live_monitor(0)

    def _stop_live_monitor(self) -> None:
        self.live_running = False
        self.live_button_var.set("开始监控")
        if self.log_running:
            self._stop_data_log("监控停止，日志记录已停止")
        if self.live_after_id is not None:
            try:
                self.after_cancel(self.live_after_id)
            except tk.TclError:
                pass
            self.live_after_id = None
        self.live_snapshot_cache = _zero_live_words()
        self._clear_main_live_display("已停止")
        self.comm_state_var.set("通信: 已停止")
        if self.monitor_window is not None and self.monitor_window.winfo_exists():
            self.monitor_window._clear_snapshot()

    def _schedule_live_monitor(self, delay_ms: int | None = None) -> None:
        if not self.live_running:
            return
        if self.live_after_id is not None:
            return
        self.live_after_id = self.after(self._live_interval_ms() if delay_ms is None else delay_ms, self._poll_live_monitor)

    def _live_interval_ms(self) -> int:
        try:
            seconds = float(self.live_interval_var.get())
        except ValueError:
            seconds = 2.0
        seconds = max(1.0, min(seconds, 60.0))
        return int(seconds * 1000)

    def _log_interval_seconds(self) -> float:
        try:
            seconds = float(self.log_interval_var.get())
        except ValueError as exc:
            raise RuntimeError("记录间隔必须是数字") from exc
        if seconds < LOG_MIN_INTERVAL_SECONDS or seconds > LOG_MAX_INTERVAL_SECONDS:
            raise RuntimeError("记录间隔范围为 2 - 3600 秒")
        return seconds

    def _default_log_path(self) -> Path:
        return DEFAULT_LOG_DIR / f"BMS_{datetime.now():%Y%m%d_%H%M%S}_log.csv"

    def _choose_log_file(self) -> None:
        initial = Path(self.log_file_var.get()).parent if self.log_file_var.get() else DEFAULT_LOG_DIR
        target = filedialog.asksaveasfilename(
            title="保存长期监控记录",
            initialdir=str(initial),
            initialfile=f"BMS_{datetime.now():%Y%m%d_%H%M%S}_log.csv",
            defaultextension=".csv",
            filetypes=[("Excel CSV", "*.csv"), ("All files", "*.*")],
        )
        if target:
            self.log_file_var.set(str(Path(target).resolve()))

    def _toggle_data_log(self) -> None:
        if self.log_running:
            self._stop_data_log("日志记录已停止")
        else:
            self._start_data_log()

    def _start_data_log(self) -> None:
        try:
            self._capture_runtime_settings(include_bin=False)
            interval = self._log_interval_seconds()
            path = Path(self.log_file_var.get()).resolve() if self.log_file_var.get() else self._default_log_path()
            path.parent.mkdir(parents=True, exist_ok=True)
            write_header = (not path.exists()) or path.stat().st_size == 0
            with path.open("a", encoding="utf-8-sig", newline="") as handle:
                writer = csv.writer(handle)
                if write_header:
                    writer.writerow(self._battery_log_headers())
        except Exception as exc:
            messagebox.showerror("日志记录失败", str(exc))
            return

        self.log_path = path
        self.log_file_var.set(str(path))
        self.log_running = True
        self.log_count = 0
        self.log_started_monotonic = time.monotonic()
        self.log_next_due_monotonic = 0.0
        self.log_button_var.set("停止记录")
        self.log_status_var.set(f"记录: 运行中 0 条  {path.name}")
        self._log(f"长期监控记录开始: {path}")

        try:
            live_interval = float(self.live_interval_var.get())
        except ValueError:
            live_interval = interval
        if live_interval > interval:
            self.live_interval_var.set(f"{interval:g}")
        if not self.live_running:
            self.live_running = True
            self.live_button_var.set("停止监控")
            self.comm_state_var.set("通信: 监控中")
            self._schedule_live_monitor(0)
        else:
            if self.live_after_id is not None:
                try:
                    self.after_cancel(self.live_after_id)
                except tk.TclError:
                    pass
                self.live_after_id = None
            self._schedule_live_monitor(0)

    def _stop_data_log(self, message: str) -> None:
        self.log_running = False
        self.log_next_due_monotonic = 0.0
        self.log_button_var.set("开始记录")
        name = self.log_path.name if self.log_path is not None else "--"
        self.log_status_var.set(f"记录: 已停止 {self.log_count} 条  {name}")
        self._log(message)

    def _prepare_exclusive_upgrade(self) -> None:
        if self.live_running:
            self._stop_live_monitor()
        if self.log_running:
            self._stop_data_log("升级开始，实时监控记录已暂停")

    def _reset_live_capability_cache(self) -> None:
        self.live_word_count = None
        self.live_snapshot_cache = None
        self.live_status_words_cache = None
        self.live_status_retry_due_monotonic = 0.0

    def _reset_product_info_cache(self) -> None:
        self.product_info_cache = None
        self.product_info_cache_target = None
        self.product_info_next_due_monotonic = 0.0

    def _read_live_status_words_optional(self, client) -> list[int] | None:
        now = time.monotonic()
        if self.live_status_retry_due_monotonic != 0.0 and now < self.live_status_retry_due_monotonic:
            self.live_status_words_cache = None
            return None
        try:
            words = self._read_bms_words(client, BMS_LIVE_STATUS_ADDR, BMS_LIVE_STATUS_WORDS)
        except Exception:
            self.live_status_words_cache = None
            self.live_status_retry_due_monotonic = now + BMS_LIVE_STATUS_RETRY_SECONDS
            return None
        self.live_status_words_cache = words
        self.live_status_retry_due_monotonic = 0.0
        return words

    def _read_live_words(self, client) -> list[int]:
        if self.live_word_count == BMS_OVERVIEW_WORDS:
            words = self._read_bms_words(client, BMS_OVERVIEW_ADDR, BMS_OVERVIEW_WORDS)
            self._read_live_status_words_optional(client)
            return words
        if self.live_word_count == BMS_LIVE_WORDS:
            try:
                words = self._read_bms_words(client, BMS_OVERVIEW_ADDR, BMS_LIVE_WORDS)
                self.live_status_words_cache = words[84:88]
                return words
            except Exception:
                words = self._read_bms_words(client, BMS_OVERVIEW_ADDR, BMS_OVERVIEW_WORDS)
                self.live_word_count = BMS_OVERVIEW_WORDS
                self._read_live_status_words_optional(client)
                return words

        try:
            words = self._read_bms_words(client, BMS_OVERVIEW_ADDR, BMS_LIVE_WORDS)
            self.live_word_count = BMS_LIVE_WORDS
            self.live_status_words_cache = words[84:88]
            return words
        except Exception:
            words = self._read_bms_words(client, BMS_OVERVIEW_ADDR, BMS_OVERVIEW_WORDS)
            self.live_word_count = BMS_OVERVIEW_WORDS
            self._read_live_status_words_optional(client)
            return words

    def _connection_cache_key(self) -> tuple[object, ...]:
        if self.active_connection_mode == COMM_MODE_DIRECT_BMS:
            return (
                self.active_connection_mode,
                self.active_port,
                self.active_baud,
                self.active_modbus_slave,
            )
        return (
            self.active_connection_mode,
            self.active_can_bitrate,
            self.active_node_id,
            self.active_app_can_addr,
        )

    def _require_comm_tool_client(self, client, feature: str) -> CommToolClient:
        if isinstance(client, DirectBmsModbusClient):
            raise RuntimeError(f"{feature} 依赖 comm tool/CAN桥模式，请切换通信方式后重试")
        return client

    def _read_bms_product_info_cached(self, client, force: bool = False) -> dict[str, str]:
        target = self._connection_cache_key()
        now = time.monotonic()
        if (
            not force
            and self.product_info_cache is not None
            and self.product_info_cache_target == target
            and now < self.product_info_next_due_monotonic
        ):
            return self.product_info_cache

        try:
            info = self._read_bms_product_info(client)
        except Exception:
            if not force and self.product_info_cache is not None and self.product_info_cache_target == target:
                return self.product_info_cache
            raise

        self.product_info_cache = info
        self.product_info_cache_target = target
        self.product_info_next_due_monotonic = now + PRODUCT_INFO_REFRESH_SECONDS
        return info

    def _battery_log_headers(self) -> list[str]:
        headers = [
            "时间",
            "记录序号",
            "运行秒",
            "串口",
            "总压(V)",
            "充电电流(A)",
            "放电电流(A)",
            "电流(A)",
            "SOC(%)",
            "SOH(%)",
            "剩余容量(mAh)",
            "满电容量(mAh)",
            "出厂容量(mAh)",
            "循环次数",
            "最高单体(mV)",
            "最高单体序号",
            "最低单体(mV)",
            "最低单体序号",
            "平均单体(mV)",
            "最大压差(mV)",
            "最高温度(℃)",
            "最低温度(℃)",
        ]
        headers.extend(f"{_live_temp_label(index)}(℃)" for index in range(BMS_LIVE_TEMP_COUNT))
        headers.extend(
            [
                "一级告警字",
                "二级告警字",
                "三级保护字",
                "均衡低字",
                "均衡高字",
                "系统状态字",
                "功能字",
            ]
        )
        headers.extend(f"单体{index:02d}(mV)" for index in range(1, 33))
        return headers

    def _battery_log_row(self, words: list[int]) -> list[str]:
        valid_cells = _valid_cell_items(words[0:32])
        if valid_cells:
            max_index, max_mv = max(valid_cells, key=lambda item: item[1])
            min_index, min_mv = min(valid_cells, key=lambda item: item[1])
            avg_mv = sum(value for _, value in valid_cells) / len(valid_cells)
            cell_stats = [str(max_mv), str(max_index + 1), str(min_mv), str(min_index + 1), f"{avg_mv:.2f}", str(max_mv - min_mv)]
        else:
            cell_stats = ["", "", "", "", "", ""]

        ichg = words[50] / 10.0
        idsg = words[51] / 10.0
        net_current = ichg if ichg > 0 else -idsg
        row = [
            datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
            str(self.log_count + 1),
            f"{time.monotonic() - self.log_started_monotonic:.1f}",
            self.active_port,
            f"{words[37] / 100.0:.2f}",
            f"{ichg:.1f}",
            f"{idsg:.1f}",
            f"{net_current:.1f}",
            str(words[52]),
            str(words[53]),
            str(words[54] * 10),
            str(words[55] * 10),
            str(words[56] * 10),
            str(words[57]),
            *cell_stats,
            self._temp_display(words[48]),
            self._temp_display(words[49]),
        ]
        row.extend(self._temp_display(raw) for raw in _live_temp_words(words))
        status_words = self._live_status_words(words)
        row.extend(
            [
                f"0x{words[58]:04X}",
                f"0x{words[59]:04X}",
                f"0x{words[60]:04X}",
                f"0x{words[61]:04X}",
                f"0x{words[62]:04X}",
                f"0x{status_words[0]:04X}" if status_words is not None else "",
                f"0x{status_words[2]:04X}" if status_words is not None and len(status_words) > 2 else "",
            ]
        )
        row.extend(str(value) if _is_valid_cell_voltage(value) else "" for value in words[0:32])
        return row

    def _maybe_record_snapshot(self, words: list[int]) -> None:
        if not self.log_running:
            return
        if len(words) < BMS_OVERVIEW_WORDS:
            return
        now = time.monotonic()
        if self.log_next_due_monotonic != 0.0 and now < self.log_next_due_monotonic:
            return
        try:
            interval = self._log_interval_seconds()
            if self.log_path is None:
                raise RuntimeError("日志文件未设置")
            with self.log_path.open("a", encoding="utf-8-sig", newline="") as handle:
                csv.writer(handle).writerow(self._battery_log_row(words))
        except Exception as exc:
            self._stop_data_log("日志记录已停止")
            messagebox.showerror("日志记录失败", f"写入日志失败，请确认文件未被 Excel 打开。\n\n{exc}")
            return

        self.log_count += 1
        self.log_next_due_monotonic = now + interval
        if self.log_path is not None:
            self.log_status_var.set(f"记录: 运行中 {self.log_count} 条  {self.log_path.name}")

    def _poll_live_monitor(self) -> None:
        self.live_after_id = None
        if not self.live_running:
            return
        if (self.worker is not None and self.worker.is_alive()) or (
            self.live_worker is not None and self.live_worker.is_alive()
        ):
            self._schedule_live_monitor(500)
            return
        self.live_worker = threading.Thread(target=self._worker_live_guard, daemon=True)
        self.live_worker.start()

    def _worker_live_guard(self) -> None:
        if not self.serial_lock.acquire(blocking=False):
            self._emit("comm_state", "通信: 忙，等待当前任务")
            self._emit("live_done")
            return
        try:
            with self._open_client() as client:
                self._set_can_target(client)
                words = self._read_live_words(client)
                try:
                    product_info = self._read_bms_product_info_cached(client)
                except Exception as exc:
                    product_info = {"error": str(exc)}
            if not self.live_running:
                return
            self._emit("live_snapshot", words)
            self._emit("product_info", product_info)
            self._emit("progress", 100)
        except Exception as exc:
            self._emit("log", f"实时监控读取失败: {exc}")
            self._emit("live_clear")
            self._emit("comm_state", "通信: 异常")
        finally:
            self.serial_lock.release()
            self._emit("live_done")

    def _draw_battery(self, soc: int) -> None:
        soc = max(0, min(100, int(soc)))
        canvas = self.battery_canvas
        canvas.delete("all")
        canvas.create_rectangle(30, 4, 54, 14, outline="#666666", fill="#666666")
        canvas.create_rectangle(18, 14, 66, 160, outline="#666666", width=3)
        canvas.create_rectangle(23, 19, 61, 155, outline="#dddddd", fill="#f7f7f7")
        fill_height = int(132 * soc / 100)
        top = 151 - fill_height
        color = "#30c85a" if soc >= 20 else "#d93025"
        canvas.create_rectangle(27, top, 57, 151, outline=color, fill=color)

    def _set_badge(self, name: str, enabled: bool | None) -> None:
        badge = self.status_badges.get(name)
        if badge is None:
            return
        if enabled is None:
            badge.configure(text="--", bg="#b0b0b0", fg="white")
        elif enabled:
            badge.configure(text="on", bg="#36c85a", fg="white")
        else:
            badge.configure(text="off", bg="#d93025", fg="white")

    def _live_status_words(self, words: list[int]) -> list[int] | None:
        if len(words) >= BMS_LIVE_WORDS:
            return words[84:88]
        if self.live_status_words_cache is not None and len(self.live_status_words_cache) >= BMS_LIVE_STATUS_WORDS:
            return self.live_status_words_cache[:BMS_LIVE_STATUS_WORDS]
        return None

    def _live_status_badges(self, words: list[int], status_low: int | None) -> dict[str, bool]:
        if status_low is not None:
            return {
                "充电MOS": bool(status_low & (1 << BMS_STATUS_CHARGE_MOS_BIT)),
                "放电MOS": bool(status_low & (1 << BMS_STATUS_DISCHARGE_MOS_BIT)),
                "加热": bool(status_low & (1 << BMS_STATUS_HEAT_BIT)),
                "冷凝": bool(status_low & (1 << BMS_STATUS_COOL_BIT)),
            }
        return {
            "充电MOS": len(words) > 50 and words[50] > 0,
            "放电MOS": len(words) > 51 and words[51] > 0,
            "加热": False,
            "冷凝": False,
        }

    def _temp_display(self, raw: int) -> str:
        if raw == 0:
            return "--"
        return f"{_temp_c(raw):.1f}"

    def _show_main_snapshot(self, words: list[int]) -> None:
        if len(words) < BMS_OVERVIEW_WORDS:
            return
        cells = words[0:32]
        valid_cells = _valid_cell_items(cells)
        for index, (frame, row, col) in enumerate(self.cell_slots):
            if _is_valid_cell_voltage(cells[index]):
                self.cell_value_vars[index].set(str(cells[index]))
                frame.grid(row=row, column=col, sticky="w", padx=(0, 14), pady=3)
            else:
                frame.grid_remove()

        if valid_cells:
            max_index, max_mv = max(valid_cells, key=lambda item: item[1])
            min_index, min_mv = min(valid_cells, key=lambda item: item[1])
            avg_mv = sum(value for _, value in valid_cells) / len(valid_cells)
            self.cell_stat_vars["max"].set(f"最高电压 {max_mv}  {max_index + 1}")
            self.cell_stat_vars["min"].set(f"最低电压 {min_mv}  {min_index + 1}")
            self.cell_stat_vars["avg"].set(f"平均电压 {avg_mv:.2f}")
            self.cell_stat_vars["delta"].set(f"最大压差 {max_mv - min_mv}")
        else:
            for var in self.cell_stat_vars.values():
                var.set("--")

        ichg = words[50] / 10.0
        idsg = words[51] / 10.0
        net_current = ichg if ichg > 0 else -idsg
        soc = words[52]
        self.basic_vars["total_v"].set(f"{words[37] / 100.0:.2f}")
        self.basic_vars["current"].set(f"{net_current:.1f}")
        self.basic_vars["soh"].set(str(words[53]))
        self.basic_vars["cap_now"].set(str(words[54] * 10))
        self.basic_vars["cap_full"].set(str(words[55] * 10))
        self.basic_vars["cap_factory"].set(str(words[56] * 10))
        self.basic_vars["cycle"].set(str(words[57]))
        self.basic_vars["soc"].set(f"SOC:{soc}%")
        self.basic_vars["state"].set("充电" if ichg > 0 else ("放电" if idsg > 0 else "静置"))
        self._draw_battery(soc)

        temp_values = _live_temp_words(words)
        self.temp_vars["max"].set(self._temp_display(words[48]))
        self.temp_vars["min"].set(self._temp_display(words[49]))
        self.temp_vars["t1"].set(self._temp_display(temp_values[0] if len(temp_values) > 0 else 0))
        self.temp_vars["t2"].set(self._temp_display(temp_values[1] if len(temp_values) > 1 else 0))
        self.temp_vars["mos"].set(
            self._temp_display(temp_values[BMS_LIVE_TEMP_MOS_OFFSET] if len(temp_values) > BMS_LIVE_TEMP_MOS_OFFSET else 0)
        )

        status_words = self._live_status_words(words)
        status_low = status_words[0] if status_words is not None else None
        func_low = status_words[2] if status_words is not None and len(status_words) > 2 else None
        for name, enabled in self._live_status_badges(words, status_low).items():
            self._set_badge(name, enabled)

        self.fault_vars["first"].set(f"0x{words[58]:04X}  {_fault_text(words[58])}")
        self.fault_vars["second"].set(f"0x{words[59]:04X}  {_fault_text(words[59])}")
        self.fault_vars["third"].set(f"0x{words[60]:04X}  {_fault_text(words[60])}")
        monitor = f"均衡: 0x{words[61]:04X} 0x{words[62]:04X}"
        if status_low is not None:
            monitor += f"\n系统字: 0x{status_low:04X}"
        else:
            monitor += "\n系统字: --（兼容模式）"
        if func_low is not None:
            monitor += f"\n功能字: 0x{func_low:04X}"
        self.fault_vars["monitor"].set(monitor)

        self.bms_info_var.set(self._format_bms_overview(words[:BMS_OVERVIEW_WORDS]))
        self.comm_state_var.set("通信: 正在通讯" if self.live_running else "通信: 已响应")
        self._update_data_tree(words)
        self._maybe_record_snapshot(words)

    def _clear_main_live_display(self, state_text: str = "通信异常") -> None:
        for index, (frame, _row, _col) in enumerate(self.cell_slots):
            self.cell_value_vars[index].set("0")
            frame.grid_remove()

        self.cell_stat_vars["max"].set("最高电压 --")
        self.cell_stat_vars["min"].set("最低电压 --")
        self.cell_stat_vars["avg"].set("平均电压 --")
        self.cell_stat_vars["delta"].set("最大压差 --")

        self.basic_vars["total_v"].set("0.00")
        self.basic_vars["current"].set("0.0")
        self.basic_vars["soh"].set("0")
        self.basic_vars["cap_now"].set("0")
        self.basic_vars["cap_full"].set("0")
        self.basic_vars["cap_factory"].set("0")
        self.basic_vars["cycle"].set("0")
        self.basic_vars["soc"].set("SOC:0%")
        self.basic_vars["state"].set(state_text)
        self._draw_battery(0)

        for var in self.temp_vars.values():
            var.set("0")

        for name in ["充电MOS", "放电MOS", "加热", "冷凝"]:
            self._set_badge(name, None)

        self.fault_vars["first"].set("0x0000  无")
        self.fault_vars["second"].set("0x0000  无")
        self.fault_vars["third"].set("0x0000  无")
        self.fault_vars["monitor"].set("均衡: 0x0000 0x0000\n系统字: 0x0000\n功能字: 0x0000")
        self.bms_info_var.set(_zero_bms_overview_text())
        self._update_zero_data_tree()

    def _update_zero_data_tree(self) -> None:
        tree = getattr(self, "data_tree", None)
        if tree is None:
            return
        for item in tree.get_children():
            tree.delete(item)
        rows: list[tuple[str, str, str]] = [
            ("总压", "0.00", "V"),
            ("充电电流", "0.0", "A"),
            ("放电电流", "0.0", "A"),
            ("SOC", "0", "%"),
            ("SOH", "0", "%"),
            ("剩余容量", "0", "mAh"),
            ("满电容量", "0", "mAh"),
            ("出厂容量", "0", "mAh"),
            ("循环次数", "0", "次"),
            ("一级告警字", "0x0000", ""),
            ("二级告警字", "0x0000", ""),
            ("三级保护字", "0x0000", ""),
        ]
        rows.extend((_live_temp_label(index), "0", "℃") for index in range(BMS_LIVE_TEMP_COUNT))
        for name, value, unit in rows:
            tree.insert("", tk.END, values=(name, value, unit))

    def _update_data_tree(self, words: list[int]) -> None:
        tree = getattr(self, "data_tree", None)
        if tree is None:
            return
        for item in tree.get_children():
            tree.delete(item)
        rows: list[tuple[str, str, str]] = [
            ("总压", f"{words[37] / 100.0:.2f}", "V"),
            ("充电电流", f"{words[50] / 10.0:.1f}", "A"),
            ("放电电流", f"{words[51] / 10.0:.1f}", "A"),
            ("SOC", str(words[52]), "%"),
            ("SOH", str(words[53]), "%"),
            ("剩余容量", str(words[54] * 10), "mAh"),
            ("满电容量", str(words[55] * 10), "mAh"),
            ("出厂容量", str(words[56] * 10), "mAh"),
            ("循环次数", str(words[57]), "次"),
            ("一级告警字", f"0x{words[58]:04X}", ""),
            ("二级告警字", f"0x{words[59]:04X}", ""),
            ("三级保护字", f"0x{words[60]:04X}", ""),
        ]
        rows.extend((f"单体{index + 1:02d}", str(value), "mV") for index, value in _valid_cell_items(words[0:32]))
        for index, raw in enumerate(_live_temp_words(words)):
            if raw != 0:
                rows.append((_live_temp_label(index), f"{_temp_c(raw):.1f}", "℃"))
        for name, value, unit in rows:
            tree.insert("", tk.END, values=(name, value, unit))

    def _update_log_tree(self, records: list[tuple[int, int]]) -> None:
        if self.log_tree is None:
            return
        for item in self.log_tree.get_children():
            self.log_tree.delete(item)
        for index, (event, delta) in enumerate(records, start=1):
            if event == 0 and delta == 0:
                continue
            name = BMS_EVENT_NAMES[event] if event < len(BMS_EVENT_NAMES) else f"未知事件{event}"
            self.log_tree.insert(
                "",
                tk.END,
                values=(f"{index:03d}", name, _event_interval_text(delta), f"0x{event:02X} 0x{delta:02X}"),
            )

    def _status_card(self, parent: ttk.Frame, title: str, var: tk.StringVar, column: int) -> None:
        frame = ttk.LabelFrame(parent, text=title)
        frame.grid(row=0, column=column, sticky="nsew", padx=(0 if column == 0 else 8, 0))
        frame.columnconfigure(0, weight=1)
        ttk.Label(frame, textvariable=var, justify=tk.LEFT).grid(row=0, column=0, sticky="ew", padx=10, pady=8)

    def _populate_param_tree(self) -> None:
        group_iids: dict[str, str] = {}
        for param in BMS_PARAM_DEFS:
            if param.group not in group_iids:
                iid = f"group:{param.group}"
                group_iids[param.group] = iid
                self.param_tree.insert("", tk.END, iid=iid, text=param.group, open=True, values=("", "", ""))
            self.param_tree.insert(
                group_iids[param.group],
                tk.END,
                iid=param.key,
                text=param.name,
                values=(f"0x{param.addr:04X}", "--", param.unit),
            )

    def _selected_param_key(self) -> str:
        selection = self.param_tree.selection() if hasattr(self, "param_tree") else ()
        if not selection:
            return ""
        key = selection[0]
        return key if key in BMS_PARAM_BY_KEY else ""

    def _on_param_select(self, _event=None) -> None:
        key = self._selected_param_key()
        if not key:
            self.param_selected_var.set("未选择")
            self.param_edit_var.set("")
            return
        param = BMS_PARAM_BY_KEY[key]
        raw = self.param_values.get(key)
        value_text = _param_display_value(param, raw) if raw is not None else ""
        self.param_selected_var.set(f"{param.group} / {param.name}\n0x{param.addr:04X}")
        self.param_edit_var.set(value_text)
        if raw is None:
            self.param_current_var.set("未读取")
        else:
            self.param_current_var.set(f"当前 {value_text} {param.unit}，原始值 {raw}")

    def _update_param_row(self, key: str, raw: int) -> None:
        param = BMS_PARAM_BY_KEY[key]
        self.param_values[key] = raw
        display = _param_display_value(param, raw)
        if hasattr(self, "param_tree"):
            self.param_tree.item(key, values=(f"0x{param.addr:04X}", display, param.unit))
        var = self.param_entry_vars.get(key)
        if var is not None:
            old_loading = self.param_loading
            self.param_loading = True
            try:
                var.set(display)
            finally:
                self.param_loading = old_loading

    def _set_busy(self, busy: bool) -> None:
        state = tk.DISABLED if busy else tk.NORMAL
        if self.monitor_window is not None:
            self.monitor_window.set_paused_by_parent(busy)
        for child in self.winfo_children():
            self._set_child_state(child, state)
        if busy:
            self.result_var.set("正在执行...")

    def _set_child_state(self, widget, state: str) -> None:
        for child in widget.winfo_children():
            if isinstance(child, ttk.Combobox):
                if state == tk.NORMAL and child in (
                    getattr(self, "param_combo", None),
                    getattr(self, "connection_mode_combo", None),
                ):
                    child.configure(state="readonly")
                else:
                    child.configure(state=state)
            elif isinstance(child, (ttk.Button, ttk.Entry)):
                child.configure(state=state)
            self._set_child_state(child, state)

    def _refresh_ports(self) -> None:
        try:
            require_pyserial()
            from serial.tools import list_ports  # type: ignore

            ports = [port.device for port in list_ports.comports()]
            self.port_combo["values"] = ports
        except Exception as exc:
            self._log(f"串口枚举失败: {exc}")

    def _choose_bin(self) -> None:
        path = filedialog.askopenfilename(
            title="选择 BMS APP 升级文件",
            initialdir=str(Path.home() / 'Documents'),
            filetypes=[("APP 升级文件", "*.bin *.c073"), ("所有文件", "*.*")],
        )
        if path:
            self.bin_var.set(path)
            self._describe_selected_bin()

    def _check_connection(self) -> None:
        self._run_worker("连接检测", self._worker_check_connection)

    def _read_cache(self) -> None:
        self._run_worker("读取缓存", self._worker_read_cache)

    def _download_only(self) -> None:
        self._run_worker("写入缓存", self._worker_download_only)

    def _apply_can_settings(self) -> None:
        self._run_worker("应用目标设备设置", self._worker_apply_can_settings)

    def _upgrade_selected(self) -> None:
        path = Path(self.bin_var.get())
        if not path.exists():
            messagebox.showerror("文件不存在", "请选择有效的 BMS App bin 文件。")
            return
        if self._parse_connection_mode() == COMM_MODE_DIRECT_BMS:
            prompt = (
                "将通过 BMS 直连串口让 App 进入 IAP，然后把当前 bin 直接写入 BMS App 区。\n\n"
                f"文件: {path}\n\n"
                "升级期间不要断电、断开串口或启动其他通信任务。\n\n"
                "确认继续？"
            )
        else:
            prompt = (
                "将核对所选固件并通过 Comm Tool 的 CAN 接口升级 BMS。\n\n"
                f"文件: {path}\n\n"
                "确认继续？"
            )
        if not messagebox.askyesno(
            "确认升级",
            prompt,
        ):
            return
        self._prepare_exclusive_upgrade()
        self._run_worker("一键升级", self._worker_upgrade_selected)

    def _upgrade_cached_selected(self) -> None:
        if self._parse_connection_mode() == COMM_MODE_DIRECT_BMS:
            messagebox.showinfo("使用缓存升级", "BMS直连串口不经过 comm tool，没有可用的固件缓存。")
            return
        path = Path(self.bin_var.get())
        if not path.exists():
            messagebox.showerror("文件不存在", "请选择有效的 BMS App bin 文件。")
            return
        if not messagebox.askyesno(
            "确认使用缓存升级",
            "将跳过写入 comm tool 缓存，仅在缓存大小和 CRC 与当前选择的 bin 完全一致时升级 BMS。\n\n"
            f"文件: {path}\n\n"
            "确认继续？",
        ):
            return
        self._prepare_exclusive_upgrade()
        self._run_worker("使用缓存升级", self._worker_upgrade_cached_selected)

    def _read_bms_status(self) -> None:
        self._run_worker("读取BMS状态", self._worker_read_bms_status)

    def _read_bms_overview(self) -> None:
        self._run_worker("读取BMS信息", self._worker_read_bms_overview)

    def _read_selected_param(self) -> None:
        key = self._selected_param_key()
        if not key:
            messagebox.showinfo("参数设置", "请先选择一个保护参数或其它参数。")
            return
        self.active_param_key = key
        self._run_worker("读取BMS参数", self._worker_read_selected_param)

    def _write_selected_param(self) -> None:
        try:
            key = self._selected_param_key()
            if not key:
                raise ValueError("请先选择一个保护参数或其它参数")
            param = BMS_PARAM_BY_KEY[key]
            value = _param_parse_display_value(param, self.param_edit_var.get())
            self._validate_sh309_param_values({key: value})
        except Exception as exc:
            messagebox.showerror("参数错误", str(exc))
            return
        self.active_param_key = key
        self.active_param_raw = value
        if not messagebox.askyesno(
            "确认写入参数",
            f"参数: {param.group} / {param.name}\n地址: 0x{param.addr:04X}\n"
            f"显示值: {self.param_edit_var.get().strip()} {param.unit}\n板端原始值: {value} (0x{value:04X})\n\n"
            "确认写入 BMS？",
        ):
            return
        self.active_param_values = {key: value}
        self._run_worker("写入BMS参数", self._worker_write_dirty_params)

    def _read_current_param_page(self) -> None:
        page_id = self.param_tabs.select()
        self.active_param_range = self.param_page_ranges.get(page_id, "all")
        page_name = self.param_tabs.tab(page_id, "text") if page_id else "参数"
        self._run_worker(f"读取{page_name}", self._worker_read_param_range)

    def _read_all_params(self) -> None:
        self.active_param_range = "all"
        self._run_worker("读取全部参数", self._worker_read_param_range)

    def _read_bms_log(self) -> None:
        self._run_worker("读取BMS日志", self._worker_read_bms_log)

    def _reset_bms_log(self) -> None:
        if not messagebox.askyesno(
            "确认重置BMS日志",
            "将清空板端存储的100条事件日志，并写入Flash保存。\n\n确认继续？",
        ):
            return
        self._run_worker("重置100条日志", self._worker_reset_bms_log)

    def _read_bms_regs(self) -> None:
        try:
            self.active_bms_addr = self._parse_u16(self.bms_addr_var.get(), "地址")
            self.active_bms_count = self._parse_count(self.bms_count_var.get())
        except Exception as exc:
            messagebox.showerror("参数错误", str(exc))
            return
        self._run_worker("读取BMS寄存器", self._worker_read_bms_regs)

    def _write_bms_regs(self) -> None:
        try:
            self.active_bms_addr = self._parse_u16(self.bms_addr_var.get(), "地址")
            self.active_bms_words = self._parse_words(self.bms_values_var.get())
        except Exception as exc:
            messagebox.showerror("参数错误", str(exc))
            return
        if not messagebox.askyesno(
            "确认写入",
            f"将通过当前通信方式写入 BMS。\n\n"
            f"起始地址: 0x{self.active_bms_addr:04X}\n"
            f"数量: {len(self.active_bms_words)}\n\n"
            "板端仍会做地址、范围和权限检查，若参数越界会返回错误。",
        ):
            return
        self._run_worker("写入BMS寄存器", self._worker_write_bms_regs)

    def _write_bms_soc(self) -> None:
        try:
            soc = int(self.bms_soc_write_var.get().strip(), 0)
            if soc < 0 or soc > 100:
                raise ValueError("SOC 必须是 0..100")
        except Exception as exc:
            messagebox.showerror("SOC 参数错误", str(exc))
            return
        if not messagebox.askyesno(
            "确认写SOC",
            f"将通过当前通信方式写入一次 SOC={soc}%。\n\n"
            f"底层固定写寄存器 0x{APP_SET_ONCE_SOC_ADDR:04X}，用户不需要手动填写寄存器地址。\n\n"
            "确认继续？",
        ):
            return
        self.active_soc_write = soc
        self._run_worker("写SOC", self._worker_write_bms_soc)

    def _aging_start(self) -> None:
        self._aging_control("开启老化模式", APP_AGING_ACTION_START)

    def _aging_stop(self) -> None:
        self._aging_control("关闭老化模式", APP_AGING_ACTION_STOP)

    def _aging_reset_time(self) -> None:
        self._aging_control("重置老化时间", APP_AGING_ACTION_RESET_TIME)

    def _read_aging_status(self) -> None:
        self._run_worker("读取老化时间", self._worker_read_aging_status)

    def _aging_set_hours(self) -> None:
        try:
            hours = int(self.bms_aging_hours_var.get().strip(), 0)
            if hours < 1 or hours > 168:
                raise ValueError("老化时长必须是 1..168 小时")
        except Exception as exc:
            messagebox.showerror("老化时间参数错误", str(exc))
            return
        if not messagebox.askyesno(
            "确认修改老化时间",
            f"将老化总时长修改为 {hours} 小时，并重置已累计老化时间。\n\n确认继续？",
        ):
            return
        self.active_aging_hours = hours
        self._run_worker("修改老化时间", self._worker_aging_set_hours)

    def _aging_control(self, name: str, action: int) -> None:
        prompt = f"确认{name}？"
        if action == APP_AGING_ACTION_STOP:
            prompt = "关闭老化模式会提前结束本轮老化时间，剩余时间将变为 0。\n\n确认继续？"
        if not messagebox.askyesno("确认老化模式操作", prompt):
            return
        self.active_aging_action = action
        self.active_aging_name = name
        self._run_worker(name, self._worker_aging_control)

    def _can_diag(self) -> None:
        self._run_worker("CAN诊断", self._worker_can_diag)

    def _enter_iap(self) -> None:
        if not messagebox.askyesno(
            "确认进入IAP",
            "该操作会让 BMS App 写升级标志并复位进入 IAP，实时数据会暂时中断。\n\n确认继续？",
        ):
            return
        self._prepare_exclusive_upgrade()
        self._run_worker("进入IAP", self._worker_enter_iap)

    def _open_monitor(self) -> None:
        if self.monitor_window is not None and self.monitor_window.winfo_exists():
            self.monitor_window.lift()
            return
        self.monitor_window = BmsMonitorWindow(self)
        self.monitor_window.start()

    def _run_worker(self, name: str, target: Callable[[], None]) -> None:
        if self.worker and self.worker.is_alive():
            messagebox.showinfo("正在执行", "当前任务还没有结束。")
            return
        try:
            self._capture_runtime_settings(include_bin=True)
        except Exception as exc:
            messagebox.showerror("参数错误", str(exc))
            return
        self.progress_var.set(0)
        self.upgrade_stage_var.set("升级进度: --")
        self._set_busy(True)
        self.worker = threading.Thread(target=self._worker_guard, args=(name, target), daemon=True)
        self.worker.start()

    def _parse_connection_mode(self) -> str:
        text = self.connection_mode_var.get().strip()
        for mode, label in COMM_MODE_LABELS.items():
            if text in (label, tr(label)):
                return mode
        return COMM_MODE_FROM_LABEL.get(text, COMM_MODE_COMM_TOOL)

    def _capture_runtime_settings(self, include_bin: bool = True) -> None:
        self.active_port = self.port_var.get().strip()
        self.active_baud = int(self.baud_var.get().strip())
        self.active_connection_mode = self._parse_connection_mode()
        self.active_modbus_slave = int(self.modbus_slave_var.get().strip(), 0)
        if include_bin:
            self.active_bin = Path(self.bin_var.get()).resolve()
        self.active_can_bitrate = int(self.can_bitrate_var.get().strip(), 0)
        self.active_node_id = int(self.node_id_var.get().strip(), 0)
        self.active_app_can_addr = int(self.app_can_addr_var.get().strip(), 0)
        if not self.active_port:
            raise ValueError("未选择串口")
        if self.active_baud <= 0:
            raise ValueError("BMS/串口2波特率必须大于 0")
        if self.active_baud < 1200 or self.active_baud > 460800:
            raise ValueError("BMS/串口2波特率必须在 1200..460800 范围内")
        if self.active_modbus_slave <= 0 or self.active_modbus_slave > 247:
            raise ValueError("Modbus地址必须是 1..247")
        if self.active_node_id <= 0 or self.active_node_id > 0x7F:
            raise ValueError("IAP节点必须是 1..127")
        if self.active_app_can_addr < 0 or self.active_app_can_addr > 0x0F:
            raise ValueError("BMS地址必须是 0..15")

    def _on_connection_mode_changed(self, _event=None) -> None:
        baud_text = self.baud_var.get().strip()
        if baud_text == "":
            self.baud_var.set(str(BMS_UART_DEFAULT_BAUD))

    def _parse_u16(self, text: str, name: str) -> int:
        value = int(text.strip(), 0)
        if value < 0 or value > 0xFFFF:
            raise ValueError(f"{name} 超出 0x0000..0xFFFF")
        return value

    def _parse_count(self, text: str) -> int:
        value = int(text.strip(), 0)
        if value <= 0 or value > 120:
            raise ValueError("数量必须是 1..120")
        return value

    def _parse_words(self, text: str) -> list[int]:
        parts = text.replace(",", " ").replace(";", " ").split()
        if not parts:
            raise ValueError("请填写至少一个写入值")
        if len(parts) > 120:
            raise ValueError("一次最多写入 120 个寄存器")
        return [self._parse_u16(part, "写入值") for part in parts]

    def _worker_guard(self, name: str, target: Callable[[], None]) -> None:
        locked = False
        try:
            self._emit("log", f"开始: {name}")
            if not self.serial_lock.acquire(blocking=False):
                self._emit("log", "串口正在被实时监控占用，等待当前读数结束")
                self.serial_lock.acquire()
            locked = True
            target()
            self._emit("result", f"{name} 完成")
        except Exception as exc:
            self._emit("error", f"{name} 失败: {exc}")
        finally:
            if locked:
                self.serial_lock.release()
            self._emit("busy", False)

    def _worker_check_connection(self) -> None:
        with self._open_client() as client:
            self._set_can_target(client)
            info = self._read_info(client)
            cache = self._read_cache_info(client)
            if isinstance(client, DirectBmsModbusClient):
                words = self._read_live_words(client)
                try:
                    product_info = self._read_bms_product_info_cached(client, force=True)
                except Exception as exc:
                    product_info = {"error": str(exc)}
            else:
                words = None
                product_info = None
        self._emit("info", info)
        self._emit("cache", cache)
        if info.get("mode") != COMM_MODE_DIRECT_BMS:
            self._emit("target", info)
        if words is not None:
            self._emit("live_snapshot", words)
            self._emit("bms_info", self._format_bms_overview(words[:BMS_OVERVIEW_WORDS]))
        if product_info is not None:
            self._emit("product_info", product_info)
        self._emit("progress", 100)

    def _worker_apply_can_settings(self) -> None:
        with self._open_client() as client:
            self._set_can_target(client, force=True)
            info = self._read_info(client)
        self._emit("info", info)
        if info.get("mode") == COMM_MODE_DIRECT_BMS:
            self._emit("log", "BMS直连串口模式无需设置 CAN 目标设备")
        else:
            self._emit("target", info)
            self._emit("log", f"目标设备已设置: BMS地址={self.active_app_can_addr} IAP节点={self.active_node_id}")
        self._emit("progress", 100)

    def _worker_read_cache(self) -> None:
        with self._open_client() as client:
            cache = self._read_cache_info(client)
        self._emit("cache", cache)
        self._emit("progress", 100)

    def _worker_download_only(self) -> None:
        image = self._load_selected_image()
        with self._open_client() as client:
            self._require_comm_tool_client(client, "写入缓存")
            self._check_can_upgrade_adapter(client, image)
            self._download_image(client, image)
            cache = self._read_cache_info(client)
        self._assert_cache_matches(image, cache)
        self._emit("cache", cache)
        self._emit("progress", 100)

    def _worker_upgrade_selected(self) -> None:
        self._upgrade_selected_image(require_cached=False)

    def _worker_upgrade_cached_selected(self) -> None:
        self._upgrade_selected_image(require_cached=True)

    def _upgrade_selected_image(self, require_cached: bool) -> None:
        image = self._load_selected_image()
        with self._open_client() as client:
            if isinstance(client, DirectBmsModbusClient) and not require_cached:
                if image.board:
                    raise RuntimeError("C073 的串口或 BLE 升级请使用统一 BMS 上位机")
                self._upgrade_bms_direct_serial(client, image.data)
            else:
                self._require_comm_tool_client(client, "CAN 升级")
                self._set_can_target(client, force=True)
                info = self._check_can_upgrade_adapter(client, image)
                self._emit("info", info)
                self._emit("target", info)
                if image.board:
                    self._confirm_c073_app(client, image, allow_boot_recovery=True)
                if not require_cached and not image.board:
                    self._download_image(client, image, progress_base=0, progress_span=45)
                    cache = self._read_cache_info(client)
                    self._assert_cache_matches(image, cache)
                else:
                    cache = self._read_cache_info(client)
                    try:
                        self._assert_cache_matches(image, cache)
                    except ValueError:
                        if require_cached:
                            raise
                        self._download_image(client, image, progress_base=0, progress_span=45)
                        cache = self._read_cache_info(client)
                        self._assert_cache_matches(image, cache)
                    else:
                        self._emit("log", "缓存与当前文件一致，跳过串口下载")
                self._emit("cache", cache)
                self._upgrade_bms_from_verified_cache(client, progress_base=50, progress_span=45, image=image)
        self._emit("progress", 100)

    def _check_can_upgrade_adapter(self, client, image: CanUpgradeImage) -> dict:
        info = self._read_info(client)
        if image.board:
            minimum = (0, 2, 11) if image.board == C073_GUARDED_BOARD else (
                (0, 2, 10) if image.capacity == 44 * C073_PAGE_BYTES else (0, 2, 9))
            version = tuple(int(part) for part in info["version"].split("."))
            if version < minimum:
                needed = ".".join(map(str, minimum))
                raise RuntimeError(f"Comm Tool {info['version']} 不支持所选 C073 镜像，需要 {needed} 或更新版")
        return info

    def _confirm_c073_app(self, client, image: CanUpgradeImage, allow_boot_recovery: bool = False) -> None:
        if (self.active_can_bitrate, self.active_node_id, self.active_app_can_addr) != (250000, 1, 0):
            raise RuntimeError("C073 CAN 升级固定使用 250 kbit/s、Boot 节点 1、APP 地址 0")
        try:
            words = self._read_bms_words_with_retry(client, 0xD170, 12, "C073 升级身份")
        except Exception as exc:
            if allow_boot_recovery and image.capacity == 46 * C073_PAGE_BYTES:
                self._emit("log", "未读到旧 46 KiB APP，继续由 Comm Tool 核对 Boot 身份")
                return
            raise RuntimeError("未读到 C073 APP 的升级身份。若板子已在 Boot，请用统一 BMS 上位机从串口或 BLE 恢复") from exc
        if words != image.running_identity():
            raise RuntimeError("当前设备的升级身份或布局与 C073 文件不匹配")

    def _worker_read_bms_status(self) -> None:
        with self._open_client() as client:
            self._set_can_target(client)
            status = self._read_bms_app_status(client)
        self._emit("log", f"BMS App 状态: SOC={status[0]} SOH={status[1]}")
        self._emit("progress", 100)

    def _worker_read_bms_overview(self) -> None:
        with self._open_client() as client:
            self._set_can_target(client)
            words = self._read_live_words(client)
            try:
                product_info = self._read_bms_product_info_cached(client, force=True)
            except Exception as exc:
                product_info = {"error": str(exc)}
        self._emit("live_snapshot", words)
        self._emit("product_info", product_info)
        self._emit("bms_info", self._format_bms_overview(words[:BMS_OVERVIEW_WORDS]))
        self._emit("log", "BMS 信息读取完成")
        self._emit("progress", 100)

    def _worker_read_selected_param(self) -> None:
        key = self.active_param_key
        param = BMS_PARAM_BY_KEY[key]
        with self._open_client() as client:
            self._set_can_target(client)
            words = self._read_bms_words(client, param.addr, 1)
        value = words[0]
        self._emit("param_value", (key, value))
        self._emit("log", f"参数读取完成: {param.group}/{param.name}=0x{value:04X} ({value})")
        self._emit("progress", 100)

    def _worker_write_selected_param(self) -> None:
        key = self.active_param_key
        param = BMS_PARAM_BY_KEY[key]
        value = self.active_param_raw
        with self._open_client() as client:
            self._set_can_target(client)
            self._write_bms_words(client, param.addr, [value])
            words = self._read_bms_words(client, param.addr, 1)
        self._emit("param_value", (key, words[0]))
        self._emit("log", f"参数写入完成: {param.group}/{param.name}=0x{words[0]:04X} ({words[0]})")
        self._emit("progress", 100)

    def _write_param_block(
        self,
        client: CommToolClient,
        pending: dict[str, int],
        base_addr: int,
        word_count: int,
    ) -> dict[str, int]:
        block_keys = [
            key
            for key in pending
            if base_addr <= BMS_PARAM_BY_KEY[key].addr < base_addr + word_count
        ]
        if not block_keys:
            return {}

        words = self._read_bms_words(client, base_addr, word_count)
        for key in block_keys:
            param = BMS_PARAM_BY_KEY[key]
            words[param.addr - base_addr] = pending[key]

        self._write_bms_words(client, base_addr, words)
        readback = self._read_bms_words(client, base_addr, word_count)
        verified: dict[str, int] = {}
        for key in block_keys:
            param = BMS_PARAM_BY_KEY[key]
            value = pending.pop(key)
            actual = readback[param.addr - base_addr]
            verified[key] = actual
            if actual != value:
                raise RuntimeError(
                    f"{param.group}/{param.name} 写入后回读不一致: 写0x{value:04X} 回读0x{actual:04X}"
                )
        return verified

    def _worker_write_dirty_params(self) -> None:
        pending = dict(self.active_param_values)
        verified: dict[str, int] = {}
        with self._open_client() as client:
            self._set_can_target(client)
            verified.update(self._write_param_block(client, pending, SH309_AFE_PARAM_ADDR, SH309_AFE_PARAM_WORDS))
            verified.update(self._write_param_block(client, pending, SH309_TMOS_PARAM_ADDR, SH309_TMOS_PARAM_WORDS))
            for key, value in sorted(pending.items(), key=lambda item: BMS_PARAM_BY_KEY[item[0]].addr):
                param = BMS_PARAM_BY_KEY[key]
                self._write_bms_words(client, param.addr, [value])
                words = self._read_bms_words(client, param.addr, 1)
                verified[key] = words[0]
                if words[0] != value:
                    raise RuntimeError(
                        f"{param.group}/{param.name} 写入后回读不一致: 写0x{value:04X} 回读0x{words[0]:04X}"
                    )
        self._emit("param_values", verified)
        self._emit("log", f"修改参数写入完成: {len(verified)} 项")
        self._emit("progress", 100)

    def _read_sh309_param_values(self, client: CommToolClient) -> dict[str, int]:
        values: dict[str, int] = {}
        for addr, count in ((0x231D, 2), (SH309_AFE_PARAM_ADDR, SH309_AFE_PARAM_WORDS), (SH309_TMOS_PARAM_ADDR, SH309_TMOS_PARAM_WORDS)):
            words = self._read_bms_words(client, addr, count)
            for index, raw in enumerate(words):
                key = BMS_PARAM_ADDR_TO_KEY.get(addr + index)
                if key:
                    values[key] = raw
        return values

    def _worker_reset_sh309_protect_params(self) -> None:
        with self._open_client() as client:
            self._set_can_target(client)
            self._write_bms_words(client, SH309_RESET_AFE_ADDR, [1])
            self._write_bms_words(client, SH309_RESET_PROTECT_ADDR, [1])
            values = self._read_sh309_param_values(client)
        self._emit("param_values", values)
        self._emit("log", "SH309 保护点已重置并回读")
        self._emit("progress", 100)

    def _worker_read_param_range(self) -> None:
        ranges: list[tuple[int, int]]
        if self.active_param_range == "protect":
            ranges = [(0x231D, 2), (SH309_AFE_PARAM_ADDR, SH309_AFE_PARAM_WORDS), (SH309_TMOS_PARAM_ADDR, SH309_TMOS_PARAM_WORDS)]
        elif self.active_param_range == "other":
            ranges = [(0x2300, 32)]
        else:
            ranges = [
                (SH309_AFE_PARAM_ADDR, SH309_AFE_PARAM_WORDS),
                (SH309_TMOS_PARAM_ADDR, SH309_TMOS_PARAM_WORDS),
                (0x2300, 32),
            ]
        values: dict[str, int] = {}
        with self._open_client() as client:
            self._set_can_target(client)
            for addr, count in ranges:
                words = self._read_bms_words(client, addr, count)
                for index, raw in enumerate(words):
                    key = BMS_PARAM_ADDR_TO_KEY.get(addr + index)
                    if key:
                        values[key] = raw
        self._emit("param_values", values)
        self._emit("log", f"参数读取完成: {len(values)} 项")
        self._emit("progress", 100)

    def _worker_read_bms_log(self) -> None:
        with self._open_client() as client:
            self._set_can_target(client)
            self._emit("log", "读取BMS日志: 分段读取 0xC008 事件记录窗口")
            words = self._read_bms_log_words(client)
        records = [((word >> 8) & 0xFF, word & 0xFF) for word in words]
        valid_count = sum(1 for event, delta in records if event != 0 or delta != 0)
        self._emit("bms_log", records)
        self._emit("log", f"BMS 日志读取完成: {valid_count} 条")
        self._emit("progress", 100)

    def _worker_reset_bms_log(self) -> None:
        with self._open_client() as client:
            self._set_can_target(client)
            self._emit(
                "log",
                f"重置BMS日志: 写 0x{BMS_RESET_EVENT_RECORD_ADDR:04X}=0x{BMS_RESET_EVENT_RECORD_VALUE:04X}",
            )
            self._write_bms_words(client, BMS_RESET_EVENT_RECORD_ADDR, [BMS_RESET_EVENT_RECORD_VALUE])
            words = self._read_bms_log_words(client)
        records = [((word >> 8) & 0xFF, word & 0xFF) for word in words]
        valid_count = sum(1 for event, delta in records if event != 0 or delta != 0)
        self._emit("bms_log", records)
        self._emit("log", f"BMS 日志重置完成: 当前有效记录 {valid_count} 条")
        self._emit("progress", 100)

    def _read_bms_log_words(self, client: CommToolClient) -> list[int]:
        words: list[int] = []
        for offset in range(0, BMS_EVENT_RECORD_WORDS, BMS_EVENT_RECORD_CHUNK_WORDS):
            count = min(BMS_EVENT_RECORD_CHUNK_WORDS, BMS_EVENT_RECORD_WORDS - offset)
            label = f"BMS日志窗口 0x{BMS_EVENT_RECORD_ADDR + offset:04X}+{count}"
            words.extend(
                self._read_bms_words_with_retry(
                    client,
                    BMS_EVENT_RECORD_ADDR + offset,
                    count,
                    label,
                )
            )
        return words

    def _worker_read_bms_regs(self) -> None:
        addr = self.active_bms_addr
        count = self.active_bms_count
        with self._open_client() as client:
            self._set_can_target(client)
            words = self._read_bms_words(client, addr, count)
        text = self._format_bms_words(addr, words)
        self._emit("bms_result", text)
        self._emit("log", "BMS 寄存器读取完成")
        self._emit("progress", 100)

    def _worker_write_bms_regs(self) -> None:
        addr = self.active_bms_addr
        words = self.active_bms_words
        with self._open_client() as client:
            self._set_can_target(client)
            self._write_bms_words(client, addr, words)
        text = self._format_bms_words(addr, words)
        self._emit("bms_result", "已写入:\n" + text)
        self._emit("log", f"BMS 寄存器写入完成: addr=0x{addr:04X} count={len(words)}")
        self._emit("progress", 100)

    def _worker_write_bms_soc(self) -> None:
        soc = self.active_soc_write
        with self._open_client() as client:
            self._set_can_target(client)
            self._write_bms_words(client, APP_SET_ONCE_SOC_ADDR, [soc])
        self._emit("bms_result", f"已写入SOC: {soc}%\n地址: 0x{APP_SET_ONCE_SOC_ADDR:04X}")
        self._emit("log", f"写SOC完成: {soc}%")
        self._emit("progress", 100)

    def _worker_read_aging_status(self) -> None:
        with self._open_client() as client:
            self._set_can_target(client)
            if isinstance(client, DirectBmsModbusClient):
                text = self._format_direct_aging_status("老化时间", self._direct_aging_status(client))
                self._emit("aging_status", text)
                self._emit("bms_result", text)
                self._emit("log", text)
                self._emit("progress", 100)
                return
            self._require_comm_tool_client(client, "读取老化时间")
            resp = client.command(CMD_BMS_AGING_STATUS, timeout=8.0)
        if len(resp.payload) < 3:
            raise RuntimeError("老化时间响应长度不足")
        state = resp.payload[0]
        remaining_minutes = struct.unpack_from("<H", resp.payload, 1)[0]
        text = (
            "老化时间: "
            f"状态={self._aging_state_name(state)} "
            f"剩余={self._format_aging_remaining_minutes(remaining_minutes)}"
        )
        self._emit("aging_status", text)
        self._emit("bms_result", text)
        self._emit("log", text)
        self._emit("progress", 100)

    def _worker_aging_control(self) -> None:
        action = self.active_aging_action
        name = self.active_aging_name
        with self._open_client() as client:
            self._set_can_target(client)
            if isinstance(client, DirectBmsModbusClient):
                if action == APP_AGING_ACTION_START:
                    self._write_bms_words(client, BMS_DIRECT_FUNCTION_ON_ADDR, [BMS_DIRECT_AGING_FUNCTION_ID])
                    text = self._format_direct_aging_status("开启老化模式", self._direct_aging_status(client))
                elif action == APP_AGING_ACTION_STOP:
                    self._write_bms_words(client, BMS_DIRECT_FUNCTION_OFF_ADDR, [BMS_DIRECT_AGING_FUNCTION_ID])
                    text = self._format_direct_aging_status("关闭老化模式", self._direct_aging_status(client))
                else:
                    self._write_bms_words(client, BMS_DIRECT_AGING_RESET_TIME_ADDR, [BMS_DIRECT_AGING_RESET_GUARD])
                    text = self._format_direct_aging_status("重置老化时间", self._direct_aging_status(client))
                self._emit("aging_status", text)
                self._emit("bms_result", text)
                self._emit("log", text)
                self._emit("progress", 100)
                return
            resp = client.command(CMD_BMS_AGING_CTRL, bytes([action]), timeout=10.0)
        if len(resp.payload) < 2:
            raise RuntimeError("老化模式响应长度不足")
        state = resp.payload[0]
        remaining_hours = resp.payload[1]
        text = f"{name}: 状态={self._aging_state_name(state)} 剩余约={remaining_hours}h"
        self._emit("aging_status", text)
        self._emit("bms_result", text)
        self._emit("log", text)
        self._emit("progress", 100)

    def _worker_aging_set_hours(self) -> None:
        hours = self.active_aging_hours
        with self._open_client() as client:
            self._set_can_target(client)
            if isinstance(client, DirectBmsModbusClient):
                self._write_bms_words(client, BMS_DIRECT_AGING_SET_HOURS_ADDR, [hours])
                text = self._format_direct_aging_status("修改老化时间", self._direct_aging_status(client))
                self._emit("aging_status", text)
                self._emit("bms_result", text)
                self._emit("log", text)
                self._emit("progress", 100)
                return
            self._require_comm_tool_client(client, "修改老化时间")
            resp = client.command(CMD_BMS_AGING_SET_HOURS, struct.pack("<H", hours), timeout=10.0)
        if len(resp.payload) < 4:
            raise RuntimeError("老化时间设置响应长度不足")
        state = resp.payload[0]
        remaining_hours = resp.payload[1]
        applied_hours = struct.unpack_from("<H", resp.payload, 2)[0]
        text = (
            f"修改老化时间: 时长={applied_hours}h "
            f"状态={self._aging_state_name(state)} 剩余约={remaining_hours}h"
        )
        self._emit("aging_status", text)
        self._emit("bms_result", text)
        self._emit("log", text)
        self._emit("progress", 100)

    def _worker_can_diag(self) -> None:
        with self._open_client() as client:
            self._require_comm_tool_client(client, "CAN诊断")
            resp = client.command(CMD_CAN_DIAG, b"\x00", timeout=2.0)
        self._emit("log", self._format_can_diag(resp.payload))
        self._emit("progress", 100)

    def _worker_enter_iap(self) -> None:
        with self._open_client() as client:
            self._set_can_target(client)
            if isinstance(client, DirectBmsModbusClient):
                self._write_bms_words(client, BMS_DIRECT_ENTER_IAP_ADDR, [1])
                self._emit("log", "已通过 BMS直连串口写入 0xFFFD，请等待 BMS 复位进入 IAP")
            else:
                client.command(CMD_ENTER_IAP, timeout=10.0)
                self._emit("log", "已通过 comm tool/CAN桥请求 BMS App 进入 IAP")
        self._emit("progress", 100)

    def _set_can_target(self, client, force: bool = False) -> None:
        if isinstance(client, DirectBmsModbusClient):
            target = self._connection_cache_key()
            if force or self.applied_target != target:
                self.applied_target = target
                self._reset_live_capability_cache()
                self._reset_product_info_cache()
            return
        target = (self.active_can_bitrate, self.active_node_id, self.active_app_can_addr)
        if not force and self.applied_target == target:
            return
        payload = struct.pack(
            "<IBBH",
            self.active_can_bitrate,
            self.active_node_id & 0xFF,
            self.active_app_can_addr & 0x0F,
            0,
        )
        client.command(CMD_SET_CAN, payload, timeout=2.0)
        self.applied_target = target
        self._reset_live_capability_cache()
        self._reset_product_info_cache()

    def _set_comm_tool_bms_uart_baud(self, client: CommToolClient, force: bool = False) -> None:
        target = (self.active_port, self.active_baud)
        changed = self.applied_bms_uart_target != target
        if not force and self.applied_bms_uart_target == target:
            return
        try:
            resp = client.command(CMD_SET_BMS_UART, struct.pack("<I", self.active_baud), timeout=2.0)
        except RuntimeError as exc:
            if "UNSUPPORTED" in str(exc) and self.active_baud == BMS_UART_DEFAULT_BAUD:
                self.applied_bms_uart_target = target
                if changed:
                    self._emit("log", "当前 comm tool 固件不支持动态 USART2 波特率，继续使用默认 19200")
                return
            raise
        if len(resp.payload) >= 4:
            applied = struct.unpack_from("<I", resp.payload, 0)[0]
            if applied != self.active_baud:
                raise RuntimeError(f"comm tool USART2 波特率设置不一致: expect={self.active_baud} actual={applied}")
        self.applied_bms_uart_target = target
        if changed:
            self._emit("log", f"comm tool USART2/BMS 波特率已设置为 {self.active_baud}")

    def _upgrade_state_text(self, status: dict) -> str:
        state_text = {
            0: "空闲",
            1: "升级中",
            2: "完成",
            3: "错误",
            4: "已终止",
        }.get(status["state"], f"状态{status['state']}")
        return (
            f"升级进度: {status['percent']}%  {status['written']}/{status['total']} bytes  "
            f"{state_text}  error=0x{status['error']:02X}"
        )

    def _upgrade_bms_from_verified_cache(
        self,
        client: CommToolClient,
        progress_base: int = 0,
        progress_span: int = 95,
        image: Optional[CanUpgradeImage] = None,
    ) -> None:
        self._require_comm_tool_client(client, "CAN-IAP升级")
        self._emit("log", "缓存校验通过，开始 CAN 升级 BMS")
        self._emit("upgrade_stage", "升级进度: 正在启动 BMS CAN-IAP")
        client.command(CMD_CAN_DIAG, b"\x01", timeout=2.0)
        client.command(CMD_UPGRADE, timeout=5.0)

        deadline = time.monotonic() + DEFAULT_LONG_TIMEOUT
        last_percent = -1
        last_log_time = 0.0
        status: dict | None = None
        while time.monotonic() < deadline:
            status = self._read_upgrade_status(client)
            self._emit("upgrade_stage", self._upgrade_state_text(status))
            self._emit("progress", min(99, progress_base + int(status["percent"] * progress_span / 100)))

            now = time.monotonic()
            if (
                status["percent"] != last_percent
                and (status["percent"] == 100 or status["percent"] - last_percent >= 5 or now - last_log_time >= 2.0)
            ):
                self._emit("log", self._format_upgrade_status(status))
                last_percent = status["percent"]
                last_log_time = now

            if status["state"] == 2 and status["error"] == 0:
                break
            if status["state"] in (3, 4) or status["error"] != 0:
                raise RuntimeError(self._format_upgrade_status(status))
            time.sleep(0.25)
        else:
            raise TimeoutError("等待 CAN 升级完成超时")

        if status is None or status["state"] != 2 or status["error"] != 0:
            raise RuntimeError(self._format_upgrade_status(status or {"state": 0, "percent": 0, "error": 0xFF, "written": 0, "total": 0, "expect_seq": 0}))

        self._emit("upgrade_stage", "升级进度: CAN升级完成，等待 BMS App 恢复")
        bms_status = self._wait_bms_status_after_upgrade(client)
        if bms_status is None:
            self._emit("log", "升级已完成，但 BMS App 状态暂未响应；请稍后点击“读取BMS状态”复核。")
        else:
            self._emit("log", f"BMS App 状态: SOC={bms_status[0]} SOH={bms_status[1]}")
        if image is not None and image.board:
            self._confirm_c073_app(client, image)
            words = self._read_bms_words_with_retry(client, 0xD17C, 8, "C073 运行镜像")
            actual = tuple((words[i] << 16) | words[i + 1] for i in (0, 2, 4))
            if actual != (image.version, len(image.data), image.crc32) or words[6:] != [1, 0]:
                raise RuntimeError("C073 重启后的版本、长度或 CRC32 与所选 APP 不一致")
        self._emit("upgrade_stage", "升级进度: 完成")

    def _upgrade_bms_direct_serial(self, client: DirectBmsModbusClient, image: bytes) -> None:
        image_end = APP_BASE_ADDR + len(image)
        if image_end > BMS_DIRECT_IAP_APP_LIMIT:
            raise RuntimeError(
                f"直连串口 IAP 镜像超出 App 区: end=0x{image_end:08X}, "
                f"limit=0x{BMS_DIRECT_IAP_APP_LIMIT:08X}"
            )

        app_slave = self.active_modbus_slave
        self._emit("upgrade_stage", "升级进度: 正在请求 BMS App 进入串口 IAP")
        self._emit("log", f"直连串口 IAP: App Modbus地址={app_slave}, IAP固定地址={BMS_DIRECT_IAP_SLAVE}")
        try:
            client.write_words(BMS_DIRECT_ENTER_IAP_ADDR, [1])
            self._emit("log", "BMS App 已确认进入 IAP 请求")
        except Exception as exc:
            self._emit("log", f"BMS App 进入 IAP 响应未确认，继续检测 IAP: {exc}")

        time.sleep(BMS_DIRECT_IAP_RESET_WAIT)
        client.set_slave(BMS_DIRECT_IAP_SLAVE)
        client.set_timeout(BMS_DIRECT_IAP_ACK_TIMEOUT)

        detect_error: Exception | None = None
        for attempt in range(1, BMS_DIRECT_IAP_READY_RETRIES + 1):
            try:
                client.reset_input_buffer()
                client.iap_connect()
                detect_error = None
                self._emit("log", f"已收到升级连接 ACK（第 {attempt} 次检测）")
                break
            except Exception as exc:
                detect_error = exc
                if attempt < BMS_DIRECT_IAP_READY_RETRIES:
                    self._emit("log", f"等待串口 IAP: 第 {attempt} 次未响应")
                    time.sleep(BMS_DIRECT_IAP_READY_RETRY_DELAY)
        if detect_error is not None:
            raise RuntimeError(f"串口 IAP 连接失败: {detect_error}") from detect_error

        # 首个 ACK 可能仍由地址 1 的 App 返回；等待复位后必须再初始化一次 IAP 会话。
        time.sleep(BMS_DIRECT_IAP_RESET_WAIT)
        session_error: Exception | None = None
        for attempt in range(1, BMS_DIRECT_IAP_READY_RETRIES + 1):
            try:
                client.reset_input_buffer()
                client.iap_connect()
                session_error = None
                self._emit("log", "串口 IAP 升级会话已确认，开始从第 0 块完整写入")
                break
            except Exception as exc:
                session_error = exc
                if attempt < BMS_DIRECT_IAP_READY_RETRIES:
                    self._emit("log", f"初始化串口 IAP 会话: 第 {attempt} 次未响应")
                    time.sleep(BMS_DIRECT_IAP_READY_RETRY_DELAY)
        if session_error is not None:
            raise RuntimeError(f"串口 IAP 会话初始化失败: {session_error}") from session_error

        block_total = math.ceil(len(image) / BMS_DIRECT_IAP_BLOCK_SIZE)
        start_time = time.monotonic()
        self._emit("upgrade_stage", f"升级进度: 串口 IAP 写入 0/{block_total} 块")
        for block_index, offset in enumerate(range(0, len(image), BMS_DIRECT_IAP_BLOCK_SIZE), start=1):
            block = image[offset : offset + BMS_DIRECT_IAP_BLOCK_SIZE]
            try:
                client.iap_write_block(block)
            except Exception as exc:
                raise RuntimeError(
                    f"串口 IAP 第 {block_index}/{block_total} 块未确认；"
                    "协议没有块序号，为避免重发导致写入错位，已停止升级。"
                    f"请从第 0 块重新完整升级。原因: {exc}"
                ) from exc

            percent = int(block_index * 90 / block_total)
            self._emit("progress", min(95, 5 + percent))
            self._emit("upgrade_stage", f"升级进度: 串口 IAP 写入 {block_index}/{block_total} 块")
            if block_index == block_total or block_index % 8 == 0:
                self._emit("log", f"串口 IAP 写入: {block_index}/{block_total}, {offset + len(block)}/{len(image)} bytes")

        self._emit("upgrade_stage", "升级进度: 校验 App 向量并完成升级")
        try:
            client.iap_complete()
        except Exception as exc:
            raise RuntimeError(f"串口 IAP 完成命令失败，App 向量校验或 Flash 写入未通过: {exc}") from exc

        elapsed = max(0.001, time.monotonic() - start_time)
        self._emit("log", f"串口 IAP 写入完成: {len(image)} bytes, {elapsed:.1f}s, {len(image) / 1024.0 / elapsed:.1f} KiB/s")
        self._emit("upgrade_stage", "升级进度: 等待 BMS App 复位恢复")

        client.set_slave(app_slave)
        client.set_timeout(1.0)
        bms_status = self._wait_bms_status_after_upgrade(client)
        if bms_status is None:
            self._emit("log", "串口 IAP 已完成，但 BMS App 状态暂未响应；请稍后读取实时监控或产品信息复核。")
        else:
            self._emit("log", f"BMS App 已恢复: SOC={bms_status[0]} SOH={bms_status[1]}")
            try:
                product_info = self._read_bms_product_info(client)
                self._emit("product_info", product_info)
                self._emit("log", f"BMS 升级后软件版本: {product_info.get('software', '--')}")
            except Exception as exc:
                self._emit("log", f"BMS App 已恢复，但升级后版本读取失败: {exc}")
        self._emit("upgrade_stage", "升级进度: 完成")

    def _open_client(self):
        port = self.active_port
        if not port:
            raise RuntimeError("未选择串口")
        if self.active_connection_mode == COMM_MODE_DIRECT_BMS:
            with CommToolClient(port, COMM_TOOL_PC_BAUD, timeout=1.0) as client:
                self._set_comm_tool_bms_uart_baud(client, force=True)
            time.sleep(0.05)
            return DirectBmsModbusClient(
                port,
                COMM_TOOL_PC_BAUD,
                timeout=1.0,
                slave=self.active_modbus_slave,
            )
        return CommToolClient(port, COMM_TOOL_PC_BAUD, timeout=1.0)

    def _read_info(self, client) -> dict:
        if isinstance(client, DirectBmsModbusClient):
            return {
                "mode": COMM_MODE_DIRECT_BMS,
                "transport": "Modbus RTU over comm tool USART2",
                "pc_baud": COMM_TOOL_PC_BAUD,
                "bms_uart_baud": self.active_baud,
                "slave": self.active_modbus_slave,
            }
        payload = client.command(CMD_GET_INFO, timeout=2.0).payload
        if len(payload) < 20:
            raise RuntimeError("GET_INFO 响应长度不足")
        proto, major, minor, patch = struct.unpack_from("<BBBB", payload, 0)
        bitrate, cache_base, cache_size, flags = struct.unpack_from("<IIII", payload, 4)
        return {
            "proto": proto,
            "version": f"{major}.{minor}.{patch}",
            "bitrate": bitrate,
            "cache_base": cache_base,
            "cache_size": cache_size,
            "flags": flags,
            "node_id": payload[20] if len(payload) >= 21 else self.active_node_id,
            "app_can_addr": payload[21] if len(payload) >= 22 else self.active_app_can_addr,
            "bms_uart_baud": struct.unpack_from("<I", payload, 24)[0] if len(payload) >= 28 else self.active_baud,
        }

    def _read_cache_info(self, client) -> dict:
        if isinstance(client, DirectBmsModbusClient):
            return {
                "unavailable": "BMS直连串口不经过 comm tool，无法读取或写入 comm tool 缓存",
            }
        payload = client.command(CMD_FW_INFO, timeout=2.0).payload
        if len(payload) < 15:
            raise RuntimeError("FW_INFO 响应长度不足")
        app_addr, size, crc16, crc32, valid = struct.unpack_from("<IIHIB", payload, 0)
        return {
            "app_addr": app_addr,
            "size": size,
            "crc16": crc16,
            "crc32": crc32,
            "valid": valid,
        }

    def _read_upgrade_status(self, client) -> dict:
        self._require_comm_tool_client(client, "读取升级状态")
        payload = client.command(CMD_UPGRADE_STATUS, timeout=2.0).payload
        if len(payload) < 13:
            raise RuntimeError("UPGRADE_STATUS 响应长度不足")
        state, percent, error = struct.unpack_from("<BBB", payload, 0)
        written, total, expect_seq = struct.unpack_from("<IIH", payload, 3)
        return {
            "state": state,
            "percent": percent,
            "error": error,
            "written": written,
            "total": total,
            "expect_seq": expect_seq,
        }

    def _read_bms_app_status(self, client) -> tuple[int, int]:
        words = self._read_bms_words(client, 0xD034, 2)
        return words[0], words[1]

    def _read_bms_product_info(self, client) -> dict[str, str]:
        words = self._read_bms_words(client, BMS_PRODUCT_INFO_ADDR, BMS_PRODUCT_INFO_WORDS)
        return _decode_product_info_words(words)

    def _read_bms_words(self, client, addr: int, count: int) -> list[int]:
        if isinstance(client, DirectBmsModbusClient):
            return client.read_words(addr, count)
        payload = struct.pack("<HH", addr, count)
        resp = client.command(CMD_BMS_READ, payload, timeout=max(10.0, count * 1.5))
        if len(resp.payload) != count * 2:
            raise RuntimeError(f"BMS_READ 响应长度错误: {len(resp.payload)}")
        return list(struct.unpack("<" + "H" * count, resp.payload))

    def _read_bms_words_with_retry(self, client, addr: int, count: int, label: str) -> list[int]:
        last_error: Exception | None = None
        for attempt in range(1, BMS_READ_RETRY_COUNT + 1):
            try:
                return self._read_bms_words(client, addr, count)
            except Exception as exc:
                last_error = exc
                if attempt >= BMS_READ_RETRY_COUNT:
                    break
                self._emit(
                    "log",
                    f"{label} 第 {attempt} 次失败，{BMS_READ_RETRY_DELAY_SECONDS:.1f}s 后重试: {exc}",
                )
                time.sleep(BMS_READ_RETRY_DELAY_SECONDS)
        raise RuntimeError(f"{label} 重试 {BMS_READ_RETRY_COUNT} 次仍失败: {last_error}") from last_error

    def _write_bms_words(self, client, addr: int, words: list[int]) -> None:
        if isinstance(client, DirectBmsModbusClient):
            client.write_words(addr, words)
            return
        payload = struct.pack("<HH", addr, len(words))
        payload += struct.pack("<" + "H" * len(words), *words)
        client.command(CMD_BMS_WRITE, payload, timeout=max(10.0, len(words) * 2.5))

    def _wait_bms_status_after_upgrade(self, client: CommToolClient) -> Optional[tuple[int, int]]:
        deadline = time.monotonic() + POST_UPGRADE_APP_READY_TIMEOUT
        attempt = 0
        last_error = ""

        time.sleep(POST_UPGRADE_APP_READY_INTERVAL)
        while time.monotonic() < deadline:
            attempt += 1
            try:
                status = self._read_bms_app_status(client)
                if attempt > 1:
                    self._emit("log", f"BMS App 第 {attempt} 次确认成功")
                return status
            except Exception as exc:
                last_error = str(exc)
                self._emit("log", f"等待 BMS App 恢复响应: 第 {attempt} 次未响应")
                time.sleep(POST_UPGRADE_APP_READY_INTERVAL)

        if last_error:
            self._emit("log", f"BMS App 状态确认超时，最后错误: {last_error}")
        return None

    def _load_selected_image(self) -> CanUpgradeImage:
        path = self.active_bin
        image = load_can_upgrade_image(path)
        self._emit("image", self._image_info_text(path, image))
        return image

    def _download_image(
        self,
        client: CommToolClient,
        image: CanUpgradeImage,
        progress_base: int = 0,
        progress_span: int = 90,
    ) -> None:
        self._require_comm_tool_client(client, "写入缓存")
        crc16 = crc16_modbus(image.data)
        crc32 = image.crc32
        start_time = time.monotonic()
        chunk_bytes = C073_CACHE_CHUNK_BYTES if image.board else DEFAULT_CHUNK_SIZE
        total = math.ceil(len(image.data) / chunk_bytes)
        client.command(CMD_FW_BEGIN, struct.pack("<IIHI", image.address, len(image.data), crc16, crc32), timeout=5.0)
        for index, offset in enumerate(range(0, len(image.data), chunk_bytes), start=1):
            chunk = image.data[offset : offset + chunk_bytes]
            client.command(CMD_FW_DATA, struct.pack("<I", offset) + chunk, timeout=5.0)
            if index == total or index % 4 == 0:
                self._emit("progress", min(99, progress_base + int(index * progress_span / total)))
                self._emit("log", f"写入缓存: {index}/{total}")
        client.command(CMD_FW_END, struct.pack("<IHI", len(image.data), crc16, crc32), timeout=5.0)
        elapsed = max(0.001, time.monotonic() - start_time)
        self._emit("log", f"写入缓存完成: {len(image.data)} bytes, {elapsed:.1f}s, {len(image.data) / 1024.0 / elapsed:.1f} KiB/s")

    def _assert_cache_matches(self, image: CanUpgradeImage, cache: dict) -> None:
        crc16 = crc16_modbus(image.data)
        crc32 = image.crc32
        if cache["app_addr"] != image.address or cache["size"] != len(image.data):
            raise ValueError("comm tool 缓存地址或大小与所选文件不一致")
        if cache["crc16"] != crc16 or cache["crc32"] != crc32 or cache["valid"] != 1:
            raise ValueError("comm tool 缓存 CRC 与所选文件不一致")

    def _describe_selected_bin(self) -> None:
        if not self.bin_var.get().strip():
            self.image_var.set("请选择固件文件")
            return
        path = Path(self.bin_var.get()).resolve()
        try:
            if not path.exists():
                self.image_var.set("文件不存在")
                return
            image = load_can_upgrade_image(path)
            self.image_var.set(self._image_info_text(path, image))
        except Exception as exc:
            self.image_var.set(f"校验失败: {exc}")

    def _image_info_text(self, path: Path, image: CanUpgradeImage) -> str:
        crc16 = crc16_modbus(image.data)
        crc32 = image.crc32
        msp, reset, msp_ok, reset_thumb_ok = vector_summary(image.data)
        reset_ok = reset_thumb_ok and (image.address <= reset < image.address + len(image.data))
        return (
            f"{path.name}\n"
            f"APP=0x{image.address:08X}, {len(image.data)} bytes\n"
            f"CRC16=0x{crc16:04X} CRC32=0x{crc32:08X}\n"
            f"MSP=0x{msp:08X} {'OK' if msp_ok else 'BAD'}\n"
            f"Reset=0x{reset:08X} {'OK' if reset_ok else 'BAD'}"
        )

    def _format_bms_words(self, addr: int, words: list[int]) -> str:
        return "  ".join(f"0x{addr + index:04X}=0x{value:04X}({value})" for index, value in enumerate(words))

    def _aging_state_name(self, value: int) -> str:
        return {
            0: "停止",
            1: "运行",
            2: "完成",
        }.get(value, f"未知({value})")

    def _format_aging_remaining_minutes(self, minutes: int) -> str:
        hours, mins = divmod(minutes, 60)
        if hours:
            return f"{hours}h{mins:02d}min ({minutes}min)"
        return f"{mins}min"

    def _direct_aging_status(self, client) -> tuple[int, int, int, int]:
        words = self._read_bms_words(client, BMS_DIRECT_AGING_STATUS_ADDR, BMS_DIRECT_AGING_STATUS_WORDS)
        state = words[0]
        remaining_minutes = words[1]
        remaining_seconds = ((words[2] & 0xFFFF) << 16) | (words[3] & 0xFFFF)
        duration_hours = words[4]
        return state, remaining_minutes, remaining_seconds, duration_hours

    def _format_direct_aging_status(self, title: str, status: tuple[int, int, int, int]) -> str:
        state, remaining_minutes, remaining_seconds, duration_hours = status
        duration_text = f"{duration_hours}h" if 1 <= duration_hours <= 168 else "--"
        return (
            f"{title}: 状态={self._aging_state_name(state)} "
            f"剩余={self._format_aging_remaining_minutes(remaining_minutes)} "
            f"({remaining_seconds}s) 时长={duration_text}"
        )

    def _format_bms_overview(self, words: list[int]) -> str:
        if len(words) < BMS_OVERVIEW_WORDS:
            raise RuntimeError("BMS 信息长度不足")

        cells = words[0:32]
        valid_cells = _valid_cell_items(cells)
        max_cell_text, min_cell_text, delta_cell_text = _cell_stat_text(valid_cells)
        cell_text = " ".join(f"{index + 1}:{value}" for index, value in valid_cells)
        if not cell_text:
            cell_text = "无有效单体电压"

        total_v = words[37] / 100.0
        ichg = words[50] / 10.0
        idsg = words[51] / 10.0
        soc = words[52]
        soh = words[53]
        capacity_now = words[54] / 100.0
        capacity_full = words[55] / 100.0
        capacity_factory = words[56] / 100.0

        return (
            f"SOC {soc}%  SOH {soh}%  总压 {total_v:.2f}V  "
            f"充电 {ichg:.1f}A  放电 {idsg:.1f}A\n"
            f"单体: max {max_cell_text}  min {min_cell_text}  "
            f"压差 {delta_cell_text}  有效串数 {len(valid_cells)}\n"
            f"温度: max {_temp_c(words[48]):.1f}℃  min {_temp_c(words[49]):.1f}℃  "
            f"容量 {capacity_now:.2f}/{capacity_full:.2f}Ah  出厂 {capacity_factory:.2f}Ah  循环 {words[57]}\n"
            f"故障字: 0x{words[58]:04X} 0x{words[59]:04X} 0x{words[60]:04X}  "
            f"均衡: 0x{words[61]:04X} 0x{words[62]:04X}\n"
            f"单体电压(mV): {cell_text}"
        )

    def _format_upgrade_status(self, status: dict) -> str:
        return (
            f"升级状态: state={status['state']} percent={status['percent']}% "
            f"error=0x{status['error']:02X} written={status['written']}/{status['total']} "
            f"expect_seq={status['expect_seq']}"
        )

    def _format_can_diag(self, data: bytes) -> str:
        if len(data) < 62:
            return f"CAN_DIAG 响应长度不足: {len(data)}"
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
        node_id = data[61]
        app_can_addr = data[62] if len(data) >= 63 else 0
        return (
            "CAN 诊断:\n"
            f"  tx={tx_count} ok={tx_ok} fail={tx_fail} timeout={tx_timeout}\n"
            f"  rx={rx_count} drop={rx_drop}\n"
            f"  target: BMS地址={app_can_addr} IAP节点={node_id}\n"
            f"  ESR=0x{last_esr:08X} TSR=0x{last_tsr:08X} MSR=0x{last_msr:08X} RF0R=0x{last_rf0r:08X}\n"
            f"  last_tx=0x{last_tx_id:08X} last_rx=0x{last_rx_id:08X}"
        )

    def _format_info(self, info: dict) -> str:
        if info.get("mode") == COMM_MODE_DIRECT_BMS:
            return (
                "BMS直连串口\n"
                f"{info.get('transport', 'Modbus RTU')}\n"
                f"Modbus地址 {info.get('slave', self.active_modbus_slave)}\n"
                f"PC串口1 {info.get('pc_baud', COMM_TOOL_PC_BAUD)}\n"
                f"BMS/串口2 {info.get('bms_uart_baud', self.active_baud)}"
            )
        return (
            f"固件 {info['version']}\n"
            f"协议 {info['proto']}\n"
            f"CAN {info['bitrate']}\n"
            f"USART2/BMS {info.get('bms_uart_baud', self.active_baud)}\n"
            f"BMS地址 {info.get('app_can_addr', 0)}  IAP节点 {info.get('node_id', 1)}\n"
            f"缓存 0x{info['cache_base']:08X} + {info['cache_size']}"
        )

    def _format_cache(self, cache: dict) -> str:
        if "unavailable" in cache:
            return str(cache["unavailable"])
        return (
            f"地址 0x{cache['app_addr']:08X}\n"
            f"{cache['size']} bytes\n"
            f"CRC16=0x{cache['crc16']:04X} CRC32=0x{cache['crc32']:08X}\n"
            f"有效={cache['valid']}"
        )

    def _emit(self, kind: str, payload=None) -> None:
        self.events.put(UiEvent(kind, payload))

    def _after_events(self) -> None:
        while True:
            try:
                event = self.events.get_nowait()
            except queue.Empty:
                break
            self._handle_event(event)
        self.after(80, self._after_events)

    def _handle_event(self, event: UiEvent) -> None:
        if event.kind == "log":
            self._log(str(event.payload))
        elif event.kind == "error":
            self._log(str(event.payload))
            if str(event.payload).startswith("读取BMS信息 失败:"):
                self.live_snapshot_cache = _zero_live_words()
                self._clear_main_live_display()
                if self.monitor_window is not None and self.monitor_window.winfo_exists():
                    self.monitor_window._clear_snapshot()
            self.result_var.set(str(event.payload))
            messagebox.showerror("操作失败", str(event.payload))
        elif event.kind == "result":
            self.result_var.set(str(event.payload))
            self._log(str(event.payload))
        elif event.kind == "busy":
            self._set_busy(bool(event.payload))
        elif event.kind == "progress":
            self.progress_var.set(float(event.payload))
        elif event.kind == "info":
            self.info_var.set(self._format_info(event.payload))
            self.comm_state_var.set("通信: 已连接")
        elif event.kind == "target":
            if isinstance(event.payload, dict) and event.payload.get("mode") == COMM_MODE_DIRECT_BMS:
                target = self._connection_cache_key()
                if self.applied_target != target:
                    self._reset_live_capability_cache()
                    self._reset_product_info_cache()
                self.applied_target = target
                return
            self.can_bitrate_var.set(str(event.payload.get("bitrate", self.active_can_bitrate)))
            self.node_id_var.set(str(event.payload.get("node_id", self.active_node_id)))
            self.app_can_addr_var.set(str(event.payload.get("app_can_addr", self.active_app_can_addr)))
            target = (
                int(event.payload.get("bitrate", self.active_can_bitrate)),
                int(event.payload.get("node_id", self.active_node_id)),
                int(event.payload.get("app_can_addr", self.active_app_can_addr)),
            )
            if self.applied_target != target:
                self._reset_live_capability_cache()
                self._reset_product_info_cache()
            self.applied_target = target
        elif event.kind == "cache":
            self.cache_var.set(self._format_cache(event.payload))
        elif event.kind == "image":
            self.image_var.set(str(event.payload))
        elif event.kind == "bms_info":
            self.bms_info_var.set(str(event.payload))
        elif event.kind == "bms_result":
            self.bms_result_var.set(str(event.payload))
        elif event.kind == "aging_status":
            self.bms_aging_var.set(str(event.payload))
        elif event.kind == "product_info":
            if isinstance(event.payload, dict) and "error" in event.payload:
                self.product_info_var.set(
                    f"软件版本: --    硬件版本: --    BMS序列号: --    读取失败: {event.payload['error']}"
                )
            else:
                self.product_info_cache = event.payload
                self.product_info_cache_target = self._connection_cache_key()
                self.product_info_next_due_monotonic = time.monotonic() + PRODUCT_INFO_REFRESH_SECONDS
                self.product_info_var.set(_format_product_info(event.payload))
                if self.monitor_window is not None and self.monitor_window.winfo_exists():
                    self.monitor_window.product_info_var.set(_format_product_info(event.payload))
        elif event.kind == "param_value":
            key, value = event.payload
            self._update_param_row(key, value)
            self._refresh_sh309_dynamic_choices()
            self.param_dirty.discard(key)
            self._set_param_dirty_style(key, False)
            self._update_param_dirty_label()
            param = BMS_PARAM_BY_KEY[key]
            display = _param_display_value(param, value)
            self.param_current_var.set(f"当前 {display} {param.unit}，原始值 {value}")
            if self._selected_param_key() == key:
                self.param_edit_var.set(display)
        elif event.kind == "param_values":
            for key, value in event.payload.items():
                self._update_param_row(key, value)
                self.param_dirty.discard(key)
                self._set_param_dirty_style(key, False)
            self._refresh_sh309_dynamic_choices()
            self._update_param_dirty_label()
            self.param_current_var.set(f"已读取 {len(event.payload)} 项")
            self._on_param_select()
        elif event.kind == "bms_log":
            self._update_log_tree(event.payload)
        elif event.kind == "live_snapshot":
            self.live_snapshot_cache = event.payload
            self._show_main_snapshot(event.payload)
            if self.monitor_window is not None and self.monitor_window.winfo_exists():
                self.monitor_window._show_snapshot(event.payload)
        elif event.kind == "live_clear":
            self.live_snapshot_cache = _zero_live_words()
            self._clear_main_live_display()
            if self.monitor_window is not None and self.monitor_window.winfo_exists():
                self.monitor_window._clear_snapshot()
        elif event.kind == "live_done":
            self._schedule_live_monitor()
        elif event.kind == "comm_state":
            self.comm_state_var.set(str(event.payload))
        elif event.kind == "upgrade_stage":
            self.upgrade_stage_var.set(str(event.payload))

    def _log(self, text: str) -> None:
        now = time.strftime("%H:%M:%S")
        self.log_text.configure(state="normal")
        self.log_text.insert(tk.END, f"[{now}] {tr(text)}\n")
        self.log_text.see(tk.END)
        self.log_text.configure(state="disabled")


def _ui_variant_from_executable(executable_stem: str, frozen: bool) -> tuple[str, str]:
    if not frozen:
        return UI_EDITION_INTERNAL, LANG_ZH
    stem = executable_stem.lower()
    edition = UI_EDITION_INTERNAL if "internal" in stem else UI_EDITION_CUSTOMER
    language = LANG_EN if stem.endswith("_en") else LANG_ZH
    return edition, language


def _runtime_ui_variant() -> tuple[str, str]:
    return _ui_variant_from_executable(Path(sys.executable).stem, getattr(sys, "frozen", False))


def parse_args() -> argparse.Namespace:
    default_edition, default_language = _runtime_ui_variant()
    parser = argparse.ArgumentParser(description="BMS CAN 升级图形上位机")
    parser.add_argument("--port", default="")
    parser.add_argument("--baud", type=int, default=None)
    parser.add_argument("--bin", default="")
    parser.add_argument("--mode", choices=(COMM_MODE_COMM_TOOL, COMM_MODE_DIRECT_BMS), default=COMM_MODE_COMM_TOOL)
    parser.add_argument("--slave", type=int, default=BMS_DIRECT_DEFAULT_SLAVE)
    parser.add_argument("--edition", choices=(UI_EDITION_CUSTOMER, UI_EDITION_INTERNAL), default=default_edition)
    parser.add_argument("--language", choices=(LANG_ZH, LANG_EN), default=default_language)
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    install_ui_language(args.language)
    if args.self_test:
        require_pyserial()
        expected_variants = {
            "BMS_CommTool_Upgrade_UI": (UI_EDITION_CUSTOMER, LANG_ZH),
            "BMS_CommTool_Upgrade_UI_EN": (UI_EDITION_CUSTOMER, LANG_EN),
            "BMS_CommTool_Upgrade_UI_Internal": (UI_EDITION_INTERNAL, LANG_ZH),
            "BMS_CommTool_Upgrade_UI_Internal_EN": (UI_EDITION_INTERNAL, LANG_EN),
        }
        for executable_stem, expected_variant in expected_variants.items():
            if _ui_variant_from_executable(executable_stem, True) != expected_variant:
                raise RuntimeError(f"EXE 名称与版本映射错误: {executable_stem}")
        if CUSTOMER_MAINTENANCE_PASSWORD != "hs456":
            raise RuntimeError("客户版默认维护密码不正确")
        if len(SH309_PARAM_DEFS) != (SH309_AFE_PARAM_WORDS + SH309_TMOS_PARAM_WORDS):
            raise RuntimeError("SH309 参数定义数量不正确")
        expected_param_ui_keys = {param.key for param in SH309_PARAM_DEFS}
        expected_param_ui_keys.update(param.key for param in BMS_PARAM_DEFS if 0x2300 <= param.addr < 0x2400)
        if len(SH309_PARAM_UI_KEY_SEQUENCE) != len(set(SH309_PARAM_UI_KEY_SEQUENCE)):
            raise RuntimeError("SH309 参数页存在重复控件映射")
        if PARAM_UI_KEYS != expected_param_ui_keys:
            missing = sorted(expected_param_ui_keys - PARAM_UI_KEYS)
            duplicated = sorted(PARAM_UI_KEYS - expected_param_ui_keys)
            raise RuntimeError(f"参数页映射不完整: missing={missing[:3]} extra={duplicated[:3]}")
        for key in SH309_TMOS_DEFAULT_DISPLAY:
            if key not in BMS_PARAM_BY_KEY:
                raise RuntimeError(f"SH309 默认值键不存在: {key}")
        for key in ("SH309/单节过压(mv)", "SH309/MOS过温1(℃)", "SH309/延时(10ms)"):
            if key not in BMS_PARAM_BY_KEY:
                raise RuntimeError(f"SH309 UI 参数键不存在: {key}")
        if _param_display_value(BMS_PARAM_BY_KEY["SH309/过压延时(ms)"], 10) != "100":
            raise RuntimeError("SH309 过压延时显示换算错误")
        if _param_parse_display_value(BMS_PARAM_BY_KEY["SH309/充电高温(℃)"], "60") != 1000:
            raise RuntimeError("SH309 温度写入换算错误")
        if BMS_PARAM_BY_KEY["SH309/短路电流(A)"].kind != "u16":
            raise RuntimeError("SH309 短路电流 raw 单位应为 A")
        if BMS_PARAM_BY_KEY["SH309/二级充电过流(A)"].kind != "x10":
            raise RuntimeError("SH309 二级过流 raw 单位应为 A*10")
        if BMS_PARAM_BY_KEY["短路参数/短路延时"].kind != "x10":
            raise RuntimeError("短路延时应按 10ms raw 和 ms 显示换算")
        if _modbus_crc_frame(bytes((1, 3, 0, 0, 0, 1))) != b"\x01\x03\x00\x00\x00\x01\x84\x0A":
            raise RuntimeError("Modbus CRC 打包错误")
        iap_connect_frame = _build_direct_iap_write_frame(
            BMS_DIRECT_IAP_SLAVE,
            BMS_DIRECT_ENTER_IAP_ADDR,
            1,
            b"\x00\x00",
            2,
        )
        if iap_connect_frame[:7] != b"\x01\x10\xFF\xFD\x00\x01\x02":
            raise RuntimeError("串口 IAP 连接帧格式错误")
        iap_block = bytes(range(256)) * 4
        iap_block_frame = _build_direct_iap_write_frame(
            BMS_DIRECT_IAP_SLAVE,
            BMS_DIRECT_IAP_DATA_ADDR,
            len(iap_block),
            iap_block,
            0,
        )
        if len(iap_block_frame) != 1033 or iap_block_frame[:7] != b"\x01\x10\xFF\xFE\x04\x00\x00":
            raise RuntimeError("串口 IAP 1024 字节原始块格式错误")
        if crc16_modbus(iap_block_frame[:-2]) != (iap_block_frame[-2] | (iap_block_frame[-1] << 8)):
            raise RuntimeError("串口 IAP 数据块 CRC 错误")
        iap_ack = _modbus_crc_frame(iap_block_frame[:6])
        _validate_modbus_write_response(iap_ack, BMS_DIRECT_IAP_SLAVE, 0x10, iap_block_frame[2:6])
        iap_nack = _modbus_crc_frame(bytes((BMS_DIRECT_IAP_SLAVE, 0x90, 0x04)))
        try:
            _validate_modbus_write_response(iap_nack, BMS_DIRECT_IAP_SLAVE, 0x10, iap_block_frame[2:6])
        except RuntimeError as exc:
            if "code=0x04" not in str(exc):
                raise RuntimeError("串口 IAP NACK 错误码解析错误") from exc
        else:
            raise RuntimeError("串口 IAP NACK 未被识别")
        if BMS_DIRECT_IAP_APP_LIMIT - APP_BASE_ADDR != 0x1B000:
            raise RuntimeError("串口 IAP App 区边界错误")
        probe = object.__new__(UpgradeUi)
        probe.log_count = 0
        probe.log_started_monotonic = time.monotonic()
        probe.active_port = "COM_TEST"
        probe.active_baud = BMS_DIRECT_DEFAULT_BAUD
        probe.active_connection_mode = COMM_MODE_DIRECT_BMS
        probe.active_modbus_slave = BMS_DIRECT_DEFAULT_SLAVE
        probe.active_can_bitrate = 250000
        probe.active_node_id = 1
        probe.active_app_can_addr = 0
        if UpgradeUi._connection_cache_key(probe) != (
            COMM_MODE_DIRECT_BMS,
            "COM_TEST",
            BMS_DIRECT_DEFAULT_BAUD,
            BMS_DIRECT_DEFAULT_SLAVE,
        ):
            raise RuntimeError("直连模式缓存 key 错误")
        words = [0] * BMS_LIVE_WORDS
        for index in range(10):
            words[index] = 3300 + index
        words[10] = CELL_VOLTAGE_NOT_PRESENT
        words[37] = 3350
        words[38] = 640
        words[39] = 650
        words[47] = 700
        words[48] = 650
        words[49] = 640
        words[50] = 12
        words[51] = 0
        words[52] = 80
        words[53] = 100
        words[54] = 1200
        words[55] = 1800
        words[56] = 1800
        words[57] = 3
        headers = UpgradeUi._battery_log_headers(probe)
        row = UpgradeUi._battery_log_row(probe, words)
        if len(headers) != len(row):
            raise RuntimeError("长期监控 CSV 表头和数据列数不一致")
        if "MOS温度(℃)" not in headers:
            raise RuntimeError("长期监控 CSV 缺少 MOS 温度列")
        mos_column = headers.index("MOS温度(℃)")
        if row[mos_column] != UpgradeUi._temp_display(probe, words[47]):
            raise RuntimeError("MOS 温度列未映射到 D000 第 47 个 word")
        if "61001" in row[-32:]:
            raise RuntimeError("不存在的单体电压不应写入 CSV")
        zero_words = _zero_live_words()
        if len(zero_words) != BMS_LIVE_WORDS or not _is_zero_live_snapshot(zero_words):
            raise RuntimeError("实时监控异常清零快照格式错误")
        if _is_zero_live_snapshot(words):
            raise RuntimeError("正常实时数据不应被识别为异常清零快照")
        app = UpgradeUi("", BMS_UART_DEFAULT_BAUD, DEFAULT_BIN, edition=UI_EDITION_INTERNAL)
        app.withdraw()
        app.update_idletasks()
        if set(app.param_widgets) != expected_param_ui_keys:
            raise RuntimeError("参数页控件与参数定义不一致")
        if len(app.param_page_ranges) != 4:
            raise RuntimeError("参数页业务分类数量不正确")
        if list(app.param_page_ranges.values()).count("protect") != 1 or list(app.param_page_ranges.values()).count("other") != 3:
            raise RuntimeError("参数页读取范围映射不正确")
        dirty_test_key = "SH309/单节过压(mv)"
        dirty_test_param = BMS_PARAM_BY_KEY[dirty_test_key]
        app.param_values[dirty_test_key] = 4200
        app.param_loading = True
        app.param_entry_vars[dirty_test_key].set(_param_display_value(dirty_test_param, 4200))
        app.param_loading = False
        app.param_entry_vars[dirty_test_key].set("4210")
        if dirty_test_key not in app.param_dirty or app.param_write_button_var.get() != tr("写入修改(1)"):
            raise RuntimeError("参数修改高亮状态未生效")
        app._clear_param_dirty()
        if app.param_entry_vars[dirty_test_key].get() != "4200" or app.param_dirty:
            raise RuntimeError("放弃参数修改未恢复最近读取值")
        app.destroy()
        customer_app = UpgradeUi("", BMS_UART_DEFAULT_BAUD, DEFAULT_BIN, edition=UI_EDITION_CUSTOMER)
        customer_app.withdraw()
        customer_app.update_idletasks()
        if len(customer_app.main_notebook.tabs()) != 2:
            raise RuntimeError("客户版默认页签数量不正确")
        original_askstring = simpledialog.askstring
        original_showerror = messagebox.showerror
        try:
            messagebox.showerror = lambda *args, **kwargs: None
            simpledialog.askstring = lambda *args, **kwargs: "wrong-password"
            customer_app._toggle_customer_access()
            if customer_app.customer_unlocked or len(customer_app.main_notebook.tabs()) != 2:
                raise RuntimeError("客户版错误密码未被拒绝")
            simpledialog.askstring = lambda *args, **kwargs: CUSTOMER_MAINTENANCE_PASSWORD
            customer_app._toggle_customer_access()
            if not customer_app.customer_unlocked or len(customer_app.main_notebook.tabs()) != 6:
                raise RuntimeError("客户版正确密码未能解锁维护页")
            customer_app._toggle_customer_access()
            if customer_app.customer_unlocked or len(customer_app.main_notebook.tabs()) != 2:
                raise RuntimeError("客户版重新锁定页签数量不正确")
        finally:
            simpledialog.askstring = original_askstring
            messagebox.showerror = original_showerror
        if args.language == LANG_EN:
            remaining_chinese: list[str] = []
            pending = [customer_app]
            while pending:
                widget = pending.pop()
                pending.extend(widget.winfo_children())
                try:
                    text = str(widget.cget("text"))
                except tk.TclError:
                    text = ""
                if contains_chinese(text):
                    remaining_chinese.append(text)
                try:
                    textvariable = str(widget.cget("textvariable"))
                    variable_text = str(widget.getvar(textvariable)) if textvariable else ""
                except (AttributeError, tk.TclError):
                    variable_text = ""
                if contains_chinese(variable_text) and not Path(variable_text).exists():
                    remaining_chinese.append(variable_text)
                if isinstance(widget, ttk.Notebook):
                    for tab_id in widget.tabs():
                        tab_text = str(widget.tab(tab_id, "text"))
                        if contains_chinese(tab_text):
                            remaining_chinese.append(tab_text)
                if isinstance(widget, ttk.Combobox):
                    for value in widget.cget("values"):
                        if contains_chinese(str(value)):
                            remaining_chinese.append(str(value))
                if isinstance(widget, ttk.Treeview):
                    for column in widget.cget("columns"):
                        heading_text = str(widget.heading(column, "text"))
                        if contains_chinese(heading_text):
                            remaining_chinese.append(heading_text)
                    tree_items = list(widget.get_children(""))
                    while tree_items:
                        item_id = tree_items.pop()
                        item = widget.item(item_id)
                        tree_items.extend(widget.get_children(item_id))
                        for value in (item.get("text", ""), *item.get("values", ())):
                            if contains_chinese(str(value)):
                                remaining_chinese.append(str(value))
            if remaining_chinese:
                raise RuntimeError(f"英文界面仍包含中文: {sorted(set(remaining_chinese))}")
        customer_app.destroy()
        print("comm_tool_upgrade_ui self-test OK")
        return 0
    baud = args.baud
    if baud is None:
        baud = BMS_UART_DEFAULT_BAUD
    app = UpgradeUi(args.port, baud, Path(args.bin), mode=args.mode, slave=args.slave, edition=args.edition)
    app.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
