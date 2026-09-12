#!/usr/bin/env python3
"""Host-only storage and BMS cleanup contract checks.

These tests deliberately avoid depending on TC32. They protect compatibility
boundaries (Flash layout, OTA space, persistence semantics, SOC behavior and
critical failure handling) without pinning the implementation to obsolete
variable names or dead sample functions.
"""

import re
import sys
import unittest
from pathlib import Path
from typing import Dict, Iterable, List, Tuple

REPO_ROOT = Path(__file__).resolve().parents[1]
SDK_DIR = REPO_ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001" / "tc_ble_single_sdk"
VENDOR_DIR = SDK_DIR / "vendor"
MODULE_DIR = VENDOR_DIR / "ble_sample"
COMMON_DIR = VENDOR_DIR / "common"

FLASH_CFG = MODULE_DIR / "flash_store_cfg.h"
BMS_COLD_HDR = MODULE_DIR / "bms_cold_kv_store.h"
BLE_FLASH = COMMON_DIR / "ble_flash.h"
FLASH_SAFE = MODULE_DIR / "flash_store_safe.h"
APP_C = MODULE_DIR / "app.c"
APP_ATT_C = MODULE_DIR / "app_att.c"
MODBUS_RTU_C = MODULE_DIR / "modbus_rtu.c"
RUNTIME_C = MODULE_DIR / "runtime.c"
EVENT_LOG_C = MODULE_DIR / "bms_event_log.c"
PARAM_C = MODULE_DIR / "param.c"
PARAM_H = MODULE_DIR / "param.h"
BTNAME_C = MODULE_DIR / "btname_modbus.c"
BMS_COLD_C = MODULE_DIR / "bms_cold_kv_store.c"
SOC_KV_C = MODULE_DIR / "soc_kv_store.c"
SOC_KV_H = MODULE_DIR / "soc_kv_store.h"
SOC_ENHANCE_C = MODULE_DIR / "SocEnhance.c"
SH367309_C = MODULE_DIR / "sh367309_datadeal.c"
SIF_SEND_C = MODULE_DIR / "sif_send.c"
OTA_SERVER_H = SDK_DIR / "stack" / "ble" / "service" / "ota" / "ota_server.h"
BLE_SAMPLE_BIN = SDK_DIR / "project" / "tlsr_tc32" / "B85" / "825x_ble_sample" / "825x_ble_sample.bin"

DEFAULT_OTA_FW_MAX_SIZE = 124 * 1024


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
        cls.flash_cfg = read_text(FLASH_CFG)
        cls.ble_flash = read_text(BLE_FLASH)
        cls.cold_hdr = read_text(BMS_COLD_HDR)
        cls.ota_server = read_text(OTA_SERVER_H)
        cls.sector_size = parse_macro_int(cls.flash_cfg, "FLASH_SECTOR_SIZE")
        cls.runtime_sectors = parse_macro_int(cls.flash_cfg, "FLASH_ADDR_RUNTIME_SECTORS")
        cls.hot_kv_sectors = parse_macro_int(cls.flash_cfg, "FLASH_ADDR_RUN_KV_SECTORS")
        cls.event_log_sectors = parse_macro_int(cls.flash_cfg, "FLASH_ADDR_LOG_SECTORS")
        cls.cold_kv_sectors = parse_macro_int(cls.cold_hdr, "BMS_COLD_KV_SECTORS")

    def reserved_512k_profiles(self):
        # type: () -> Dict[str, Dict[str, Tuple[int, int]]]
        smp = unique_sorted(parse_macro_ints(self.ble_flash, "FLASH_ADR_SMP_PAIRING_512K_FLASH"))
        mac = unique_sorted(parse_macro_ints(self.ble_flash, "CFG_ADR_MAC_512K_FLASH"))
        cal = unique_sorted(parse_macro_ints(self.ble_flash, "CFG_ADR_CALIBRATION_512K_FLASH"))
        self.assertEqual(len(smp), 2)
        self.assertEqual(len(mac), 2)
        self.assertEqual(len(cal), 2)
        return {
            "825x_827x": {
                "smp": (smp[0], mac[0]),
                "mac": (mac[0], cal[0]),
                "calibration": (cal[0], 0x80000),
            },
            "tc321x": {
                "smp": (smp[1], mac[1]),
                "mac": (mac[1], cal[1]),
                "calibration": (cal[1], 0x80000),
            },
        }

    def layout_ranges(self, prefix):
        # type: (str) -> Dict[str, Tuple[int, int]]
        return {
            "runtime": (
                parse_macro_int(self.flash_cfg, f"FLASH_ADDR_LAYOUT_{prefix}_RUNTIME_BASE"),
                range_end(parse_macro_int(self.flash_cfg, f"FLASH_ADDR_LAYOUT_{prefix}_RUNTIME_BASE"),
                          self.runtime_sectors, self.sector_size),
            ),
            "soc_kv": (
                parse_macro_int(self.flash_cfg, f"FLASH_ADDR_LAYOUT_{prefix}_RUN_KV_BASE"),
                range_end(parse_macro_int(self.flash_cfg, f"FLASH_ADDR_LAYOUT_{prefix}_RUN_KV_BASE"),
                          self.hot_kv_sectors, self.sector_size),
            ),
            "cold_kv": (
                parse_macro_int(self.flash_cfg, f"FLASH_ADDR_LAYOUT_{prefix}_SOFT_PROTECT"),
                range_end(parse_macro_int(self.flash_cfg, f"FLASH_ADDR_LAYOUT_{prefix}_SOFT_PROTECT"),
                          self.cold_kv_sectors, self.sector_size),
            ),
            "event_log": (
                parse_macro_int(self.flash_cfg, f"FLASH_ADDR_LAYOUT_{prefix}_LOG_BASE"),
                range_end(parse_macro_int(self.flash_cfg, f"FLASH_ADDR_LAYOUT_{prefix}_LOG_BASE"),
                          self.event_log_sectors, self.sector_size),
            ),
        }

    def assert_no_internal_overlap(self, ranges):
        items = list(ranges.items())
        for idx, (name_a, range_a) in enumerate(items):
            for name_b, range_b in items[idx + 1:]:
                self.assertFalse(overlaps(range_a, range_b),
                                 f"{name_a} overlaps {name_b}: {range_a} vs {range_b}")

    def test_512k_layout_does_not_overlap_reserved_profiles(self):
        ranges = self.layout_ranges("512K")
        self.assert_no_internal_overlap(ranges)
        for profile, reserved in self.reserved_512k_profiles().items():
            for app_name, app_range in ranges.items():
                for reserved_name, reserved_range in reserved.items():
                    self.assertFalse(
                        overlaps(app_range, reserved_range),
                        f"512K/{profile} {app_name} overlaps {reserved_name}: "
                        f"{format_range(app_range)} vs {format_range(reserved_range)}",
                    )

    def test_1m_and_2m_layouts_do_not_overlap_reserved_top_area(self):
        for prefix, smp_macro, end in (
            ("1M", "FLASH_ADR_SMP_PAIRING_1M_FLASH", 0x100000),
            ("2M", "FLASH_ADR_SMP_PAIRING_2M_FLASH", 0x200000),
        ):
            ranges = self.layout_ranges(prefix)
            self.assert_no_internal_overlap(ranges)
            reserved = (parse_macro_int(self.ble_flash, smp_macro), end)
            for name, region in ranges.items():
                self.assertFalse(overlaps(region, reserved), f"{prefix} {name} overlaps reserved")

    def test_legacy_btname_area_remains_outside_active_layout(self):
        for prefix in ("512K", "1M", "2M"):
            base = parse_macro_int(self.flash_cfg, f"FLASH_ADDR_LAYOUT_{prefix}_BTNAME_BASE")
            legacy = (base, base + self.sector_size)
            for name, region in self.layout_ranges(prefix).items():
                self.assertFalse(overlaps(legacy, region), f"{prefix} btname overlaps {name}")

    def test_legacy_param_base_is_not_reused(self):
        legacy = parse_macro_int(self.flash_cfg, "FLASH_ADDR_SOFT_PROTECT_BASE")
        for name, region in self.layout_ranges("512K").items():
            self.assertFalse(point_in_range(legacy, region), f"legacy param base reused by {name}")
        self.assertGreaterEqual(legacy, self.reserved_512k_profiles()["825x_827x"]["smp"][0])

    def test_incompatible_ota_layouts_are_explicitly_rejected(self):
        self.assertIn("MULTI_BOOT_ADDR_0x20000", self.flash_cfg)
        self.assertIn("MULTI_BOOT_ADDR_0x40000", self.flash_cfg)
        self.assertIn("MULTI_BOOT_ADDR_0x80000", self.flash_cfg)
        self.assertIn("blc_flash_capacity == FLASH_SIZE_1M", self.flash_cfg)

    @unittest.skipUnless(BLE_SAMPLE_BIN.exists(), "825x_ble_sample.bin not found")
    def test_current_bin_fits_telink_default_ota_window(self):
        self.assertLessEqual(BLE_SAMPLE_BIN.stat().st_size, DEFAULT_OTA_FW_MAX_SIZE)
        self.assertIn("default maximum firmware size is 124K byte", self.ota_server)
        self.assertIn("default OTA new firmware boot address is 0x20000", self.ota_server)


class SourceContractTests(unittest.TestCase):
    def test_flash_cfg_has_no_previous_layout_restore_helpers(self):
        text = read_text(FLASH_CFG)
        for symbol in (
            "flash_store_cfg_get_previous_soc_kv_base",
            "flash_store_cfg_get_previous_cold_kv_base",
            "flash_store_cfg_get_legacy_runtime_base",
            "flash_store_cfg_get_legacy_bt_name_base",
            "flash_store_cfg_get_legacy_soc_kv_base",
            "FLASH_ADDR_LAYOUT_512K_RUN_KV_BASE_PREV",
            "FLASH_ADDR_LAYOUT_512K_SOFT_PROTECT_PREV",
        ):
            self.assertNotIn(symbol, text)

    def test_flash_helper_respects_stack_session(self):
        text = read_text(FLASH_SAFE)
        self.assertIn("app_flash_lock_restore_enabled()", text)

        app = read_text(APP_C)
        self.assertRegex(app, r"s_flash_stack_session_active\s*=\s*1u\s*;")
        self.assertRegex(app, r"s_flash_stack_session_active\s*=\s*0u\s*;")
        self.assertRegex(app, r"int\s+app_flash_lock_restore_enabled\s*\(void\)")

    def test_runtime_counts_awake_time_only(self):
        text = read_text(RUNTIME_C)
        self.assertNotIn("RUNTIME_SLEEP_TICK", text)
        self.assertNotIn("analog_write", text)
        self.assertNotIn("analog_read", text)
        self.assertIn("Aging runtime counts awake BMS execution only", text)

    def test_runtime_does_not_complete_factory_when_layout_unavailable(self):
        text = read_text(RUNTIME_C)
        self.assertRegex(text, r"if\s*\(runtime_flash_base\(\)\s*==\s*0u\)")
        self.assertNotRegex(text, r"runtime_flash_base\(\).*FACTORY_TIME_LIMIT_MIN")

    def test_runtime_saves_only_when_store_is_ready(self):
        text = read_text(RUNTIME_C)
        self.assertRegex(text, r"g_runtime_store_ready\s*&&[\s\S]{0,120}RUNTIME_SAVE_INTERVAL_MIN")

    def test_modbus_factory_command_uses_runtime_reset_api(self):
        text = read_text(MODBUS_RTU_C)
        self.assertIn('#include "runtime.h"', text)
        self.assertIn("Runtime_ReenterFactoryMode()", text)

    def test_event_log_reports_store_errors_and_latches_only_after_append(self):
        text = read_text(EVENT_LOG_C)
        self.assertIn("bms_event_log_report_store_error", text)
        self.assertIn("BMS_EVENT_LOG_INVALID_SLOT", text)
        self.assertRegex(text, r"if\s*\(bms_event_log_append\(event,\s*0\)\)")
        self.assertIn("event_latched[event] = 1u", text)

    def test_upgrade_epochs_are_marked_only_inside_success_branches(self):
        text = read_text(PARAM_C)
        apply_names = (
            "protect", "system", "soc", "event_log", "runtime",
        )
        for name in apply_names:
            match = re.search(
                rf"if\s*\(param_upgrade_apply_default_{name}\(\)\)\s*\{{([\s\S]*?)\n\s*\}}\s*else",
                text,
            )
            self.assertIsNotNone(match, f"missing success branch for {name}")
            self.assertIn("param_upgrade_mark_epoch", match.group(1))

    def test_parameter_storage_has_only_cold_kv_path(self):
        c = read_text(PARAM_C)
        h = read_text(PARAM_H)
        self.assertNotIn("PARAM_ADDR", c)
        self.assertNotIn("PARAM_SAVE_TO_EEPROM", h)
        self.assertNotIn("PARAM_SAVE_TO_FLASH", h)
        self.assertIn("bms_cold_kv_store_get_protect", c)
        self.assertIn("bms_cold_kv_store_set_protect", c)

    def test_soc_kv_is_immediate_and_defaults_are_zero_dsg_cycle(self):
        c = read_text(SOC_KV_C)
        h = read_text(SOC_KV_H)
        self.assertNotIn("SOC_KV_FLUSH_INTERVAL_US", c)
        self.assertIn("flash_kv32_write_pairs", c)
        self.assertIn("#define SOC_PARAM_DEFAULT_DSG    0u", h)
        self.assertIn("#define SOC_PARAM_DEFAULT_CYCLE  0u", h)

    def test_soc_cycle_soh_and_capacity_recalculation_contract(self):
        text = read_text(SOC_ENHANCE_C)
        self.assertIn("#define SOC_EQUIV_CYCLE_PERCENT", text)
        self.assertIn("while (dsg_acc >= SOC_EQUIV_CYCLE_PERCENT)", text)
        self.assertIn("soc_recalc_full_capacity();", text)
        self.assertIn("soc_recalc_now_capacity();", text)
        self.assertIn("SOC_Calculate_Element.soh = bms_soh_from_cycle", text)

    def test_soc_display_remains_separate_from_real_soc(self):
        text = read_text(SOC_ENHANCE_C)
        self.assertIn("static uint8_t g_soc_display_soc", text)
        self.assertIn("soc_display_follow_real", text)
        self.assertIn("g_stCellInfoReport.SocElement.u16Soc = get_soc_display();", text)
        self.assertNotIn("g_stCellInfoReport.SocElement.u16Soc = get_soc_real();", text)

    def test_soc_idle_ocv_is_deferred_and_never_directly_jumps_up(self):
        text = read_text(SOC_ENHANCE_C)
        self.assertIn("deferred_ocv_target", text)
        self.assertIn("soc_deferred_ocv_set_target", text)
        self.assertIn("soc_apply_deferred_ocv_step", text)
        self.assertIn("SOC_OCV_IDLE_MIN_STABLE_TICKS", text)
        self.assertNotIn("SOC_DEFERRED_OCV_ACTIVE_STEP_TICKS", text)

    def test_soc_terminal_discharge_uses_rule_table(self):
        text = read_text(SOC_ENHANCE_C)
        self.assertIn("g_soc_dsg_terminal_rules", text)
        self.assertIn("soc_discharge_terminal_lookup", text)
        self.assertIn("soc_discharge_terminal_step_ticks", text)
        self.assertIn("soc_apply_real_value(0u, 1u);", text)

    def test_soc_sag_hold_blocks_aggressive_voltage_correction(self):
        text = read_text(SOC_ENHANCE_C)
        self.assertIn("SOC_DSG_SAG_HOLDOFF_TICKS", text)
        self.assertIn("SOC_DSG_REBOUND_STABLE_TICKS", text)
        self.assertIn("soc_discharge_sag_hold_active", text)
        self.assertIn("soc_discharge_rebound_stable_update", text)

    def test_afe_read_failure_freezes_current_and_preserves_soc_report(self):
        text = read_text(SH367309_C)
        self.assertIn("DataLoad_ClearCurrent();", text)
        self.assertIn("DataLoad_ClearAfeReportPreserveSoc();", text)
        self.assertNotIn("memset(&g_stCellInfoReport, 0, sizeof(g_stCellInfoReport) - 6);", text)

    def test_legacy_current_conversion_formula_is_unchanged(self):
        text = read_text(SH367309_C)
        self.assertIn("g_u32CS_Res_AFE / (21470)", text)
        self.assertIn("* g_u32CS_Res_AFE / (21470) * 200", text)

    def test_sif_keeps_raw_profile_capacity_value(self):
        text = read_text(SIF_SEND_C)
        self.assertRegex(text, r"capacity_factory\s*=\s*CapacityFactory\s*;")
        self.assertNotRegex(text, r"capacity_factory\s*=.*SocElement\.u16CapacityFactory")

    def test_sif_timer_is_owned_by_sif_module(self):
        sif = read_text(SIF_SEND_C)
        app = read_text(APP_C)
        self.assertIn("void sif_timer_init(void)", sif)
        self.assertIn("void sif_timer_irq_proc(void)", sif)
        self.assertNotIn("app_timer_test_init", app)
        self.assertNotIn("app_timer_test_irq_proc", app)

    def test_gatt_no_longer_contains_inactive_hid_sample(self):
        text = read_text(APP_ATT_C)
        self.assertNotIn("reportMap", text)
        self.assertNotIn("my_hidServiceUUID", text)
        self.assertNotIn("HID_REPORT_ID_KEYBOARD_INPUT", text)
        self.assertIn("SPP_PS_H", text)
        self.assertIn("OTA_PS_H", read_text(MODULE_DIR / "app_att.h"))

    def test_btname_uses_cold_kv_without_direct_flash_write(self):
        text = read_text(BTNAME_C)
        self.assertIn("bms_cold_kv_store_get_bt_name_suffix", text)
        self.assertIn("bms_cold_kv_store_set_bt_name_suffix", text)
        self.assertNotIn("flash_store_prog_checked", text)
        self.assertNotIn("flash_store_erase_sector_checked", text)
        self.assertNotIn("sh367309_datadeal.h", text)

    def test_hot_and_cold_kv_do_not_reference_previous_layouts(self):
        self.assertNotIn("flash_store_cfg_get_previous_soc_kv_base", read_text(SOC_KV_C))
        self.assertNotIn("flash_store_cfg_get_previous_cold_kv_base", read_text(BMS_COLD_C))


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__])
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    sys.exit(0 if result.wasSuccessful() else 1)
