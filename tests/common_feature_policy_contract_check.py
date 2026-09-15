#!/usr/bin/env python3
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
APP=ROOT/"tc_ble_single_sdk-V3.4.2.8_Patch_0001"/"tc_ble_single_sdk"/"vendor"/"ble_sample"
def text(name):
    p=APP/name
    if not p.exists(): raise AssertionError(f"missing {p}")
    return p.read_text(encoding="utf-8",errors="replace")
features_h=text("bms_features.h");features_c=text("bms_features.c");board_c=text("bms_board.c");guard_c=text("bms_afe_guard.c");afe_h=text("bms_afe.h");dvc=text("dvc1124_feature_backend.c")
assert "#define BMS_HEATER_START_TEMP_X10 400u" in features_h
assert "#define BMS_HEATER_STOP_TEMP_X10 450u" in features_h
assert "bms_afe_get_charge_source_present" in afe_h and "bms_afe_get_charge_source_present" in guard_c
assert "charge_source_present" in features_c
assert "openwire_fault_latched" in features_c
assert "CHG_IN_PIN" in board_c
assert "dvc1124_backend_get_charge_source_present" in dvc and "return 0u" in dvc
assert "DVC1124_BalanceService" in dvc and "DVC1124_OpenWireBegin" in dvc
assert "out->determinate = 0u" in dvc
assert "bms_afe_set_balance_mask(0u)" in features_c
assert "bms_afe_openwire_start" in features_c and "bms_afe_openwire_poll" in features_c
charge_block=guard_c.index("bms_features_charge_blocked");fet_write=guard_c.index("AFE_FETS",charge_block);assert charge_block<fet_write
print("common feature policy contract: PASS")
