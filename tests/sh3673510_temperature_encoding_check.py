#!/usr/bin/env python3
"""Regression checks for SH3673510 hardware temperature threshold encoding."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CONTROL = (
    ROOT
    / "tc_ble_single_sdk-V3.4.2.8_Patch_0001"
    / "tc_ble_single_sdk"
    / "vendor"
    / "ble_sample"
    / "sh3673510_control.c"
).read_text(encoding="utf-8", errors="ignore")

CORRECT_HIGH_FORMULA = (
    "result = ((uint32_t)r100 * 512u + (denominator / 2u)) / denominator;"
)
OBSOLETE_HIGH_FORMULA = "700L - (3L * (int32_t)r100)"

if CORRECT_HIGH_FORMULA not in CONTROL:
    raise AssertionError("SH3673510 high-temperature divider formula is missing")
if OBSOLETE_HIGH_FORMULA in CONTROL:
    raise AssertionError("obsolete SH3673510 high-temperature formula reintroduced")


def high_code(r100: int) -> int:
    """Rntc/(10K+Rntc)*512, with resistance in 100-ohm units."""
    denominator = r100 + 100
    return (r100 * 512 + denominator // 2) // denominator


# Existing 10K NTC table points used by the firmware:
# 55 C -> 3.5 kOhm, 60 C -> 3.0 kOhm.
assert high_code(35) == 0x85
assert high_code(30) == 0x76

print("SH3673510 temperature encoding contract: PASS")
