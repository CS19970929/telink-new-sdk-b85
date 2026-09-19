#!/usr/bin/env python3
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
APP=ROOT/"tc_ble_single_sdk-V3.4.2.8_Patch_0001"/"tc_ble_single_sdk"/"vendor"/"ble_sample"
def text(name):
    p=APP/name
    if not p.exists(): raise AssertionError(f"missing {p}")
    return p.read_text(encoding="utf-8",errors="replace")
features_h=text("bms_features.h");features_c=text("bms_features.c");board_h=text("bms_board.h");board_c=text("bms_board.c");guard_c=text("bms_afe_guard.c");afe_h=text("bms_afe.h");dvc=text("dvc1124_feature_backend.c");dvc_ll=text("dvc1124.c");dvc_bms=text("dvc1124_bms.c");dvc_service=text("dvc1124_config_service.c");project=text("dvc1124_project_config.h");config_h=text("bms_config_store.h");config_c=text("bms_config_store.c")
assert "#define BMS_HEATER_START_TEMP_X10 400u" in features_h
assert "#define BMS_HEATER_STOP_TEMP_X10 450u" in features_h
assert "bms_afe_get_charge_source_present" in afe_h and "bms_afe_get_charge_source_present" in guard_c
assert "charge_source_present" in features_c
assert "charge_session_active" in features_c
assert "g_stCellInfoReport.u16Ichg != 0u" in features_c
assert "g_stCellInfoReport.u16IDischg != 0u" in features_c
assert "BMS_HEATER_ARMING" in features_c and "BMS_HEATER_ACTIVE" in features_c
assert "openwire_fault_latched" in features_c and "openwire_suspected" in features_c
assert "gpio_read(CHG_IN_PIN)" not in board_c
assert "dvc1124_backend_get_charge_source_present" in dvc and "return 0u" in dvc
assert "DVC1124_BalanceService" in dvc and "DVC1124_OpenWireBegin" in dvc
assert "out->determinate = raw.valid ? 1u : 0u;" in dvc
assert "raw.cell_mv[i] == 0u" in dvc
assert "#define DVC_OPENWIRE_SETTLE_US            200000u" in dvc_ll
assert "if (!(cp & DVC1124_COW_MASK))" in dvc_ll
assert "bms_afe_set_balance_mask(desired)" in features_c
assert "bms_afe_openwire_start" in features_c and "bms_afe_openwire_poll" in features_c
assert "bms_features_charge_hard_blocked" in guard_c
assert "bms_features_charge_direction_blocked" in guard_c
fet_write=guard_c.index("return AFE_FETS(c, d);")
assert guard_c.index("bms_features_charge_hard_blocked") < fet_write
assert "bms_features_charge_direction_blocked()" in dvc_bms

# D008 final temperature ownership: GP1 heater MOS, GP2/GP3 battery, GP4 power MOS.
assert "#define DVC1124_DEFAULT_HEATER_NTC_GP        1u" in project
assert "#define DVC1124_DEFAULT_BATTERY_NTC_GP       2u" in project
assert "#define DVC1124_DEFAULT_BATTERY_NTC2_GP      3u" in project
assert "#define DVC1124_DEFAULT_MOS_NTC_GP           4u" in project
assert "DVC1124_DEFAULT_BATTERY_NTC2_GP" in dvc_bms
assert "*min_temp = (t1 <= t2) ? t1 : t2;" in dvc_bms
assert "*max_temp = (t1 >= t2) ? t1 : t2;" in dvc_bms
assert "DVC1124_DEFAULT_HEATER_NTC_GP" in dvc
assert "DVC1124_DEFAULT_MOS_NTC_GP" in dvc
assert "out->heater_temp_x10" in dvc and "out->mos_temp_x10" in dvc
assert "out->battery_temp_min_x10 = (bat1 <= bat2) ? bat1 : bat2;" in dvc
assert "out->battery_temp_max_x10 = (bat1 >= bat2) ? bat1 : bat2;" in dvc

# Heater fail-safe is independent from normal power-MOS OTP parameters.
assert "#define DVC1124_HEATER_OFF_FAULT_TEMP_X10    1350u" in project
assert "#define DVC1124_HEATER_OFF_FAULT_CONFIRM_MS  10000u" in project
assert "bms_board_heater_fuse_supported" in board_h
assert "bms_board_heater_fuse_fire" in board_h
assert "gpio_write(RF_EN_PIN, 1u);" in board_c
assert "heater_circuit_safe" in features_c
assert "bms_board_heater_fuse_fire();" in features_c
assert "heater_off_hot_samples" in features_c
heater_fn=features_c.split("static void service_heater",1)[1].split("static uint8_t openwire_hard_fault",1)[0]
assert "battery_temp_min_x10" in heater_fn
assert "BMS_HEATER_ARMING" in heater_fn
assert "g_stCellInfoReport.u16Ichg != 0u" in heater_fn
assert "set_heater(1u)" in heater_fn

# Balance owns independent business parameters; protection Vdelta must never be
# reused as a balance threshold.
assert "#define BMS_BALANCE_START_DELTA_MV_DEFAULT 50u" in features_h
assert "#define BMS_BALANCE_STOP_DELTA_MV_DEFAULT 30u" in features_h
for token in ("balance_enable", "balance_start_mv", "balance_start_delta_mv", "balance_stop_delta_mv"):
    assert token in config_h and token in config_c and token in features_c
assert "u16VdeltaOvp_First" not in features_c
assert "balance_voltage_trusted" in features_c
assert "balance_sample_plausible" in features_c
assert "BMS_BALANCE_CELL_MAX_STEP_MV" in features_c
assert "BMS_BALANCE_SUSPECT_DELTA_MV" in features_c
assert "s_feature.openwire_suspected" in features_c
assert "config.balance_start_mv" in features_c
assert "config.balance_start_delta_mv" in features_c
assert "config.balance_stop_delta_mv" in features_c

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
assert "if (s_guard.comm_inhibit || s_guard.bus_silenced || s_guard.test_shutdown_hold) return 1u;" in apply
sleep=guard_c.split("void bms_afe_sleep",1)[1].split("uint8_t bms_afe_apply_protection_config",1)[0]
assert sleep.index("if (s_guard.bus_silenced || s_guard.test_shutdown_hold) return;") < sleep.index("AFE_SLEEP();")
setfets=guard_c.split("uint8_t bms_afe_set_fets",1)[1].split("void bms_afe_get_requested_fets",1)[0]
assert "if (s_guard.comm_inhibit || s_guard.bus_silenced || s_guard.test_shutdown_hold) return 1u;" in setfets

print("common feature policy contract: PASS")
