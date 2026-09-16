#!/usr/bin/env python3
"""Storage V1 layout, architecture and power-loss contracts."""

import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001" / "tc_ble_single_sdk"
MOD = SDK / "vendor" / "ble_sample"
COMMON = SDK / "vendor" / "common"

FLASH_CFG = MOD / "flash_store_cfg.h"
FLASH_SAFE = MOD / "flash_store_safe.h"
PORT_H = MOD / "storage_port.h"
RECORD_H = MOD / "storage_record.h"
RECORD_C = MOD / "storage_record.c"
PLATFORM_H = MOD / "bms_storage_platform.h"
PLATFORM_C = MOD / "bms_storage_platform_telink.c"
CONFIG_C = MOD / "bms_config_store.c"
STATE_C = MOD / "bms_state_store.c"
STATE_H = MOD / "bms_state_store.h"
EVENT_C = MOD / "bms_event_log.c"
RUNTIME_C = MOD / "runtime.c"
APP_C = MOD / "app.c"
SOURCE_ORDER = ROOT / "bms_tools" / "source_order.txt"
HOST_TEST = ROOT / "tests" / "storage_record_host_test.c"
BLE_FLASH = COMMON / "ble_flash.h"


def text(path):
    return path.read_text(encoding="utf-8", errors="ignore")


def macro_values(src, name):
    pat = re.compile(rf"^\s*#define\s+{re.escape(name)}\s+\(?(0x[0-9A-Fa-f]+|[0-9]+)u?\)?", re.M)
    values = [int(x, 0) for x in pat.findall(src)]
    if not values:
        raise AssertionError(f"missing macro {name}")
    return values


def macro(src, name):
    return macro_values(src, name)[0]


def overlaps(a, b):
    return max(a[0], b[0]) < min(a[1], b[1])


class LayoutTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cfg = text(FLASH_CFG)
        cls.ble = text(BLE_FLASH)
        cls.sector = macro(cls.cfg, "FLASH_SECTOR_SIZE")

    def regions(self, capacity):
        out = {}
        for domain in ("EVENT", "STATE", "CONFIG", "FACTORY"):
            base = macro(self.cfg, f"FLASH_ADDR_LAYOUT_{capacity}_{domain}_BASE")
            count = macro(self.cfg, f"FLASH_ADDR_{domain}_SECTORS")
            out[domain] = (base, base + count * self.sector)
        return out

    def test_every_domain_has_power_loss_rotation_room(self):
        for domain in ("EVENT", "STATE", "CONFIG", "FACTORY"):
            self.assertGreaterEqual(macro(self.cfg, f"FLASH_ADDR_{domain}_SECTORS"), 2)

    def test_512k_layout_is_the_reviewed_storage_v1_map(self):
        r = self.regions("512K")
        self.assertEqual(r["EVENT"], (0x40000, 0x48000))
        self.assertEqual(r["STATE"], (0x53000, 0x5B000))
        self.assertEqual(r["CONFIG"], (0x5B000, 0x5F000))
        self.assertEqual(r["FACTORY"], (0x5F000, 0x61000))

    def test_domains_do_not_overlap(self):
        for capacity in ("512K", "1M", "2M"):
            items = list(self.regions(capacity).items())
            for i, (na, ra) in enumerate(items):
                for nb, rb in items[i + 1:]:
                    self.assertFalse(overlaps(ra, rb), f"{capacity}: {na} overlaps {nb}")

    def test_domains_end_before_sdk_pairing_identity_area(self):
        checks = (
            ("512K", "FLASH_ADR_SMP_PAIRING_512K_FLASH"),
            ("1M", "FLASH_ADR_SMP_PAIRING_1M_FLASH"),
            ("2M", "FLASH_ADR_SMP_PAIRING_2M_FLASH"),
        )
        for capacity, pairing_macro in checks:
            highest_end = max(end for _, end in self.regions(capacity).values())
            pairing_start = min(macro_values(self.ble, pairing_macro))
            self.assertLessEqual(highest_end, pairing_start)

    def test_ota_guards_are_still_explicit(self):
        self.assertIn("MULTI_BOOT_ADDR_0x20000", self.cfg)
        self.assertIn("MULTI_BOOT_ADDR_0x80000", self.cfg)
        self.assertIn("flash_store_cfg_layout_supported", self.cfg)


class ArchitectureTests(unittest.TestCase):
    def test_record_core_is_platform_independent(self):
        core = text(RECORD_C) + text(RECORD_H) + text(PORT_H)
        for token in ("tl_common.h", "drivers.h", "stm32", "flash_read_page",
                      "flash_write_page", "flash_erase_sector", "blc_", "pm_get_32k_tick"):
            self.assertNotIn(token, core)
        self.assertNotIn("malloc", core)
        self.assertNotIn("free(", core)

    def test_record_core_has_crc_sequence_and_commit_last(self):
        rec = text(RECORD_C)
        for token in ("crc_update", "sequence_newer", "OFF_COMMIT0", "OFF_COMMIT1", "prepare_target"):
            self.assertIn(token, rec)
        header = rec.index("program_bytes(s, addr, header, OFF_COMMIT0)")
        payload = rec.index("program_bytes(s, addr + OFF_PAYLOAD, payload")
        commit = rec.index("program_bytes(s, addr + OFF_COMMIT0, commit")
        self.assertLess(header, payload)
        self.assertLess(payload, commit)

    def test_telink_flash_access_is_confined_to_platform_adapter(self):
        plat = text(PLATFORM_C)
        self.assertIn('#include "drivers.h"', plat)
        for token in ("flash_read_page", "flash_write_page", "flash_erase_sector",
                      "flash_store_begin_modify", "flash_store_end_modify"):
            self.assertIn(token, plat)
        porth = text(PLATFORM_H)
        self.assertIn("BMS_STORAGE_DOMAIN_CONFIG", porth)
        self.assertIn("BMS_STORAGE_DOMAIN_STATE", porth)
        self.assertIn("BMS_STORAGE_DOMAIN_FACTORY", porth)
        self.assertIn("BMS_STORAGE_DOMAIN_EVENT", porth)

    def test_semantic_stores_share_record_engine(self):
        for path in (CONFIG_C, STATE_C, EVENT_C):
            src = text(path)
            self.assertIn("storage_record_", src, path.name)
            for raw in ("flash_kv32", "flash_read_page", "flash_write_page", "flash_erase_sector"):
                self.assertNotIn(raw, src, path.name)

    def test_config_and_state_have_explicit_little_endian_formats(self):
        cfg = text(CONFIG_C)
        state = text(STATE_C)
        for token in ("bms_config_put_u16le", "bms_config_put_u32le",
                      "bms_config_get_u16le", "bms_config_get_u32le"):
            self.assertIn(token, cfg)
        self.assertIn("bms_state_put_u32le", state)
        self.assertIn("bms_state_get_u32le", state)

    def test_runtime_is_part_of_state_not_a_second_flash_engine(self):
        run = text(RUNTIME_C)
        self.assertIn("bms_state_store_get_runtime_min", run)
        self.assertIn("bms_state_store_write_runtime_min", run)
        self.assertNotIn("flash_read_page", run)
        self.assertNotIn("runtime_crc", run)
        self.assertIn("Aging runtime counts awake BMS execution only", run)

    def test_state_keeps_changed_value_write_semantics(self):
        state = text(STATE_C)
        self.assertIn("memcmp(&g_bms_state, next, sizeof(*next)) == 0", state)
        self.assertIn("soc_kv_store_update_and_log_if_changed", text(APP_C))
        compat = text(MOD / "soc_kv_store.h")
        self.assertIn("bms_state_store_update_and_log_if_changed", compat)
        self.assertIn("runtime_min", state)
        self.assertIn("BMS_STATE_DEFAULT_DSG    0u", text(STATE_H))

    def test_event_latch_is_only_advanced_after_successful_persist(self):
        event = text(EVENT_C)
        self.assertIn("bms_error_raise(BMS_ERROR_EEPROM_STORE)", event)
        self.assertIn("!g_bms_event_log.event_latched[event] && bms_event_log_append(event, 0)", event)
        self.assertIn("g_bms_event_log.event_latched[event] = 1u", event)

    def test_old_kv_engines_are_out_of_build_and_removed(self):
        order = text(SOURCE_ORDER)
        for path in ("bms_config_store.c", "bms_state_store.c", "bms_storage_platform_telink.c", "storage_record.c"):
            self.assertIn(f"vendor/ble_sample/{path}", order)
        for path in ("bms_cold_kv_store.c", "soc_kv_store.c", "flash_kv32.c"):
            self.assertNotIn(f"vendor/ble_sample/{path}", order)
        self.assertFalse((MOD / "flash_kv32.c").exists())
        self.assertFalse((MOD / "flash_kv32.h").exists())
        self.assertFalse((MOD / "bms_cold_kv_store.c").exists())
        self.assertFalse((MOD / "soc_kv_store.c").exists())

    def test_flash_protection_session_contract_remains(self):
        self.assertIn("app_flash_lock_restore_enabled()", text(FLASH_SAFE))
        app = text(APP_C)
        self.assertIn("g_app_flash_stack_session_active = 1u;", app)
        self.assertIn("g_app_flash_stack_session_active = 0u;", app)


class HostPowerLossTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which("cc"), "host C compiler not found")
    def test_portable_record_engine(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "storage_record_host_test"
            subprocess.run([
                shutil.which("cc"), "-std=c99", "-Wall", "-Wextra", "-Werror",
                f"-I{MOD}", str(RECORD_C), str(HOST_TEST), "-o", str(exe),
            ], check=True)
            out = subprocess.run([str(exe)], check=True, text=True, capture_output=True).stdout
            self.assertIn("storage_record_host_test: OK", out)


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__])
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    sys.exit(0 if result.wasSuccessful() else 1)
