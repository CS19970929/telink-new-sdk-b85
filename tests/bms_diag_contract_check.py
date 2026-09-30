#!/usr/bin/env python3
"""Static contract for the shared SH3673510 diagnostics window."""

from pathlib import Path
from project_paths import Sources, host_includes, selected_source


ROOT = Path(__file__).resolve().parents[1]
BLE = Sources(ROOT)


def require(text: str, token: str, source: str) -> None:
    if token not in text:
        raise AssertionError(f"{source}: missing {token}")


def main() -> int:
    header = (BLE / "bms_diag.h").read_text(encoding="utf-8")
    source = (BLE / "bms_diag.c").read_text(encoding="utf-8") + (BLE / "bms_storage_platform_telink.c").read_text(encoding="utf8")
    app = (BLE / "app.c").read_text(encoding="utf-8")
    modbus = (BLE / "modbus_rtu.c").read_text(encoding="utf-8")
    order = (ROOT / "bms/products" / BLE.product / "sources.txt").read_text(encoding="utf-8")
    build = (ROOT / "bms_tools" / "build.mk").read_text(encoding="utf-8")

    for token in ("0x2A00u", "0x2B00u", "0x2E00u", "BMS_DIAG_RUNTIME_VERSION 3u"):
        require(header, token, "bms_diag.h")
    for token in ("s_words[0] = 0x4447u", "bms_diag_boot_word(14u, bms_afe_hw_profile_expected_model())",
                  "bms_diag_boot_word(15u, 0x8251u)", "BMS_DIAG_BUILD_ID"):
        require(source, token, "bms_diag.c")
    for token in ("bms_diag_init();", "bms_diag_freeze_boot();",
                  "bms_diag_poll_runtime("):
        require(app, token, "app.c")
    require(modbus, "bms_diag_read(reg, qty, &rsp[3])", "modbus_rtu.c")
    if modbus.count("bms_diag_overlaps") < 3:
        raise AssertionError("diagnostic window must reject both single and multiple writes")
    require(order, "bms/core/bms_diag.c", "source_order.txt")
    require(build, "-DMCU_STARTUP_8251", "build.mk")
    require(build, "$(EXTRA_DEFINES)", "build.mk")
    print("SH3673510 diagnostics contract PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
