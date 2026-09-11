#!/usr/bin/env python3
"""Host-only DVC1124 configuration contract checks.

No TC32 toolchain is required. These checks are deliberately source-level so
that register truth, persistence boundaries and communication ownership do not
silently regress while the target build remains tied to the vendor compiler.
"""

import re
import unittest
from pathlib import Path


HERE = Path(__file__).resolve().parent
REG_H = HERE / "dvc1124_reg.h"
PROJECT_CFG_H = HERE / "dvc1124_project_config.h"
CONFIG_STORE_H = HERE / "dvc1124_config_store.h"
CONFIG_STORE_C = HERE / "dvc1124_config_store.c"
FLASH_CFG_H = HERE / "flash_store_cfg.h"
MODBUS_H = HERE / "modbus_rtu.h"
MODBUS_C = HERE / "modbus_rtu.c"
CONF_H = HERE / "conf.h"


def read(path):
    return path.read_text(encoding="utf-8", errors="ignore")


def macro_int(text, name):
    match = re.search(
        rf"^\s*#define\s+{re.escape(name)}\s+\(?\s*(0x[0-9A-Fa-f]+|[0-9]+)u?\s*\)?",
        text,
        re.MULTILINE,
    )
    if not match:
        raise AssertionError(f"macro not found or non-literal: {name}")
    return int(match.group(1), 0)


class RegisterTruthTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.reg = read(REG_H)

    def test_v12_critical_masks(self):
        self.assertEqual(macro_int(self.reg, "DVC1124_OC2_ENABLE_MASK"), 0x40)
        self.assertEqual(macro_int(self.reg, "DVC1124_SCD_ENABLE_MASK"), 0x40)
        self.assertEqual(macro_int(self.reg, "DVC1124_CPVS_MASK"), 0x38)
        self.assertEqual(macro_int(self.reg, "DVC1124_COW_MASK"), 0x04)
        self.assertEqual(macro_int(self.reg, "DVC1124_CMM_MASK"), 0x02)
        self.assertEqual(macro_int(self.reg, "DVC1124_CVS_MASK"), 0x01)

    def test_oc_delay_formulas_are_documented_with_plus_one(self):
        self.assertIn("OC1 delay = (code + 1) * 8ms", self.reg)
        self.assertIn("delay = (code + 1) * 4ms", self.reg)

    def test_persistent_mask_excludes_commands_and_runtime_outputs(self):
        self.assertIn("case DVC1124_REG_ALARM:", self.reg)
        self.assertIn("case DVC1124_REG_STATUS:", self.reg)
        self.assertIn("case DVC1124_REG_FET_CTRL:", self.reg)
        self.assertIn("case DVC1124_REG_BAL_24_17:", self.reg)
        self.assertIn("~DVC1124_CADC_CAMZ_MASK", self.reg)
        self.assertIn("~DVC1124_COW_MASK", self.reg)

    def test_gp_unsupported_codes_not_named_as_features(self):
        self.assertNotIn("SCD_Q", self.reg)
        self.assertNotIn("OCD1_Q", self.reg)
        self.assertNotIn("HALF_CLK", self.reg)
        self.assertNotIn("PACK_DET", self.reg)


class PersistenceLayoutTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.flash = read(FLASH_CFG_H)

    def test_512k_afe_kv_uses_previously_unallocated_window(self):
        sector = macro_int(self.flash, "FLASH_SECTOR_SIZE")
        sectors = macro_int(self.flash, "FLASH_ADDR_AFE_CFG_KV_SECTORS")
        base = macro_int(self.flash, "FLASH_ADDR_LAYOUT_512K_AFE_CFG_KV_BASE")
        self.assertEqual(sector, 0x1000)
        self.assertEqual(sectors, 4)
        self.assertEqual(base, 0x5F000)
        self.assertEqual(base + sector * sectors, 0x63000)

    def test_afe_kv_does_not_overlap_cold_kv_or_smp(self):
        sector = macro_int(self.flash, "FLASH_SECTOR_SIZE")
        afe_base = macro_int(self.flash, "FLASH_ADDR_LAYOUT_512K_AFE_CFG_KV_BASE")
        afe_end = afe_base + sector * macro_int(self.flash, "FLASH_ADDR_AFE_CFG_KV_SECTORS")
        cold_base = macro_int(self.flash, "FLASH_ADDR_LAYOUT_512K_SOFT_PROTECT")
        self.assertGreaterEqual(afe_base, cold_base + 4 * sector)
        self.assertLessEqual(afe_end, 0x74000)


class ConfigStoreTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.hdr = read(CONFIG_STORE_H)
        cls.src = read(CONFIG_STORE_C)
        cls.project = read(PROJECT_CFG_H)
        cls.conf = read(CONF_H)

    def test_store_is_semantic_not_raw_struct_dump(self):
        self.assertIn("dvc1124_persistent_config_t", self.hdr)
        self.assertIn("DVC1124_ConfigStoreValidate", self.src)
        self.assertIn("DVC1124_ConfigStoreApply", self.src)
        self.assertNotIn("flash_write_page", self.src)
        self.assertIn("flash_kv32_write_pairs", self.src)

    def test_default_dpc_and_core_ot_are_named(self):
        self.assertIn("DVC1124_DEFAULT_DSG_PULLDOWN_STRENGTH", self.project)
        self.assertIn("DVC1124_DEFAULT_CORE_OT_CODE", self.project)

    def test_reset_and_config_paths_restore_persistent_config(self):
        self.assertIn("DVC1124_ConfigStore_AFE_Reset", self.conf)
        self.assertIn("DVC1124_ConfigStore_UpdataAfeConfig", self.conf)
        self.assertIn("DVC1124_ConfigStore_BmsApp_AFEGet", self.conf)
        self.assertIn("DVC1124_ConfigStoreRestore()", self.src)

    def test_apply_orders_timeout_policy_before_watchdog_operating_config(self):
        timeout_pos = self.src.find("DVC1124_CHGMASK_CWM_MASK")
        operating_pos = self.src.find("DVC1124_ApplyOperatingConfig")
        self.assertGreaterEqual(timeout_pos, 0)
        self.assertGreater(operating_pos, timeout_pos)

    def test_safety_ranges_are_validated(self):
        self.assertIn("cfg->dsg_pulldown_strength > 30u", self.src)
        self.assertIn("cfg->current_wake_threshold_uv > 2550u", self.src)
        self.assertIn("cfg->body_diode_threshold_uv > 10200u", self.src)
        self.assertIn("cfg->scd_threshold_mv > 630u", self.src)


class TransportContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.hdr = read(MODBUS_H)
        cls.src = read(MODBUS_C)

    def test_ble_uart_share_one_semantic_window(self):
        self.assertEqual(macro_int(self.hdr, "DVC1124_COMM_REG_BASE"), 0x2800)
        self.assertEqual(macro_int(self.hdr, "DVC1124_RAW_REG_BASE"), 0x2900)
        self.assertIn("read_dvc1124_comm_reg", self.src)
        self.assertIn("write_dvc1124_comm_reg", self.src)

    def test_requested_oc_uses_existing_bms_parameter_source(self):
        self.assertIn("g_tParam.protect.u16IdsgOcp_First", self.src)
        self.assertIn("g_tParam.protect.u16IchgOcp_First", self.src)
        self.assertIn("SaveParam();", self.src)
        self.assertIn("AFE_PARAM_WRITE_Flag = 1;", self.src)

    @unittest.expectedFailure
    def test_semantic_write_failure_returns_modbus_exception(self):
        """TODO: write_dvc1124_comm_reg is still void and needs status propagation."""
        self.assertRegex(self.src, r"static\s+int\s+write_dvc1124_comm_reg")

    @unittest.expectedFailure
    def test_raw_persistent_write_commits_afe_config_store(self):
        """TODO: raw write currently changes live AFE before persistence wiring."""
        self.assertIn("DVC1124_ConfigStoreWritePersistentRegister", self.src)


if __name__ == "__main__":
    unittest.main(verbosity=2)
