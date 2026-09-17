#!/usr/bin/env python3
"""Static contract for the AFE-independent D008/D011/D013 software protection core."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HERE = ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001" / "tc_ble_single_sdk" / "vendor" / "ble_sample"
source = (HERE / "bms_sw_protection.c").read_text(encoding="utf-8", errors="ignore")
header = (HERE / "bms_sw_protection.h").read_text(encoding="utf-8", errors="ignore")
backend = (HERE / "bms_afe_backend.h").read_text(encoding="utf-8", errors="ignore")

for token in (
    "BMS_SW_PROTECTION_LEVEL_COUNT     3u",
    "BMS_SW_PROTECTION_FILTER_COUNT    12u",
    "trip_count",
    "recover_count",
    "unMdlFault_First",
    "unMdlFault_Second",
    "unMdlFault_Third",
    "u16VcellOvp_First",
    "u16VcellOvp_Second",
    "u16VcellOvp_Third",
    "u16VcellUvp_First",
    "u16VbusOvp_First",
    "u16VbusUvp_First",
    "u16IchgOcp_First",
    "u16IdsgOcp_First",
    "u16TChgOTp_First",
    "u16TchgUTp_First",
    "u16TdischgOTp_First",
    "u16TdischgUTp_First",
    "u16TmosOTp_First",
    "u16VdeltaOvp_First",
    "BMS_ERROR_TEMP_BREAK",
    "bms_sw_protection_record_fault_edges",
):
    if token not in source and token not in header:
        raise AssertionError(f"missing common protection invariant: {token}")

for forbidden in ("DVC1124_", "SH3673510_", "SH3673520_", "gpio_", "ReadReg", "WriteReg"):
    if forbidden in source:
        raise AssertionError(f"common protection leaked backend detail: {forbidden}")

if "p->u16SocUp_" in source:
    raise AssertionError("legacy SOC protection must remain out until semantics are specified")

# Recover applies only to Third. First/Second alarms clear outside their own
# threshold after filtering; equality remains active (no boundary oscillation).
for token in (
    "bms_sw_high_recovery_valid",
    "bms_sw_low_recovery_valid",
    "return !third || recover < third;",
    "return !third || recover > third;",
    "(value < trip) : (value > trip)",
    "level == 2u",
    "if (!bms_protection_params_valid())",
    "bms_sw_protection_clear();",
):
    if token not in source:
        raise AssertionError(f"software protection fail-safe missing: {token}")

# Battery OTP/UTP and MOS OTP have different sensor ownership. One invalid NTC
# must not erase the other sensor's protection filters/fault state.
for token in (
    "if (temperature_enabled && inputs->battery_temp_valid)",
    "if (temperature_enabled && inputs->mos_temp_valid)",
    "bms_sw_filter_reset(&s_filter[level][BMS_SW_F_MOS_OT])",
    "bms_sw_filter_reset(&s_filter[level][BMS_SW_F_CHG_OT])",
):
    if token not in source:
        raise AssertionError(f"independent temperature validity missing: {token}")
if "if (temp_valid)" in source:
    raise AssertionError("battery and MOS temperature protections must not share one combined validity gate")

# Charge/discharge battery-temperature faults must not start from temperature
# alone. New charge faults require charge current, new discharge faults require
# discharge current. Once active, recovery must remain temperature-driven so the
# current dropping to zero after FET shutdown cannot immediately clear the fault.
for token in (
    "static uint8_t bms_sw_temp_filter_update",
    "charge_current_present = (g_stCellInfoReport.u16Ichg > 0u)",
    "discharge_current_present = (g_stCellInfoReport.u16IDischg > 0u)",
    "if (!state->active && !trip_enabled)",
    "state->trip_count = 0u;",
    "charge_current_present, inputs->battery_temp_max",
    "charge_current_present, inputs->battery_temp_min",
    "discharge_current_present, inputs->battery_temp_max",
    "discharge_current_present, inputs->battery_temp_min",
):
    if token not in source:
        raise AssertionError(f"directional temperature trigger gate missing: {token}")

battery_block = source.split("if (temperature_enabled && inputs->battery_temp_valid)", 1)[1].split("else", 1)[0]
if battery_block.count("bms_sw_temp_filter_update") != 4:
    raise AssertionError("all four battery temperature faults must use the directional trigger gate")

mos_block = source.split("if (temperature_enabled && inputs->mos_temp_valid)", 1)[1].split("else", 1)[0]
if "bms_sw_temp_filter_update" in mos_block:
    raise AssertionError("power-MOS OTP must remain independent of charge/discharge current direction")
if "bms_sw_filter_update" not in mos_block:
    raise AssertionError("power-MOS OTP filter missing")

if "BMS_AFE_BACKEND_DVC1124" in backend and "#define BMS_AFE_BACKEND BMS_AFE_BACKEND_DVC1124" in backend:
    dvc = (HERE / "dvc1124_bms.c").read_text(encoding="utf-8", errors="ignore")
    for token in ("bms_sw_protection_update_groups(&sw, DVC1124_SW_PROTECT_ENABLE,", "dvc_merge_hw_faults(alarm);", "bms_sw_protection_record_fault_edges();"):
        if token not in dvc:
            raise AssertionError(f"D008 integration missing: {token}")
    if "dvc_publish_faults(alarm, &cfg);" in dvc:
        raise AssertionError("D008 still executes legacy per-backend software protection")

if "BMS_AFE_BACKEND_SH3673510" in backend and "#define BMS_AFE_BACKEND BMS_AFE_BACKEND_SH3673510" in backend:
    sh = (HERE / "sh3673510_bms.c").read_text(encoding="utf-8", errors="ignore")
    for token in ("bms_sw_protection_update(&sw);", "bms_sw_protection_clear();", "bms_sw_protection_record_fault_edges();"):
        if token not in sh:
            raise AssertionError(f"SH3673510 integration missing: {token}")
    if "update_faults();" in sh:
        raise AssertionError("SH3673510 still executes legacy per-backend software protection")
    if sh.index("bms_sw_protection_update(&sw);") > sh.index("merge_hw_protection_faults(&status);"):
        raise AssertionError("software protection must run before hardware flags are merged")
    if sh.index("merge_hw_protection_faults(&status);") > sh.index("bms_sw_protection_record_fault_edges();"):
        raise AssertionError("fault history must observe merged software + hardware Third state")

print("Unified software protection contract: PASS")
