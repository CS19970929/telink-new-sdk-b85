from __future__ import annotations

from dataclasses import dataclass, field
from datetime import datetime
from enum import Enum

from .protocol import afe_apply_status_text as format_afe_apply_status_text, format_register_word


class ScanMode(str, Enum):
    TARGET_FIRMWARE = "当前固件"
    ALL_DEVICES = "全部设备"

    @property
    def note(self) -> str:
        if self is ScanMode.TARGET_FIRMWARE:
            return "Qt 桌面 BLE 扫描先收全量设备，再按 `BT* / 180F / 1812` 过滤显示。"
        return "显示周围全部 BLE 设备。"


class DetailPage(str, Enum):
    BATTERY_STATUS = "电池状态"
    DEBUG_WORKBENCH = "调试工作台"


class ConnectionStatus(str, Enum):
    IDLE = "空闲"
    SCANNING = "扫描中"
    CONNECTING = "连接中"
    CONNECTED = "已连接"
    READY = "可收发"
    DISCONNECTED = "已断开"
    FAILED = "异常"


class ExchangeDirection(str, Enum):
    TX = "TX"
    RX = "RX"
    INFO = "INFO"
    ERROR = "ERR"


class BatteryDataSource(str, Enum):
    UNAVAILABLE = "未读取"
    LEGACY_REGISTERS = "旧寄存器兼容模式"
    REALTIME_WINDOW = "实时窗口模式"

    @property
    def note(self) -> str:
        if self is BatteryDataSource.UNAVAILABLE:
            return "尚未建立数据快照。"
        if self is BatteryDataSource.LEGACY_REGISTERS:
            return (
                "当前板子未返回 `0xD120~0xD12A`，上位机退回到 `0xD000~0xD03E` 与 "
                "`0xD115~0xD116` 的当前工程旧布局。"
            )
        return (
            "当前板子已返回 `0xD120~0xD12A`，电压、电流、温度、SOC 采用实时窗口；"
            "单串电压与状态字仍来自旧寄存器区。"
        )


@dataclass
class DiscoverySnapshot:
    local_name: str = ""
    peripheral_name: str = ""
    advertised_services: list[str] = field(default_factory=list)
    is_connectable: bool | None = None

    @property
    def preferred_name(self) -> str:
        if self.local_name:
            return self.local_name
        return self.peripheral_name

    @property
    def alternate_name(self) -> str:
        if self.local_name and self.peripheral_name and self.local_name != self.peripheral_name:
            return self.peripheral_name
        return ""


@dataclass
class DiscoveryEvent:
    device_id: str
    snapshot: DiscoverySnapshot
    rssi: int


@dataclass
class DiscoveredDevice:
    id: str
    name: str = ""
    alternate_name: str = ""
    rssi: int = -127
    last_seen: datetime = field(default_factory=datetime.now)
    is_connected: bool = False
    advertised_services: list[str] = field(default_factory=list)
    is_connectable: bool | None = None

    @property
    def display_name(self) -> str:
        if self.name:
            return self.name
        return f"Unnamed {self.id[:6]}"

    @property
    def rssi_summary(self) -> str:
        return f"{self.rssi} dBm"

    @property
    def advertised_services_summary(self) -> str:
        if not self.advertised_services:
            return "No advertised UUID"
        return ", ".join(self.advertised_services)

    @property
    def is_likely_bms(self) -> bool:
        uppercase_name = self.display_name.upper()
        if uppercase_name.startswith("BT"):
            return True

        advertised_set = {item.upper() for item in self.advertised_services}
        return (
            "180F" in advertised_set
            or "1812" in advertised_set
            or "0000180F-0000-1000-8000-00805F9B34FB" in advertised_set
            or "00001812-0000-1000-8000-00805F9B34FB" in advertised_set
        )


@dataclass
class ExchangeLogEntry:
    timestamp: datetime
    direction: ExchangeDirection
    title: str
    payload_hex: str
    note: str

    @property
    def timestamp_text(self) -> str:
        return self.timestamp.strftime("%H:%M:%S")


@dataclass
class DeviceIdentitySnapshot:
    display_name: str = "—"
    mac_address: str = "—"
    serial_number: str = "—"
    hardware_version: str = "—"
    software_version: str = "—"


@dataclass
class CellVoltageSample:
    index: int
    millivolts: int

    @property
    def title(self) -> str:
        return f"Cell {self.index}"

    @property
    def voltage_text(self) -> str:
        return f"{self.millivolts / 1000.0:.3f} V"

    @property
    def detail_text(self) -> str:
        return f"{self.millivolts} mV"


@dataclass
class StatusFlagSample:
    key: str
    title: str
    is_active: bool


@dataclass
class RegisterBlock:
    title: str
    start_address: int
    words: list[int]
    updated_at: datetime
    response_hex: str

    @property
    def updated_at_text(self) -> str:
        return self.updated_at.strftime("%H:%M:%S")

    @property
    def start_address_text(self) -> str:
        return f"0x{self.start_address:04X}"

    def word_lines(self) -> str:
        if not self.words:
            return "无数据"
        lines: list[str] = []
        for offset, value in enumerate(self.words):
            lines.append(format_register_word(self.start_address, offset, value))
        return "\n".join(lines)


@dataclass
class BatteryStatusSnapshot:
    is_supported: bool = False
    source: BatteryDataSource = BatteryDataSource.UNAVAILABLE
    supports_realtime_window: bool = False
    protocol_version: int = 0
    pack_voltage_raw: int = 0
    signed_current_raw: int = 0
    pack_voltage_32_raw_mv: int = 0
    signed_current_32_raw_ma: int = 0
    current_unit_ma: int = 100
    afe_apply_status_raw: int = 0
    soc_raw: int = 0
    max_temp_raw: int = 0
    min_temp_raw: int = 0
    mos_temp_raw: int = 0
    max_cell_voltage_raw: int = 0
    min_cell_voltage_raw: int = 0
    cell_delta_raw: int = 0
    max_cell_position: int = 0
    min_cell_position: int = 0
    soh_raw: int = 0
    capacity_now_raw: int = 0
    capacity_full_raw: int = 0
    capacity_factory_raw: int = 0
    cycle_count_raw: int = 0
    legacy_pack_voltage_raw_mv: int = 0
    legacy_battery_temp_adc_mv: int = 0
    legacy_mos_temp_adc_mv: int = 0
    cell_voltages: list[CellVoltageSample] = field(default_factory=list)
    status_flags: list[StatusFlagSample] = field(default_factory=list)
    system_status_raw: int = 0
    updated_at: datetime | None = None

    @staticmethod
    def empty() -> "BatteryStatusSnapshot":
        return BatteryStatusSnapshot()

    @staticmethod
    def decode(
        *,
        realtime_words: list[int],
        legacy_cell_words: list[int],
        system_status_words: list[int],
        register_catalog: object,
        updated_at: datetime,
    ) -> "BatteryStatusSnapshot":
        def word(index: int, words: list[int]) -> int:
            if 0 <= index < len(words):
                return int(words[index]) & 0xFFFF
            return 0

        def to_int16(value: int) -> int:
            value &= 0xFFFF
            return value - 0x10000 if value & 0x8000 else value

        def to_int32(value: int) -> int:
            value &= 0xFFFFFFFF
            return value - 0x100000000 if value & 0x80000000 else value

        cell_words = legacy_cell_words[: int(register_catalog.legacyCellArrayCount)]
        series_count = min(int(register_catalog.currentProjectSeriesCount), len(cell_words))
        cells = [
            CellVoltageSample(index=index + 1, millivolts=int(value))
            for index, value in enumerate(cell_words[:series_count])
        ]

        legacy_charge_current = to_int16(word(register_catalog.legacyChargeCurrentIndex, cell_words))
        legacy_discharge_current = to_int16(word(register_catalog.legacyDischargeCurrentIndex, cell_words))
        legacy_signed_current = -legacy_discharge_current if legacy_discharge_current > 0 else legacy_charge_current

        status_low = int(system_status_words[0]) if len(system_status_words) >= 1 else 0
        status_high = int(system_status_words[1]) if len(system_status_words) >= 2 else 0
        status_raw = status_low | (status_high << 16)

        snapshot = BatteryStatusSnapshot(
            is_supported=bool(cells or system_status_words),
            source=BatteryDataSource.LEGACY_REGISTERS,
            supports_realtime_window=False,
            protocol_version=0,
            pack_voltage_raw=word(register_catalog.legacyPackVoltageEngineeringIndex, cell_words),
            signed_current_raw=legacy_signed_current,
            soc_raw=word(register_catalog.legacySocIndex, cell_words),
            max_temp_raw=word(register_catalog.legacyMaxTempIndex, cell_words),
            min_temp_raw=word(register_catalog.legacyMinTempIndex, cell_words),
            mos_temp_raw=word(register_catalog.legacyMosTemperatureIndex, cell_words),
            max_cell_voltage_raw=word(register_catalog.legacyMaxCellVoltageIndex, cell_words),
            min_cell_voltage_raw=word(register_catalog.legacyMinCellVoltageIndex, cell_words),
            cell_delta_raw=word(register_catalog.legacyCellDeltaIndex, cell_words),
            max_cell_position=word(register_catalog.legacyMaxCellPositionIndex, cell_words),
            min_cell_position=word(register_catalog.legacyMinCellPositionIndex, cell_words),
            soh_raw=word(register_catalog.legacySohIndex, cell_words),
            capacity_now_raw=word(register_catalog.legacyCapacityNowIndex, cell_words),
            capacity_full_raw=word(register_catalog.legacyCapacityFullIndex, cell_words),
            capacity_factory_raw=word(register_catalog.legacyCapacityFactoryIndex, cell_words),
            cycle_count_raw=word(register_catalog.legacyCycleCountIndex, cell_words),
            legacy_pack_voltage_raw_mv=word(register_catalog.legacyPackVoltageADCIndex, cell_words),
            legacy_battery_temp_adc_mv=word(register_catalog.legacyBatteryTempADCIndex, cell_words),
            legacy_mos_temp_adc_mv=word(register_catalog.legacyMosTempADCIndex, cell_words),
            cell_voltages=cells,
            status_flags=battery_status_flags(status_raw),
            system_status_raw=status_raw,
            updated_at=updated_at,
        )

        if (
            len(realtime_words) >= int(register_catalog.realtimeStatusCount)
            and realtime_words[0] == int(register_catalog.realtimeStatusMagic)
        ):
            snapshot.source = BatteryDataSource.REALTIME_WINDOW
            snapshot.supports_realtime_window = True
            snapshot.protocol_version = int(realtime_words[1])
            snapshot.pack_voltage_raw = int(realtime_words[2])
            snapshot.signed_current_raw = to_int16(int(realtime_words[3]))
            snapshot.soc_raw = int(realtime_words[4])
            snapshot.max_temp_raw = int(realtime_words[5])
            snapshot.min_temp_raw = int(realtime_words[6])
            snapshot.mos_temp_raw = int(realtime_words[7])
            snapshot.max_cell_voltage_raw = int(realtime_words[8])
            snapshot.min_cell_voltage_raw = int(realtime_words[9])
            snapshot.cell_delta_raw = int(realtime_words[10])
            if snapshot.protocol_version >= 2:
                pack_mv = (
                    (word(register_catalog.realtimePackVoltage32HighIndex, realtime_words) << 16)
                    | word(register_catalog.realtimePackVoltage32LowIndex, realtime_words)
                )
                current_ma = to_int32(
                    (word(register_catalog.realtimeCurrentMaHighIndex, realtime_words) << 16)
                    | word(register_catalog.realtimeCurrentMaLowIndex, realtime_words)
                )
                snapshot.pack_voltage_32_raw_mv = pack_mv
                snapshot.signed_current_32_raw_ma = current_ma
                snapshot.current_unit_ma = word(register_catalog.realtimeCurrentUnitIndex, realtime_words) or 1
                snapshot.afe_apply_status_raw = word(register_catalog.realtimeAfeApplyStatusIndex, realtime_words)
                snapshot.pack_voltage_raw = pack_mv
                snapshot.signed_current_raw = current_ma

        return snapshot

    @property
    def source_title(self) -> str:
        return self.source.value

    @property
    def source_note(self) -> str:
        return self.source.note

    @property
    def pack_voltage_text(self) -> str:
        if not self.is_supported:
            return "—"
        divisor = 1000.0 if self.supports_realtime_window and self.protocol_version >= 2 else 100.0
        return f"{self.pack_voltage_raw / divisor:.2f} V"

    @property
    def pack_voltage_detail_text(self) -> str:
        if not self.is_supported:
            return "—"
        if self.supports_realtime_window:
            if self.protocol_version >= 2:
                return f"实时窗口 v{self.protocol_version} {self.pack_voltage_32_raw_mv} mV"
            return f"实时窗口 raw {self.pack_voltage_raw}"
        return f"旧寄存器 raw {self.pack_voltage_raw} / 镜像 {self.legacy_pack_voltage_raw_mv} mV"

    @property
    def current_text(self) -> str:
        if not self.is_supported:
            return "—"
        if self.supports_realtime_window and self.protocol_version >= 2:
            return f"{(self.signed_current_raw * self.current_unit_ma) / 1000.0:.3f} A"
        return f"{self.signed_current_raw / 10.0:.1f} A"

    @property
    def current_direction_text(self) -> str:
        if not self.is_supported:
            return "未支持"
        if self.signed_current_raw > 0:
            return "充电"
        if self.signed_current_raw < 0:
            return "放电"
        return "静置"

    @property
    def afe_apply_status_text(self) -> str:
        if not self.supports_realtime_window or self.protocol_version < 2:
            return "N/A"
        return format_afe_apply_status_text(self.afe_apply_status_raw)

    @property
    def soc_text(self) -> str:
        if not self.is_supported:
            return "—"
        return f"{self.soc_raw} %"

    def _temperature_text(self, raw: int) -> str:
        if not self.is_supported:
            return "—"
        return f"{raw / 10.0 - 40.0:.1f} °C"

    @property
    def max_temp_text(self) -> str:
        return self._temperature_text(self.max_temp_raw)

    @property
    def min_temp_text(self) -> str:
        return self._temperature_text(self.min_temp_raw)

    @property
    def mos_temp_text(self) -> str:
        return self._temperature_text(self.mos_temp_raw)

    @property
    def max_cell_voltage_text(self) -> str:
        if not self.is_supported or self.max_cell_voltage_raw <= 0:
            return "—"
        return f"{self.max_cell_voltage_raw} mV"

    @property
    def min_cell_voltage_text(self) -> str:
        if not self.is_supported or self.min_cell_voltage_raw <= 0:
            return "—"
        return f"{self.min_cell_voltage_raw} mV"

    @property
    def cell_delta_text(self) -> str:
        if not self.is_supported:
            return "—"
        if self.cell_delta_raw <= 0 and self.max_cell_voltage_raw <= 0 and self.min_cell_voltage_raw <= 0:
            return "—"
        return f"{self.cell_delta_raw} mV"

    @property
    def max_cell_position_text(self) -> str:
        if self.max_cell_position <= 0:
            return "—"
        return f"Cell {self.max_cell_position}"

    @property
    def min_cell_position_text(self) -> str:
        if self.min_cell_position <= 0:
            return "—"
        return f"Cell {self.min_cell_position}"

    @property
    def legacy_battery_temp_adc_text(self) -> str:
        if self.legacy_battery_temp_adc_mv <= 0:
            return "—"
        return f"{self.legacy_battery_temp_adc_mv} mV"

    @property
    def legacy_mos_temp_adc_text(self) -> str:
        if self.legacy_mos_temp_adc_mv <= 0:
            return "—"
        return f"{self.legacy_mos_temp_adc_mv} mV"

    @property
    def soh_text(self) -> str:
        if not self.is_supported:
            return "—"
        return f"{self.soh_raw} %"

    @property
    def cycle_count_text(self) -> str:
        if not self.is_supported:
            return "—"
        return str(self.cycle_count_raw)

    def _capacity_text(self, raw: int) -> str:
        if not self.is_supported:
            return "—"
        return f"{raw / 100.0:.2f} Ah"

    @property
    def capacity_now_text(self) -> str:
        return self._capacity_text(self.capacity_now_raw)

    @property
    def capacity_full_text(self) -> str:
        return self._capacity_text(self.capacity_full_raw)

    @property
    def capacity_factory_text(self) -> str:
        return self._capacity_text(self.capacity_factory_raw)

    @property
    def active_status_flags(self) -> list[StatusFlagSample]:
        return [item for item in self.status_flags if item.is_active]

    @property
    def system_status_hex_text(self) -> str:
        return f"0x{self.system_status_raw:08X}"

    @property
    def updated_at_text(self) -> str:
        if self.updated_at is None:
            return "未刷新"
        return self.updated_at.strftime("%H:%M:%S")


def battery_status_flags(raw: int) -> list[StatusFlagSample]:
    def bit(index: int) -> bool:
        return ((raw >> index) & 0x1) == 0x1

    return [
        StatusFlagSample("startup", "启动完成", bit(0)),
        StatusFlagSample("mos_pre", "预充 MOS", bit(1)),
        StatusFlagSample("mos_chg", "充电 MOS", bit(2)),
        StatusFlagSample("mos_dsg", "放电 MOS", bit(3)),
        StatusFlagSample("relay_pre", "预充继电器", bit(4)),
        StatusFlagSample("relay_chg", "充电继电器", bit(5)),
        StatusFlagSample("relay_dsg", "放电继电器", bit(6)),
        StatusFlagSample("relay_main", "主继电器", bit(7)),
        StatusFlagSample("heat", "加热", bit(8)),
        StatusFlagSample("cool", "冷却", bit(9)),
        StatusFlagSample("afe1", "AFE1", bit(10)),
        StatusFlagSample("afe2", "AFE2", bit(11)),
        StatusFlagSample("balance", "均衡", bit(12)),
        StatusFlagSample("sleep", "待休眠", bit(13)),
        StatusFlagSample("bn_close", "BMS 关断输出", bit(14)),
        StatusFlagSample("heat_close", "加热关闭输出", bit(15)),
        StatusFlagSample("driver_ext", "外部驱动控制", bit(18)),
    ]
