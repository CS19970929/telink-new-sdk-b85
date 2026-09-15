#!/usr/bin/env python3
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001" / "tc_ble_single_sdk" / "vendor" / "ble_sample"

def read(name):
    return (SRC / name).read_text(encoding="utf-8", errors="strict")

class D008CommonPortFetContract(unittest.TestCase):
    def test_normal_operation_requests_both_fets(self):
        app = read("app.c")
        m = re.search(r"void mos_update\(void\)\n\{(.*?)\n\}\n", app, re.S)
        self.assertIsNotNone(m)
        body = m.group(1)
        charger = re.search(r"if\(IsChargerWakeupActive\(\)\)(.*?)(?:else if)", body, re.S).group(1)
        key = re.search(r"else if \(IsKeyWakeupActive\(\)\)(.*?)(?:else\n)", body, re.S).group(1)
        for branch in (charger, key):
            self.assertIn("chg_target = 1;", branch)
            self.assertIn("dsg_target = 1;", branch)
        self.assertNotIn("Runtime_GetMode()", body)

    def test_dvc_body_diode_recovery_is_compile_time_policy(self):
        cfg = read("dvc1124_project_config.h")
        backend = read("dvc1124_config_store.c")
        self.assertRegex(cfg, r"#define\s+DVC1124_BODY_DIODE_THRESHOLD_UV\s+80u")
        self.assertIn("DVC1124_DSGMASK_DBDM_MASK", cfg)
        self.assertIn("DVC1124_CHGMASK_CBDM_MASK", cfg)
        self.assertIn("DVC1124_BODY_DIODE_THRESHOLD_UV", backend)
        self.assertIn("dvc_project_encode_body_diode", backend)

    def test_body_diode_policy_has_no_operating_config_flash_owner(self):
        backend = read("dvc1124_config_store.c")
        header = read("dvc1124_config_store.h")
        self.assertNotIn("body_diode_threshold_uv", header)
        self.assertNotIn("ConfigStoreLoad", backend)
        self.assertNotIn("ConfigStoreSave", backend)
        self.assertNotIn("flash_kv32", backend)
        self.assertIn("DVC1124_FIXED_CONFIG_COMPILE_TIME", header)

    def test_one_sided_protection_uses_auto_diode_without_hard_off_transition(self):
        bms = read("dvc1124_bms.c")
        policy = bms.split("static uint8_t dvc_apply_common_port_fet_state", 1)[1]
        policy = policy.split("void DVC1124_BmsApp_AFEGet", 1)[0]
        self.assertIn("charge_blocked && !discharge_blocked", policy)
        self.assertIn("discharge_blocked && !charge_blocked", policy)
        self.assertGreaterEqual(policy.count("DVC1124_FET_DRIVE_AUTO_DIODE"), 2)
        self.assertIn("dvc_set_fet_modes_if_changed(charge_mode, discharge_mode)", policy)
        self.assertNotIn("DVC1124_SetMosState", policy)
        self.assertNotIn("DVC1124_WriteRegisterFieldSafe", policy)
        self.assertNotIn("effective_charge", policy)
        self.assertNotIn("effective_discharge", policy)

    def test_steady_auto_diode_mode_does_not_rewrite_r81_every_200ms(self):
        bms = read("dvc1124_bms.c")
        helper = bms.split("static uint8_t dvc_set_fet_modes_if_changed", 1)[1]
        helper = helper.split("static uint8_t dvc_apply_common_port_fet_state", 1)[0]
        self.assertIn("DVC1124_ReadRegisters(DVC1124_REG_FET_CTRL", helper)
        self.assertIn("DVC1124_FET_CHGC_MASK", helper)
        self.assertIn("DVC1124_FET_DSGC_MASK", helper)
        same_mode_return = re.search(
            r"if\s*\(.*?charge_mode.*?discharge_mode.*?\)\s*\{\s*return\s+1u;",
            helper,
            re.S,
        )
        self.assertIsNotNone(same_mode_return)
        self.assertEqual(helper.count("DVC1124_WriteRegisterSafe(DVC1124_REG_FET_CTRL"), 1)

    def test_single_r81_write_encodes_final_chg_and_dsg_modes(self):
        bms = read("dvc1124_bms.c")
        helper = bms.split("static uint8_t dvc_set_fet_modes_if_changed", 1)[1]
        helper = helper.split("static uint8_t dvc_apply_common_port_fet_state", 1)[0]
        self.assertIn("DVC1124_FIELD_PREP(DVC1124_FET_CHGC_MASK", helper)
        self.assertIn("DVC1124_FIELD_PREP(DVC1124_FET_DSGC_MASK", helper)
        self.assertNotIn("DVC1124_WriteRegisterFieldSafe", helper)

    def test_guard_hard_off_paths_are_preserved(self):
        guard = read("bms_afe_guard.c")
        self.assertIn("static void inhibit(void)", guard)
        self.assertGreaterEqual(guard.count("(void)AFE_FETS(0u, 0u);"), 4)
        self.assertIn("if (!s_guard.output_enabled)", guard)
        self.assertIn("bms_afe_openwire_start", guard)

if __name__ == "__main__":
    unittest.main()
