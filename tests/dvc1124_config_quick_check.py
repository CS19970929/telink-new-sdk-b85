#!/usr/bin/env python3
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001" / "tc_ble_single_sdk" / "vendor" / "ble_sample"


def read(name):
    return (SRC / name).read_text(encoding="utf-8", errors="strict")


def macro_literal(text, name):
    m = re.search(
        rf"^\s*#define\s+{re.escape(name)}\s+\(?\s*(0x[0-9A-Fa-f]+|[0-9]+)u?\s*\)?",
        text,
        re.MULTILINE,
    )
    if not m:
        raise AssertionError(f"literal macro not found: {name}")
    return int(m.group(1), 0)


class RegisterTruthTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.reg = read("dvc1124_reg.h")
        cls.hdr = read("dvc1124.h")
        cls.driver = read("dvc1124.c")

    def test_critical_register_truth(self):
        self.assertEqual(macro_literal(self.reg, "DVC1124_OC2_ENABLE_MASK"), 0x40)
        self.assertEqual(macro_literal(self.reg, "DVC1124_SCD_ENABLE_MASK"), 0x40)
        self.assertEqual(macro_literal(self.reg, "DVC1124_DSGMASK_DWM_MASK"), 0x08)
        self.assertEqual(macro_literal(self.reg, "DVC1124_CHGMASK_CWM_MASK"), 0x80)
        self.assertEqual(macro_literal(self.reg, "DVC1124_I2C_WDT_TIME_MASK"), 0x07)

    def test_watchdog_encodings_match_v12_mapping(self):
        self.assertRegex(self.reg, r"DVC1124_I2C_WDT_OFF\s*=\s*0u")
        self.assertRegex(self.reg, r"DVC1124_I2C_WDT_4S\s*=\s*4u")
        self.assertRegex(self.reg, r"DVC1124_I2C_WDT_8S\s*=\s*5u")
        self.assertRegex(self.reg, r"DVC1124_I2C_WDT_16S\s*=\s*6u")
        self.assertRegex(self.reg, r"DVC1124_I2C_WDT_32S\s*=\s*7u")

    def test_read_clear_registers_remain_protected(self):
        self.assertIn("DVC1124_RegReadHasSideEffect", self.hdr)
        self.assertIn("DVC1124_REG_STATUS", self.hdr)
        self.assertIn("DVC1124_REG_CORE_OT", self.hdr)

    def test_hw_off_still_disables_autonomous_sources(self):
        block = self.driver.split("static uint8_t dvc_disable_threshold_protection", 1)[1]
        block = block.split("#endif", 1)[0]
        self.assertIn("DVC1124_I2C_WDT_OFF", block)
        self.assertIn("DVC1124_REG_DSG_MASK, 0xFFu", block)
        self.assertIn("DVC1124_REG_CHG_MASK, 0xFFu", block)
        self.assertIn("DVC1124_REG_BODY_DIODE, 0u", block)


class CompileTimeOwnershipTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.product = read("d008_product_profile.h")
        cls.project = read("dvc1124_project_config.h")
        cls.backend = read("dvc1124_config_store.c")
        cls.store_hdr = read("dvc1124_config_store.h")
        cls.service = read("dvc1124_config_service.c")
        cls.service_hdr = read("dvc1124_config_service.h")

    def test_product_fail_safe_defaults_are_compile_time(self):
        self.assertEqual(macro_literal(self.product, "DVC1124_I2C_WATCHDOG_SECONDS"), 4)
        self.assertEqual(macro_literal(self.product, "DVC1124_I2C_TIMEOUT_CLOSE_CHG"), 1)
        self.assertEqual(macro_literal(self.product, "DVC1124_I2C_TIMEOUT_CLOSE_DSG"), 1)

    def test_project_config_keeps_body_diode_and_mask_policy_named(self):
        self.assertEqual(macro_literal(self.project, "DVC1124_BODY_DIODE_THRESHOLD_UV"), 80)
        self.assertIn("DVC1124_DSGMASK_DBDM_MASK", self.project)
        self.assertIn("DVC1124_CHGMASK_CBDM_MASK", self.project)

    def test_legacy_store_file_no_longer_owns_flash(self):
        forbidden = (
            "flash_kv32",
            "flash_store_cfg",
            "DVC1124_ConfigStoreLoad",
            "DVC1124_ConfigStoreSave",
            "DVC1124_ConfigStoreRestore",
            "DVC1124_ConfigStoreCapture",
        )
        for token in forbidden:
            self.assertNotIn(token, self.backend)
        self.assertIn("DVC1124_FIXED_CONFIG_COMPILE_TIME", self.store_hdr)
        self.assertNotIn("dvc1124_persistent_config_t", self.store_hdr)

    def test_every_afe_init_reapplies_firmware_policy(self):
        self.assertIn("DVC1124_AFE_Reset();", self.backend)
        self.assertIn("DVC1124_UpdataAfeConfig();", self.backend)
        self.assertIn("dvc_project_apply_compile_time_config", self.backend)
        self.assertIn("s_project_config_pending", self.backend)
        self.assertNotIn("ConfigStoreRestore", self.backend)

    def test_compile_time_apply_covers_previous_persistent_fields(self):
        expected = (
            "DVC1124_DEFAULT_CC1_WORK_TIME",
            "DVC1124_DEFAULT_CC1_SLEEP_WAKE_TIME",
            "DVC1124_CHARGE_PUMP_VOLTAGE_CODE",
            "DVC1124_DEFAULT_CELL_MEASUREMENT_MASK",
            "DVC1124_DEFAULT_CELL_VOLTAGE_SIGNED",
            "DVC1124_DEFAULT_VADC_ENABLE",
            "DVC1124_DEFAULT_VADC_SYNC_WITH_CC2",
            "DVC1124_DEFAULT_VADC_PERIOD",
            "DVC1124_DEFAULT_VADC_TIME",
            "DVC1124_GP1_DEFAULT_MODE",
            "DVC1124_GP6_DEFAULT_MODE",
            "DVC1124_DEFAULT_V3P3_SLEEP_ENABLE",
            "DVC1124_DEFAULT_V3P3_WORK_ENABLE",
            "DVC1124_DEFAULT_TIMED_WAKE",
            "DVC1124_DEFAULT_INTERRUPT_MASK",
            "DVC1124_DEFAULT_DSG_PULLDOWN_STRENGTH",
            "DVC1124_CURRENT_WAKE_THRESHOLD_UV",
            "DVC1124_BODY_DIODE_THRESHOLD_UV",
            "DVC1124_I2C_WATCHDOG_SECONDS",
            "DVC1124_I2C_TIMEOUT_CLOSE_CHG",
            "DVC1124_I2C_TIMEOUT_CLOSE_DSG",
            "DVC1124_DEFAULT_CORE_OT_CODE",
        )
        for token in expected:
            self.assertIn(token, self.backend)
        self.assertIn("DVC1124_ApplyOperatingConfig(&cfg)", self.backend)

    def test_hw_off_compile_path_does_not_reenable_watchdog(self):
        self.assertIn("#if DVC1124_HW_PROTECT_ENABLE", self.backend)
        self.assertIn("wdt = DVC1124_I2C_WDT_OFF;", self.backend)
        self.assertIn("dsg_mask = 0xFFu;", self.backend)
        self.assertIn("chg_mask = 0xFFu;", self.backend)
        self.assertIn("body_diode_code = 0u;", self.backend)

    def test_semantic_fixed_config_is_read_only(self):
        self.assertNotIn("dvc1124_config_store.h", self.service)
        self.assertNotIn("DVC1124_ConfigStore", self.service)
        self.assertIn("DVC1124_CFG_ERR_READ_ONLY", self.service)
        self.assertIn("fixed DVC operating/board policy", self.service)
        self.assertIn("diagnostic READ-ONLY", self.service_hdr)

    def test_raw_register_mirror_is_read_only(self):
        raw_write = self.service.split("DVC1124_ConfigServiceWriteRaw", 1)[1]
        self.assertIn("DVC1124_CFG_ERR_READ_ONLY", raw_write)
        self.assertNotIn("DVC1124_WriteRegisters", raw_write)
        self.assertNotIn("ConfigStore", raw_write)


class ProtectionOwnershipTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.service = read("dvc1124_config_service.c")
        cls.modbus = read("modbus_rtu.c")
        cls.hw_profile = read("bms_afe_hw_profile.c")
        cls.backend = read("dvc1124_config_store.c")

    def test_requested_afe_protection_still_comes_from_hw_profile(self):
        self.assertIn("bms_afe_hw_profile_get(&hw)", self.service)
        for field in (
            "hw.cov_mv",
            "hw.cuv_mv",
            "hw.ocd1_a10",
            "hw.occ1_a10",
            "hw.ocd2_a10",
            "hw.occ2_a10",
            "hw.sc_a10",
        ):
            self.assertIn(field, self.service)

    def test_effective_afe_protection_is_read_from_dvc(self):
        for reg in (
            "DVC1124_REG_COV_H",
            "DVC1124_REG_CUV_H",
            "DVC1124_REG_OCD1_THR",
            "DVC1124_REG_OCC1_THR",
            "DVC1124_REG_OCD2",
            "DVC1124_REG_OCC2",
            "DVC1124_REG_SCD",
        ):
            self.assertIn(reg, self.service)

    def test_hw_profile_atomic_transaction_remains_persistent_owner(self):
        self.assertIn("afe_hw_profile_write_block", self.modbus)
        self.assertIn("bms_afe_hw_profile_set(&candidate)", self.modbus)
        self.assertIn("bms_afe_apply_protection_config()", self.modbus)
        self.assertIn("bms_afe_hw_profile_get(&verify)", self.modbus)

    def test_backend_runtime_apply_only_delegates_protection(self):
        fn = self.backend.split("uint8_t bms_afe_apply_protection_config", 1)[1]
        self.assertIn("DVC1124_ApplyProtectionConfig()", fn)
        self.assertNotIn("ConfigStore", fn)


if __name__ == "__main__":
    unittest.main()
