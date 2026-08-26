from bmsassistantqt.ota import (
    build_data_packet,
    build_end_packet,
    build_start_packet,
    parse_result_packet,
)


def test_start_packet() -> None:
    assert build_start_packet() == bytes.fromhex("01 FF")


def test_first_data_packet() -> None:
    payload = bytes.fromhex("26 80 00 00 00 00 5D 02 4B 4E 4C 54 30 04 88 00")
    expected = bytes.fromhex("00 00 26 80 00 00 00 00 5D 02 4B 4E 4C 54 30 04 88 00 1C A3")
    assert build_data_packet(0, payload) == expected


def test_end_packet() -> None:
    assert build_end_packet(0x14FA) == bytes.fromhex("02 FF FA 14 05 EB")


def test_result_packet() -> None:
    assert parse_result_packet(bytes.fromhex("06 FF 00")) == 0
    assert parse_result_packet(bytes.fromhex("06 FF 03")) == 3
