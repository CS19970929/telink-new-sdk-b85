#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001" / "tc_ble_single_sdk" / "vendor" / "ble_sample"

def text(name: str) -> str:
    path = APP / name
    if not path.exists(): raise AssertionError(f"missing {path}")
    return path.read_text(encoding="utf-8", errors="replace")

features_h=text("bms_features.h"); features_c=text("bms_features.c"); board_h=text("bms_board.h"); guard_c=text("bms_afe_guard.c"); afe_h=text("bms_afe.h")
assert "#define BMS_HEATER_START_TEMP_X10 400u" in features_h
assert "#define BMS_HEATER_STOP_TEMP_X10 450u" in features_h
assert "bms_afe_get_charge_source_present" in afe_h
assert "bms_afe_get_charge_source_present" in guard_c
assert "charge_source_present" in features_c
assert "BMS_HEATER_START_TEMP_X10" in features_c and "BMS_HEATER_STOP_TEMP_X10" in features_c
assert "openwire_fault_latched" in features_c
assert "bms_board_heater_set" in board_h and "bms_board_charge_source_present" in board_h
assert "service_balance" in features_c and "bms_afe_set_balance_mask(0u)" in features_c
assert "service_openwire" in features_c and "bms_afe_openwire_start" in features_c and "bms_afe_openwire_poll" in features_c
charge_block=guard_c.index("bms_features_charge_blocked"); fet_write=guard_c.index("AFE_FETS",charge_block)
assert charge_block < fet_write
assert "bms_features_service();" in guard_c and "bms_features_on_afe_invalid();" in guard_c
for symbol in ("bms_afe_get_feature_snapshot","bms_afe_get_charge_source_present","bms_afe_set_balance_mask","bms_afe_get_balance_mask","bms_afe_openwire_start","bms_afe_openwire_poll"): assert symbol in afe_h

sh=text("sh3673510_feature_backend.c")
for token in (
    "SH3673520_REG_VCHGRH","SH_CHARGER_PRESENT_ON_MV","SH_CHARGER_PRESENT_OFF_MV",
    "SH3673520_SCONF3_OWD_EN_MASK","SH3673520_SCONF3_OWD_TRG_MASK",
    "SH3673520_REG_FLAG3","SH3673520_REG_OWDH","0x000AAAAA","0x00055555",
    "SH_FEATURE_BALANCE_REFRESH_SAMPLES 100u","sh_read_balance_mask"
): assert token in sh, token

print("common feature policy contract: PASS")
