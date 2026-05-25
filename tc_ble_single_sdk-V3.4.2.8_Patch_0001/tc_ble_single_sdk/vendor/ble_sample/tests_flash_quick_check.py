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
MODBUS_RTU_C = MODULE_DIR / "modbus_rtu.c"
RUNTIME_C = MODULE_DIR / "runtime.c"
EVENT_LOG_C = MODULE_DIR / "bms_event_log.c"
PARAM_C = MODULE_DIR / "param.c"
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

    def legacy_btname_range(self, prefix):
        # type: (str) -> Tuple[int, int]
        text = self.flash_cfg_text
        base = parse_macro_int(text, f"FLASH_ADDR_LAYOUT_{prefix}_BTNAME_BASE")
        return (base, range_end(base, 1, self.sector_size))

    def assert_no_internal_overlap(self, ranges):
        # type: (Dict[str, Tuple[int, int]]) -> None
        items = list(ranges.items())
        for idx, (name_a, range_a) in enumerate(items):
            for name_b, range_b in items[idx + 1 :]:
                self.assertFalse(
                    overlaps(range_a, range_b),
                    msg=f"{name_a} overlaps {name_b}: {range_a} vs {range_b}",
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

    def test_legacy_btname_area_stays_outside_active_layout(self):
        for prefix in ("512K", "1M", "2M"):
            legacy_range = self.legacy_btname_range(prefix)
            for name, region in self.layout_ranges(prefix).items():
                self.assertFalse(
                    overlaps(legacy_range, region),
                    msg=(
                        f"{prefix} legacy btname overlaps active {name}: "
                        f"{format_range(legacy_range)} vs {format_range(region)}"
                    ),
                )

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
    def test_flash_cfg_no_longer_exports_migration_helpers(self):
        text = read_text(FLASH_CFG)
        self.assertNotIn("flash_store_cfg_get_previous_soc_kv_base", text)
        self.assertNotIn("flash_store_cfg_get_previous_cold_kv_base", text)
        self.assertNotIn("flash_store_cfg_get_legacy_runtime_base", text)
        self.assertNotIn("flash_store_cfg_get_legacy_bt_name_base", text)
        self.assertNotIn("flash_store_cfg_get_legacy_soc_kv_base", text)
        self.assertNotIn("FLASH_ADDR_LAYOUT_512K_RUN_KV_BASE_PREV", text)
        self.assertNotIn("FLASH_ADDR_LAYOUT_512K_SOFT_PROTECT_PREV", text)

    def test_flash_helper_does_not_restore_lock_during_stack_session(self):
        text = read_text(FLASH_SAFE)
        self.assertIn("app_flash_lock_restore_enabled()", text)
        self.assertNotIn("flash_lock(flash_lockBlock_cmd);\n#endif", text)

    def test_app_tracks_stack_flash_session(self):
        text = read_text(APP_C)
        self.assertIn("g_app_flash_stack_session_active = 1u;", text)
        self.assertIn("g_app_flash_stack_session_active = 0u;", text)

    def test_runtime_starts_clean_without_legacy_restore(self):
        text = read_text(RUNTIME_C)
        self.assertNotIn("flash_store_cfg_get_legacy_runtime_base()", text)
        self.assertNotIn("runtime_load_legacy_value", text)

    def test_runtime_counts_awake_time_without_deepsleep_compensation(self):
        text = read_text(RUNTIME_C)
        self.assertNotIn("RUNTIME_SLEEP_TICK", text)
        self.assertNotIn("runtime_sleep_tick", text)
        self.assertNotIn("analog_write", text)
        self.assertNotIn("analog_read", text)
        self.assertIn("Aging runtime counts awake BMS execution only", text)

    def test_factory_mode_disables_rtc_low_power_path(self):
        text = read_text(APP_C)
        self.assertIn("MODE_FACTORY == Runtime_GetMode()", text)
        self.assertIn("quit_rtc_mode();", text)
        self.assertIn("enter_rtc_mode();", text)

    def test_modbus_factory_command_resets_runtime(self):
        text = read_text(MODBUS_RTU_C)
        self.assertIn("#include \"runtime.h\"", text)
        self.assertIn("Runtime_ReenterFactoryMode()", text)
        self.assertNotIn("if (val == 0x03)\n            enter_fac_mode(true);", text)

    def test_runtime_does_not_complete_factory_when_layout_unavailable(self):
        text = read_text(RUNTIME_C)
        self.assertNotIn(
            "if (runtime_flash_base() == 0u) {\n            g_runtime_min = FACTORY_TIME_LIMIT_MIN;",
            text,
        )
        self.assertIn("if (runtime_flash_base() == 0u) {", text)
        self.assertIn("g_runtime_last_tick_32k = pm_get_32k_tick();", text)

    def test_runtime_saves_only_when_store_ready(self):
        text = read_text(RUNTIME_C)
        self.assertIn(
            "if (g_runtime_store_ready &&\n        ((g_runtime_min - g_runtime_last_saved_min) >= RUNTIME_SAVE_INTERVAL_MIN))",
            text,
        )

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

    def test_soc_kv_flushes_immediately_on_any_value_change(self):
        text = read_text(MODULE_DIR / "soc_kv_store.c")
        self.assertNotIn("SOC_KV_FLUSH_INTERVAL_US", text)
        self.assertNotIn("g_soc_last_flush_tick", text)
        self.assertIn("(void)soc_kv_store_write_all(soc, dsg, cycle);", text)

    def test_soc_defaults_start_from_zero_discharge_and_cycle(self):
        text = read_text(SOC_KV_H)
        self.assertIn("#define SOC_PARAM_DEFAULT_DSG    0u", text)
        self.assertIn("#define SOC_PARAM_DEFAULT_CYCLE  0u", text)

    def test_soc_cycle_recalcs_capacity_after_soh_change(self):
        text = read_text(SOC_ENHANCE_C)
        self.assertIn("#define SOC_EQUIV_CYCLE_PERCENT         100u", text)
        self.assertIn("while (dsg_acc >= SOC_EQUIV_CYCLE_PERCENT)", text)
        self.assertIn("cycle_changed = 1u;", text)
        self.assertIn("soc_recalc_full_capacity();", text)
        self.assertIn("soc_recalc_now_capacity();", text)
        self.assertIn("SOC_Calculate_Element.soh = bms_soh_from_cycle", text)
        self.assertNotIn("SOC_Calculate_Element.soh = bms_soh_from_capacity", text)

    def test_soc_full_sync_uses_voltage_only_with_slow_hold(self):
        text = read_text(SOC_ENHANCE_C)
        self.assertIn("#define SOC_FULL_LOCK_TICKS             (5u * 60u)", text)
        self.assertIn("#define SOC_FULL_SYNC_STEP_TICKS        10u", text)
        self.assertIn("if ((VCELLMAX >= SOC_100_VAL) && (VCELLMIN >= SOC_FULL_SYNC_MIN_MV))", text)
        self.assertNotIn("isCHG() && (VCELLMAX >= SOC_100_VAL)", text)

    def test_soc_reports_display_soc_not_real_soc_directly(self):
        text = read_text(SOC_ENHANCE_C)
        self.assertIn("static uint8_t g_soc_display_soc", text)
        self.assertIn("#define SOC_DISPLAY_STEP_TICKS          5u", text)
        self.assertIn("static void soc_display_follow_real(void)", text)
        self.assertIn("g_stCellInfoReport.SocElement.u16Soc = get_soc_display();", text)
        self.assertIn("soc_display_capacity_now() / SOC_REPORT_CAPACITY_DIVISOR", text)
        self.assertNotIn("g_stCellInfoReport.SocElement.u16Soc = get_soc_real();", text)

    def test_soc_idle_ocv_uses_deferred_target(self):
        text = read_text(SOC_ENHANCE_C)
        self.assertIn("deferred_ocv_target", text)
        self.assertIn("deferred_ocv_valid", text)
        self.assertIn("#define SOC_OCV_IDLE_MIN_STABLE_TICKS   (5u * 30u)", text)
        self.assertIn("#define SOC_OCV_IDLE_SLOPE_MAX_MV       8u", text)
        self.assertIn("#define SOC_OCV_CONFIDENCE_TARGET       80u", text)
        self.assertIn("static uint8_t soc_idle_ocv_confidence_ready(void)", text)
        self.assertIn("diff = soc_abs_diff_u16(mv, g_soc_strategy_state.idle_ocv_last_mv);", text)
        self.assertIn("if (diff > SOC_OCV_IDLE_SLOPE_MAX_MV)", text)
        self.assertIn("#define SOC_OCV_IDLE_TARGET_REFRESH_TICKS (5u * 60u)", text)
        self.assertIn("#define SOC_IDLE_STATIC_DOWN_DIFF_THRESHOLD 10u", text)
        self.assertIn("#define SOC_LONG_REST_DOWN_STEP_TICKS", text)
        self.assertIn("static uint8_t soc_apply_idle_deferred_down_step(void)", text)
        self.assertIn("if (diff < SOC_IDLE_STATIC_DOWN_DIFF_THRESHOLD)", text)
        self.assertIn("soc_deferred_ocv_set_target(ocv_soc);", text)
        self.assertIn("if (soc_apply_deferred_ocv_step())", text)
        self.assertIn("if (soc_apply_idle_deferred_down_step())", text)
        self.assertLess(
            text.index("if (soc_apply_idle_deferred_down_step())"),
            text.index("if (g_soc_strategy_state.deferred_ocv_valid)"),
        )
        self.assertNotIn("if ((diff < SOC_OCV_RUNTIME_DIFF_THRESHOLD) || (ocv_soc >= current_soc))", text)
        self.assertNotIn("SOC_DEFERRED_OCV_ACTIVE_STEP_TICKS", text)
        self.assertNotIn("SOC_OCV_IDLE_ADJUST_TICKS", text)

    def test_soc_discharge_terminal_uses_table_rules(self):
        text = read_text(SOC_ENHANCE_C)
        self.assertIn("typedef struct\n{\n\tuint16_t max_mv;\n\tuint8_t target_soc;\n\tuint8_t step_ticks[SOC_DSG_TERMINAL_BAND_COUNT];", text)
        self.assertIn("static const soc_dsg_terminal_rule_t g_soc_dsg_terminal_rules[]", text)
        self.assertIn("static uint8_t soc_discharge_terminal_lookup", text)
        self.assertIn("static uint16_t soc_discharge_terminal_step_ticks", text)
        self.assertIn("if (!soc_discharge_terminal_lookup(&target_soc, &step_ticks, &sag_hold_blocks))", text)
        self.assertIn("if (current_soc <= target_soc)\n\t{\n\t\tg_soc_strategy_state.dsg_terminal_adjust_ticks = 0u;\n\t\tg_soc_strategy_state.dsg_empty_lock_ticks = 0u;", text)
        self.assertIn("soc_apply_real_value(0u, 1u);", text)
        self.assertNotIn("static uint8_t soc_discharge_terminal_soc_ceiling", text)

    def test_soc_discharge_correction_is_capacity_adaptive(self):
        text = read_text(SOC_ENHANCE_C)
        self.assertIn("#define SOC_TICKS_PER_SECOND", text)
        self.assertIn("#define SOC_DSG_CORR_STEP_MIN_TICKS", text)
        self.assertIn("#define SOC_DSG_CORR_STEP_MAX_TICKS", text)
        self.assertIn("static uint16_t soc_discharge_natural_1pct_ticks(uint16_t dsg_current)", text)
        self.assertIn("static uint16_t soc_discharge_gap_correction_step_ticks", text)
        self.assertIn("factory_a10 = (uint16_t)CapacityFactory;", text)
        self.assertIn("1% time(s) = 36 * CapacityFactory / IDSG", text)
        self.assertIn("step_ticks = soc_discharge_gap_correction_step_ticks(current_soc,", text)
        self.assertIn("adjust_limit = soc_discharge_gap_correction_step_ticks(current_soc, ocv_soc);", text)

    def test_soc_discharge_sag_hold_blocks_voltage_down_correction(self):
        text = read_text(SOC_ENHANCE_C)
        self.assertIn("#define SOC_DSG_SAG_HOLD_CURR_MIN       SOC_DSG_OCV_MID_CURR_MAX", text)
        self.assertIn("#define SOC_DSG_SAG_HOLDOFF_TICKS       (5u * 30u)", text)
        self.assertIn("#define SOC_DSG_SAG_HOLDOFF_HIGH_TICKS  (5u * 60u)", text)
        self.assertIn("#define SOC_DSG_SAG_HOLDOFF_VHIGH_TICKS (5u * 90u)", text)
        self.assertIn("#define SOC_DSG_REBOUND_STABLE_TICKS    (5u * 10u)", text)
        self.assertIn("static uint16_t soc_discharge_sag_hold_ticks_for_current(uint16_t dsg_current)", text)
        self.assertIn("static uint8_t soc_discharge_rebound_stable_update(void)", text)
        self.assertIn("if (diff <= SOC_DSG_REBOUND_SLOPE_MAX_MV)", text)
        self.assertIn("soc_update_discharge_sag_hold();", text)
        self.assertIn("if (soc_discharge_sag_hold_active())", text)
        self.assertIn("if (sag_hold_blocks && (target_soc > 1u) && soc_discharge_sag_hold_active())", text)

    def test_afe_read_failure_freezes_soc_current_and_preserves_soc_report(self):
        text = read_text(SH367309_C)
        self.assertIn("DataLoad_ClearCurrent();", text)
        self.assertIn("DataLoad_ClearAfeReportPreserveSoc();", text)
        self.assertNotIn("memset(&g_stCellInfoReport, 0, sizeof(g_stCellInfoReport) - 6);", text)

    def test_current_conversion_uses_shared_rounded_64bit_formula(self):
        text = read_text(SH367309_C)
        self.assertIn("static UINT32 DataLoad_CurrentRawToScaled_mA(UINT32 raw)", text)
        self.assertIn("(uint64_t)raw * 200u * (uint64_t)g_u32CS_Res_AFE", text)
        self.assertIn("u32_ChgCur_mA = DataLoad_CurrentRawToScaled_mA", text)
        self.assertIn("u32_DsgCur_mA = DataLoad_CurrentRawToScaled_mA", text)

    def test_sif_reports_capacity_as_raw_profile_value(self):
        text = read_text(SIF_SEND_C)
        self.assertIn("sif_report.public.CAPACITYFACTORY = CapacityFactory;", text)
        self.assertNotIn("sif_report.public.CAPACITYFACTORY = g_stCellInfoReport.SocElement.u16CapacityFactory;", text)

    def test_runtime_factory_reset_api_exists(self):
        text = read_text(RUNTIME_C)
        self.assertIn("int Runtime_FactoryReset(void)", text)
        self.assertIn("int Runtime_ReenterFactoryMode(void)", text)
        self.assertIn("enter_fac_mode(true);", text)

    def test_param_loads_only_from_cold_kv(self):
        text = read_text(PARAM_C)
        self.assertNotIn("flash_read_page(PARAM_ADDR", text)
        self.assertIn("bms_cold_kv_store_set_protect(&g_tParam.protect)", text)

    def test_btname_uses_cold_kv_store_only(self):
        text = read_text(BTNAME_C)
        self.assertIn("bms_cold_kv_store_get_bt_name_suffix", text)
        self.assertIn("bms_cold_kv_store_set_bt_name_suffix", text)
        self.assertNotIn("flash_store_prog_checked", text)
        self.assertNotIn("flash_store_erase_sector_checked", text)

    def test_hot_and_cold_kv_no_longer_reference_previous_layouts(self):
        self.assertNotIn("flash_store_cfg_get_previous_soc_kv_base()", read_text(SOC_KV_C))
        self.assertNotIn("flash_store_cfg_get_previous_cold_kv_base()", read_text(BMS_COLD_C))


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__])
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    sys.exit(0 if result.wasSuccessful() else 1)
