from __future__ import annotations

from typing import Any

from PySide6.QtBluetooth import (
    QBluetoothDeviceDiscoveryAgent,
    QBluetoothDeviceInfo,
    QBluetoothUuid,
    QLowEnergyCharacteristic,
    QLowEnergyController,
    QLowEnergyService,
)
from PySide6.QtCore import QByteArray, QObject, QTimer, QUuid, Signal

from .models import ConnectionStatus, DiscoveryEvent, DiscoverySnapshot
from .protocol import BMSUUIDs, ModbusCodecError


class BLETransport(QObject):
    bluetoothStateChanged = Signal(str)
    discoveryReceived = Signal(object)
    connectionChanged = Signal(str, str, str)
    ready = Signal(str)
    dataReceived = Signal(object)
    errorOccurred = Signal(str)

    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._discovery_agent = QBluetoothDeviceDiscoveryAgent(self)
        self._known_devices: dict[str, QBluetoothDeviceInfo] = {}
        self._controller: QLowEnergyController | None = None
        self._service: QLowEnergyService | None = None
        self._write_characteristic: QLowEnergyCharacteristic | None = None
        self._notify_characteristic: QLowEnergyCharacteristic | None = None
        self._connected_device_id: str | None = None
        self._service_found = False

        self._spp_service_uuid = QBluetoothUuid(QUuid(BMSUUIDs.sppService))
        self._request_uuid = QBluetoothUuid(QUuid(BMSUUIDs.requestCharacteristic))
        self._response_uuid = QBluetoothUuid(QUuid(BMSUUIDs.responseCharacteristic))
        self._bluetooth_state_label = "系统托管"
        self._scan_requested = False

        self._discovery_agent.deviceDiscovered.connect(self._on_device_discovered)
        if hasattr(self._discovery_agent, "deviceUpdated"):
            self._discovery_agent.deviceUpdated.connect(self._on_device_updated)
        self._discovery_agent.finished.connect(self._on_scan_finished)
        self._discovery_agent.canceled.connect(self._on_scan_finished)
        if hasattr(self._discovery_agent, "errorOccurred"):
            self._discovery_agent.errorOccurred.connect(self._on_scan_error)
        elif hasattr(self._discovery_agent, "error"):
            self._discovery_agent.error.connect(self._on_scan_error)

        try:
            self._discovery_agent.setLowEnergyDiscoveryTimeout(12000)
        except Exception:
            pass

    def activate(self) -> None:
        self._emit_bluetooth_state()

    def start_scan(self) -> None:
        self._scan_requested = True
        try:
            if self._discovery_agent.isActive():
                return
            method = _discovery_method_low_energy()
            self._discovery_agent.start(method)
            self._bluetooth_state_label = "扫描中"
            self._emit_bluetooth_state()
        except Exception as exc:
            self.errorOccurred.emit(f"启动扫描失败: {exc}")

    def stop_scan(self) -> None:
        self._scan_requested = False
        try:
            self._discovery_agent.stop()
        except Exception:
            pass

    def connect_device(self, device_id: str) -> None:
        if device_id not in self._known_devices:
            raise ModbusCodecError(f"未找到设备: {device_id}")

        self.stop_scan()
        self._dispose_connection_objects()

        device_info = self._known_devices[device_id]
        self._connected_device_id = device_id
        self._service_found = False

        self._controller = QLowEnergyController.createCentral(device_info, self)
        self._controller.connected.connect(self._on_controller_connected)
        self._controller.disconnected.connect(self._on_controller_disconnected)
        self._controller.serviceDiscovered.connect(self._on_service_discovered)
        self._controller.discoveryFinished.connect(self._on_service_discovery_finished)
        if hasattr(self._controller, "errorOccurred"):
            self._controller.errorOccurred.connect(self._on_controller_error)
        elif hasattr(self._controller, "error"):
            self._controller.error.connect(self._on_controller_error)

        self.connectionChanged.emit(device_id, ConnectionStatus.CONNECTING.value, "正在建立 BLE 连接")
        self._controller.connectToDevice()

    def disconnect_current(self) -> None:
        if self._controller is not None:
            try:
                self._controller.disconnectFromDevice()
            except Exception:
                self._dispose_connection_objects()

    def send(self, data: bytes) -> None:
        if self._service is None or self._write_characteristic is None:
            raise ModbusCodecError("BLE 通道尚未就绪，请先连接并完成特征发现")

        if not self._write_characteristic.isValid():
            raise ModbusCodecError("请求特征无效，无法写入")

        write_mode = _write_mode_for(self._write_characteristic)
        self._service.writeCharacteristic(self._write_characteristic, QByteArray(data), write_mode)

    def _emit_bluetooth_state(self, *args: Any) -> None:
        self.bluetoothStateChanged.emit(self._bluetooth_state_label)

    def _on_scan_error(self, error: Any) -> None:
        self._bluetooth_state_label = "异常"
        self._emit_bluetooth_state()
        self.errorOccurred.emit(_friendly_scan_error_message(error))

    def _on_scan_finished(self) -> None:
        for info in self._discovery_agent.discoveredDevices():
            self._emit_discovery(info)
        if self._scan_requested:
            QTimer.singleShot(150, self._restart_scan_if_needed)
            self._bluetooth_state_label = "扫描中"
        elif self._bluetooth_state_label == "扫描中":
            self._bluetooth_state_label = "已开启"
        self._emit_bluetooth_state()

    def _on_device_discovered(self, info: QBluetoothDeviceInfo) -> None:
        self._emit_discovery(info)

    def _on_device_updated(self, info: QBluetoothDeviceInfo, updated_fields: Any) -> None:
        self._emit_discovery(info)

    def _emit_discovery(self, info: QBluetoothDeviceInfo) -> None:
        device_id = _device_identifier(info)
        self._known_devices[device_id] = QBluetoothDeviceInfo(info)

        name = info.name().strip()
        service_uuids = [_normalized_uuid_text(uuid) for uuid in info.serviceUuids()]
        if not name:
            name = _fallback_device_name(info)
        snapshot = DiscoverySnapshot(
            local_name=name,
            peripheral_name=name,
            advertised_services=service_uuids,
            is_connectable=None,
        )
        event = DiscoveryEvent(device_id=device_id, snapshot=snapshot, rssi=info.rssi())
        self.discoveryReceived.emit(event)

    def _restart_scan_if_needed(self) -> None:
        if not self._scan_requested:
            return
        if self._discovery_agent.isActive():
            return
        try:
            self._discovery_agent.start(_discovery_method_low_energy())
        except Exception as exc:
            self._scan_requested = False
            self._bluetooth_state_label = "异常"
            self._emit_bluetooth_state()
            self.errorOccurred.emit(f"重新启动扫描失败: {exc}")

    def _on_controller_connected(self) -> None:
        if self._controller is None or self._connected_device_id is None:
            return
        self.connectionChanged.emit(
            self._connected_device_id,
            ConnectionStatus.CONNECTED.value,
            "连接完成，正在发现服务",
        )
        self._controller.discoverServices()

    def _on_controller_disconnected(self) -> None:
        device_id = self._connected_device_id or ""
        self._dispose_connection_objects()
        self.connectionChanged.emit(device_id, ConnectionStatus.DISCONNECTED.value, "连接已断开")

    def _on_controller_error(self, error: Any) -> None:
        device_id = self._connected_device_id or ""
        message = _friendly_connect_error_message(error)
        self.connectionChanged.emit(device_id, ConnectionStatus.FAILED.value, message)
        self.errorOccurred.emit(message)

    def _on_service_discovered(self, service_uuid: QBluetoothUuid) -> None:
        if _normalized_uuid_text(service_uuid) == _normalized_uuid_text(self._spp_service_uuid):
            self._service_found = True

    def _on_service_discovery_finished(self) -> None:
        if self._controller is None:
            return
        if not self._service_found:
            self.errorOccurred.emit(f"未发现目标 SPP 服务 {BMSUUIDs.sppService}")
            return

        self._service = self._controller.createServiceObject(self._spp_service_uuid, self)
        if self._service is None:
            self.errorOccurred.emit("创建 QLowEnergyService 失败")
            return

        self._service.stateChanged.connect(self._on_service_state_changed)
        self._service.characteristicChanged.connect(self._on_characteristic_changed)
        self._service.descriptorWritten.connect(self._on_descriptor_written)
        if hasattr(self._service, "errorOccurred"):
            self._service.errorOccurred.connect(self._on_service_error)
        elif hasattr(self._service, "error"):
            self._service.error.connect(self._on_service_error)

        self._service.discoverDetails()

    def _on_service_error(self, error: Any) -> None:
        self.errorOccurred.emit(f"服务发现失败: {error}")

    def _on_service_state_changed(self, state: Any) -> None:
        if state != _remote_service_discovered_value():
            return
        if self._service is None:
            return

        self._write_characteristic = self._service.characteristic(self._request_uuid)
        self._notify_characteristic = self._service.characteristic(self._response_uuid)

        if self._notify_characteristic is None or not self._notify_characteristic.isValid():
            self.errorOccurred.emit(f"未找到响应特征 {BMSUUIDs.responseCharacteristic}")
            return

        if self._write_characteristic is None or not self._write_characteristic.isValid():
            self.errorOccurred.emit(f"未找到请求特征 {BMSUUIDs.requestCharacteristic}")
            return

        descriptor = self._notify_characteristic.clientCharacteristicConfiguration()
        if not descriptor.isValid():
            self.errorOccurred.emit("当前平台未暴露通知配置描述符，无法订阅响应特征")
            return

        self._service.writeDescriptor(descriptor, QByteArray(b"\x01\x00"))

    def _on_descriptor_written(self, descriptor: Any, value: QByteArray) -> None:
        if self._notify_characteristic is None or self._connected_device_id is None:
            return

        notify_descriptor = self._notify_characteristic.clientCharacteristicConfiguration()
        if not notify_descriptor.isValid():
            return

        if descriptor.uuid() == notify_descriptor.uuid() and bytes(value) == b"\x01\x00":
            self.ready.emit(self._connected_device_id)

    def _on_characteristic_changed(self, characteristic: QLowEnergyCharacteristic, value: QByteArray) -> None:
        if self._notify_characteristic is None:
            return
        if characteristic.uuid() != self._notify_characteristic.uuid():
            return
        self.dataReceived.emit(bytes(value))

    def _dispose_connection_objects(self) -> None:
        self._write_characteristic = None
        self._notify_characteristic = None
        self._service_found = False

        if self._service is not None:
            self._service.deleteLater()
            self._service = None

        if self._controller is not None:
            self._controller.deleteLater()
            self._controller = None

        self._connected_device_id = None


def _normalized_uuid_text(uuid: QBluetoothUuid) -> str:
    text = uuid.toString()
    text = text.replace("{", "").replace("}", "").upper()
    if text.startswith("0000") and text.endswith("-0000-1000-8000-00805F9B34FB"):
        return text[4:8]
    return text


def _device_identifier(info: QBluetoothDeviceInfo) -> str:
    try:
        device_uuid = info.deviceUuid()
        uuid_text = device_uuid.toString().replace("{", "").replace("}", "").upper()
        if uuid_text and uuid_text != "00000000-0000-0000-0000-000000000000":
            return uuid_text
    except Exception:
        pass

    try:
        address = info.address().toString().upper()
        if address and address != "00:00:00:00:00:00":
            return address
    except Exception:
        pass

    name = info.name().strip() or "BLE"
    return f"{name}-{abs(hash(name)) & 0xFFFF:04X}"


def _fallback_device_name(info: QBluetoothDeviceInfo) -> str:
    try:
        address = info.address().toString().upper()
        if address and address != "00:00:00:00:00:00":
            return f"BLE {address[-5:].replace(':', '')}"
    except Exception:
        pass
    try:
        device_uuid = info.deviceUuid().toString().replace("{", "").replace("}", "").upper()
        if device_uuid and device_uuid != "00000000-0000-0000-0000-000000000000":
            return f"BLE {device_uuid[-6:]}"
    except Exception:
        pass
    return ""


def _remote_service_discovered_value() -> Any:
    owner = getattr(QLowEnergyService, "ServiceState", QLowEnergyService)
    return getattr(owner, "RemoteServiceDiscovered")


def _discovery_method_low_energy() -> Any:
    if hasattr(QBluetoothDeviceDiscoveryAgent, "LowEnergyMethod"):
        return getattr(QBluetoothDeviceDiscoveryAgent, "LowEnergyMethod")
    owner = getattr(QBluetoothDeviceDiscoveryAgent, "DiscoveryMethod", QBluetoothDeviceDiscoveryAgent)
    return getattr(owner, "LowEnergyMethod")


def _preferred_discovery_method(agent: QBluetoothDeviceDiscoveryAgent) -> Any:
    try:
        supported = agent.supportedDiscoveryMethods()
    except Exception:
        return _discovery_method_low_energy()

    low_energy = _discovery_method_low_energy()
    try:
        if _enum_int(supported) & _enum_int(low_energy):
            return supported
    except Exception:
        pass
    return low_energy


def _friendly_scan_error_message(error: Any) -> str:
    raw = str(error)
    if "PoweredOffError" in raw:
        return (
            "扫描失败：Qt 返回 `PoweredOffError`。当前机器系统蓝牙并未真正关闭，"
            "这在 macOS 上通常表示 `BMSAssistantQt` 还没有被授予蓝牙权限。"
            "请到“系统设置 -> 隐私与安全性 -> 蓝牙”里允许 `BMSAssistantQt`，"
            "然后彻底退出 App 再重开。"
        )
    return f"扫描失败: {raw}"


def _friendly_connect_error_message(error: Any) -> str:
    raw = str(error)
    if "PoweredOffError" in raw:
        return (
            "连接失败：Qt 返回 `PoweredOffError`。当前机器系统蓝牙并未真正关闭，"
            "更可能是 `BMSAssistantQt` 没有蓝牙权限。"
        )
    return f"连接失败: {raw}"


def _write_mode_for(characteristic: QLowEnergyCharacteristic) -> Any:
    properties = _enum_int(characteristic.properties())
    property_owner = getattr(QLowEnergyCharacteristic, "PropertyType", QLowEnergyCharacteristic)
    write_with_response = _enum_int(getattr(property_owner, "Write"))
    write_without_response = _enum_int(getattr(property_owner, "WriteNoResponse"))
    mode_owner = getattr(QLowEnergyService, "WriteMode", QLowEnergyService)

    if properties & write_with_response:
        return getattr(mode_owner, "WriteWithResponse")
    if properties & write_without_response:
        return getattr(mode_owner, "WriteWithoutResponse")
    return getattr(mode_owner, "WriteWithResponse")


def _enum_int(value: Any) -> int:
    if hasattr(value, "value"):
        return int(value.value)
    return int(value)
