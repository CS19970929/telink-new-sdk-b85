#!/usr/bin/env python3
"""D008/DVC1124 architecture, safety and configuration-ownership contracts."""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001" / "tc_ble_single_sdk" / "vendor" / "ble_sample"


def read(name):
    return (SRC / name).read_text(encoding="utf-8", errors="ignore")


def macro_literal(text, name):
    m = re.search(
        rf"^\s*#define\s+{re.escape(name)}\s+\(?\s*(0x[0-9A-Fa-f]+|[0-9]+)u?\s*\)?",
        text,
        re.MULTILINE,
    )
    if not m:
        raise AssertionError(f"literal macro not found: {name}")
    return int(m.group(1), 0)


class D008FrameworkContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.backend_h = read("bms_afe_backend.h")
        cls.afe_h = read("bms_afe.h")
        cls.guard = read("bms_afe_guard.c")
        cls.dvc = read("dvc1124.c")
        cls.dvc_bms = read("dvc1124_bms.c")
        cls.fixed_backend = read("dvc1124_config_store.c")
        cls.fixed_header = read("dvc1124_config_store.h")
        cls.service = read("dvc1124_config_service.c")
        cls.project = read("dvc1124_project_config.h")
        cls.product = read("d008_product_profile.h")
        cls.features = read("bms_features.c")
        cls.param = read("param.c")
        cls.app = read("app.c")
        cls.conf = read("conf.h")
        cls.hw_profile = read("bms_afe_hw_profile.c")

    def test_backend_defaults_to_dvc1124(self):
        self.assertIn("BMS_AFE_BACKEND_DVC1124", self.backend_h)
        self.assertRegex(self.backend_h, r"#define\s+BMS_AFE_BACKEND\s+BMS_AFE_BACKEND_DVC1124")
        self.assertIn("dvc1124_backend_sample", self.afe_h)

    def test_guard_comm_loss_uses_hardware_watchdog_silence(self):
        self.assertEqual(macro_literal(self.guard, "BMS_AFE_VALID_SNAPSHOT_RELEASE_COUNT"), 3)
        self.assertEqual(macro_literal(self.guard, "BMS_AFE_COMM_FAILS_BEFORE_SILENCE"), 2)
        self.assertEqual(macro_literal(self.guard, "BMS_AFE_FAILSAFE_WAIT_SAMPLES"), 25)
        self.assertIn("s_guard.comm_inhibit = 1u;", self.guard)
        self.assertIn("s_guard.bus_silenced = 1u;", self.guard)
        self.assertIn("if (service_failsafe_wait()) return;", self.guard)
        self.assertIn("if (s_guard.comm_failures == 0u) best_effort_shutdown();", self.guard)
        self.assertIn("AFE_INIT();", self.guard)
        self.assertNotIn("BMS_AFE_REINIT_TRIGGER", self.guard)
        self.assertNotIn("BMS_AFE_REINIT_COOLDOWN_SAMPLES", self.guard)
        self.assertIn("valid_snapshot_streak", self.guard)
        self.assertIn("bms_features_on_afe_invalid", self.guard)
        self.assertIn("bms_afe_bus_access_allowed", self.guard)

    def test_guard_owns_shutdown_wake_test_lifecycle(self):
        for token in (
            "test_shutdown_hold",
            "bms_afe_test_enter_shutdown",
            "bms_afe_test_wake",
            "if (s_guard.test_shutdown_hold) return;",
            "AFE_TEST_SHUTDOWN()",
            "AFE_INIT();",
            "s_guard.valid_snapshot_streak = 0u;",
        ):
            self.assertIn(token, self.guard)
        self.assertNotIn("DVC1124_AFE_WakeupFromShutdown", self.fixed_backend)
        self.assertNotIn("DVC1124_AFE_Shutdown", self.fixed_header)

    def test_guard_keeps_requested_state_separate_from_feedback(self):
        self.assertIn("requested_charge_on", self.guard)
        self.assertIn("requested_discharge_on", self.guard)
        self.assertIn("bms_afe_get_requested_fets", self.guard)
        set_fets = re.search(
            r"(?s)uint8_t\s+bms_afe_set_fets\s*\([^)]*\)\s*\{.*?\n\}",
            self.guard,
        )
        self.assertIsNotNone(set_fets)
        self.assertNotIn("b1Status_MOS_CHG =", set_fets.group(0))
        self.assertNotIn("b1Status_MOS_DSG =", set_fets.group(0))

    def test_mos_report_is_owned_by_dvc_feedback(self):
        self.assertRegex(
            self.dvc,
            r"b1Status_MOS_CHG\s*=\s*\(data\[DVC1124_REG_CC2_L_FLAGS\]\s*&\s*DVC1124_CC2_CHGF_MASK\)",
        )
        self.assertRegex(
            self.dvc,
            r"b1Status_MOS_DSG\s*=\s*\(data\[DVC1124_REG_CC2_L_FLAGS\]\s*&\s*DVC1124_CC2_DSGF_MASK\)",
        )
        self.assertNotRegex(self.app, r"b1Status_MOS_(?:CHG|DSG)\s*=")
        self.assertNotRegex(self.dvc_bms, r"b1Status_MOS_(?:CHG|DSG)\s*=")

    def test_common_port_policy_uses_auto_diode_without_off_pulse(self):
        policy = self.dvc_bms.split("static uint8_t dvc_apply_common_port_fet_state", 1)[1]
        policy = policy.split("void DVC1124_BmsApp_AFEGet", 1)[0]
        self.assertIn("DVC1124_FET_DRIVE_AUTO_DIODE", policy)
        self.assertIn("dvc_set_fet_modes_if_changed", policy)
        self.assertNotIn("DVC1124_SetMosState", policy)
        self.assertNotIn("DVC1124_WriteRegisterFieldSafe", policy)

    def test_normal_app_requests_both_common_port_fets(self):
        fn = self.app.split("void mos_update(void)", 1)[1]
        fn = fn.split("static void board_init", 1)[0]
        self.assertIn("bms_afe_set_fets(1u, 1u)", fn)
        self.assertNotIn("IsKeyWakeupActive", fn)
        self.assertNotIn("Runtime_GetMode()", fn)

    def test_app_has_no_legacy_mcu_adc_or_cert_temperature_path(self):
        self.assertNotIn("app_adc_multi_sample", self.app)
        self.assertNotIn("iSheldTemp_10K_mcu", self.app)
        self.assertNotIn("battery_check.h", self.app)
        self.assertNotIn("user_battery_power_check", self.app)
        self.assertNotIn("battery_clear_adc_setting_flag", self.app)
        self.assertNotIn("_UL_RENZHENG_ENABLE_", self.app)
        self.assertNotIn("_UL_RENZHENG_ENABLE_", self.conf)
        self.assertNotIn("BMS_FAULT_MOS_OTP_THIRD", self.app)
        self.assertIn("gpio_write(RF_EN_PIN, 0);", self.app)

    def test_dvc_is_single_temperature_owner_for_protection_and_reporting(self):
        sample = self.dvc_bms.split("void DVC1124_BmsApp_AFEGet", 1)[1]
        sample = sample.split("uint8_t bms_afe_set_fets", 1)[0]
        self.assertIn("dvc_get_battery_temperature_range", sample)
        self.assertIn("DVC1124_DEFAULT_MOS_NTC_GP", sample)
        self.assertIn("dvc_publish_temperature_report(&sw);", sample)
        self.assertIn("bms_sw_protection_update_groups(&sw, DVC1124_SW_PROTECT_ENABLE,", sample)
        self.assertIn("DVC1124_DEFAULT_BATTERY_NTC_GP       2u", self.project)
        self.assertIn("DVC1124_DEFAULT_BATTERY_NTC2_GP      3u", self.project)
        self.assertIn("DVC1124_DEFAULT_MOS_NTC_GP           4u", self.project)
        self.assertIn("u16Temperature[ENV_TEMP3]", self.dvc_bms)
        self.assertIn("u16Temperature[MOS_TEMP1]", self.dvc_bms)
        self.assertIn("u16TempMin = sw->battery_temp_min", self.dvc_bms)
        self.assertIn("u16TempMax = sw->battery_temp_max", self.dvc_bms)

    def test_protection_switches_default_to_production(self):
        self.assertEqual(macro_literal(self.project, "DVC1124_SW_PROTECT_ENABLE"), 1)
        self.assertEqual(macro_literal(self.project, "DVC1124_HW_PROTECT_ENABLE"), 1)
        self.assertIn("DVC1124 protection enable macros must be 0 or 1", self.project)

    def test_hw_off_really_disables_dvc_autonomous_protection(self):
        block = self.dvc.split("static uint8_t dvc_disable_threshold_protection", 1)[1]
        block = block.split("#endif", 1)[0]
        self.assertIn("DVC1124_SetShortCircuitProtection(0u, 0u)", block)
        self.assertIn("DVC1124_REG_BODY_DIODE, 0u", block)
        self.assertIn("DVC1124_I2C_WDT_OFF", block)
        self.assertIn("DVC1124_REG_DSG_MASK, 0xFFu", block)
        self.assertIn("DVC1124_REG_CHG_MASK, 0xFFu", block)

    def test_sw_off_clears_software_managed_fault_state(self):
        sample = self.dvc_bms.split("void DVC1124_BmsApp_AFEGet", 1)[1]
        sample = sample.split("uint8_t bms_afe_set_fets", 1)[0]
        self.assertIn("DVC1124_SW_TEMP_PROTECT_ENABLE", sample)
        self.assertIn("bms_sw_protection_update_groups(&sw, DVC1124_SW_PROTECT_ENABLE,", sample)
        self.assertNotIn("bms_sw_protection_clear();", sample)
        self.assertLess(sample.index("dvc_publish_temperature_report(&sw);"),
                        sample.index("bms_sw_protection_update_groups"))

    def test_fixed_dvc_operating_config_has_no_flash_owner(self):
        self.assertIn("DVC1124_FIXED_CONFIG_COMPILE_TIME", self.fixed_header)
        for token in (
            "flash_kv32",
            "ConfigStoreLoad",
            "ConfigStoreSave",
            "ConfigStoreRestore",
            "ConfigStoreCapture",
        ):
            self.assertNotIn(token, self.fixed_backend)
        self.assertIn("dvc_project_apply_compile_time_config", self.fixed_backend)

    def test_fixed_config_is_reapplied_after_afe_reset(self):
        self.assertIn("DVC1124_AFE_Reset();", self.fixed_backend)
        self.assertIn("DVC1124_UpdataAfeConfig();", self.fixed_backend)
        self.assertIn("s_project_config_pending", self.fixed_backend)
        self.assertIn("DVC1124_ApplyOperatingConfig(&cfg)", self.fixed_backend)

    def test_fixed_semantic_window_is_diagnostic_only(self):
        self.assertNotIn("dvc1124_config_store.h", self.service)
        self.assertNotIn("DVC1124_ConfigStore", self.service)
        self.assertIn("DVC1124_CFG_ERR_READ_ONLY", self.service)
        self.assertIn("bms_afe_bus_access_allowed", self.service)
        raw = self.service.split("DVC1124_ConfigServiceWriteRaw", 1)[1]
        self.assertIn("DVC1124_CFG_ERR_READ_ONLY", raw)
        self.assertNotIn("DVC1124_WriteRegisters", raw)

    def test_fail_safe_watchdog_policy_is_project_compile_time(self):
        self.assertEqual(macro_literal(self.project, "DVC1124_I2C_WATCHDOG_SECONDS"), 4)
        self.assertEqual(macro_literal(self.project, "DVC1124_I2C_TIMEOUT_CLOSE_CHG"), 1)
        self.assertEqual(macro_literal(self.project, "DVC1124_I2C_TIMEOUT_CLOSE_DSG"), 1)
        self.assertNotIn("DVC1124_I2C_WATCHDOG_SECONDS", self.product)
        self.assertIn("DVC1124_DSGMASK_DWM_MASK", self.fixed_backend)
        self.assertIn("DVC1124_CHGMASK_CWM_MASK", self.fixed_backend)

    def test_body_diode_policy_remains_common_port_compile_time(self):
        self.assertEqual(macro_literal(self.project, "DVC1124_BODY_DIODE_THRESHOLD_UV"), 80)
        self.assertIn("DVC1124_DSGMASK_DBDM_MASK", self.project)
        self.assertIn("DVC1124_CHGMASK_CBDM_MASK", self.project)
        self.assertIn("DVC1124_BODY_DIODE_THRESHOLD_UV", self.fixed_backend)

    def test_only_protection_profiles_remain_runtime_persistent_for_afe(self):
        self.assertIn("bms_afe_hw_profile_get(&hw)", self.service)
        self.assertIn("bms_afe_hw_profile_get", self.hw_profile)
        self.assertIn("bms_afe_apply_protection_config", self.fixed_backend)
        fn = self.fixed_backend.split("uint8_t bms_afe_apply_protection_config", 1)[1]
        self.assertIn("DVC1124_ApplyProtectionConfig()", fn)

    def test_product_profile_still_selects_physical_cell_count_and_soc_identity(self):
        self.assertEqual(macro_literal(self.product, "D008_PRODUCT_PROFILE_24S_LFP"), 1)
        self.assertEqual(macro_literal(self.product, "D008_PRODUCT_PROFILE_20S_NMC"), 2)
        self.assertIn("D008_PRODUCT_CELL_COUNT       24u", self.product)
        self.assertIn("D008_PRODUCT_CELL_COUNT       20u", self.product)
        self.assertIn("DVC1124_DEFAULT_CELL_COUNT           D008_PRODUCT_CELL_COUNT", self.project)

    def test_no_legacy_parameter_migration(self):
        self.assertNotIn("param_apply_d008_product_identity_if_unset", self.param)
        self.assertNotIn("param_migrate_temperature_protection_v1", self.param)
        self.assertIn("bms_config_store_apply_revisions()", self.param)

    def test_invalid_software_protection_params_block_outputs(self):
        self.assertIn("static uint8_t s_protection_params_valid", self.param)
        self.assertIn("uint8_t bms_protection_params_valid(void)", self.param)
        self.assertIn("s_protection_params_valid = 0u;", self.param)
        self.assertIn("s_protection_params_valid = 1u;", self.param)
        self.assertIn("!bms_protection_params_valid()", self.features)

    def test_startup_update_failure_has_separate_gate(self):
        self.assertIn("s_protection_params_valid && s_storage_upgrade_valid", self.param)
        self.assertIn("s_storage_upgrade_valid = 0u", self.param)
        self.assertNotIn("param_upgrade_mark_epoch", self.param)

    def test_openwire_and_balance_safety_gate_remain(self):
        self.assertIn("bms_afe_openwire_start", self.features)
        self.assertIn("bms_afe_set_balance_mask(0u)", self.features)
        self.assertIn("DVC1124_OpenWireBegin", self.dvc)
        self.assertIn("DVC1124_BalanceService", self.dvc)

    def test_source_order_still_contains_backend_lifecycle_unit(self):
        order = (ROOT / "bms_tools" / "source_order.txt").read_text(encoding="utf-8")
        self.assertIn("vendor/ble_sample/dvc1124_config_store.c", order)
        self.assertIn("vendor/ble_sample/dvc1124_config_service.c", order)


if __name__ == "__main__":
    unittest.main()
