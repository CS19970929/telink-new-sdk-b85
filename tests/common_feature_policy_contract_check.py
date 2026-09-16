#!/usr/bin/env python3
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
APP=ROOT/"tc_ble_single_sdk-V3.4.2.8_Patch_0001"/"tc_ble_single_sdk"/"vendor"/"ble_sample"
def text(name):
    p=APP/name
    if not p.exists(): raise AssertionError(f"missing {p}")
    return p.read_text(encoding="utf-8",errors="replace")
features_h=text("bms_features.h");features_c=text("bms_features.c");board_c=text("bms_board.c");guard_c=text("bms_afe_guard.c");afe_h=text("bms_afe.h");dvc=text("dvc1124_feature_backend.c");dvc_service=text("dvc1124_config_service.c")
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

# Communication-loss fail-safe contract: one bounded software OFF attempt, then
# a completely silent AFE bus while the hardware watchdog owns final MOS safety.
assert "#define BMS_AFE_COMM_FAILS_BEFORE_SILENCE    2u" in guard_c
assert "#define BMS_AFE_FAILSAFE_WAIT_SAMPLES 25u" in guard_c
assert "#define BMS_AFE_FAILSAFE_WAIT_SAMPLES 175u" in guard_c
assert "if (service_failsafe_wait()) return;" in guard_c
assert "Absolutely no AFE I2C/SPI access while the hardware watchdog is timing." in guard_c
assert "if (s_guard.comm_failures == 0u) best_effort_shutdown();" in guard_c
assert "s_guard.bus_silenced = 1u;" in guard_c
assert "bms_afe_bus_access_allowed" in afe_h and "bms_afe_bus_access_allowed" in guard_c
assert "bms_afe_bus_access_allowed" in dvc_service
assert dvc_service.count("if (!bms_afe_bus_access_allowed()) return DVC1124_CFG_ERR_AFE_IO;") >= 2
assert "AFE_INIT();" in guard_c and "AFE_OUTPUT(s_guard.output_enabled);" in guard_c
apply=guard_c.split("static uint8_t apply_requested",1)[1].split("static void note_invalid",1)[0]
assert "if (s_guard.comm_inhibit || s_guard.bus_silenced) return 1u;" in apply
sleep=guard_c.split("void bms_afe_sleep",1)[1].split("uint8_t bms_afe_apply_protection_config",1)[0]
assert sleep.index("if (s_guard.bus_silenced) return;") < sleep.index("AFE_SLEEP();")
setfets=guard_c.split("uint8_t bms_afe_set_fets",1)[1].split("void bms_afe_get_requested_fets",1)[0]
assert "if (s_guard.comm_inhibit || s_guard.bus_silenced) return 1u;" in setfets

print("common feature policy contract: PASS")
