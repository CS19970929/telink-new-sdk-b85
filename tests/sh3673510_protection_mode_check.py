#!/usr/bin/env python3
"""Static contract checks for independent SH3673510 SW/HW protection modes."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HERE = ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001" / "tc_ble_single_sdk" / "vendor" / "ble_sample"


def text(name: str) -> str:
    return (HERE / name).read_text(encoding="utf-8", errors="ignore")


def require(src: str, needle: str) -> None:
    if needle not in src:
        raise AssertionError(f"missing protection-mode invariant: {needle}")


cfg = text("sh3673510_project_config.h")
bms = text("sh3673510_bms.c")

for needle in (
    "#define SH3673510_SW_PROTECT_ENABLE             1u",
    "#define SH3673510_HW_PROTECT_ENABLE             1u",
    "#define SH3673510_D011_PD_EN                      SH3673510_HW_PROTECT_ENABLE",
    "#define SH3673510_D011_MOS_EN                     SH3673510_HW_PROTECT_ENABLE",
    "#define SH3673510_D011_OCC_EN                     SH3673510_HW_PROTECT_ENABLE",
    "#define SH3673510_D011_WDT_EN                     SH3673510_HW_PROTECT_ENABLE",
    "#define SH3673510_D011_TS2_HW_PROTECT_EN           SH3673510_HW_PROTECT_ENABLE",
    "#define SH3673510_D011_TS1_HW_PROTECT_EN           SH3673510_HW_PROTECT_ENABLE",
    "#define SH3673510_D011_SC_HW_PROTECT_EN            SH3673510_HW_PROTECT_ENABLE",
    "#define SH3673510_D011_OCD_HW_PROTECT_EN           SH3673510_HW_PROTECT_ENABLE",
    "#define SH3673510_D011_UV_HW_PROTECT_EN            SH3673510_HW_PROTECT_ENABLE",
    "#define SH3673510_D011_OV_HW_PROTECT_EN            SH3673510_HW_PROTECT_ENABLE",
    "#define SH3673510_D011_CADC_EN                    1u",
):
    require(cfg, needle)

for needle in (
    "#if SH3673510_SW_PROTECT_ENABLE",
    "bms_sw_protection_clear();",
    "#if SH3673510_HW_PROTECT_ENABLE",
    "merge_hw_protection_faults(&status);",
    "service_short_recovery(&status);",
    "service_hw_flag_recovery(&status);",
    "s_hw_charge_protect = 0u;",
    "s_hw_discharge_protect = 0u;",
):
    require(bms, needle)

# The HW-only test must not be masked by a backend-owned heater state machine.
if "static void apply_heater" in bms or "s_heater_mos_overtemp" in bms:
    raise AssertionError("SH backend must not own heater policy")

print("SH3673510 protection mode contract: PASS")
