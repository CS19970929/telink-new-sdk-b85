#!/usr/bin/env python3
"""Quick static checks for ble_sample flash storage contracts.

This script does not depend on the TC32 toolchain. It verifies the flash
layout and a few source-level safety contracts that are easy to regress.
"""

import re
import sys
import unittest
from pathlib import Path
from typing import Dict, Tuple


MODULE_DIR = Path(__file__).resolve().parent
VENDOR_DIR = MODULE_DIR.parent
COMMON_DIR = VENDOR_DIR / "common"

FLASH_CFG = MODULE_DIR / "flash_store_cfg.h"
BMS_COLD_HDR = MODULE_DIR / "bms_cold_kv_store.h"
BLE_FLASH = COMMON_DIR / "ble_flash.h"
FLASH_SAFE = MODULE_DIR / "flash_store_safe.h"
APP_C = MODULE_DIR / "app.c"
RUNTIME_C = MODULE_DIR / "runtime.c"
EVENT_LOG_C = MODULE_DIR / "bms_event_log.c"
PARAM_C = MODULE_DIR / "param.c"


def read_text(path):
    return path.read_text(encoding="utf-8", errors="ignore")


def parse_macro_int(text, name):
    pattern = re.compile(
        rf"^\s*#define\s+{re.escape(name)}\s+\(?(0x[0-9A-Fa-f]+|[0-9]+)u?\)?",
        re.MULTILINE,
    )
    match = pattern.search(text)
    if not match:
        raise AssertionError(f"macro not found: {name}")
    return int(match.group(1), 0)


def range_end(base, sectors, sector_size):
    return base + sectors * sector_size


def overlaps(a, b):
    return max(a[0], b[0]) < min(a[1], b[1])


class FlashLayoutTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.flash_cfg_text = read_text(FLASH_CFG)
        cls.ble_flash_text = read_text(BLE_FLASH)
        cls.bms_cold_text = read_text(BMS_COLD_HDR)
        cls.sector_size = parse_macro_int(cls.flash_cfg_text, "FLASH_SECTOR_SIZE")
        cls.runtime_sectors = parse_macro_int(cls.flash_cfg_text, "FLASH_ADDR_RUNTIME_SECTORS")
        cls.hot_kv_sectors = parse_macro_int(cls.flash_cfg_text, "FLASH_ADDR_RUN_KV_SECTORS")
        cls.event_log_sectors = parse_macro_int(cls.flash_cfg_text, "FLASH_ADDR_LOG_SECTORS")
        cls.cold_kv_sectors = parse_macro_int(cls.bms_cold_text, "BMS_COLD_KV_SECTORS")

    def layout_ranges(self, prefix):
        # type: (str) -> Dict[str, Tuple[int, int]]
        text = self.flash_cfg_text
        return {
            "btname": (
                parse_macro_int(text, f"FLASH_ADDR_LAYOUT_{prefix}_BTNAME_BASE"),
                range_end(
                    parse_macro_int(text, f"FLASH_ADDR_LAYOUT_{prefix}_BTNAME_BASE"),
                    1,
                    self.sector_size,
                ),
            ),
            "runtime": (
                parse_macro_int(text, f"FLASH_ADDR_LAYOUT_{prefix}_RUNTIME_BASE"),
                range_end(
                    parse_macro_int(text, f"FLASH_ADDR_LAYOUT_{prefix}_RUNTIME_BASE"),
                    self.runtime_sectors,
                    self.sector_size,
                ),
            ),
            "soc_kv": (
                parse_macro_int(text, f"FLASH_ADDR_LAYOUT_{prefix}_RUN_KV_BASE"),
                range_end(
                    parse_macro_int(text, f"FLASH_ADDR_LAYOUT_{prefix}_RUN_KV_BASE"),
                    self.hot_kv_sectors,
                    self.sector_size,
                ),
            ),
            "cold_kv": (
                parse_macro_int(text, f"FLASH_ADDR_LAYOUT_{prefix}_SOFT_PROTECT"),
                range_end(
                    parse_macro_int(text, f"FLASH_ADDR_LAYOUT_{prefix}_SOFT_PROTECT"),
                    self.cold_kv_sectors,
                    self.sector_size,
                ),
            ),
            "event_log": (
                parse_macro_int(text, f"FLASH_ADDR_LAYOUT_{prefix}_LOG_BASE"),
                range_end(
                    parse_macro_int(text, f"FLASH_ADDR_LAYOUT_{prefix}_LOG_BASE"),
                    self.event_log_sectors,
                    self.sector_size,
                ),
            ),
        }

    def assert_no_internal_overlap(self, ranges):
        # type: (Dict[str, Tuple[int, int]]) -> None
        items = list(ranges.items())
        for idx, (name_a, range_a) in enumerate(items):
            for name_b, range_b in items[idx + 1 :]:
                self.assertFalse(
                    overlaps(range_a, range_b),
                    msg=f"{name_a} overlaps {name_b}: {range_a} vs {range_b}",
                )

    def test_layout_512k_no_overlap(self):
        ranges = self.layout_ranges("512K")
        self.assert_no_internal_overlap(ranges)

        reserved = {
            "smp": (
                parse_macro_int(self.ble_flash_text, "FLASH_ADR_SMP_PAIRING_512K_FLASH"),
                parse_macro_int(self.ble_flash_text, "CFG_ADR_MAC_512K_FLASH"),
            ),
            "mac": (
                parse_macro_int(self.ble_flash_text, "CFG_ADR_MAC_512K_FLASH"),
                parse_macro_int(self.ble_flash_text, "CFG_ADR_CALIBRATION_512K_FLASH"),
            ),
            "calibration": (
                parse_macro_int(self.ble_flash_text, "CFG_ADR_CALIBRATION_512K_FLASH"),
                0x80000,
            ),
        }
        for app_name, app_range in ranges.items():
            for reserved_name, reserved_range in reserved.items():
                self.assertFalse(
                    overlaps(app_range, reserved_range),
                    msg=f"512K {app_name} overlaps reserved {reserved_name}",
                )

    def test_layout_1m_no_overlap(self):
        ranges = self.layout_ranges("1M")
        self.assert_no_internal_overlap(ranges)
        reserved = (
            parse_macro_int(self.ble_flash_text, "FLASH_ADR_SMP_PAIRING_1M_FLASH"),
            0x100000,
        )
        for app_name, app_range in ranges.items():
            self.assertFalse(overlaps(app_range, reserved), msg=f"1M {app_name} overlaps reserved")

    def test_layout_2m_no_overlap(self):
        ranges = self.layout_ranges("2M")
        self.assert_no_internal_overlap(ranges)
        reserved = (
            parse_macro_int(self.ble_flash_text, "FLASH_ADR_SMP_PAIRING_2M_FLASH"),
            0x200000,
        )
        for app_name, app_range in ranges.items():
            self.assertFalse(overlaps(app_range, reserved), msg=f"2M {app_name} overlaps reserved")

    def test_512k_layout_explicitly_rejects_ota_0x40000(self):
        text = self.flash_cfg_text
        self.assertIn("MULTI_BOOT_ADDR_0x20000", text)
        self.assertIn("return 0;", text)

    def test_1m_layout_explicitly_rejects_ota_0x80000(self):
        text = self.flash_cfg_text
        self.assertIn("MULTI_BOOT_ADDR_0x80000", text)
        self.assertIn("blc_flash_capacity == FLASH_SIZE_1M", text)


class SourceContractTests(unittest.TestCase):
    def test_flash_helper_does_not_restore_lock_during_stack_session(self):
        text = read_text(FLASH_SAFE)
        self.assertIn("app_flash_lock_restore_enabled()", text)
        self.assertNotIn("flash_lock(flash_lockBlock_cmd);\n#endif", text)

    def test_app_tracks_stack_flash_session(self):
        text = read_text(APP_C)
        self.assertIn("g_app_flash_stack_session_active = 1u;", text)
        self.assertIn("g_app_flash_stack_session_active = 0u;", text)

    def test_runtime_same_address_migration_prefers_second_sector(self):
        text = read_text(RUNTIME_C)
        self.assertIn("flash_store_cfg_get_legacy_runtime_base() == runtime_flash_base()", text)
        self.assertIn("g_runtime_next_index = RUNTIME_RECORDS_PER_SECTOR;", text)

    def test_event_log_reports_store_error(self):
        text = read_text(EVENT_LOG_C)
        self.assertIn("bms_event_log_report_store_error()", text)
        self.assertIn("return BMS_EVENT_LOG_INVALID_SLOT;", text)
        self.assertIn("System_ERROR_UserCallback(ERROR_EEPROM_STORE);", text)

    def test_event_log_edge_latch_depends_on_persist_success(self):
        text = read_text(EVENT_LOG_C)
        self.assertIn("if (bms_event_log_append(event, 0)) {", text)
        self.assertIn("g_bms_event_log.event_latched[event] = 1u;", text)

    def test_upgrade_epoch_marking_happens_only_after_success(self):
        text = read_text(PARAM_C)
        self.assertIn("if (param_upgrade_apply_default_protect()) {", text)
        self.assertIn("if (param_upgrade_apply_default_system()) {", text)
        self.assertIn("if (param_upgrade_apply_default_soc()) {", text)
        self.assertIn("if (param_upgrade_apply_default_event_log()) {", text)
        self.assertIn("if (param_upgrade_apply_default_runtime()) {", text)
        self.assertGreaterEqual(text.count("System_ERROR_UserCallback(ERROR_EEPROM_STORE);"), 6)

    def test_soc_kv_flush_interval_is_rate_limited(self):
        text = read_text(MODULE_DIR / "soc_kv_store.c")
        self.assertIn("SOC_KV_FLUSH_INTERVAL_US", text)
        self.assertIn("clock_time_exceed(g_soc_last_flush_tick, SOC_KV_FLUSH_INTERVAL_US)", text)

    def test_runtime_factory_reset_api_exists(self):
        text = read_text(RUNTIME_C)
        self.assertIn("int Runtime_FactoryReset(void)", text)


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__])
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    sys.exit(0 if result.wasSuccessful() else 1)
