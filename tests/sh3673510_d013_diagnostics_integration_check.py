#!/usr/bin/env python3
"""D013 runtime-to-diagnostics integration contract checks."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BLE = ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001" / "tc_ble_single_sdk" / "vendor" / "ble_sample"


def require(text: str, token: str, source: str) -> None:
    if token not in text:
        raise AssertionError(f"{source}: missing {token}")


def main() -> int:
    app = (BLE / "app.c").read_text(encoding="utf-8", errors="ignore")
    diag = (BLE / "bms_diag.c").read_text(encoding="utf-8", errors="ignore")
    afe = (BLE / "sh3673510_bms.c").read_text(encoding="utf-8", errors="ignore")

    for token in (
        "clock_time_exceed(test_task_tick, 1000 * 200)",
        "sample_valid = bms_afe_get_aux_measurements(&sample);",
        "bms_diag_poll_runtime(sample_valid,",
        "sample_valid ? sample.current_ma : 0",
        "sample_valid ? sample.sample_tick_32k : pm_get_32k_tick()",
    ):
        require(app, token, "app.c")

    for token in (
        "sh3673510_bms_afe_get_fet_diagnostics(",
        "bms_soc_get_diag(&soc)",
        "populate_storage_layout();",
    ):
        require(diag, token, "bms_diag.c")

    for token in (
        "s_aux.current_ma = current_ma;",
        "s_aux.sample_tick_32k = pm_get_32k_tick();",
        "*driver_bits = (uint8_t)((g_bms_system_status.bits.b1Status_MOS_CHG ? 1u : 0u)",
    ):
        require(afe, token, "sh3673510_bms.c")

    for name in ("bms_config_store.c", "bms_state_store.c", "bms_event_log.c"):
        store = (BLE / name).read_text(encoding="utf-8", errors="ignore")
        require(store, "bms_diag_attempt(", name)
        require(store, "bms_diag_result(", name)

    print("D013 diagnostics integration contract PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
