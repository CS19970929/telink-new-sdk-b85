#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001" / "tc_ble_single_sdk" / "vendor" / "ble_sample"

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
assert "BMS_HEATER_START_TEMP_X10" in features_c and "BMS_HEATER_STOP_TEMP_X10" in features_c
assert "#define BMS_BALANCE_START_DELTA_MV_DEFAULT 50u" in features_h
assert "#define BMS_BALANCE_STOP_DELTA_MV_DEFAULT 30u" in features_h
assert "openwire_fault_latched" in features_c and "openwire_suspected" in features_c
assert "balance_voltage_trusted" in features_c and "balance_sample_plausible" in features_c
assert "u16VdeltaOvp_First" not in features_c
assert "bms_board_heater_set" in board_h and "bms_board_charge_source_present" in board_h
assert "bms_board_heater_allowed" in board_h and "bms_board_balance_supported" in board_h
assert "bms_board_heater_allowed()" in features_c and "bms_board_balance_supported()" in features_c
assert "service_balance" in features_c and "bms_afe_set_balance_mask(0u)" in features_c
assert "service_openwire" in features_c and "bms_afe_openwire_start" in features_c and "bms_afe_openwire_poll" in features_c
charge_block=guard_c.index("bms_features_charge_hard_blocked"); fet_write=guard_c.index("AFE_FETS",charge_block)
assert charge_block < fet_write
assert "bms_features_charge_direction_blocked()" in text("sh3673510_bms.c")
assert "bms_features_service();" in guard_c and "bms_features_on_afe_invalid();" in guard_c
for symbol in ("bms_afe_get_feature_snapshot","bms_afe_get_charge_source_present","bms_afe_set_balance_mask","bms_afe_get_balance_mask","bms_afe_openwire_start","bms_afe_openwire_poll"): assert symbol in afe_h

# Communication-loss fail-safe contract. SH36735xx WDT is ~32 s in the
# production profile, so the common guard must leave the SPI bus silent for a
# full 35 s margin before a single bounded recovery attempt.
assert "#define BMS_AFE_COMM_FAILS_BEFORE_SILENCE    2u" in guard_c
assert "#define BMS_AFE_FAILSAFE_WAIT_SAMPLES 175u" in guard_c
assert "if (service_failsafe_wait()) return;" in guard_c
assert "Absolutely no AFE I2C/SPI access while the hardware watchdog is timing." in guard_c
assert "if (s_guard.comm_failures == 0u) best_effort_shutdown();" in guard_c
assert "s_guard.bus_silenced = 1u;" in guard_c
assert "bms_afe_bus_access_allowed" in afe_h and "bms_afe_bus_access_allowed" in guard_c
apply=guard_c.split("static uint8_t apply_requested",1)[1].split("static void note_invalid",1)[0]
assert "if (s_guard.comm_inhibit || s_guard.bus_silenced) return 1u;" in apply
sleep=guard_c.split("void bms_afe_sleep",1)[1].split("uint8_t bms_afe_apply_protection_config",1)[0]
assert sleep.index("if (s_guard.bus_silenced) return;") < sleep.index("AFE_SLEEP();")
setfets=guard_c.split("uint8_t bms_afe_set_fets",1)[1].split("void bms_afe_get_requested_fets",1)[0]
assert "if (s_guard.comm_inhibit || s_guard.bus_silenced) return 1u;" in setfets

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
    "SH3673520_REG_VCHGRH","SH_CHARGER_PRESENT_ON_MV","SH_CHARGER_PRESENT_OFF_MV",
    "SH3673520_REG_BSTATUS1","SH3673520_BSTATUS2_CHGING_MASK",
    "SH3673520_SCONF3_OWD_EN_MASK","SH3673520_SCONF3_OWD_TRG_MASK",
    "SH3673520_REG_FLAG3","SH3673520_REG_OWDH","0x000AAAAA","0x00055555",
    "SH_FEATURE_BALANCE_REFRESH_SAMPLES 100u","sh_read_balance_mask"
): assert token in sh, token

for token in ("balance_enable","balance_start_mv","balance_start_delta_mv","balance_stop_delta_mv"):
    assert token in config_h and token in config_c and token in features_c
assert "#define BMS_CONFIG_SCHEMA_VERSION        2u" in config_c
assert "#define BMS_CONFIG_FEATURE_BYTES         14u" in config_c
assert "0x2E20u" in modbus and "0x2E70u" in modbus
assert "feature_config_write_block" in modbus

sh_bms=text("sh3673510_bms.c")
assert "SH3673520_BSTATUS2_DSGING_MASK" in sh_bms
assert "SH3673520_BSTATUS2_CHGING_MASK" in sh_bms
assert "s_fet_command_valid" in sh_bms
assert "repeated software writes would fight that behavior" in sh_bms

print("common feature policy contract: PASS")
