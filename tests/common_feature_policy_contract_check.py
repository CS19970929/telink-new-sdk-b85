#!/usr/bin/env python3
from pathlib import Path
from project_paths import Sources, host_includes, selected_source

ROOT = Path(__file__).resolve().parents[1]
APP = Sources(ROOT)

def text(name: str) -> str:
    path = APP / name
    if not path.exists(): raise AssertionError(f"missing {path}")
    return path.read_text(encoding="utf-8", errors="replace")

features_h=text("bms_features.h"); features_c=text("bms_features.c"); board_h=text("bms_board.h"); board_c=text("bms_board.c"); guard_c=text("bms_afe_guard.c"); afe_h=text("bms_afe.h"); config_h=text("bms_config_store.h"); config_c=text("bms_config_store.c"); modbus=text("modbus_rtu.c")
assert "#define BMS_HEATER_START_TEMP_X10 400u" in features_h
assert "#define BMS_HEATER_STOP_TEMP_X10 450u" in features_h
assert "bms_afe_get_charge_source_present" in afe_h
assert "bms_afe_get_charge_source_present" in guard_c
assert "charge_source_present" in features_c
assert "BMS_HEATER_START_TEMP_X10" in config_c and "BMS_HEATER_STOP_TEMP_X10" in config_c
assert "#define BMS_BALANCE_START_DELTA_MV_DEFAULT 50u" in features_h
assert "#define BMS_BALANCE_STOP_DELTA_MV_DEFAULT 30u" in features_h
assert "openwire_fault_latched" in features_c and "openwire_suspected" in features_c
assert "balance_voltage_trusted" in features_c and "balance_sample_plausible" in features_c
assert "u16VdeltaOvp_First" not in features_c
assert "bms_board_heater_set" in board_h and "bms_board_charge_source_present" in board_h
assert "bms_board_heater_allowed" in board_h and "bms_board_balance_supported" in board_h
assert "bms_board_heater_allowed()" in features_c and "bms_board_balance_supported()" in features_c
assert "service_balance" in features_c and "apply_balance_mask(0u)" in features_c
assert "service_openwire" in features_c and "bms_afe_openwire_start" in features_c and "bms_afe_openwire_poll" in features_c
charge_block=guard_c.index("bms_features_outputs_blocked"); fet_write=guard_c.index("AFE_FETS",charge_block)
assert charge_block < fet_write
assert "bms_features_charge_direction_blocked()" in text("sh3673510_bms.c")
assert "bms_features_service();" in guard_c and "bms_features_on_afe_invalid();" in guard_c
for symbol in ("bms_afe_get_feature_snapshot","bms_afe_get_charge_source_present","bms_afe_set_balance_mask","bms_afe_get_balance_mask","bms_afe_openwire_start","bms_afe_openwire_poll"): assert symbol in afe_h

# Communication-loss fail-safe contract. SH36735xx WDT is ~32 s in the
# production profile, so the common guard must leave the SPI bus silent for a
# full 35 s margin before a single bounded recovery attempt.
assert "#define BMS_AFE_COMM_FAILS_BEFORE_SILENCE    2u" in guard_c
assert "#define BMS_AFE_FAILSAFE_WAIT_SAMPLES 175u" in guard_c
assert "if (s_guard.test_shutdown_hold || service_failsafe_wait()) return;" in guard_c
# 校验静默等待分支在任何总线动作之前返回，不依赖注释语言。
wait = guard_c.split("static uint8_t service_failsafe_wait", 1)[1].split(
    "static uint8_t apply_requested", 1)[0]
waiting = wait.split("if (s_guard.failsafe_wait_samples != 0u)", 1)[1].split(
    "s_guard.bus_silenced = 0u;", 1)[0]
assert "--s_guard.failsafe_wait_samples;" in waiting and "return 1u;" in waiting
assert "AFE_" not in waiting
assert wait.index("return 1u;") < wait.index("AFE_INIT();")
assert "if (s_guard.comm_failures == 0u) best_effort_shutdown();" in guard_c
assert "s_guard.bus_silenced = 1u;" in guard_c
assert "bms_afe_bus_access_allowed" in afe_h and "bms_afe_bus_access_allowed" in guard_c
apply=guard_c.split("static uint8_t apply_requested",1)[1].split("static void note_invalid",1)[0]
assert "if (s_guard.comm_inhibit || s_guard.bus_silenced || s_guard.test_shutdown_hold) return 1u;" in apply
sleep=guard_c.split("uint8_t bms_afe_sleep",1)[1].split("#else",1)[0]
assert sleep.index("if (s_guard.bus_silenced || s_guard.test_shutdown_hold) return 0u;") < sleep.index("AFE_SLEEP()")
setfets=guard_c.split("uint8_t bms_afe_set_fets",1)[1].split("void bms_afe_get_requested_fets",1)[0]
assert "if (s_guard.comm_inhibit || s_guard.bus_silenced || s_guard.test_shutdown_hold) return 1u;" in setfets

sh_bms=text("sh3673510_bms.c")
for forbidden in (
    "static void apply_heater",
    "static void apply_balance",
    "SH3510_REINIT_TRIGGER",
    "SH3510_REINIT_COOLDOWN",
):
    assert forbidden not in sh_bms, forbidden

sample = sh_bms.split("void sh3673510_bms_afe_sample(void)", 1)[1].split(
    "uint8_t sh3673510_bms_afe_apply_protection_config", 1
)[0]
for forbidden in (
    "apply_heater();",
    "apply_balance();",
    "sh3510_apply_requested_fets();",
    "sh3673510_control_init()",
):
    assert forbidden not in sample, forbidden

assert "comm_fault_latched" in guard_c
assert "else if (s_guard.comm_fault_latched)" in guard_c

sh=text("sh3673510_feature_backend.c")
for token in (
    "SH3673520_SCONF3_OWD_EN_MASK","SH3673520_SCONF3_OWD_TRG_MASK",
    "SH3673520_REG_FLAG3","SH3673520_REG_OWDH","0x000AAAAA","0x00055555",
    "SH_FEATURE_BALANCE_REFRESH_SAMPLES 100u","sh_read_balance_mask"
): assert token in sh, token
for token in ("SH3673520_REG_VCHGRH", "SH3510_CHARGER_ON_MV", "SH3510_CHARGER_OFF_MV",
              "sample_release_evidence", "sh3673510_backend_get_charge_source_present"):
    assert token in sh_bms, token
assert "sh3673510_backend_get_charge_source_present" not in sh

for token in ("balance_enable","balance_start_mv","balance_start_delta_mv","balance_stop_delta_mv"):
    assert token in config_h and token in config_c and token in features_c
assert "#define BMS_CONFIG_SCHEMA_VERSION        3u" in config_c
assert "#define BMS_CONFIG_USER_BYTES            54u" in config_c
parameters=text("bms_parameter_access.c")
assert "0x2E20u" in parameters and "0x2E70u" in parameters
assert "bms_parameter_write" in modbus

sh_bms=text("sh3673510_bms.c")
assert "SH3673520_BSTATUS2_DSGING_MASK" in sh_bms
assert "SH3673520_BSTATUS2_CHGING_MASK" in sh_bms
assert "s_fet_command_valid" in sh_bms
# 相同有效 FET 命令必须在再次写硬件之前返回，避免干扰自主保护。
fets = sh_bms.split("static uint8_t sh3510_apply_requested_fets", 1)[1].split(
    "static void publish_hw_status", 1)[0]
cached = fets.split("if (s_fet_command_valid &&", 1)[1].split(
    "if (!sh3673510_control_set_fets", 1)[0]
assert "s_last_charge_command == charge_on" in cached
assert "s_last_discharge_command == discharge_on" in cached
assert "return 1u;" in cached
assert fets.index("if (s_fet_command_valid &&") < fets.index(
    "sh3673510_control_set_fets(charge_on, discharge_on)")

print("common feature policy contract: PASS")
