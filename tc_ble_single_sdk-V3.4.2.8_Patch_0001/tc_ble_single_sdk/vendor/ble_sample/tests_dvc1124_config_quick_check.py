#!/usr/bin/env python3
"""Quick host-only DVC1124 configuration contract checks.

No TC32 toolchain is required. These checks are deliberately source-level so
register truth, persistence boundaries and transport ownership cannot silently
regress while the target build remains tied to the vendor compiler.
"""

import re
import unittest
from pathlib import Path


HERE = Path(__file__).resolve().parent
REG_H = HERE / "dvc1124_reg.h"
DVC_H = HERE / "dvc1124.h"
DVC_C = HERE / "dvc1124.c"
CORE_OT_C = HERE / "dvc1124_core_ot.c"
DVC_BMS_C = HERE / "dvc1124_bms.c"
PROJECT_CFG_H = HERE / "dvc1124_project_config.h"
CONFIG_STORE_H = HERE / "dvc1124_config_store.h"
CONFIG_STORE_C = HERE / "dvc1124_config_store.c"
CONFIG_SERVICE_H = HERE / "dvc1124_config_service.h"
CONFIG_SERVICE_C = HERE / "dvc1124_config_service.c"
FLASH_CFG_H = HERE / "flash_store_cfg.h"
MODBUS_H = HERE / "modbus_rtu.h"
MODBUS_C = HERE / "modbus_rtu.c"
APP_C = HERE / "app.c"
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
        cls.dvc_h = read(DVC_H)
        cls.dvc_c = read(DVC_C)
        cls.core_ot = read(CORE_OT_C)
        cls.dvc_bms = read(DVC_BMS_C)

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

    def test_field_writer_rejects_silent_mask_truncation(self):
        self.assertIn("field_max = (uint8_t)(mask >> shift);", self.dvc_h)
        self.assertIn("value > field_max", self.dvc_h)
        self.assertNotIn(
            "DVC1124_FIELD_PREP(mask, shift, value) & (uint8_t)~mask",
            self.dvc_h,
        )

    def test_driver_has_no_local_register_truth_macros(self):
        self.assertNotRegex(self.dvc_c, r"(?m)^\s*#define\s+DVC_REG_")
        self.assertNotRegex(self.dvc_c, r"(?m)^\s*#define\s+DVC_ALARM_")
        self.assertNotRegex(self.dvc_c, r"(?m)^\s*#define\s+DVC_CPVS_")
        self.assertNotRegex(self.dvc_c, r"(?m)^\s*#define\s+DVC_OC2_")
        self.assertNotRegex(self.dvc_c, r"(?m)^\s*#define\s+DVC_SCD_")
        self.assertIn("DVC1124_REG_OCD2", self.dvc_c)
        self.assertIn("DVC1124_CPVS_MASK", self.dvc_c)

    def test_bms_adapter_uses_canonical_alarm_names(self):
        self.assertNotIn("DVC_BMS_REG_ALARM", self.dvc_bms)
        self.assertNotIn("DVC_BMS_ALARM_", self.dvc_bms)
        self.assertIn("DVC1124_REG_ALARM", self.dvc_bms)
        self.assertIn("DVC1124_ALARM_COV_MASK", self.dvc_bms)

    def test_read_clear_metadata_names_status_and_core_ot(self):
        self.assertIn("DVC1124_RegReadEffect", self.dvc_h)
        self.assertIn("reg == DVC1124_REG_STATUS", self.dvc_h)
        self.assertIn("reg == DVC1124_REG_CORE_OT", self.dvc_h)
        self.assertIn("DVC1124_REG_READ_CLEAR", self.dvc_h)

    def test_core_ot_access_preserves_read_clear_event_in_software(self):
        self.assertIn("s_core_ot_event_latched", self.core_ot)
        self.assertIn("DVC1124_CORE_OT_FLAG_MASK", self.core_ot)
        self.assertIn("DVC1124_ReadCoreOtThresholdCode", self.core_ot)
        self.assertIn("DVC1124_SetCoreOtThresholdCode", self.core_ot)
        self.assertIn("DVC1124_GetCoreOtEventLatched", self.core_ot)


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

    def test_core_ot_uses_dedicated_rc_safe_api(self):
        self.assertIn("DVC1124_SetCoreOtThresholdCode(cfg->core_ot_code)", self.src)
        self.assertIn("DVC1124_ReadCoreOtThresholdCode(&cfg->core_ot_code)", self.src)
        self.assertNotIn(
            "DVC1124_ReadRegisters(DVC1124_REG_CORE_OT",
            self.src,
        )
        self.assertNotIn(
            "DVC1124_WriteRegisterFieldSafe(DVC1124_REG_CORE_OT",
            self.src,
        )


class ConfigServiceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.hdr = read(CONFIG_SERVICE_H)
        cls.src = read(CONFIG_SERVICE_C)

    def test_field_ids_are_transport_neutral(self):
        self.assertIn("dvc1124_config_field_t", self.hdr)
        self.assertIn("DVC1124_ConfigServiceRead", self.hdr)
        self.assertIn("DVC1124_ConfigServiceWrite", self.hdr)

    def test_afe_write_rolls_back_when_kv_save_fails(self):
        self.assertIn("DVC1124_ConfigStoreApply(after)", self.src)
        self.assertIn("DVC1124_ConfigStoreSave(after)", self.src)
        self.assertIn("DVC1124_ConfigStoreApply(before)", self.src)

    def test_existing_bms_protection_store_remains_source_of_truth(self):
        self.assertIn("struct PRT_E2ROM_PARAS candidate = g_tParam.protect", self.src)
        self.assertIn("bms_cold_kv_store_set_protect(&candidate)", self.src)
        self.assertIn("g_tParam.protect = candidate", self.src)
        self.assertIn("AFE_PARAM_WRITE_Flag = 1", self.src)

    def test_raw_write_is_factory_only(self):
        self.assertIn("Runtime_GetMode() != MODE_FACTORY", self.src)
        self.assertIn("DVC1124_CFG_ERR_FORBIDDEN", self.src)

    def test_raw_write_decodes_into_semantic_config(self):
        self.assertIn("dvc_cfg_raw_to_candidate", self.src)
        self.assertIn("DVC1124_REG_GP123_MODE", self.src)
        self.assertIn("DVC1124_REG_I2C_WDT", self.src)
        self.assertIn("DVC1124_REG_CORE_OT", self.src)

    def test_raw_read_refuses_read_clear_registers(self):
        self.assertIn("DVC1124_RegReadHasSideEffect(reg)", self.src)
        self.assertIn("return DVC1124_CFG_ERR_FORBIDDEN", self.src)


class TransportContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.hdr = read(MODBUS_H)
        cls.src = read(MODBUS_C)
        cls.app = read(APP_C)

    def test_ble_uart_share_one_semantic_window(self):
        self.assertEqual(macro_int(self.hdr, "DVC1124_COMM_REG_BASE"), 0x2800)
        self.assertEqual(macro_int(self.hdr, "DVC1124_RAW_REG_BASE"), 0x2900)
        self.assertIn("DVC1124_ConfigServiceRead", self.src)
        self.assertIn("DVC1124_ConfigServiceWrite", self.src)

    def test_transport_no_longer_contains_dvc_register_encoding(self):
        self.assertNotIn("dvc_current_x10_from_sense_uv", self.src)
        self.assertNotIn("dvc_core_ot_code_from_x10", self.src)
        self.assertNotIn("DVC1124_WriteRegisterFieldSafe", self.src)
        self.assertNotIn("DVC1124_SetShortCircuitProtection", self.src)

    def test_semantic_write_failure_returns_modbus_exception(self):
        self.assertIn("dvc_result_to_modbus_exception", self.src)
        self.assertIn("MB_EX_ILLEGAL_VALUE", self.src)
        self.assertIn("MB_EX_DEVICE_FAILURE", self.src)
        self.assertIn("modbus_exception(addr, func, exception", self.src)

    def test_dvc_multi_write_is_rejected_until_batch_transaction_exists(self):
        self.assertIn("qty > 1u && dvc_comm_range_contains", self.src)
        self.assertIn("Reject multi-field writes", self.src)

    def test_raw_write_uses_factory_gated_config_service(self):
        self.assertIn("DVC1124_ConfigServiceWriteRaw", self.src)

    def test_bms_report_has_single_owner(self):
        self.assertIn("struct stCell_Info g_stCellInfoReport;", self.app)
        self.assertIn("extern struct stCell_Info g_stCellInfoReport;", self.src)
        self.assertNotRegex(
            self.src,
            r"(?m)^struct\s+stCell_Info\s+g_stCellInfoReport\s*;",
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
