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

    def test_dvc_body_diode_recovery_is_enabled(self):
        cfg = read("dvc1124_project_config.h")
        self.assertRegex(cfg, r"#define\s+DVC1124_BODY_DIODE_THRESHOLD_UV\s+80u")
        self.assertIn("DVC1124_DSGMASK_DBDM_MASK", cfg)
        self.assertIn("DVC1124_CHGMASK_CBDM_MASK", cfg)

    def test_persisted_zero_bdpt_is_migrated_and_rejected(self):
        store = read("dvc1124_config_store.c")
        self.assertIn("cfg->body_diode_threshold_uv = DVC1124_BODY_DIODE_THRESHOLD_UV;", store)
        self.assertIn("if ((cfg->body_diode_threshold_uv < 40u) ||", store)
        self.assertNotIn("if ((cfg->body_diode_threshold_uv != 0u) &&", store)

    def test_one_sided_protection_uses_auto_diode(self):
        bms = read("dvc1124_bms.c")
        self.assertIn("dvc_apply_common_port_fet_state", bms)
        self.assertIn("charge_blocked && !discharge_blocked", bms)
        self.assertIn("discharge_blocked && !charge_blocked", bms)
        self.assertGreaterEqual(bms.count("DVC1124_FET_DRIVE_AUTO_DIODE"), 2)
        self.assertNotIn("dvc_enforce_fault_fet_state", bms)

    def test_guard_hard_off_paths_are_preserved(self):
        guard = read("bms_afe_guard.c")
        self.assertIn("static void inhibit(void)", guard)
        self.assertGreaterEqual(guard.count("(void)AFE_FETS(0u, 0u);"), 4)
        self.assertIn("if (!s_guard.output_enabled)", guard)
        self.assertIn("bms_afe_openwire_start", guard)

if __name__ == "__main__":
    unittest.main()
