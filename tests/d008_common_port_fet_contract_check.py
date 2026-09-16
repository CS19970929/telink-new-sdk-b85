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
        self.assertIn("bms_afe_set_fets(1u, 1u)", body)
        self.assertNotIn("IsChargerWakeupActive", body)
        self.assertNotIn("IsKeyWakeupActive", body)
        self.assertNotIn("b1Status_MOS_CHG", body)
        self.assertNotIn("b1Status_MOS_DSG", body)
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

    def test_controlled_off_is_preserved_but_comm_loss_uses_wdt(self):
        guard = read("bms_afe_guard.c")
        bms = read("dvc1124_bms.c")

        # When communication is healthy, an explicit 0/0 request still maps to
        # the true DVC OFF/OFF command; open-wire can also request controlled off.
        policy = bms.split("static uint8_t dvc_apply_common_port_fet_state", 1)[1]
        policy = policy.split("void DVC1124_BmsApp_AFEGet", 1)[0]
        self.assertIn("dvc1124_fet_drive_t charge_mode = DVC1124_FET_DRIVE_OFF", policy)
        self.assertIn("dvc1124_fet_drive_t discharge_mode = DVC1124_FET_DRIVE_OFF", policy)
        self.assertIn("bms_afe_openwire_start", guard)
        self.assertIn("if (!AFE_FETS(0u, 0u)) return 0u;", guard)

        # Communication loss is different: make only one best-effort off
        # attempt, then keep the bus silent so the DVC hardware WDT can fire.
        self.assertIn("static void best_effort_shutdown(void)", guard)
        self.assertIn("if (s_guard.comm_failures == 0u) best_effort_shutdown();", guard)
        self.assertIn("s_guard.bus_silenced = 1u;", guard)
        self.assertIn("if (service_failsafe_wait()) return;", guard)
        self.assertIn("if (s_guard.comm_inhibit || s_guard.bus_silenced || s_guard.test_shutdown_hold) return 1u;", guard)
        self.assertNotIn("BMS_AFE_REINIT_TRIGGER", guard)

if __name__ == "__main__":
    unittest.main()
