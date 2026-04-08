#!/usr/bin/env python3
"""Quick static checks for ble_sample flash storage contracts.

This script does not depend on the TC32 toolchain. It verifies the flash
layout and a few source-level safety contracts that are easy to regress.
"""

import re
import sys
import unittest
from pathlib import Path
from typing import Dict, Iterable, List, Tuple


MODULE_DIR = Path(__file__).resolve().parent
VENDOR_DIR = MODULE_DIR.parent
COMMON_DIR = VENDOR_DIR / "common"
SDK_DIR = VENDOR_DIR.parent

FLASH_CFG = MODULE_DIR / "flash_store_cfg.h"
BMS_COLD_HDR = MODULE_DIR / "bms_cold_kv_store.h"
BLE_FLASH = COMMON_DIR / "ble_flash.h"
FLASH_SAFE = MODULE_DIR / "flash_store_safe.h"
APP_C = MODULE_DIR / "app.c"
RUNTIME_C = MODULE_DIR / "runtime.c"
EVENT_LOG_C = MODULE_DIR / "bms_event_log.c"
PARAM_C = MODULE_DIR / "param.c"
OTA_SERVER_H = SDK_DIR / "stack" / "ble" / "service" / "ota" / "ota_server.h"
BLE_SAMPLE_BIN = SDK_DIR / "project" / "tlsr_tc32" / "B85" / "825x_ble_sample" / "825x_ble_sample.bin"

DEFAULT_OTA_FW_MAX_SIZE = 124 * 1024
DEFAULT_OTA_BOOT_ADDR = 0x20000


def read_text(path):
    return path.read_text(encoding="utf-8", errors="ignore")


def parse_macro_ints(text, name):
    pattern = re.compile(
        rf"^\s*#define\s+{re.escape(name)}\s+\(?(0x[0-9A-Fa-f]+|[0-9]+)u?\)?",
        re.MULTILINE,
    )
    values = [int(value, 0) for value in pattern.findall(text)]
    if not values:
        raise AssertionError(f"macro not found: {name}")
    return values


def parse_macro_int(text, name):
    return parse_macro_ints(text, name)[0]


def range_end(base, sectors, sector_size):
    return base + sectors * sector_size


def overlaps(a, b):
    return max(a[0], b[0]) < min(a[1], b[1])


def point_in_range(point, region):
    return region[0] <= point < region[1]


def unique_sorted(values):
    # type: (Iterable[int]) -> List[int]
    return sorted(set(values))


def format_range(region):
    return f"0x{region[0]:05X}-0x{region[1] - 1:05X}"


class FlashLayoutTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.flash_cfg_text = read_text(FLASH_CFG)
        cls.ble_flash_text = read_text(BLE_FLASH)
        cls.bms_cold_text = read_text(BMS_COLD_HDR)
        cls.ota_server_text = read_text(OTA_SERVER_H)
        cls.sector_size = parse_macro_int(cls.flash_cfg_text, "FLASH_SECTOR_SIZE")
        cls.runtime_sectors = parse_macro_int(cls.flash_cfg_text, "FLASH_ADDR_RUNTIME_SECTORS")
        cls.hot_kv_sectors = parse_macro_int(cls.flash_cfg_text, "FLASH_ADDR_RUN_KV_SECTORS")
        cls.event_log_sectors = parse_macro_int(cls.flash_cfg_text, "FLASH_ADDR_LOG_SECTORS")
        cls.cold_kv_sectors = parse_macro_int(cls.bms_cold_text, "BMS_COLD_KV_SECTORS")
        cls.prev_hot_kv_sectors = parse_macro_int(cls.flash_cfg_text, "FLASH_ADDR_PREV_RUN_KV_SECTORS")
        cls.prev_cold_kv_sectors = parse_macro_int(cls.flash_cfg_text, "FLASH_ADDR_PREV_COLD_KV_SECTORS")

    def reserved_512k_profiles(self):
        # type: () -> Dict[str, Dict[str, Tuple[int, int]]]
        smp_addrs = unique_sorted(parse_macro_ints(self.ble_flash_text, "FLASH_ADR_SMP_PAIRING_512K_FLASH"))
        mac_addrs = unique_sorted(parse_macro_ints(self.ble_flash_text, "CFG_ADR_MAC_512K_FLASH"))
        calibration_addrs = unique_sorted(parse_macro_ints(self.ble_flash_text, "CFG_ADR_CALIBRATION_512K_FLASH"))
        self.assertEqual(len(smp_addrs), 2, "expected 2 reserved profiles for 512K flash")
        self.assertEqual(len(mac_addrs), 2, "expected 2 MAC profiles for 512K flash")
        self.assertEqual(len(calibration_addrs), 2, "expected 2 calibration profiles for 512K flash")
        return {
            "825x_827x": {
                "smp": (smp_addrs[0], mac_addrs[0]),
                "mac": (mac_addrs[0], calibration_addrs[0]),
                "calibration": (calibration_addrs[0], 0x80000),
            },
            "tc321x": {
                "smp": (smp_addrs[1], mac_addrs[1]),
                "mac": (mac_addrs[1], calibration_addrs[1]),
                "calibration": (calibration_addrs[1], 0x80000),
            },
        }

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

    def previous_layout_ranges(self, prefix):
        # type: (str) -> Dict[str, Tuple[int, int]]
        text = self.flash_cfg_text
        return {
            "prev_soc_kv": (
                parse_macro_int(text, f"FLASH_ADDR_LAYOUT_{prefix}_RUN_KV_BASE_PREV"),
                range_end(
                    parse_macro_int(text, f"FLASH_ADDR_LAYOUT_{prefix}_RUN_KV_BASE_PREV"),
                    self.prev_hot_kv_sectors,
                    self.sector_size,
                ),
            ),
            "prev_cold_kv": (
                parse_macro_int(text, f"FLASH_ADDR_LAYOUT_{prefix}_SOFT_PROTECT_PREV"),
                range_end(
                    parse_macro_int(text, f"FLASH_ADDR_LAYOUT_{prefix}_SOFT_PROTECT_PREV"),
                    self.prev_cold_kv_sectors,
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

    def assert_no_cross_overlap(self, left, right):
        # type: (Dict[str, Tuple[int, int]], Dict[str, Tuple[int, int]]) -> None
        for left_name, left_range in left.items():
            for right_name, right_range in right.items():
                self.assertFalse(
                    overlaps(left_range, right_range),
                    msg=(
                        f"{left_name} overlaps {right_name}: "
                        f"{format_range(left_range)} vs {format_range(right_range)}"
                    ),
                )

    def test_layout_512k_no_overlap_for_all_reserved_profiles(self):
        ranges = self.layout_ranges("512K")
        self.assert_no_internal_overlap(ranges)
        for profile_name, reserved in self.reserved_512k_profiles().items():
            for app_name, app_range in ranges.items():
                for reserved_name, reserved_range in reserved.items():
                    self.assertFalse(
                        overlaps(app_range, reserved_range),
                        msg=(
                            f"512K/{profile_name} {app_name} overlaps reserved {reserved_name}: "
                            f"{format_range(app_range)} vs {format_range(reserved_range)}"
                        ),
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

    def test_current_layouts_do_not_overlap_migration_source_layouts(self):
        for prefix in ("512K", "1M", "2M"):
            self.assert_no_cross_overlap(self.layout_ranges(prefix), self.previous_layout_ranges(prefix))

    def test_512k_current_layout_leaves_legacy_param_base_unused(self):
        ranges = self.layout_ranges("512K")
        legacy_param_base = parse_macro_int(self.flash_cfg_text, "FLASH_ADDR_SOFT_PROTECT_BASE")
        for name, region in ranges.items():
            self.assertFalse(
                point_in_range(legacy_param_base, region),
                msg=f"legacy PARAM_ADDR base 0x{legacy_param_base:05X} falls inside {name} {format_range(region)}",
            )

    def test_legacy_param_base_sits_in_512k_reserved_top_area(self):
        legacy_param_base = parse_macro_int(self.flash_cfg_text, "FLASH_ADDR_SOFT_PROTECT_BASE")
        b85_reserved = self.reserved_512k_profiles()["825x_827x"]["smp"]
        self.assertGreaterEqual(
            legacy_param_base,
            b85_reserved[0],
            msg="legacy PARAM_ADDR should remain outside the current 512K application layout",
        )

    @unittest.skipUnless(BLE_SAMPLE_BIN.exists(), "825x_ble_sample.bin not found")
    def test_current_825x_ble_sample_bin_fits_default_ota_limit(self):
        size = BLE_SAMPLE_BIN.stat().st_size
        self.assertLessEqual(
            size,
            DEFAULT_OTA_FW_MAX_SIZE,
            msg=(
                f"825x_ble_sample.bin is too large for default OTA limit: "
                f"size=0x{size:X}, max=0x{DEFAULT_OTA_FW_MAX_SIZE:X}"
            ),
        )
        self.assertIn("default maximum firmware size is 124K byte", self.ota_server_text)
        self.assertIn("default OTA new firmware boot address is 0x20000", self.ota_server_text)


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

    def test_legacy_param_area_is_read_only_compatibility_path(self):
        text = read_text(PARAM_C)
        self.assertIn("flash_read_page(PARAM_ADDR, sizeof(PARAM_T), (u8 *)&legacy_param);", text)
        self.assertNotIn("flash_write_page(PARAM_ADDR", text)
        self.assertNotIn("flash_erase_sector(PARAM_ADDR", text)
        self.assertIn("bms_cold_kv_store_set_protect(&g_tParam.protect)", text)


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__])
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    sys.exit(0 if result.wasSuccessful() else 1)
