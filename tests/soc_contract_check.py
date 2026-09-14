#!/usr/bin/env python3
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
MOD = ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001" / "tc_ble_single_sdk" / "vendor" / "ble_sample"
C = (MOD / "SocEnhance.c").read_text(encoding="utf-8", errors="ignore")
H = (MOD / "SocEnhance.h").read_text(encoding="utf-8", errors="ignore")
KV_C = (MOD / "soc_kv_store.c").read_text(encoding="utf-8", errors="ignore")
KV_H = (MOD / "soc_kv_store.h").read_text(encoding="utf-8", errors="ignore")

class SocContract(unittest.TestCase):
    def test_dual_chemistry_profiles_exist(self):
        self.assertIn("BMS_SOC_CHEMISTRY_LFP", H)
        self.assertIn("BMS_SOC_CHEMISTRY_NMC", H)
        self.assertIn("g_soc_ocv_lfp", C)
        self.assertIn("g_soc_ocv_nmc", C)
        self.assertIn("SOC_AUTO_LFP_OVP_MAX_MV              3900u", C)

    def test_coulomb_integration_and_deadband(self):
        self.assertIn("SOC_INTEGRAL_PERIOD_MS              200u", C)
        self.assertIn("SOC_CURRENT_DEADBAND_MA_DEFAULT     200u", C)
        self.assertIn("soc_current_direction", C)
        self.assertIn("g_soc_integral_ms_remainder", C)

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
        self.assertIn("150u, 100u, 50u, 20u", C)
        self.assertIn("300u, 200u, 150u, 50u", C)

    def test_soc_low_faults_are_implemented_without_mos_policy(self):
        self.assertIn("soc_update_low_faults", C)
        self.assertIn("fault->bits.b1SocLow", C)
        self.assertIn("u16SocUp_First", C)
        self.assertNotIn("b1SocLow ||", C)

    def test_learning_is_present_but_default_disabled(self):
        self.assertIn("BMS_SOC_CAPACITY_LEARNING_ENABLE_DEFAULT 0u", C)
        self.assertIn("BMS_SOC_LEARNING_EMPTY_TO_FULL", H)
        self.assertIn("BMS_SOC_LEARNING_FULL_TO_EMPTY", H)
        self.assertIn("soc_learning_on_full_anchor", C)
        self.assertIn("soc_learning_on_empty_anchor", C)
        self.assertIn("SOC_KV_FLAG_CAPACITY_LEARNED", KV_H)
        self.assertIn("SOC_KV_KEY_LEARNED_CAPACITY", KV_C)
        self.assertIn("soc_kv_store_write_learning", KV_C)

    def test_diag_api_exists(self):
        self.assertIn("bms_soc_diag_t", H)
        self.assertIn("void bms_soc_get_diag", C)

if __name__ == "__main__":
    unittest.main(verbosity=2)
