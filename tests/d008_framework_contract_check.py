#!/usr/bin/env python3
"""D008/DVC1124 architecture, product-profile and safety-guard contracts."""

import re
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
HERE = (
    REPO_ROOT
    / "tc_ble_single_sdk-V3.4.2.8_Patch_0001"
    / "tc_ble_single_sdk"
    / "vendor"
    / "ble_sample"
)


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="ignore")


def macro_literal(text: str, name: str) -> int:
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
        cls.backend = read(HERE / "bms_afe_backend.h")
        cls.api = read(HERE / "bms_afe.h")
        cls.guard = read(HERE / "bms_afe_guard.c")
        cls.cfg = read(HERE / "dvc1124_project_config.h")
        cls.profile = read(HERE / "d008_product_profile.h")
        cls.param = read(HERE / "param.c")
        cls.dvc_bms = read(HERE / "dvc1124_bms.c")

    def test_compile_time_backend_defaults_to_dvc1124(self):
        self.assertIn("BMS_AFE_BACKEND_DVC1124", self.backend)
        self.assertRegex(
            self.backend,
            r"#define\s+BMS_AFE_BACKEND\s+BMS_AFE_BACKEND_DVC1124",
        )

    def test_dvc_sources_are_rebound_behind_common_guard(self):
        self.assertIn("defined(DVC1124_H_)", self.api)
        self.assertIn("dvc1124_backend_sample", self.api)
        self.assertIn("void bms_afe_sample(void);", self.api)
        self.assertIn("vendor/ble_sample/bms_afe_guard.c", read(REPO_ROOT / "bms_tools" / "source_order.txt"))

    def test_comm_failure_forces_safe_off_and_requires_three_good_snapshots(self):
        self.assertEqual(
            macro_literal(self.guard, "BMS_AFE_VALID_SNAPSHOT_RELEASE_COUNT"), 3
        )
        self.assertIn("s_guard.comm_inhibit = 1u;", self.guard)
        self.assertIn("AFE_BACKEND_SET_FETS(0u, 0u)", self.guard)
        self.assertIn("valid_snapshot_streak", self.guard)
        self.assertIn("s_guard.comm_inhibit = 0u;", self.guard)

    def test_repeated_invalid_snapshots_trigger_bounded_reinit_not_release(self):
        self.assertEqual(macro_literal(self.guard, "BMS_AFE_REINIT_TRIGGER"), 3)
        self.assertEqual(
            macro_literal(self.guard, "BMS_AFE_REINIT_COOLDOWN_SAMPLES"), 25
        )
        self.assertIn("s_guard.comm_failures", self.guard)
        self.assertIn("s_guard.reinit_cooldown", self.guard)
        self.assertIn("AFE_BACKEND_INIT();", self.guard)
        self.assertIn("fresh post-reinit measurement", self.guard)
        recovery = self.guard.split("static void bms_afe_guard_note_invalid_snapshot", 1)[1]
        recovery = recovery.split("void bms_afe_init", 1)[0]
        self.assertNotIn("s_guard.comm_inhibit = 0u", recovery)

    def test_sleep_and_protection_apply_cannot_bypass_inhibit(self):
        self.assertRegex(
            self.guard,
            r"(?s)void\s+bms_afe_sleep\s*\([^)]*\).*?bms_afe_guard_inhibit\(\);",
        )
        self.assertIn("if (!ok)", self.guard)
        self.assertIn("bms_afe_guard_note_invalid_snapshot();", self.guard)

    def test_d008_defaults_match_reviewed_low_side_policy(self):
        self.assertEqual(macro_literal(self.cfg, "DVC1124_DEFAULT_HIGH_SIDE_FET_MASK"), 1)
        self.assertEqual(macro_literal(self.cfg, "DVC1124_DEFAULT_CURRENT_WAKE_ENGINE_ENABLE"), 0)
        self.assertEqual(macro_literal(self.cfg, "DVC1124_DEFAULT_INTERRUPT_MASK"), 0xFF)

    def test_physical_product_profile_defaults_to_24s_lfp_and_supports_20s_nmc(self):
        self.assertEqual(macro_literal(self.profile, "D008_PRODUCT_PROFILE_24S_LFP"), 1)
        self.assertEqual(macro_literal(self.profile, "D008_PRODUCT_PROFILE_20S_NMC"), 2)
        self.assertRegex(
            self.profile,
            r"#define\s+D008_PRODUCT_PROFILE\s+D008_PRODUCT_PROFILE_24S_LFP",
        )
        self.assertIn("#define D008_PRODUCT_CELL_COUNT       24u", self.profile)
        self.assertIn("#define D008_PRODUCT_CHEMISTRY        BMS_SOC_CHEMISTRY_LFP", self.profile)
        self.assertIn("#define D008_PRODUCT_SOC_PROFILE_ID   BMS_SOC_PROFILE_GENERIC_LFP", self.profile)
        self.assertIn("#define D008_PRODUCT_CELL_COUNT       20u", self.profile)
        self.assertIn("#define D008_PRODUCT_CHEMISTRY        BMS_SOC_CHEMISTRY_NMC", self.profile)
        self.assertIn("#define D008_PRODUCT_SOC_PROFILE_ID   BMS_SOC_PROFILE_GENERIC_NMC", self.profile)
        self.assertIn(
            "#define DVC1124_DEFAULT_CELL_COUNT           D008_PRODUCT_CELL_COUNT",
            self.cfg,
        )

    def test_additive_soc_identity_migration_only_fills_fully_unset_state(self):
        self.assertIn("param_apply_d008_product_identity_if_unset", self.param)
        self.assertIn("system.battery_chemistry == BMS_SOC_CHEMISTRY_AUTO", self.param)
        self.assertIn("system.soc_profile_id == BMS_SOC_PROFILE_AUTO", self.param)
        self.assertIn("system.battery_chemistry = D008_PRODUCT_CHEMISTRY;", self.param)
        self.assertIn("system.soc_profile_id = D008_PRODUCT_SOC_PROFILE_ID;", self.param)
        self.assertIn("bms_cold_kv_store_set_system(&system)", self.param)
        self.assertGreaterEqual(
            self.param.count("param_apply_d008_product_identity_if_unset();"), 2,
            "profile identity must be applied after load and again after upgrade-reset epochs",
        )
        migration = self.param.split("static void param_apply_d008_product_identity_if_unset", 1)[1]
        migration = migration.split("static int param_upgrade_epoch_mismatch", 1)[0]
        self.assertNotIn("system.series_num =", migration)
        self.assertNotIn("system.capacity_factory =", migration)

    def test_unverified_safety_features_remain_explicitly_disabled(self):
        self.assertEqual(macro_literal(self.cfg, "DVC1124_HW_SCD_THRESHOLD_MV"), 0)
        self.assertEqual(macro_literal(self.cfg, "DVC1124_CURRENT_WAKE_THRESHOLD_UV"), 0)
        self.assertEqual(macro_literal(self.cfg, "DVC1124_BODY_DIODE_THRESHOLD_UV"), 0)
        self.assertEqual(macro_literal(self.cfg, "DVC1124_I2C_WATCHDOG_SECONDS"), 0)

    def test_current_latch_recovery_is_measurement_based_and_scd_stays_latched(self):
        self.assertIn("u16Ichg <= g_tParam.protect.u16IchgOcp_Rcv", self.dvc_bms)
        self.assertIn("u16IDischg <= g_tParam.protect.u16IdsgOcp_Rcv", self.dvc_bms)
        clear_block = self.dvc_bms.split("static uint8_t dvc_clear_recovered_hw_latches", 1)[1]
        clear_block = clear_block.split("static void dvc_merge_hw_faults", 1)[0]
        self.assertNotIn("DVC1124_ALARM_SCD_MASK", clear_block)
        self.assertNotIn("gpio_read(CHG_IN_PIN)", clear_block)
        self.assertNotIn("gpio_read(SW_PIN)", clear_block)


if __name__ == "__main__":
    unittest.main(verbosity=2)
