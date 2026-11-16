#!/usr/bin/env python3
"""Repeatable comm tool/BMS CAN upgrade reliability checks."""

from __future__ import annotations

import argparse
import math
import struct
import time
import zlib
from pathlib import Path

from comm_tool_host import (
    APP_BASE_ADDR,
    APP_FLASH_LIMIT,
    CMD_BMS_READ,
    CMD_CAN_DIAG,
    CMD_FW_BEGIN,
    CMD_FW_DATA,
    CMD_FW_END,
    CMD_FW_INFO,
    CMD_GET_INFO,
    CMD_UPGRADE,
    CMD_UPGRADE_STATUS,
    CommToolClient,
    crc16_modbus,
    decode_can_esr,
    load_image,
    vector_summary,
)


BMS_OVERVIEW_ADDR = 0xD000
BMS_OVERVIEW_WORDS = 63
BMS_STATUS_ADDR = 0xD034


def read_words(client: CommToolClient, addr: int, count: int) -> list[int]:
    resp = client.command(CMD_BMS_READ, struct.pack("<HH", addr, count), timeout=max(10.0, count * 0.08 + 3.0))
    if len(resp.payload) != count * 2:
        raise RuntimeError(f"BMS_READ length mismatch: {len(resp.payload)}")
    return list(struct.unpack("<" + "H" * count, resp.payload))


def read_info(client: CommToolClient) -> dict:
    payload = client.command(CMD_GET_INFO, timeout=2.0).payload
    if len(payload) < 20:
        raise RuntimeError("GET_INFO length too short")
    proto, major, minor, patch = struct.unpack_from("<BBBB", payload, 0)
    bitrate, cache_base, cache_size, flags = struct.unpack_from("<IIII", payload, 4)
    return {
        "proto": proto,
        "version": f"{major}.{minor}.{patch}",
        "bitrate": bitrate,
        "cache_base": cache_base,
        "cache_size": cache_size,
        "flags": flags,
    }


def read_fw_info(client: CommToolClient) -> dict:
    payload = client.command(CMD_FW_INFO, timeout=2.0).payload
    if len(payload) < 15:
        raise RuntimeError("FW_INFO length too short")
    app_addr, size, crc16, crc32, valid = struct.unpack_from("<IIHIB", payload, 0)
    return {"app_addr": app_addr, "size": size, "crc16": crc16, "crc32": crc32, "valid": valid}


def read_upgrade_status(client: CommToolClient) -> dict:
    payload = client.command(CMD_UPGRADE_STATUS, timeout=2.0).payload
    if len(payload) < 13:
        raise RuntimeError("UPGRADE_STATUS length too short")
    state, percent, error = struct.unpack_from("<BBB", payload, 0)
    written, total, expect_seq = struct.unpack_from("<IIH", payload, 3)
    return {"state": state, "percent": percent, "error": error, "written": written, "total": total, "expect_seq": expect_seq}


def read_can_diag(client: CommToolClient, clear: bool = False) -> dict:
    payload = b"\x01" if clear else b"\x00"
    data = client.command(CMD_CAN_DIAG, payload, timeout=2.0).payload
    if len(data) < 62:
        raise RuntimeError("CAN_DIAG length too short")
    fields = struct.unpack_from("<IIIIIIIIIIII", data, 0)
    last_tx_ide, last_tx_dlc, last_tx_status, last_rx_ide, last_rx_dlc = struct.unpack_from("<BBBBB", data, 48)
    return {
        "tx_count": fields[0],
        "tx_ok": fields[1],
        "tx_fail": fields[2],
        "tx_timeout": fields[3],
        "rx_count": fields[4],
        "rx_drop": fields[5],
        "esr": fields[6],
        "tsr": fields[7],
        "msr": fields[8],
        "rf0r": fields[9],
        "last_tx_id": fields[10],
        "last_rx_id": fields[11],
        "last_tx_ide": last_tx_ide,
        "last_tx_dlc": last_tx_dlc,
        "last_tx_status": last_tx_status,
        "last_rx_ide": last_rx_ide,
        "last_rx_dlc": last_rx_dlc,
    }


def print_can_diag(prefix: str, diag: dict) -> None:
    print(
        f"{prefix}: tx={diag['tx_count']} ok={diag['tx_ok']} fail={diag['tx_fail']} timeout={diag['tx_timeout']} "
        f"rx={diag['rx_count']} drop={diag['rx_drop']} ESR=0x{diag['esr']:08X} {decode_can_esr(diag['esr'])}"
    )


def download_image(client: CommToolClient, image: bytes, chunk_size: int) -> None:
    crc16 = crc16_modbus(image)
    crc32 = zlib.crc32(image) & 0xFFFFFFFF
    client.command(CMD_FW_BEGIN, struct.pack("<IIHI", APP_BASE_ADDR, len(image), crc16, crc32), timeout=10.0)
    total = math.ceil(len(image) / chunk_size)
    for index, offset in enumerate(range(0, len(image), chunk_size), 1):
        chunk = image[offset : offset + chunk_size]
        client.command(CMD_FW_DATA, struct.pack("<I", offset) + chunk, timeout=10.0)
        if index == total or index % 32 == 0:
            print(f"  cache download {index}/{total}")
    client.command(CMD_FW_END, struct.pack("<IHI", len(image), crc16, crc32), timeout=10.0)


def assert_cache_matches(image: bytes, cache: dict) -> None:
    crc16 = crc16_modbus(image)
    crc32 = zlib.crc32(image) & 0xFFFFFFFF
    if cache["app_addr"] != APP_BASE_ADDR or cache["size"] != len(image):
        raise RuntimeError("comm tool cache address/size mismatch")
    if cache["crc16"] != crc16 or cache["crc32"] != crc32 or cache["valid"] != 1:
        raise RuntimeError("comm tool cache CRC mismatch")


def wait_upgrade_done(client: CommToolClient, timeout_s: float) -> dict:
    deadline = time.monotonic() + timeout_s
    last_status = {}
    while time.monotonic() < deadline:
        last_status = read_upgrade_status(client)
        print(
            "  upgrade state={state} percent={percent}% error=0x{error:02X} written={written}/{total} expect_seq={expect_seq}".format(
                **last_status
            )
        )
        if last_status["state"] == 2:
            if last_status["error"] != 0:
                raise RuntimeError(f"upgrade finished with error 0x{last_status['error']:02X}")
            return last_status
        if last_status["error"] != 0:
            raise RuntimeError(f"upgrade error 0x{last_status['error']:02X}")
        time.sleep(1.0)
    raise TimeoutError(f"upgrade did not finish: {last_status}")


def wait_bms_app(client: CommToolClient, timeout_s: float) -> list[int]:
    deadline = time.monotonic() + timeout_s
    last_error = ""
    while time.monotonic() < deadline:
        try:
            words = read_words(client, BMS_STATUS_ADDR, 2)
            print(f"  BMS App ready: SOC={words[0]} SOH={words[1]}")
            return words
        except Exception as exc:
            last_error = str(exc)
            print(f"  wait BMS App: {last_error}")
            time.sleep(1.0)
    raise TimeoutError(f"BMS App did not respond: {last_error}")


def run(args: argparse.Namespace) -> int:
    image = None
    if args.bin:
        image = load_image(Path(args.bin), APP_BASE_ADDR)
        if APP_BASE_ADDR + len(image) > APP_FLASH_LIMIT:
            raise RuntimeError("bin exceeds app flash limit")
        msp, reset, msp_ok, reset_ok = vector_summary(image)
        print(f"bin: {args.bin} size={len(image)} MSP=0x{msp:08X} {msp_ok} Reset=0x{reset:08X} {reset_ok}")
        if not msp_ok or not reset_ok:
            raise RuntimeError("invalid app vector")

    with CommToolClient(args.port, args.baud, timeout=args.timeout) as client:
        print("== comm tool ==")
        info = read_info(client)
        print(f"version={info['version']} proto={info['proto']} can={info['bitrate']} cache=0x{info['cache_base']:08X}+{info['cache_size']}")

        print("== CAN diag clear ==")
        print_can_diag("before", read_can_diag(client, clear=True))

        print("== BMS App read-only check ==")
        status_words = read_words(client, BMS_STATUS_ADDR, 2)
        print(f"SOC={status_words[0]} SOH={status_words[1]}")
        overview = read_words(client, BMS_OVERVIEW_ADDR, BMS_OVERVIEW_WORDS)
        valid_cells = [value for value in overview[0:32] if value != 0]
        print(
            f"overview: cells={len(valid_cells)} total={overview[37] / 100.0:.2f}V "
            f"soc={overview[52]} soh={overview[53]} delta={overview[36]}mV"
        )

        if image is not None:
            cache = read_fw_info(client)
            if args.download_cache:
                if not args.confirm_download:
                    raise RuntimeError("add --confirm-download to write comm tool cache")
                print("== download cache ==")
                download_image(client, image, args.chunk_size)
                cache = read_fw_info(client)
            assert_cache_matches(image, cache)
            print("cache matches selected bin")

        if args.upgrade_loops > 0:
            if image is None:
                raise RuntimeError("--upgrade-loops requires --bin so cache can be verified")
            if not args.confirm_upgrade:
                raise RuntimeError("add --confirm-upgrade to run BMS upgrade loops")
            for loop in range(1, args.upgrade_loops + 1):
                print(f"== upgrade loop {loop}/{args.upgrade_loops} ==")
                client.command(CMD_CAN_DIAG, b"\x01", timeout=2.0)
                client.command(CMD_UPGRADE, timeout=args.long_timeout)
                wait_upgrade_done(client, args.long_timeout)
                wait_bms_app(client, args.post_upgrade_timeout)
                overview = read_words(client, BMS_OVERVIEW_ADDR, BMS_OVERVIEW_WORDS)
                print(f"post-upgrade overview ok: soc={overview[52]} soh={overview[53]} total={overview[37] / 100.0:.2f}V")

        print("== CAN diag after ==")
        print_can_diag("after", read_can_diag(client, clear=False))
    print("reliability test PASS")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="comm tool/BMS CAN upgrade reliability test")
    parser.add_argument("--port", default="COM4")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=1.0)
    parser.add_argument("--long-timeout", type=float, default=180.0)
    parser.add_argument("--post-upgrade-timeout", type=float, default=20.0)
    parser.add_argument("--bin", default="")
    parser.add_argument("--download-cache", action="store_true")
    parser.add_argument("--confirm-download", action="store_true")
    parser.add_argument("--upgrade-loops", type=int, default=0)
    parser.add_argument("--confirm-upgrade", action="store_true")
    parser.add_argument("--chunk-size", type=int, default=256)
    args = parser.parse_args()
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main())
