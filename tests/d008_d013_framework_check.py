#!/usr/bin/env python3
"""Host-only contract checks for the D008 <- D013 framework adaptation.

These checks intentionally verify architecture and fail-safe defaults without
pretending to execute TLSR8251 hardware, the DVC1124 analog front end, or the
self-hosted TC32 compiler.
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001" / "tc_ble_single_sdk" / "vendor" / "ble_sample"


def text(name):
    return (APP / name).read_text(encoding="utf-8", errors="ignore")


def macro_literal(source, name):
    m = re.search(
        rf"(?m)^\s*#define\s+{re.escape(name)}\s+(0x[0-9A-Fa-f]+|[0-9]+)u?\b",
        source,
    )
    if not m:
        raise AssertionError(f"literal macro not found: {name}")
    return int(m.group(1), 0)


class BackendBoundaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.backend = text("bms_afe_backend.h")
        cls.afe = text("bms_afe.h")
        cls.safe = text("dvc1124_safe_bms.c")
        cls.order = (ROOT / "bms_tools" / "source_order.txt").read_text(encoding="utf-8")

    def test_d008_defaults_to_safe_dvc_backend(self):
        self.assertIn("BMS_AFE_BACKEND_DVC1124_SAFE", self.backend)
        self.assertRegex(
            self.backend,
            r"#define\s+BMS_AFE_BACKEND\s+BMS_AFE_BACKEND_DVC1124_SAFE",
        )

    def test_application_api_is_compile_time_rebound(self):
        self.assertIn("#define bms_afe_init", self.afe)
        self.assertIn("dvc1124_safe_bms_afe_init", self.afe)
        self.assertIn("!defined(DVC1124_H_)", self.afe)
        self.assertIn("!defined(DVC1124_CONFIG_STORE_H_)", self.afe)

    def test_safe_wrapper_is_in_locked_link_order(self):
        self.assertIn("vendor/ble_sample/dvc1124_safe_bms.c", self.order)

    def test_safe_wrapper_does_not_import_d013_hardware(self):
        # Comments may name D013/SH3673510 to explain what is deliberately not
        # copied. Reject actual includes, board symbols and SPI implementation
        # dependencies instead of matching explanatory prose.
        forbidden_patterns = (
            r'#include\s+["<]sh3673510',
            r'#include\s+["<]sh3673520',
            r'\bD011_[A-Za-z0-9_]*\b',
            r'\bD013_[A-Za-z0-9_]*\b',
            r'\bD011_SWITCH_PIN\b',
            r'\bD011_AFE_[A-Za-z0-9_]*\b',
            r'\bSPI_MODE[0-9_]*\b',
        )
        for pattern in forbidden_patterns:
            self.assertIsNone(re.search(pattern, self.safe))


class SafetySupervisorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.safe = text("dvc1124_safe_bms.c")
        cls.cfg = text("dvc1124_project_config.h")

    def test_comm_failure_inhibits_outputs_and_resets_valid_streak(self):
        self.assertIn("s_safe.output_inhibit = 1u", self.safe)
        self.assertIn("s_safe.snapshot_valid_streak = 0u", self.safe)
        self.assertIn("(void)bms_afe_set_fets(0u, 0u)", self.safe)

    def test_reinit_is_bounded_and_valid_snapshots_gate_release(self):
        self.assertIn("DVC1124_SAFE_REINIT_TRIGGER_SAMPLES", self.safe)
        self.assertIn("DVC1124_SAFE_REINIT_COOLDOWN_SAMPLES", self.safe)
        self.assertIn("DVC1124_SAFE_VALID_RELEASE_SAMPLES", self.safe)
        self.assertIn("bms_afe_init();", self.safe)
        self.assertIn("s_safe.output_inhibit = 0u", self.safe)

    def test_requested_and_effective_fet_state_are_separate(self):
        self.assertIn("requested_charge_on", self.safe)
        self.assertIn("requested_discharge_on", self.safe)
        self.assertIn("dvc_safe_effective_fets", self.safe)
        self.assertIn("if (s_safe.short_latched)", self.safe)

    def test_short_auto_recovery_is_fail_safe_off_by_default(self):
        self.assertEqual(macro_literal(self.cfg, "DVC1124_SHORT_AUTO_RECOVERY_ENABLE"), 0)
        self.assertIn("current D008 evidence does not identify", self.cfg)


class RegisterDefaultAuditTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cfg = text("dvc1124_project_config.h")
        cls.reg = text("dvc1124_reg.h")
        cls.profile = text("d008_product_profile.h")

    def test_low_side_board_masks_unused_high_side_fets(self):
        self.assertEqual(macro_literal(self.cfg, "DVC1124_DEFAULT_HIGH_SIDE_FET_MASK"), 1)
        self.assertIn("HSFM", self.cfg)

    def test_current_wake_engine_matches_zero_threshold(self):
        self.assertEqual(macro_literal(self.cfg, "DVC1124_DEFAULT_CURRENT_WAKE_ENGINE_ENABLE"), 0)
        self.assertEqual(macro_literal(self.cfg, "DVC1124_CURRENT_WAKE_THRESHOLD_UV"), 0)

    def test_unused_interrupt_output_sources_are_masked(self):
        self.assertEqual(macro_literal(self.cfg, "DVC1124_DEFAULT_INTERRUPT_MASK"), 0xFF)

    def test_unverified_destructive_safety_defaults_remain_disabled(self):
        self.assertEqual(macro_literal(self.cfg, "DVC1124_HW_SCD_THRESHOLD_MV"), 0)
        self.assertEqual(macro_literal(self.cfg, "DVC1124_I2C_WATCHDOG_SECONDS"), 0)
        self.assertEqual(macro_literal(self.cfg, "DVC1124_BODY_DIODE_THRESHOLD_UV"), 0)

    def test_v12_critical_register_truth_is_unchanged(self):
        self.assertEqual(macro_literal(self.reg, "DVC1124_CPVS_MASK"), 0x38)
        self.assertEqual(macro_literal(self.reg, "DVC1124_OC2_ENABLE_MASK"), 0x40)
        self.assertEqual(macro_literal(self.reg, "DVC1124_SCD_ENABLE_MASK"), 0x40)

    def test_assembly_profiles_cover_24s_lfp_and_20s_nmc(self):
        self.assertIn("D008_PRODUCT_PROFILE_24S_LFP", self.profile)
        self.assertIn("D008_PRODUCT_PROFILE_20S_NMC", self.profile)
        self.assertIn("D008_PROFILE_CELL_COUNT      24u", self.profile)
        self.assertIn("D008_PROFILE_CELL_COUNT      20u", self.profile)
        self.assertIn("D008_PROFILE_CELL_COUNT", self.cfg)


if __name__ == "__main__":
    unittest.main(verbosity=2)
