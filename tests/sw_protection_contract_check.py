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

# Battery OTP/UTP and MOS OTP have different sensor ownership. One invalid NTC
# must not erase the other sensor's protection filters/fault state.
for token in (
    "if (inputs->battery_temp_valid)",
    "if (inputs->mos_temp_valid)",
    "bms_sw_filter_reset(&s_filter[level][BMS_SW_F_MOS_OT])",
    "bms_sw_filter_reset(&s_filter[level][BMS_SW_F_CHG_OT])",
):
    if token not in source:
        raise AssertionError(f"independent temperature validity missing: {token}")
if "if (temp_valid)" in source:
    raise AssertionError("battery and MOS temperature protections must not share one combined validity gate")

if "BMS_AFE_BACKEND_DVC1124" in backend and "#define BMS_AFE_BACKEND BMS_AFE_BACKEND_DVC1124" in backend:
    dvc = (HERE / "dvc1124_bms.c").read_text(encoding="utf-8", errors="ignore")
    for token in ("bms_sw_protection_update(&sw);", "dvc_merge_hw_faults(alarm);", "bms_sw_protection_record_fault_edges();"):
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
