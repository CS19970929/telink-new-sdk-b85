from __future__ import annotations

import sys
from pathlib import Path

from PySide6.QtBluetooth import (
    QBluetoothDeviceDiscoveryAgent,
    QBluetoothDeviceInfo,
    QBluetoothUuid,
    QLowEnergyCharacteristic,
    QLowEnergyController,
    QLowEnergyService,
)
from PySide6.QtCore import QByteArray, QObject, QTimer, QUuid, Signal
from PySide6.QtWidgets import (
    QApplication,
    QComboBox,
    QFileDialog,
    QHBoxLayout,
    QLabel,
    QMainWindow,
    QMessageBox,
    QPlainTextEdit,
    QProgressBar,
    QPushButton,
    QVBoxLayout,
    QWidget,
)

from bmsassistantqt.ota import (
    OTA_CHARACTERISTIC_UUID,
    OTA_SERVICE_UUID,
    TelinkFirmwareImage,
    TelinkOtaError,
    build_start_packet,
    parse_result_packet,
    result_text,
)


class OtaTransport(QObject):
    deviceDiscovered = Signal(str, str, int)
    stateChanged = Signal(str)
    ready = Signal()
    writeCompleted = Signal()
    otaNotification = Signal(object)
    errorOccurred = Signal(str)

    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._agent = QBluetoothDeviceDiscoveryAgent(self)
        self._devices: dict[str, QBluetoothDeviceInfo] = {}
        self._controller: QLowEnergyController | None = None
        self._service: QLowEnergyService | None = None
        self._characteristic: QLowEnergyCharacteristic | None = None
        self._service_found = False
        self._service_uuid = QBluetoothUuid(QUuid(OTA_SERVICE_UUID))
        self._char_uuid = QBluetoothUuid(QUuid(OTA_CHARACTERISTIC_UUID))

        self._agent.deviceDiscovered.connect(self._on_device)
        if hasattr(self._agent, "deviceUpdated"):
            self._agent.deviceUpdated.connect(lambda info, _fields: self._on_device(info))
        if hasattr(self._agent, "errorOccurred"):
            self._agent.errorOccurred.connect(lambda error: self.errorOccurred.emit(f"BLE scan error: {error}"))

    def start_scan(self) -> None:
        if self._agent.isActive():
            return
        owner = getattr(QBluetoothDeviceDiscoveryAgent, "DiscoveryMethod", QBluetoothDeviceDiscoveryAgent)
        method = getattr(owner, "LowEnergyMethod", getattr(QBluetoothDeviceDiscoveryAgent, "LowEnergyMethod", None))
        self._agent.start(method) if method is not None else self._agent.start()
        self.stateChanged.emit("scanning")

    def stop_scan(self) -> None:
        if self._agent.isActive():
            self._agent.stop()

    def connect_device(self, device_id: str) -> None:
        info = self._devices.get(device_id)
        if info is None:
            raise TelinkOtaError(f"device not found: {device_id}")
        self.stop_scan()
        self.disconnect()
        self._service_found = False
        self._controller = QLowEnergyController.createCentral(info, self)
        self._controller.connected.connect(self._on_connected)
        self._controller.disconnected.connect(lambda: self.stateChanged.emit("disconnected"))
        self._controller.serviceDiscovered.connect(self._on_service_discovered)
        self._controller.discoveryFinished.connect(self._on_services_finished)
        if hasattr(self._controller, "errorOccurred"):
            self._controller.errorOccurred.connect(lambda error: self.errorOccurred.emit(f"BLE connect error: {error}"))
        self.stateChanged.emit("connecting")
        self._controller.connectToDevice()

    def disconnect(self) -> None:
        if self._controller is not None:
            try:
                self._controller.disconnectFromDevice()
            except Exception:
                pass
        self._characteristic = None
        self._service = None
        self._controller = None

    def write(self, data: bytes) -> None:
        if self._service is None or self._characteristic is None or not self._characteristic.isValid():
            raise TelinkOtaError("OTA characteristic is not ready")
        mode_owner = getattr(QLowEnergyService, "WriteMode", QLowEnergyService)
        mode = getattr(mode_owner, "WriteWithResponse")
        self._service.writeCharacteristic(self._characteristic, QByteArray(data), mode)

    def _on_device(self, info: QBluetoothDeviceInfo) -> None:
        device_id = _device_id(info)
        self._devices[device_id] = QBluetoothDeviceInfo(info)
        self.deviceDiscovered.emit(device_id, info.name().strip() or device_id, info.rssi())

    def _on_connected(self) -> None:
        if self._controller is None:
            return
        self.stateChanged.emit("connected")
        self._controller.discoverServices()

    def _on_service_discovered(self, uuid: QBluetoothUuid) -> None:
        if _uuid_text(uuid) == _uuid_text(self._service_uuid):
            self._service_found = True

    def _on_services_finished(self) -> None:
        if self._controller is None:
            return
        if not self._service_found:
            self.errorOccurred.emit(f"OTA service not found: {OTA_SERVICE_UUID}")
            return
        self._service = self._controller.createServiceObject(self._service_uuid, self)
        if self._service is None:
            self.errorOccurred.emit("failed to create OTA service")
            return
        self._service.stateChanged.connect(self._on_service_state)
        self._service.characteristicWritten.connect(lambda _char, _value: self.writeCompleted.emit())
        self._service.characteristicChanged.connect(self._on_changed)
        self._service.descriptorWritten.connect(self._on_descriptor_written)
        self._service.discoverDetails()

    def _on_service_state(self, state) -> None:
        owner = getattr(QLowEnergyService, "ServiceState", QLowEnergyService)
        if state != getattr(owner, "RemoteServiceDiscovered") or self._service is None:
            return
        self._characteristic = self._service.characteristic(self._char_uuid)
        if self._characteristic is None or not self._characteristic.isValid():
            self.errorOccurred.emit(f"OTA characteristic not found: {OTA_CHARACTERISTIC_UUID}")
            return
        descriptor = self._characteristic.clientCharacteristicConfiguration()
        if descriptor.isValid():
            self._service.writeDescriptor(descriptor, QByteArray(b"\x01\x00"))
        else:
            self.ready.emit()

    def _on_descriptor_written(self, _descriptor, value: QByteArray) -> None:
        if bytes(value) == b"\x01\x00":
            self.ready.emit()

    def _on_changed(self, characteristic: QLowEnergyCharacteristic, value: QByteArray) -> None:
        if self._characteristic is not None and characteristic.uuid() == self._characteristic.uuid():
            self.otaNotification.emit(bytes(value))


def _uuid_text(uuid: QBluetoothUuid) -> str:
    return uuid.toString().replace("{", "").replace("}", "").upper()


def _device_id(info: QBluetoothDeviceInfo) -> str:
    try:
        value = info.deviceUuid().toString().replace("{", "").replace("}", "").upper()
        if value and value != "00000000-0000-0000-0000-000000000000":
            return value
    except Exception:
        pass
    try:
        value = info.address().toString().upper()
        if value and value != "00:00:00:00:00:00":
            return value
    except Exception:
        pass
    return info.name().strip() or "BLE"


class OtaWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("BMS Assistant - Telink OTA")
        self.resize(820, 620)
        self._transport = OtaTransport(self)
        self._image: TelinkFirmwareImage | None = None
        self._packets: list[bytes] = []
        self._send_index = 0
        self._waiting_result = False
        self._ota_ready = False

        root = QWidget(self)
        layout = QVBoxLayout(root)
        self.setCentralWidget(root)

        row = QHBoxLayout()
        self.device_box = QComboBox()
        self.scan_button = QPushButton("扫描")
        self.connect_button = QPushButton("连接 OTA")
        row.addWidget(self.device_box, 1)
        row.addWidget(self.scan_button)
        row.addWidget(self.connect_button)
        layout.addLayout(row)

        file_row = QHBoxLayout()
        self.file_label = QLabel("未选择 firmware.bin")
        self.file_button = QPushButton("选择 BIN")
        file_row.addWidget(self.file_label, 1)
        file_row.addWidget(self.file_button)
        layout.addLayout(file_row)

        self.info_label = QLabel("状态：未连接")
        self.progress = QProgressBar()
        self.progress.setRange(0, 100)
        self.start_button = QPushButton("开始 OTA")
        self.start_button.setEnabled(False)
        self.log = QPlainTextEdit()
        self.log.setReadOnly(True)
        layout.addWidget(self.info_label)
        layout.addWidget(self.progress)
        layout.addWidget(self.start_button)
        layout.addWidget(self.log, 1)

        self.scan_button.clicked.connect(self._transport.start_scan)
        self.connect_button.clicked.connect(self._connect_selected)
        self.file_button.clicked.connect(self._choose_file)
        self.start_button.clicked.connect(self._start_ota)
        self._transport.deviceDiscovered.connect(self._on_device)
        self._transport.stateChanged.connect(self._on_state)
        self._transport.ready.connect(self._on_ready)
        self._transport.writeCompleted.connect(self._on_write_completed)
        self._transport.otaNotification.connect(self._on_ota_notification)
        self._transport.errorOccurred.connect(self._fail)

    def _append(self, text: str) -> None:
        self.log.appendPlainText(text)

    def _on_device(self, device_id: str, name: str, rssi: int) -> None:
        for i in range(self.device_box.count()):
            if self.device_box.itemData(i) == device_id:
                self.device_box.setItemText(i, f"{name}  {rssi} dBm")
                return
        self.device_box.addItem(f"{name}  {rssi} dBm", device_id)

    def _on_state(self, state: str) -> None:
        if state == "disconnected":
            self._ota_ready = False
        self.info_label.setText(f"状态：{state}")
        self._append(f"BLE {state}")
        self._refresh_start_enabled()

    def _connect_selected(self) -> None:
        device_id = self.device_box.currentData()
        if not device_id:
            self._fail("请先扫描并选择设备")
            return
        try:
            self._transport.connect_device(str(device_id))
        except Exception as exc:
            self._fail(str(exc))

    def _choose_file(self) -> None:
        path, _ = QFileDialog.getOpenFileName(self, "选择 Telink firmware.bin", "", "Binary (*.bin);;All files (*)")
        if not path:
            return
        try:
            self._image = TelinkFirmwareImage.from_file(path)
        except Exception as exc:
            self._image = None
            self._fail(f"固件校验失败：{exc}")
            return
        self.file_label.setText(f"{Path(path).name} | {self._image.declared_size} bytes | {self._image.packet_count} packets")
        self._append(f"firmware loaded: {path}")
        self._refresh_start_enabled()

    def _on_ready(self) -> None:
        self._ota_ready = True
        self.info_label.setText("状态：OTA characteristic ready")
        self._append(f"OTA ready: {OTA_CHARACTERISTIC_UUID}")
        self._refresh_start_enabled()

    def _refresh_start_enabled(self) -> None:
        self.start_button.setEnabled(self._image is not None and self._ota_ready and not self._waiting_result and not self._packets)

    def _start_ota(self) -> None:
        if self._image is None:
            return
        if QMessageBox.warning(
            self,
            "确认 OTA",
            "升级期间禁止断电。当前固件 BLE 安全未开启，请确认连接的是目标 BMS。\n\n是否开始升级？",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No,
        ) != QMessageBox.StandardButton.Yes:
            return
        self._packets = [build_start_packet(), *self._image.iter_data_packets(), self._image.end_packet()]
        self._send_index = 0
        self._waiting_result = False
        self.progress.setValue(0)
        self.start_button.setEnabled(False)
        self._append(f"OTA START, firmware={self._image.declared_size} bytes, data_packets={self._image.packet_count}")
        self._send_next()

    def _send_next(self) -> None:
        if self._send_index >= len(self._packets):
            self._waiting_result = True
            self._packets = []
            self.info_label.setText("状态：已发送 OTA END，等待 BMS OTA_RESULT")
            self._append("all packets written; waiting OTA_RESULT")
            return
        try:
            self._transport.write(self._packets[self._send_index])
        except Exception as exc:
            self._fail(str(exc))

    def _on_write_completed(self) -> None:
        if self._image is None:
            return
        self._send_index += 1
        data_sent = max(0, min(self._image.packet_count, self._send_index - 1))
        percent = int(data_sent * 100 / max(1, self._image.packet_count))
        self.progress.setValue(percent)
        if data_sent and (data_sent % 128 == 0 or data_sent == self._image.packet_count):
            self._append(f"OTA data {data_sent}/{self._image.packet_count} ({percent}%)")
        QTimer.singleShot(0, self._send_next)

    def _on_ota_notification(self, raw: bytes) -> None:
        code = parse_result_packet(raw)
        self._append(f"OTA notify: {raw.hex(' ')}")
        if code is None:
            return
        self._waiting_result = False
        if code == 0:
            self.progress.setValue(100)
            self.info_label.setText("状态：OTA 成功，等待 BMS 重启")
            self._append("OTA SUCCESS; device should reboot into new firmware")
        else:
            self._fail(f"OTA failed: {result_text(code)} (0x{code:02X})")
        self._refresh_start_enabled()

    def _fail(self, message: str) -> None:
        self._packets = []
        self._waiting_result = False
        self.info_label.setText(f"状态：失败 - {message}")
        self._append(f"ERROR: {message}")
        self._refresh_start_enabled()


def main() -> int:
    app = QApplication(sys.argv)
    app.setApplicationName("BMSAssistantTelinkOTA")
    window = OtaWindow()
    window.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
