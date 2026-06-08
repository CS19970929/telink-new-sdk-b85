from __future__ import annotations

from dataclasses import dataclass


class BMSUUIDs:
    sppService = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
    requestCharacteristic = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
    responseCharacteristic = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"


class RegisterCatalog:
    currentProjectSeriesCount = 20

    realtimeStatusMagic = 0x4253
    realtimeStatusStart = 0xD120
    realtimeStatusCount = 17
    realtimeStatusVersion = 0x0002
    realtimePackVoltage32HighIndex = 11
    realtimePackVoltage32LowIndex = 12
    realtimeCurrentUnitIndex = 13
    realtimeAfeApplyStatusIndex = 14
    realtimeCurrentMaHighIndex = 15
    realtimeCurrentMaLowIndex = 16

    legacyCellArrayStart = 0xD000
    legacyCellArrayCount = 63
    legacyBatteryTempADCIndex = 29
    legacyMosTempADCIndex = 30
    legacyPackVoltageADCIndex = 31
    legacyMaxCellVoltageIndex = 32
    legacyMinCellVoltageIndex = 33
    legacyMaxCellPositionIndex = 34
    legacyMinCellPositionIndex = 35
    legacyCellDeltaIndex = 36
    legacyPackVoltageEngineeringIndex = 37
    legacyTemperatureBaseIndex = 38
    legacyTemperatureCount = 10
    legacyMosTemperatureIndex = 47
    legacyMaxTempIndex = 48
    legacyMinTempIndex = 49
    legacyChargeCurrentIndex = 50
    legacyDischargeCurrentIndex = 51
    legacySocIndex = 52
    legacySohIndex = 53
    legacyCapacityNowIndex = 54
    legacyCapacityFullIndex = 55
    legacyCapacityFactoryIndex = 56
    legacyCycleCountIndex = 57

    macAddressStart = 0x0000
    macAddressCount = 3

    btNameStart = 0x0100
    btNameReadCount = 12

    productSerialStart = 0xC002
    productTextCount = 16
    productHardwareStart = 0xC012
    productSoftwareStart = 0xC022

    eventLogStart = 0xC008
    eventLogPreviewCount = 20

    systemStatusStart = 0xD115
    systemStatusCount = 2

    protectStart = 0x2100
    protectPreviewCount = 15

    afeParamStart = 0x2200
    afeParamCount = 32
    afePersistentParamCount = 31
    afeParamConfigOffset = 8

    socWriteRegister = 0x1005
    debugRegister1102 = 0x1102
    debugRegister1103 = 0x1103

    btNameMaxWriteBytes = 10


class ModbusCodecError(Exception):
    pass


@dataclass(frozen=True)
class AfeParamMeta:
    offset: int
    name: str
    access: str
    afe_register: int | None = None
    bits: str = ""


AFE_PARAM_METADATA: dict[int, AfeParamMeta] = {
    0: AfeParamMeta(0, "MODEL", "RO", None, "0x3520"),
    1: AfeParamMeta(1, "SERIES_COUNT", "RO", None, "20S project"),
    2: AfeParamMeta(2, "RSENSE_UOHM", "RW", None, "current sense resistor, micro-ohm"),
    3: AfeParamMeta(3, "LAST_BSTATUS", "RO", None, "BSTATUS1 high byte, BSTATUS2 low byte"),
    4: AfeParamMeta(4, "FLAG1", "RO", 0x58, "RST1/WK/OCC/SC/OCD2/OCD1/UV/OV"),
    5: AfeParamMeta(5, "FLAG2", "RO", 0x59, "OTD/UTD/OTC/UTC/RST2/WDT/VADC/CADC"),
    6: AfeParamMeta(6, "FLAG3", "RO", 0x5A, "OWD_IND/OWD"),
    7: AfeParamMeta(7, "RESERVED", "RW", None, "reserved firmware word"),
    8: AfeParamMeta(8, "SCONF2", "RW", 0x41, "LTCLR/PD_EN/PD_CTL/PUMP_EN/PDSG_CTL/PDSGMOS/DSGMOS/CHGMOS"),
    9: AfeParamMeta(9, "SCONF3", "RW", 0x42, "CGR_WK/LD_WK/CRLD_EN/OWD_EN/OWD_TRG"),
    10: AfeParamMeta(10, "SCONF4", "RW", 0x43, "PDSGT[7:5], CN[4:0]"),
    11: AfeParamMeta(11, "SCONF5", "RW", 0x44, "MOS_EN/OCC_EN/CADC_EN/WDT_EN/WDT"),
    12: AfeParamMeta(12, "SCONF6", "RW", 0x45, "TS4/TS3/TS2/TS1/SC/OCD/UV/OV enable"),
    13: AfeParamMeta(13, "SCONF7", "RW", 0x46, "RLD/CADCT/CDV"),
    14: AfeParamMeta(14, "OWV_ALARMH", "RW", 0x47, "OWV and load/VADC/CADC alarm enables"),
    15: AfeParamMeta(15, "ALARML", "RW", 0x48, "WK/WDT/OWD/TEMP/OCC/OCD/UV/OV alarm enables"),
    16: AfeParamMeta(16, "OVT_OVH", "RW", 0x49, "OV delay and OV[9:8]"),
    17: AfeParamMeta(17, "OVL", "RW", 0x4A, "OV[7:0], threshold = code * 5mV"),
    18: AfeParamMeta(18, "UVT_UVH", "RW", 0x4B, "UV delay and UV[9:8]"),
    19: AfeParamMeta(19, "UVL", "RW", 0x4C, "UV[7:0], threshold = code * 5mV"),
    20: AfeParamMeta(20, "OCD1", "RW", 0x4D, "OCD1T/OCD1V"),
    21: AfeParamMeta(21, "OCD2", "RW", 0x4E, "OCD2T/OCD2V"),
    22: AfeParamMeta(22, "SCV_SCT", "RW", 0x4F, "short-circuit voltage/time"),
    23: AfeParamMeta(23, "OCC", "RW", 0x50, "charge over-current voltage/time"),
    24: AfeParamMeta(24, "OTC", "RW", 0x51, "charge high temperature"),
    25: AfeParamMeta(25, "OTD", "RW", 0x52, "discharge high temperature"),
    26: AfeParamMeta(26, "UTC", "RW", 0x53, "charge low temperature"),
    27: AfeParamMeta(27, "UTD", "RW", 0x54, "discharge low temperature"),
    28: AfeParamMeta(28, "BALANCEH", "RW", 0x55, "CB20..CB17"),
    29: AfeParamMeta(29, "BALANCEM", "RW", 0x56, "CB16..CB9"),
    30: AfeParamMeta(30, "BALANCEL", "RW", 0x57, "CB8..CB1"),
    31: AfeParamMeta(31, "LAST_APPLY_STATUS", "RO", None, "low byte status, high byte register/index context"),
}


def afe_apply_status_text(value: int) -> str:
    status = value & 0x00FF
    context = (value >> 8) & 0x00FF
    if status == 0:
        return "OK"
    names = [
        (0x01, "FLASH_FAIL"),
        (0x02, "SPI_FAIL"),
        (0x04, "INVALID_WRITE"),
        (0x08, "READ_FAIL"),
    ]
    active = [name for bit, name in names if status & bit]
    label = "|".join(active) if active else f"UNKNOWN_0x{status:02X}"
    return f"{label}, context=0x{context:02X}"


def format_afe_param_word(start_address: int, offset: int, value: int) -> str | None:
    if not (RegisterCatalog.afeParamStart <= start_address < RegisterCatalog.afeParamStart + RegisterCatalog.afeParamCount):
        return None
    absolute_offset = start_address - RegisterCatalog.afeParamStart + offset
    meta = AFE_PARAM_METADATA.get(absolute_offset)
    if meta is None:
        return None
    address = start_address + offset
    afe_reg = f", AFE 0x{meta.afe_register:02X}" if meta.afe_register is not None else ""
    detail = afe_apply_status_text(value) if absolute_offset == RegisterCatalog.afePersistentParamCount else meta.bits
    return f"0x{address:04X}: 0x{value:04X} ({value}) | {meta.name} | {meta.access}{afe_reg} | {detail}"


def format_register_word(start_address: int, offset: int, value: int) -> str:
    afe_line = format_afe_param_word(start_address, offset, value)
    if afe_line is not None:
        return afe_line
    address = start_address + offset
    return f"0x{address:04X}: 0x{value:04X} ({value})"


@dataclass
class ParsedResponse:
    kind: str
    words: list[int] | None = None
    register: int | None = None
    value: int | None = None
    quantity: int | None = None
    function: int | None = None
    code: int | None = None
    raw: bytes = b""


def parse_address(text: str) -> int:
    cleaned = text.strip()
    if cleaned.lower().startswith("0x"):
        try:
            return int(cleaned[2:], 16)
        except ValueError as exc:
            raise ModbusCodecError(f"无法解析十六进制输入: {text}") from exc
    try:
        return int(cleaned, 10)
    except ValueError as exc:
        raise ModbusCodecError(f"无法解析十六进制输入: {text}") from exc


def parse_words(text: str) -> list[int]:
    normalized = text.replace(",", " ")
    parts = [item for item in normalized.split() if item]
    if not parts:
        raise ModbusCodecError(f"无法解析十六进制输入: {text}")
    return [parse_address(item) for item in parts]


def parse_raw_bytes(text: str) -> bytes:
    normalized = text.replace("0x", "").replace("0X", "").replace(",", " ")
    cleaned = "".join(ch for ch in normalized if ch in "0123456789abcdefABCDEF")
    if not cleaned or len(cleaned) % 2 != 0:
        raise ModbusCodecError(f"无法解析十六进制输入: {text}")
    try:
        return bytes.fromhex(cleaned)
    except ValueError as exc:
        raise ModbusCodecError(f"无法解析十六进制输入: {text}") from exc


def read_holding(start: int, quantity: int) -> bytes:
    body = bytes([0x01, 0x03]) + u16be(start) + u16be(quantity)
    return frame(body)


def write_single(register: int, value: int) -> bytes:
    body = bytes([0x01, 0x06]) + u16be(register) + u16be(value)
    return frame(body)


def write_multiple(register: int, values: list[int]) -> bytes:
    payload = b"".join(u16be(item) for item in values)
    body = bytes([0x01, 0x10]) + u16be(register) + u16be(len(values)) + bytes([len(payload)]) + payload
    return frame(body)


def echo(payload: bytes) -> bytes:
    body = bytes([0x01, 0x7F]) + payload
    return frame(body)


def parse_response(data: bytes) -> ParsedResponse:
    if len(data) < 4:
        raise ModbusCodecError("Modbus 帧无效: 长度小于最小 Modbus RTU 帧")
    if not validate_crc(data):
        raise ModbusCodecError("收到的响应 CRC 校验失败")

    function = data[1]
    if function == 0x7F:
        return ParsedResponse(kind="echo", raw=data)
    if function & 0x80:
        if len(data) < 5:
            raise ModbusCodecError("Modbus 帧无效: 异常响应长度不足")
        return ParsedResponse(kind="exception", function=function & 0x7F, code=data[2], raw=data)

    if function == 0x03:
        if len(data) < 5:
            raise ModbusCodecError("Modbus 帧无效: 读寄存器响应长度不足")
        byte_count = int(data[2])
        if len(data) != byte_count + 5:
            raise ModbusCodecError("Modbus 帧无效: 读寄存器响应字节数不匹配")
        words = [int.from_bytes(data[index : index + 2], "big") for index in range(3, 3 + byte_count, 2)]
        return ParsedResponse(kind="read_holding", words=words, raw=data)

    if function == 0x06:
        if len(data) != 8:
            raise ModbusCodecError("Modbus 帧无效: 写单寄存器响应长度应为 8")
        return ParsedResponse(
            kind="write_single_ack",
            register=int.from_bytes(data[2:4], "big"),
            value=int.from_bytes(data[4:6], "big"),
            raw=data,
        )

    if function == 0x10:
        if len(data) != 8:
            raise ModbusCodecError("Modbus 帧无效: 写多寄存器响应长度应为 8")
        return ParsedResponse(
            kind="write_multiple_ack",
            register=int.from_bytes(data[2:4], "big"),
            quantity=int.from_bytes(data[4:6], "big"),
            raw=data,
        )

    raise ModbusCodecError(f"响应内容不符合预期: 未支持的功能码 0x{function:02X}")


def infer_expected_length(buffer: bytes, hint: int | None) -> int | None:
    if hint is not None:
        return hint
    if len(buffer) < 2:
        return None

    function = buffer[1]
    if function == 0x7F:
        return None
    if function & 0x80:
        return 5
    if function == 0x03:
        if len(buffer) < 3:
            return None
        return int(buffer[2]) + 5
    if function in (0x06, 0x10):
        return 8
    return None


def validate_crc(data: bytes) -> bool:
    if len(data) < 4:
        return False
    payload = data[:-2]
    crc = crc16(payload)
    return data[-2] == (crc & 0xFF) and data[-1] == ((crc >> 8) & 0xFF)


def ascii_string_from_words(words: list[int]) -> str:
    raw = b"".join(u16be(item) for item in words)
    return raw.split(b"\x00", 1)[0].decode("utf-8", errors="ignore")


def mac_string_from_words(words: list[int]) -> str:
    raw = b"".join(u16be(item) for item in words)
    return ":".join(f"{item:02X}" for item in raw[:6])


def encode_ascii_words(text: str) -> list[int]:
    raw = text.encode("utf-8")
    if len(raw) % 2 != 0:
        raw += b"\x00"
    return [int.from_bytes(raw[index : index + 2], "big") for index in range(0, len(raw), 2)]


def ensure_safe_ble_length(request: bytes) -> None:
    if len(request) > 20:
        raise ModbusCodecError(f"请求长度 {len(request)} byte，超过当前固件 BLE 单包安全上限 20 byte")


def frame(body: bytes) -> bytes:
    crc = crc16(body)
    return body + bytes([crc & 0xFF, (crc >> 8) & 0xFF])


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc & 0xFFFF


def u16be(value: int) -> bytes:
    return int(value & 0xFFFF).to_bytes(2, "big")


def spaced_hex(data: bytes) -> str:
    return " ".join(f"{item:02X}" for item in data)


@dataclass
class AccumulatorEvent:
    state: str
    expected_length: int | None = None
    fragments: int = 0
    frame: bytes = b""


class ResponseAccumulator:
    def __init__(self) -> None:
        self.buffer = b""
        self.fragment_count = 0
        self.expected_length_hint: int | None = None

    def reset(self, expected_length_hint: int | None = None) -> None:
        self.buffer = b""
        self.fragment_count = 0
        self.expected_length_hint = expected_length_hint

    def append(self, fragment: bytes) -> AccumulatorEvent:
        self.fragment_count += 1
        self.buffer += fragment

        expected_length = infer_expected_length(self.buffer, self.expected_length_hint)
        if expected_length is None:
            return AccumulatorEvent(state="waiting", expected_length=None, fragments=self.fragment_count)

        if len(self.buffer) < expected_length:
            return AccumulatorEvent(state="waiting", expected_length=expected_length, fragments=self.fragment_count)

        frame = self.buffer[:expected_length]
        remainder = self.buffer[expected_length:]
        fragments = self.fragment_count
        self.buffer = remainder
        self.fragment_count = 0

        if validate_crc(frame):
            return AccumulatorEvent(state="completed", fragments=fragments, frame=frame)
        return AccumulatorEvent(state="invalid_crc", fragments=fragments, frame=frame)
