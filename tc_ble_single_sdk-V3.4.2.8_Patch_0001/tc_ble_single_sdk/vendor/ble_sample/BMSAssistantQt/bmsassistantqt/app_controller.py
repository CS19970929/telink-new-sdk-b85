from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass
from datetime import datetime

from PySide6.QtCore import QObject, QTimer, Signal

from .ble_transport import BLETransport
from .serial_transport import SerialTransport
from .models import (
    BatteryStatusSnapshot,
    ConnectionStatus,
    DeviceIdentitySnapshot,
    DiscoveredDevice,
    ExchangeDirection,
    ExchangeLogEntry,
    RegisterBlock,
    ScanMode,
)
from .protocol import (
    AccumulatorEvent,
    BMSUUIDs,
    ModbusCodecError,
    RegisterCatalog,
    ResponseAccumulator,
    ascii_string_from_words,
    echo,
    encode_ascii_words,
    ensure_safe_ble_length,
    mac_string_from_words,
    parse_address,
    parse_raw_bytes,
    parse_response,
    parse_words,
    read_holding,
    spaced_hex,
    write_multiple,
    write_single,
)


@dataclass
class CommandStep:
    title: str
    request: bytes
    handler: Callable[[object, bytes], None]
    expected_length_hint: int | None = None


@dataclass
class PendingExchange:
    name: str
    on_success: Callable[[bytes], None]
    on_failure: Callable[[Exception], None]
    timer: QTimer


@dataclass
class PendingSequence:
    action_name: str
    steps: list[CommandStep]
    on_complete: Callable[[], None] | None = None
    index: int = 0


class AppController(QObject):
    devicesChanged = Signal()
    statusChanged = Signal()
    identityChanged = Signal()
    batteryChanged = Signal()
    blocksChanged = Signal()
    logsChanged = Signal()
    responsePreviewChanged = Signal()

    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)

        self.bluetooth_state_label = "未知"
        self.serial_state_label = "未打开"
        self.link_mode = "BLE"
        self.connection_status = ConnectionStatus.IDLE
        self.status_message = "等待通信初始化"
        self.scan_mode = ScanMode.ALL_DEVICES
        self.search_text = ""
        self.show_only_likely_bms = False
        self.is_scanning = False
        self.selected_device_id: str | None = None
        self.devices: dict[str, DiscoveredDevice] = {}
        self.serial_ports: list[str] = []
        self.serial_port_name = ""
        self.serial_baudrate = "115200"

        self.identity = DeviceIdentitySnapshot()
        self.battery_status = BatteryStatusSnapshot.empty()

        self.latest_cell_array_block: RegisterBlock | None = None
        self.latest_manual_block: RegisterBlock | None = None
        self.latest_protect_block: RegisterBlock | None = None
        self.latest_event_log_block: RegisterBlock | None = None
        self.latest_realtime_block: RegisterBlock | None = None
        self.latest_status_block: RegisterBlock | None = None
        self.latest_afe_param_block: RegisterBlock | None = None

        self.logs: list[ExchangeLogEntry] = []
        self.manual_read_address = "0x0000"
        self.manual_read_quantity = "3"
        self.manual_write_address = "0x1005"
        self.manual_write_words = "0x0032"
        self.afe_param_read_count = str(RegisterCatalog.afeParamCount)
        self.afe_param_write_offset = "8"
        self.afe_param_write_words = ""
        self.quick_soc_value = "60"
        self.raw_hex_command = ""
        self.bt_name_suffix = ""
        self.response_preview = ""
        self.busy_command_name: str | None = None

        self._ble_transport = BLETransport(self)
        self._serial_transport = SerialTransport(self)
        self._transport = self._ble_transport
        self._accumulator = ResponseAccumulator()
        self._pending_exchange: PendingExchange | None = None
        self._pending_sequence: PendingSequence | None = None
        self._connected_device_id: str | None = None

        self._wire_transport()
        self._ble_transport.activate()
        self._serial_transport.activate()
        self.refresh_serial_ports()

    @property
    def connected_device_name(self) -> str:
        if self.link_mode == "串口" and self._connected_device_id:
            return self._connected_device_id.replace("serial:", "")
        if not self._connected_device_id:
            return "未连接"
        device = self.devices.get(self._connected_device_id)
        if device is None:
            return "未连接"
        return device.display_name

    @property
    def current_transport_state(self) -> str:
        return self.serial_state_label if self.link_mode == "串口" else self.bluetooth_state_label

    @property
    def can_send_commands(self) -> bool:
        return self.connection_status is ConnectionStatus.READY and self.busy_command_name is None

    @property
    def filtered_devices(self) -> list[DiscoveredDevice]:
        normalized_search = self.search_text.strip().lower()
        items = list(self.devices.values())

        def matches_mode(device: DiscoveredDevice) -> bool:
            if self.scan_mode is ScanMode.ALL_DEVICES:
                return True
            return device.is_likely_bms

        filtered = []
        for device in items:
            if not matches_mode(device):
                continue
            if self.show_only_likely_bms and not device.is_likely_bms:
                continue
            if normalized_search:
                haystack = " ".join(
                    [
                        device.display_name,
                        device.alternate_name,
                        device.advertised_services_summary,
                    ]
                ).lower()
                if normalized_search not in haystack:
                    continue
            filtered.append(device)

        filtered.sort(
            key=lambda item: (
                not item.is_connected,
                not item.is_likely_bms,
                -item.rssi,
                item.display_name,
            )
        )
        return filtered

    def set_scan_mode(self, value: str) -> None:
        self.scan_mode = ScanMode(value)
        self.statusChanged.emit()
        self.devicesChanged.emit()

    def set_search_text(self, value: str) -> None:
        self.search_text = value
        self.devicesChanged.emit()

    def set_show_only_likely_bms(self, enabled: bool) -> None:
        self.show_only_likely_bms = enabled
        self.devicesChanged.emit()

    def set_link_mode(self, value: str) -> None:
        if value not in {"BLE", "串口"}:
            return
        if value == self.link_mode:
            self._transport = self._serial_transport if value == "串口" else self._ble_transport
            return
        if self.connection_status in {
            ConnectionStatus.CONNECTING,
            ConnectionStatus.CONNECTED,
            ConnectionStatus.READY,
        }:
            self.disconnect()
        self.link_mode = value
        self._transport = self._serial_transport if value == "串口" else self._ble_transport
        self.connection_status = ConnectionStatus.IDLE
        self.status_message = f"已切换到{value}连接"
        self.statusChanged.emit()
        self.devicesChanged.emit()

    def refresh_serial_ports(self) -> None:
        self.serial_ports = self._serial_transport.available_ports()
        if self.serial_port_name not in self.serial_ports:
            self.serial_port_name = self.serial_ports[0] if self.serial_ports else ""
        self.statusChanged.emit()

    def set_serial_port(self, value: str) -> None:
        self.serial_port_name = value
        self.statusChanged.emit()

    def set_serial_baudrate(self, value: str) -> None:
        self.serial_baudrate = value.strip() or "115200"
        self.statusChanged.emit()

    def set_selected_device(self, device_id: str | None) -> None:
        self.selected_device_id = device_id
        self.devicesChanged.emit()

    def toggle_scan(self) -> None:
        if self.is_scanning:
            self.stop_scan()
        else:
            self.start_scan()

    def start_scan(self) -> None:
        if self.link_mode != "BLE":
            self.status_message = "当前为串口模式，请切换到 BLE 后再扫描"
            self.statusChanged.emit()
            return
        self.is_scanning = True
        self.connection_status = ConnectionStatus.SCANNING
        self.status_message = f"正在扫描附近设备: {self.scan_mode.note}"
        self.statusChanged.emit()
        self._ble_transport.start_scan()

    def stop_scan(self) -> None:
        self.is_scanning = False
        if self.connection_status is ConnectionStatus.SCANNING:
            self.connection_status = ConnectionStatus.IDLE
        self.status_message = "已停止扫描"
        self.statusChanged.emit()
        self._ble_transport.stop_scan()

    def connect_selected(self) -> None:
        if self.selected_device_id:
            self.connect_to(self.selected_device_id)

    def connect_to(self, device_id: str) -> None:
        self.set_link_mode("BLE")
        self.selected_device_id = device_id
        self.status_message = f"准备连接 {self.devices.get(device_id).display_name if device_id in self.devices else device_id}"
        self.statusChanged.emit()
        self._ble_transport.connect_device(device_id)

    def connect_serial_selected(self) -> None:
        self.set_link_mode("串口")
        try:
            baudrate = int(self.serial_baudrate)
        except ValueError as exc:
            raise ModbusCodecError(f"波特率无效: {self.serial_baudrate}") from exc
        self._serial_transport.connect_port(self.serial_port_name, baudrate)

    def disconnect(self) -> None:
        self._transport.disconnect_current()

    def refresh_identity(self) -> None:
        result: dict[str, RegisterBlock] = {}
        steps = [
            self._make_read_step("读取 MAC 地址", RegisterCatalog.macAddressStart, RegisterCatalog.macAddressCount, lambda block: result.__setitem__("mac", block)),
            self._make_read_step("读取序列号", RegisterCatalog.productSerialStart, RegisterCatalog.productTextCount, lambda block: result.__setitem__("serial", block)),
            self._make_read_step("读取硬件版本", RegisterCatalog.productHardwareStart, RegisterCatalog.productTextCount, lambda block: result.__setitem__("hardware", block)),
            self._make_read_step("读取软件版本", RegisterCatalog.productSoftwareStart, RegisterCatalog.productTextCount, lambda block: result.__setitem__("software", block)),
        ]

        def on_complete() -> None:
            self.identity.display_name = self.connected_device_name
            self.identity.mac_address = mac_string_from_words(result["mac"].words)
            self.identity.serial_number = ascii_string_from_words(result["serial"].words)
            self.identity.hardware_version = ascii_string_from_words(result["hardware"].words)
            self.identity.software_version = ascii_string_from_words(result["software"].words)
            self.status_message = "设备身份信息已刷新"
            self.identityChanged.emit()
            self.statusChanged.emit()

        self._start_sequence("刷新设备身份", steps, on_complete)

    def refresh_battery_status(self) -> None:
        result: dict[str, RegisterBlock] = {}
        steps = [
            self._make_read_step("单串电压与兼容数据", RegisterCatalog.legacyCellArrayStart, RegisterCatalog.legacyCellArrayCount, lambda block: result.__setitem__("legacy", block)),
            self._make_read_step("系统状态", RegisterCatalog.systemStatusStart, RegisterCatalog.systemStatusCount, lambda block: result.__setitem__("status", block)),
            self._make_read_step("电池状态页", RegisterCatalog.realtimeStatusStart, RegisterCatalog.realtimeStatusCount, lambda block: result.__setitem__("realtime", block)),
        ]

        def on_complete() -> None:
            legacy = result["legacy"]
            status = result["status"]
            realtime = result["realtime"]

            self.latest_cell_array_block = legacy
            self.latest_status_block = status
            self.latest_realtime_block = realtime

            self.battery_status = BatteryStatusSnapshot.decode(
                realtime_words=realtime.words,
                legacy_cell_words=legacy.words,
                system_status_words=status.words,
                register_catalog=RegisterCatalog,
                updated_at=datetime.now(),
            )
            if self.battery_status.supports_realtime_window:
                self.status_message = "电池状态已刷新（实时窗口模式）"
            else:
                self.status_message = "电池状态已刷新（旧寄存器兼容模式）"
            self.batteryChanged.emit()
            self.blocksChanged.emit()
            self.statusChanged.emit()

        self._start_sequence("刷新电池状态", steps, on_complete)

    def read_protect_preview(self) -> None:
        steps = [
            self._make_read_step("保护参数预览", RegisterCatalog.protectStart, RegisterCatalog.protectPreviewCount, self._assign_protect_block),
        ]

        def on_complete() -> None:
            self.status_message = "已读取保护参数预览"
            self.statusChanged.emit()

        self._start_sequence("读取保护参数预览", steps, on_complete)

    def read_afe_params(self) -> None:
        quantity = parse_address(self.afe_param_read_count)
        if quantity <= 0 or quantity > RegisterCatalog.afeParamCount:
            raise ModbusCodecError(f"AFE 参数读取数量必须在 1~{RegisterCatalog.afeParamCount} 之间")
        steps = [
            self._make_read_step("SH3673520 AFE 参数", RegisterCatalog.afeParamStart, quantity, self._assign_afe_param_block),
        ]

        def on_complete() -> None:
            self.status_message = "已读取 SH3673520 AFE 参数"
            self.statusChanged.emit()

        self._start_sequence("读取 AFE 参数", steps, on_complete)

    def write_afe_params(self) -> None:
        offset = parse_address(self.afe_param_write_offset)
        words = parse_words(self.afe_param_write_words)
        if not words:
            raise ModbusCodecError("AFE 参数写入值不能为空")
        if offset < 0 or (offset + len(words)) > RegisterCatalog.afePersistentParamCount:
            raise ModbusCodecError(f"AFE 参数偏移越界: offset={offset}, words={len(words)}")

        register = RegisterCatalog.afeParamStart + offset
        request = write_single(register, words[0]) if len(words) == 1 else write_multiple(register, words)
        self._ensure_request_length(request)

        def handler(parsed: object, raw: bytes) -> None:
            response = parse_response(raw)
            if response.kind not in {"write_single_ack", "write_multiple_ack"}:
                raise ModbusCodecError("AFE 参数写入响应类型错误")

        steps = [CommandStep("写入 SH3673520 AFE 参数", request, handler)]

        def on_complete() -> None:
            self.status_message = "AFE 参数已写入，固件已保存并重新应用"
            self.statusChanged.emit()
            self.read_afe_params()

        self._start_sequence("写入 AFE 参数", steps, on_complete)

    def read_system_status(self) -> None:
        steps = [
            self._make_read_step("系统状态", RegisterCatalog.systemStatusStart, RegisterCatalog.systemStatusCount, self._assign_status_block),
        ]

        def on_complete() -> None:
            self.status_message = "已读取系统状态"
            self.statusChanged.emit()

        self._start_sequence("读取系统状态", steps, on_complete)

    def read_event_log_preview(self) -> None:
        steps = [
            self._make_read_step("事件日志预览", RegisterCatalog.eventLogStart, RegisterCatalog.eventLogPreviewCount, self._assign_event_log_block),
        ]

        def on_complete() -> None:
            self.status_message = "已读取事件日志预览"
            self.statusChanged.emit()

        self._start_sequence("读取事件日志预览", steps, on_complete)

    def send_echo_test(self) -> None:
        request = echo(bytes([0x12, 0x34, 0x56, 0x78]))
        self._ensure_request_length(request)

        def handler(parsed: object, raw: bytes) -> None:
            response = parse_response(raw)
            if response.kind != "echo":
                raise ModbusCodecError("Echo 响应类型错误")

        steps = [CommandStep("Echo 测试", request, handler, expected_length_hint=len(request))]

        def on_complete() -> None:
            self.status_message = "Echo 成功，链路可收发"
            self.statusChanged.emit()

        self._start_sequence("Echo 测试", steps, on_complete)

    def read_manual_block(self) -> None:
        start = parse_address(self.manual_read_address)
        quantity = parse_address(self.manual_read_quantity)
        steps = [self._make_read_step("手动读取", start, quantity, self._assign_manual_block)]

        def on_complete() -> None:
            self.status_message = "手动读取完成"
            self.statusChanged.emit()

        self._start_sequence("手动读取寄存器", steps, on_complete)

    def write_manual_words(self) -> None:
        register = parse_address(self.manual_write_address)
        words = parse_words(self.manual_write_words)
        if not words:
            raise ModbusCodecError("写入值为空")

        request = write_single(register, words[0]) if len(words) == 1 else write_multiple(register, words)
        self._ensure_request_length(request)

        def handler(parsed: object, raw: bytes) -> None:
            response = parse_response(raw)
            if response.kind not in {"write_single_ack", "write_multiple_ack"}:
                raise ModbusCodecError("响应内容不符合预期: 写寄存器响应类型错误")

        steps = [CommandStep("手动写寄存器", request, handler)]

        def on_complete() -> None:
            self.status_message = "写入完成"
            self.statusChanged.emit()

        self._start_sequence("手动写寄存器", steps, on_complete)

    def write_soc_value(self) -> None:
        value = parse_address(self.quick_soc_value)
        if value > 100:
            raise ModbusCodecError("SOC 建议范围 0~100")

        request = write_single(RegisterCatalog.socWriteRegister, value)
        self._ensure_request_length(request)

        def handler(parsed: object, raw: bytes) -> None:
            response = parse_response(raw)
            if response.kind != "write_single_ack":
                raise ModbusCodecError("响应内容不符合预期: SOC 写入响应类型错误")

        steps = [CommandStep("写入 SOC", request, handler)]

        def on_complete() -> None:
            self.status_message = f"SOC 写入完成，已写 `0x1005 = {value}`"
            self.statusChanged.emit()
            self.refresh_battery_status()

        self._start_sequence("写入 SOC", steps, on_complete)

    def write_debug_1103_shortcut(self) -> None:
        request = write_single(RegisterCatalog.debugRegister1103, 0x0003)
        self._ensure_request_length(request)

        def handler(parsed: object, raw: bytes) -> None:
            response = parse_response(raw)
            if response.kind != "write_single_ack":
                raise ModbusCodecError("响应内容不符合预期: `0x1103` 写入响应类型错误")

        steps = [CommandStep("写入 0x1103", request, handler)]

        def on_complete() -> None:
            self.status_message = "已写 `0x1103 = 0x0003`"
            self.statusChanged.emit()

        self._start_sequence("写入 0x1103", steps, on_complete)

    def send_raw_command(self) -> None:
        request = parse_raw_bytes(self.raw_hex_command)
        self._ensure_request_length(request)
        expected_length = len(request) if len(request) >= 2 and request[1] == 0x7F else None

        def handler(parsed: object, raw: bytes) -> None:
            _ = parse_response(raw)

        steps = [CommandStep("原始帧", request, handler, expected_length_hint=expected_length)]

        def on_complete() -> None:
            self.status_message = "原始帧发送完成"
            self.statusChanged.emit()

        self._start_sequence("发送原始 Modbus 帧", steps, on_complete)

    def write_bluetooth_name_suffix(self) -> None:
        suffix = self.bt_name_suffix.strip()
        if not suffix:
            raise ModbusCodecError("蓝牙名后缀不能为空")
        if len(suffix.encode("utf-8")) > RegisterCatalog.btNameMaxWriteBytes:
            raise ModbusCodecError("蓝牙名后缀建议不超过 10 个 ASCII 字节")

        words = encode_ascii_words(suffix)
        request = write_multiple(RegisterCatalog.btNameStart, words)
        self._ensure_request_length(request)

        def handler(parsed: object, raw: bytes) -> None:
            response = parse_response(raw)
            if response.kind != "write_multiple_ack":
                raise ModbusCodecError("响应内容不符合预期: 蓝牙名写入响应类型错误")

        steps = [CommandStep("写入蓝牙名", request, handler)]

        def on_complete() -> None:
            self.status_message = "蓝牙名已写入，请重新扫描确认广播名刷新"
            self.statusChanged.emit()

        self._start_sequence("写入蓝牙名后缀", steps, on_complete)

    def clear_logs(self) -> None:
        self.logs.clear()
        self.logsChanged.emit()

    def clear_devices(self) -> None:
        self.devices.clear()
        self.selected_device_id = None
        self.status_message = "已清空扫描列表"
        self.devicesChanged.emit()
        self.statusChanged.emit()

    def report_external_error(self, message: str) -> None:
        self._append_log(ExchangeDirection.ERROR, "输入或界面错误", "", message)
        self.logsChanged.emit()
        self.status_message = message
        self.statusChanged.emit()

    def battery_blocks(self) -> list[RegisterBlock]:
        return [item for item in [self.latest_cell_array_block, self.latest_status_block, self.latest_realtime_block] if item is not None]

    def debug_blocks(self) -> list[RegisterBlock]:
        return [
            item
            for item in [
                self.latest_status_block,
                self.latest_protect_block,
                self.latest_afe_param_block,
                self.latest_event_log_block,
                self.latest_manual_block,
            ]
            if item is not None
        ]

    def _wire_transport(self) -> None:
        self._ble_transport.bluetoothStateChanged.connect(self._on_bluetooth_state_changed)
        self._ble_transport.discoveryReceived.connect(self._on_discovery_received)
        self._ble_transport.connectionChanged.connect(self._on_connection_changed)
        self._ble_transport.ready.connect(self._on_transport_ready)
        self._ble_transport.dataReceived.connect(self._on_transport_data)
        self._ble_transport.errorOccurred.connect(self._on_transport_error)

        self._serial_transport.stateChanged.connect(self._on_serial_state_changed)
        self._serial_transport.connectionChanged.connect(self._on_connection_changed)
        self._serial_transport.ready.connect(self._on_transport_ready)
        self._serial_transport.dataReceived.connect(self._on_transport_data)
        self._serial_transport.errorOccurred.connect(self._on_transport_error)

    def _on_bluetooth_state_changed(self, label: str) -> None:
        self.bluetooth_state_label = label
        self.statusChanged.emit()

    def _on_serial_state_changed(self, label: str) -> None:
        self.serial_state_label = label
        self.statusChanged.emit()

    def _on_discovery_received(self, event: object) -> None:
        if not hasattr(event, "device_id"):
            return
        device_id = event.device_id
        snapshot = event.snapshot
        rssi = event.rssi

        if device_id in self.devices:
            device = self.devices[device_id]
            if snapshot.preferred_name:
                device.name = snapshot.preferred_name
            if snapshot.alternate_name:
                device.alternate_name = snapshot.alternate_name
            device.rssi = rssi
            device.last_seen = datetime.now()
            if snapshot.advertised_services:
                device.advertised_services = snapshot.advertised_services
            if snapshot.is_connectable is not None:
                device.is_connectable = snapshot.is_connectable
        else:
            self.devices[device_id] = DiscoveredDevice(
                id=device_id,
                name=snapshot.preferred_name,
                alternate_name=snapshot.alternate_name,
                rssi=rssi,
                last_seen=datetime.now(),
                is_connected=False,
                advertised_services=list(snapshot.advertised_services),
                is_connectable=snapshot.is_connectable,
            )

        if self.selected_device_id is None:
            filtered = self.filtered_devices
            if filtered:
                self.selected_device_id = filtered[0].id
            else:
                self.selected_device_id = device_id

        if self.is_scanning:
            total_count = len(self.devices)
            visible_count = len(self.filtered_devices)
            if visible_count == 0 and total_count > 0 and self.scan_mode is ScanMode.TARGET_FIRMWARE:
                self.status_message = (
                    f"扫描中，已发现 {total_count} 台设备，但当前“当前固件”过滤后为 0。"
                    "请切到“全部设备”再看。"
                )
            else:
                self.status_message = f"扫描中，已发现 {total_count} 台设备，当前显示 {visible_count} 台。"
            self.statusChanged.emit()
        self.devicesChanged.emit()

    def _on_connection_changed(self, device_id: str, status_text: str, message: str) -> None:
        try:
            self.connection_status = ConnectionStatus(status_text)
        except ValueError:
            self.connection_status = ConnectionStatus.FAILED

        self.status_message = message
        if self.connection_status in {ConnectionStatus.DISCONNECTED, ConnectionStatus.FAILED}:
            self._connected_device_id = None
            self._mark_connected_device(None)
            self.battery_status = BatteryStatusSnapshot.empty()
            self.latest_cell_array_block = None
            self.latest_realtime_block = None
            self.latest_status_block = None
            self.batteryChanged.emit()
            self.blocksChanged.emit()
            self._cancel_active_workflow(ModbusCodecError(message), log_error=False)

        if self.connection_status in {ConnectionStatus.CONNECTING, ConnectionStatus.CONNECTED}:
            self._connected_device_id = device_id
            self.selected_device_id = device_id
            self._mark_connected_device(device_id)

        if self.connection_status is not ConnectionStatus.SCANNING:
            self.is_scanning = False

        self.devicesChanged.emit()
        self.statusChanged.emit()

    def _on_transport_ready(self, device_id: str) -> None:
        self._connected_device_id = device_id
        self._mark_connected_device(device_id)
        self.connection_status = ConnectionStatus.READY
        self.status_message = f"{self.link_mode} 通道已就绪，可直接收发 Modbus RTU"
        note = f"已订阅响应特征 {BMSUUIDs.responseCharacteristic}" if self.link_mode == "BLE" else self.connected_device_name
        self._append_log(ExchangeDirection.INFO, f"{self.link_mode} 就绪", "", note)
        self.devicesChanged.emit()
        self.logsChanged.emit()
        self.statusChanged.emit()
        self.refresh_battery_status()

    def _on_transport_data(self, fragment: object) -> None:
        if not isinstance(fragment, (bytes, bytearray)):
            return

        event = self._accumulator.append(bytes(fragment))
        if event.state == "waiting":
            self.response_preview = spaced_hex(self._accumulator.buffer)
            if event.expected_length is None:
                self.status_message = f"接收响应中：已收 {len(self._accumulator.buffer)} byte"
            else:
                self.status_message = (
                    f"接收响应中：已收 {len(self._accumulator.buffer)}/{event.expected_length} byte，"
                    f"分片 {event.fragments}"
                )
            self.responsePreviewChanged.emit()
            self.statusChanged.emit()
            return

        if event.state == "invalid_crc":
            self.response_preview = spaced_hex(event.frame)
            self.responsePreviewChanged.emit()
            self._append_log(ExchangeDirection.ERROR, self._current_exchange_name(), self.response_preview, f"assembled from {event.fragments} notify")
            self.logsChanged.emit()
            self._fail_pending_exchange(ModbusCodecError("收到的响应 CRC 校验失败"))
            return

        self.response_preview = spaced_hex(event.frame)
        self.responsePreviewChanged.emit()
        self._append_log(ExchangeDirection.RX, self._current_exchange_name(), self.response_preview, f"assembled from {event.fragments} notify")
        self.logsChanged.emit()
        self._succeed_pending_exchange(event.frame)

    def _on_transport_error(self, message: str) -> None:
        self._append_log(ExchangeDirection.ERROR, f"{self.link_mode} 错误", "", message)
        self.logsChanged.emit()
        self.status_message = message
        self.statusChanged.emit()
        self._cancel_active_workflow(ModbusCodecError(message), log_error=False)

    def _make_read_step(
        self,
        title: str,
        start: int,
        quantity: int,
        assign: Callable[[RegisterBlock], None],
    ) -> CommandStep:
        request = read_holding(start, quantity)
        self._ensure_request_length(request)

        def handler(parsed: object, raw: bytes) -> None:
            response = parse_response(raw)
            if response.kind != "read_holding" or response.words is None:
                raise ModbusCodecError("响应内容不符合预期: 收到的不是 0x03 读寄存器响应")
            block = RegisterBlock(
                title=title,
                start_address=start,
                words=list(response.words),
                updated_at=datetime.now(),
                response_hex=spaced_hex(raw),
            )
            assign(block)
            self.blocksChanged.emit()

        return CommandStep(title, request, handler)

    def _ensure_request_length(self, request: bytes) -> None:
        if self.link_mode == "BLE":
            ensure_safe_ble_length(request)

    def _assign_manual_block(self, block: RegisterBlock) -> None:
        self.latest_manual_block = block

    def _assign_protect_block(self, block: RegisterBlock) -> None:
        self.latest_protect_block = block

    def _assign_afe_param_block(self, block: RegisterBlock) -> None:
        self.latest_afe_param_block = block

    def _assign_status_block(self, block: RegisterBlock) -> None:
        self.latest_status_block = block

    def _assign_event_log_block(self, block: RegisterBlock) -> None:
        self.latest_event_log_block = block

    def _start_sequence(
        self,
        action_name: str,
        steps: list[CommandStep],
        on_complete: Callable[[], None] | None = None,
    ) -> None:
        if self.busy_command_name is not None:
            self.status_message = f"仍有请求进行中：{self.busy_command_name}"
            self.statusChanged.emit()
            return
        if self.connection_status is not ConnectionStatus.READY:
            self.status_message = "通信通道尚未就绪，请先连接设备或打开串口"
            self.statusChanged.emit()
            return
        if not steps:
            return

        self.busy_command_name = action_name
        self._pending_sequence = PendingSequence(action_name=action_name, steps=steps, on_complete=on_complete)
        self.statusChanged.emit()
        self._run_next_step()

    def _run_next_step(self) -> None:
        if self._pending_sequence is None:
            return
        if self._pending_sequence.index >= len(self._pending_sequence.steps):
            sequence = self._pending_sequence
            self._pending_sequence = None
            self.busy_command_name = None
            self.statusChanged.emit()
            if sequence.on_complete is not None:
                sequence.on_complete()
            return

        step = self._pending_sequence.steps[self._pending_sequence.index]

        def success(frame: bytes) -> None:
            try:
                parsed = parse_response(frame)
                if parsed.kind == "exception":
                    raise ModbusCodecError(
                        f"设备返回异常响应: function 0x{parsed.function:02X}, code 0x{parsed.code:02X}"
                    )
                step.handler(parsed, frame)
                if self._pending_sequence is None:
                    return
                self._pending_sequence.index += 1
                self._run_next_step()
            except Exception as exc:
                if isinstance(exc, Exception):
                    self._cancel_active_workflow(exc)

        self._send_request(step.title, step.request, success, self._cancel_active_workflow, step.expected_length_hint)

    def _send_request(
        self,
        name: str,
        request: bytes,
        on_success: Callable[[bytes], None],
        on_failure: Callable[[Exception], None],
        expected_length_hint: int | None = None,
    ) -> None:
        if self.connection_status is not ConnectionStatus.READY:
            on_failure(ModbusCodecError("通信通道尚未就绪，请先连接设备或打开串口"))
            return
        if self._pending_exchange is not None:
            on_failure(ModbusCodecError(f"当前仍有未完成请求: {self._pending_exchange.name}"))
            return

        self._accumulator.reset(expected_length_hint)
        self.response_preview = ""
        self.responsePreviewChanged.emit()
        self._append_log(ExchangeDirection.TX, name, spaced_hex(request), f"{len(request)} byte")
        self.logsChanged.emit()

        timer = QTimer(self)
        timer.setSingleShot(True)
        timer.timeout.connect(lambda: self._handle_timeout(name))
        self._pending_exchange = PendingExchange(name=name, on_success=on_success, on_failure=on_failure, timer=timer)

        try:
            self._transport.send(request)
        except Exception as exc:
            self._clear_pending_exchange()
            on_failure(exc if isinstance(exc, Exception) else ModbusCodecError(str(exc)))
            return

        timer.start(3000)

    def _handle_timeout(self, name: str) -> None:
        if self._pending_exchange is None or self._pending_exchange.name != name:
            return
        self._append_log(ExchangeDirection.ERROR, name, "", "等待响应超时")
        self.logsChanged.emit()
        self._fail_pending_exchange(ModbusCodecError("等待响应超时"))

    def _fail_pending_exchange(self, error: Exception) -> None:
        pending = self._pending_exchange
        self._clear_pending_exchange()
        if pending is not None:
            pending.on_failure(error)

    def _succeed_pending_exchange(self, frame: bytes) -> None:
        pending = self._pending_exchange
        self._clear_pending_exchange()
        if pending is not None:
            pending.on_success(frame)

    def _clear_pending_exchange(self) -> None:
        if self._pending_exchange is None:
            return
        self._pending_exchange.timer.stop()
        self._pending_exchange.timer.deleteLater()
        self._pending_exchange = None

    def _cancel_active_workflow(self, error: Exception, log_error: bool = True) -> None:
        action_name = self.busy_command_name or self._current_exchange_name()
        self._clear_pending_exchange()
        self._pending_sequence = None
        self.busy_command_name = None
        if log_error:
            self._append_log(ExchangeDirection.ERROR, action_name, "", str(error))
            self.logsChanged.emit()
        self.status_message = f"{action_name}失败: {error}"
        self.statusChanged.emit()

    def _append_log(self, direction: ExchangeDirection, title: str, payload_hex: str, note: str) -> None:
        self.logs.insert(
            0,
            ExchangeLogEntry(
                timestamp=datetime.now(),
                direction=direction,
                title=title,
                payload_hex=payload_hex,
                note=note,
            ),
        )
        if len(self.logs) > 300:
            self.logs = self.logs[:300]

    def _mark_connected_device(self, device_id: str | None) -> None:
        for current in self.devices.values():
            current.is_connected = current.id == device_id

    def _current_exchange_name(self) -> str:
        if self._pending_exchange is not None:
            return self._pending_exchange.name
        return "收到响应"
