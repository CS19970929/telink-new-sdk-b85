from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

OTA_SERVICE_UUID = "00010203-0405-0607-0809-0A0B0C0D1912"
OTA_CHARACTERISTIC_UUID = "00010203-0405-0607-0809-0A0B0C0D2B12"
OTA_PDU_BYTES = 16
OTA_PACKET_BYTES = 20
OTA_CMD_START = 0xFF01
OTA_CMD_END = 0xFF02
OTA_CMD_RESULT = 0xFF06
TELINK_FIRMWARE_MARK = b"KNLT"
TELINK_FIRMWARE_MARK_OFFSET = 0x08
TELINK_FIRMWARE_SIZE_OFFSET = 0x18

RESULT_TEXT = {
    0x00: "OTA_SUCCESS",
    0x01: "OTA_DATA_PACKET_SEQ_ERR",
    0x02: "OTA_PACKET_INVALID",
    0x03: "OTA_DATA_CRC_ERR",
    0x04: "OTA_WRITE_FLASH_ERR",
    0x05: "OTA_DATA_INCOMPLETE",
    0x06: "OTA_FLOW_ERR",
    0x07: "OTA_FW_CHECK_ERR",
    0x08: "OTA_VERSION_COMPARE_ERR",
    0x09: "OTA_PDU_LEN_ERR",
    0x0A: "OTA_FIRMWARE_MARK_ERR",
    0x0B: "OTA_FW_SIZE_ERR",
    0x0C: "OTA_DATA_PACKET_TIMEOUT",
    0x0D: "OTA_TIMEOUT",
    0x0E: "OTA_FAIL_DUE_TO_CONNECTION_TERMINATE",
    0x0F: "OTA_MCU_NOT_SUPPORTED",
    0x10: "OTA_LOGIC_ERR",
}


class TelinkOtaError(ValueError):
    pass


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc & 0xFFFF


def build_start_packet() -> bytes:
    return OTA_CMD_START.to_bytes(2, "little")


def build_data_packet(index: int, payload: bytes) -> bytes:
    if index < 0 or index > 0xFFFF:
        raise TelinkOtaError(f"OTA index out of range: {index}")
    if len(payload) > OTA_PDU_BYTES:
        raise TelinkOtaError(f"OTA payload must be <= {OTA_PDU_BYTES} bytes")
    data = payload.ljust(OTA_PDU_BYTES, b"\xFF")
    body = index.to_bytes(2, "little") + data
    return body + crc16(body).to_bytes(2, "little")


def build_end_packet(max_index: int) -> bytes:
    if max_index < 0 or max_index > 0xFFFF:
        raise TelinkOtaError(f"OTA max index out of range: {max_index}")
    inverted = max_index ^ 0xFFFF
    return b"\x02\xFF" + max_index.to_bytes(2, "little") + inverted.to_bytes(2, "little")


def parse_result_packet(data: bytes) -> int | None:
    if len(data) < 3:
        return None
    if int.from_bytes(data[:2], "little") != OTA_CMD_RESULT:
        return None
    return data[2]


def result_text(code: int) -> str:
    return RESULT_TEXT.get(code, f"OTA_UNKNOWN_RESULT_0x{code:02X}")


@dataclass(frozen=True)
class TelinkFirmwareImage:
    source: bytes
    firmware: bytes
    declared_size: int
    packet_count: int
    max_index: int

    @classmethod
    def from_bytes(cls, raw: bytes) -> "TelinkFirmwareImage":
        if len(raw) < TELINK_FIRMWARE_SIZE_OFFSET + 4:
            raise TelinkOtaError("firmware is too small to contain Telink header")
        if raw[TELINK_FIRMWARE_MARK_OFFSET:TELINK_FIRMWARE_MARK_OFFSET + 4] != TELINK_FIRMWARE_MARK:
            raise TelinkOtaError("firmware mark at 0x08 is not KNLT")
        declared_size = int.from_bytes(
            raw[TELINK_FIRMWARE_SIZE_OFFSET:TELINK_FIRMWARE_SIZE_OFFSET + 4], "little"
        )
        if declared_size <= 0 or declared_size > len(raw):
            raise TelinkOtaError(
                f"invalid firmware size field: declared={declared_size}, file={len(raw)}"
            )
        firmware = raw[:declared_size]
        packet_count = (declared_size + OTA_PDU_BYTES - 1) // OTA_PDU_BYTES
        if packet_count <= 0 or packet_count > 0x10000:
            raise TelinkOtaError(f"unsupported OTA packet count: {packet_count}")
        return cls(raw, firmware, declared_size, packet_count, packet_count - 1)

    @classmethod
    def from_file(cls, path: str | Path) -> "TelinkFirmwareImage":
        return cls.from_bytes(Path(path).read_bytes())

    def data_packet(self, index: int) -> bytes:
        if index < 0 or index >= self.packet_count:
            raise TelinkOtaError(f"OTA packet index out of image: {index}")
        start = index * OTA_PDU_BYTES
        return build_data_packet(index, self.firmware[start:start + OTA_PDU_BYTES])

    def iter_data_packets(self):
        for index in range(self.packet_count):
            yield self.data_packet(index)

    def end_packet(self) -> bytes:
        return build_end_packet(self.max_index)
