#!/usr/bin/env python3
"""Static contract for the shared SH3673510 diagnostics window."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BLE = ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001" / "tc_ble_single_sdk" / "vendor" / "ble_sample"


def require(text: str, token: str, source: str) -> None:
    if token not in text:
        raise AssertionError(f"{source}: missing {token}")


def main() -> int:
    header = (BLE / "bms_diag.h").read_text(encoding="utf-8")
    source = (BLE / "bms_diag.c").read_text(encoding="utf-8")
    app = (BLE / "app.c").read_text(encoding="utf-8")
    modbus = (BLE / "modbus_rtu.c").read_text(encoding="utf-8")
    order = (ROOT / "bms_tools" / "source_order.txt").read_text(encoding="utf-8")
    build = (ROOT / "bms_tools" / "build.mk").read_text(encoding="utf-8")

    for token in ("0x2A00u", "0x2B00u", "0x2E00u", "BMS_DIAG_RUNTIME_VERSION 2u"):
        require(header, token, "bms_diag.h")
    for token in ("s_words[0] = 0x4447u", "s_words[14] = 0x3510u",
                  "s_words[15] = 0x8251u", "BMS_DIAG_BUILD_ID"):
        require(source, token, "bms_diag.c")
    for token in ("bms_diag_init();", "bms_diag_freeze_boot();",
                  "bms_diag_poll_runtime("):
        require(app, token, "app.c")
    require(modbus, "bms_diag_read(reg, qty, &rsp[3])", "modbus_rtu.c")
    if modbus.count("bms_diag_overlaps") < 3:
        raise AssertionError("diagnostic window must reject both single and multiple writes")
    require(order, "vendor/ble_sample/bms_diag.c", "source_order.txt")
    require(build, "-DMCU_STARTUP_8251", "build.mk")
    require(build, "$(EXTRA_DEFINES)", "build.mk")
    print("SH3673510 diagnostics contract PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
