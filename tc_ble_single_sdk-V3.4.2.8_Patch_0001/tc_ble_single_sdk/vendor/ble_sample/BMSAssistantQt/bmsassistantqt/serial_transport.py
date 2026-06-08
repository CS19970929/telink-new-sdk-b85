from __future__ import annotations

from typing import Any

from PySide6.QtCore import QIODevice, QObject, Signal

try:
    from PySide6.QtSerialPort import QSerialPort, QSerialPortInfo
except Exception:  # pragma: no cover - depends on QtSerialPort packaging
    QSerialPort = None  # type: ignore[assignment]
    QSerialPortInfo = None  # type: ignore[assignment]

from .models import ConnectionStatus
from .protocol import ModbusCodecError


class SerialTransport(QObject):
    stateChanged = Signal(str)
    connectionChanged = Signal(str, str, str)
    ready = Signal(str)
    dataReceived = Signal(object)
    errorOccurred = Signal(str)

    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._port: Any | None = None
        self._state_label = "未打开"
        self._device_id = ""

    def activate(self) -> None:
        self._emit_state()

    def available_ports(self) -> list[str]:
        if QSerialPortInfo is None:
            return []
        return [info.portName() for info in QSerialPortInfo.availablePorts()]

    def connect_port(self, port_name: str, baudrate: int) -> None:
        if QSerialPort is None:
            raise ModbusCodecError("当前 PySide6 缺少 QtSerialPort 模块，无法使用串口连接")
        if not port_name:
            raise ModbusCodecError("请先选择串口")

        self.disconnect_current()
        self._port = QSerialPort(self)
        self._port.setPortName(port_name)
        self._port.setBaudRate(int(baudrate))
        self._port.setDataBits(QSerialPort.DataBits.Data8)
        self._port.setParity(QSerialPort.Parity.NoParity)
        self._port.setStopBits(QSerialPort.StopBits.OneStop)
        self._port.setFlowControl(QSerialPort.FlowControl.NoFlowControl)
        self._port.readyRead.connect(self._on_ready_read)
        self._port.errorOccurred.connect(self._on_error)

        self._device_id = f"serial:{port_name}"
        self.connectionChanged.emit(self._device_id, ConnectionStatus.CONNECTING.value, f"正在打开串口 {port_name}")
        if not self._port.open(QIODevice.OpenModeFlag.ReadWrite):
            message = self._port.errorString()
            self._dispose_port()
            self.connectionChanged.emit(self._device_id, ConnectionStatus.FAILED.value, f"串口打开失败: {message}")
            self.errorOccurred.emit(f"串口打开失败: {message}")
            return

        self._state_label = f"{port_name} @ {baudrate}"
        self._emit_state()
        self.connectionChanged.emit(self._device_id, ConnectionStatus.READY.value, f"串口 {port_name} 已打开，可收发 Modbus RTU")
        self.ready.emit(self._device_id)

    def disconnect_current(self) -> None:
        if self._port is None:
            return
        device_id = self._device_id
        self._dispose_port()
        self._state_label = "未打开"
        self._emit_state()
        self.connectionChanged.emit(device_id, ConnectionStatus.DISCONNECTED.value, "串口已断开")

    def send(self, data: bytes) -> None:
        if self._port is None or not self._port.isOpen():
            raise ModbusCodecError("串口尚未打开")
        written = self._port.write(data)
        if written < 0:
            raise ModbusCodecError(f"串口写入失败: {self._port.errorString()}")

    def _emit_state(self) -> None:
        self.stateChanged.emit(self._state_label)

    def _dispose_port(self) -> None:
        if self._port is None:
            return
        try:
            if self._port.isOpen():
                self._port.close()
        finally:
            self._port.deleteLater()
            self._port = None

    def _on_ready_read(self) -> None:
        if self._port is None:
            return
        data = bytes(self._port.readAll())
        if data:
            self.dataReceived.emit(data)

    def _on_error(self, error: Any) -> None:
        if self._port is None:
            return
        if error == QSerialPort.SerialPortError.NoError:
            return
        message = self._port.errorString()
        self.errorOccurred.emit(f"串口错误: {message}")
