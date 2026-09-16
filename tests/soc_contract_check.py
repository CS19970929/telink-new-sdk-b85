#!/usr/bin/env python3
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
MOD = ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001" / "tc_ble_single_sdk" / "vendor" / "ble_sample"
C = (MOD / "SocEnhance.c").read_text(encoding="utf-8", errors="ignore")
H = (MOD / "SocEnhance.h").read_text(encoding="utf-8", errors="ignore")
PROFILE = (MOD / "bms_soc_profile.h").read_text(encoding="utf-8", errors="ignore")
DEFS = (MOD / "bms_soc_defs.h").read_text(encoding="utf-8", errors="ignore")
CONFIG_C = (MOD / "bms_config_store.c").read_text(encoding="utf-8", errors="ignore")
CONFIG_H = (MOD / "bms_config_store.h").read_text(encoding="utf-8", errors="ignore")
STATE_C = (MOD / "bms_state_store.c").read_text(encoding="utf-8", errors="ignore")
STATE_H = (MOD / "bms_state_store.h").read_text(encoding="utf-8", errors="ignore")

class SocContract(unittest.TestCase):
    def test_dual_chemistry_profiles_are_data_not_algorithm(self):
        self.assertIn("BMS_SOC_CHEMISTRY_LFP", DEFS)
        self.assertIn("BMS_SOC_CHEMISTRY_NMC", DEFS)
        self.assertIn("g_soc_ocv_lfp", PROFILE)
        self.assertIn("g_soc_ocv_nmc", PROFILE)
        self.assertIn("BMS_SOC_PROFILE_GENERIC_LFP_VERSION", PROFILE)
        self.assertIn("BMS_SOC_PROFILE_GENERIC_NMC_VERSION", PROFILE)
        self.assertNotIn("static const soc_ocv_point_t g_soc_ocv_lfp", C)
        self.assertNotIn("static const soc_ocv_point_t g_soc_ocv_nmc", C)
        self.assertIn("SOC_AUTO_LFP_OVP_MAX_MV              3900u", C)

    def test_product_chemistry_and_profile_are_config_fields(self):
        self.assertIn("BMS_SYS_PARAM_BATTERY_CHEMISTRY", CONFIG_H)
        self.assertIn("BMS_SYS_PARAM_SOC_PROFILE_ID", CONFIG_H)
        self.assertIn("BMS_CONFIG_SYSTEM_WORDS          10u", CONFIG_C)
        self.assertIn("bms_config_put_u32le", CONFIG_C)
        self.assertIn("system->battery_chemistry = D008_PRODUCT_CHEMISTRY", CONFIG_C)
        self.assertIn("system->soc_profile_id = D008_PRODUCT_SOC_PROFILE_ID", CONFIG_C)
        self.assertIn("bms_config_store_get_system", CONFIG_C)
        self.assertIn("bms_soc_set_product_config", C)
        self.assertIn("soc_load_persisted_product_config", C)
        self.assertNotIn("BMS_COLD_SYSTEM_KEY_BASE", CONFIG_C)

    def test_explicit_profile_wins_and_mismatches_are_rejected(self):
        self.assertIn("soc_profile_from_id(g_soc_config.profile_id)", C)
        self.assertIn("profile_id == BMS_SOC_PROFILE_GENERIC_NMC", C)
        self.assertIn("profile_id == BMS_SOC_PROFILE_GENERIC_LFP", C)
        self.assertIn("soc_product_config_valid", C)
        self.assertIn("assembly identity; no old Flash migration", C)
        self.assertIn("bms_config_store_set_soc(config)", C)

    def test_coulomb_integration_and_deadband(self):
        self.assertIn("SOC_INTEGRAL_PERIOD_MS              200u", C)
        self.assertIn("SOC_CURRENT_DEADBAND_MA_DEFAULT     200u", C)
        self.assertIn("soc_current_direction", C)
        self.assertIn("g_soc_integral_tick_remainder", C)

    def test_ocv_requires_ten_minutes_and_uses_band(self):
        self.assertIn("SOC_OCV_REST_PREPARE_SECONDS        600u", C)
        self.assertIn("SOC_OCV_ERROR_BAND_PERCENT          5u", C)
        self.assertIn("g_soc_runtime.ocv_low", C)
        self.assertIn("g_soc_runtime.ocv_high", C)
        self.assertIn("return soc_step_down_to(g_soc_runtime.ocv_high);", C)
        ocv_fn = C[C.index("static uint8_t soc_idle_ocv_tracking"):C.index("static uint16_t soc_discharge_natural_1pct_ticks")]
        self.assertNotIn("soc_step_up_to", ocv_fn)

    def test_display_soc_is_separate(self):
        self.assertIn("static uint8_t g_soc_display_soc", C)
        self.assertIn("SOC_DISPLAY_STEP_TICKS              SOC_TICKS_PER_SECOND", C)
        self.assertIn("g_stCellInfoReport.SocElement.u16Soc = get_soc_display();", C)

    def test_endpoints_and_lfp_terminal_knee_are_chemistry_specific(self):
        self.assertIn("g_stCellInfoReport.unMdlFault_Third.bits.b1CellOvp", C)
        self.assertIn("g_stCellInfoReport.unMdlFault_Third.bits.b1CellUvp", C)
        self.assertIn("150u, 100u, 50u, 20u", PROFILE)
        self.assertIn("300u, 200u, 150u, 50u", PROFILE)

    def test_upward_calibration_requires_confirmed_charging_full_anchor(self):
        start = C.index("static uint8_t soc_apply_full_anchor(void)")
        end = C.index("static uint8_t soc_apply_forced_empty_anchor(void)", start)
        full_fn = C[start:end]
        self.assertIn("(VCELLMAX >= full_mv) && (VCELLMIN >= full_min) && isCHG()", full_fn)
        self.assertIn("if (isCHG() && g_stCellInfoReport.unMdlFault_Third.bits.b1CellOvp)", full_fn)
        self.assertNotIn("&& !isDSG()", full_fn)

    def test_soc_low_faults_are_implemented_without_mos_policy(self):
        self.assertIn("soc_update_low_faults", C)
        self.assertIn("fault->bits.b1SocLow", C)
        self.assertIn("u16SocUp_First", C)
        self.assertNotIn("b1SocLow ||", C)

    def test_learning_is_state_data_and_default_disabled(self):
        self.assertIn("BMS_SOC_CAPACITY_LEARNING_ENABLE_DEFAULT 0u", C)
        self.assertIn("BMS_SOC_LEARNING_EMPTY_TO_FULL", H)
        self.assertIn("BMS_SOC_LEARNING_FULL_TO_EMPTY", H)
        self.assertIn("soc_learning_on_full_anchor", C)
        self.assertIn("soc_learning_on_empty_anchor", C)
        self.assertIn("BMS_STATE_FLAG_CAPACITY_LEARNED", STATE_H)
        self.assertIn("learned_capacity_0p1ah", STATE_C)
        self.assertIn("bms_state_store_write_learning", STATE_C)
        self.assertIn("storage_record_save", STATE_C)
        self.assertNotIn("SOC_KV_KEY_LEARNED_CAPACITY", STATE_C)

    def test_diag_reports_profile_identity_and_version(self):
        self.assertIn("bms_soc_diag_t", H)
        self.assertIn("profile_id", H)
        self.assertIn("profile_version", H)
        self.assertIn("diag->profile_id = g_soc_profile->profile_id", C)
        self.assertIn("diag->profile_version = g_soc_profile->profile_version", C)

if __name__ == "__main__":
    unittest.main(verbosity=2)
