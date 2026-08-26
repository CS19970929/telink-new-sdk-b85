#!/usr/bin/env python3
"""Focused static contracts for the TLSR8251 BMS NVM layout."""

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent
FLASH_CFG = ROOT / "flash_store_cfg.h"
FLASH_SAFE = ROOT / "flash_store_safe.h"
COLD_HDR = ROOT / "bms_cold_kv_store.h"


def text(path):
    return path.read_text(encoding="utf-8", errors="ignore")


def macro_int(source, name):
    match = re.search(
        rf"^\s*#define\s+{re.escape(name)}\s+\(?(0x[0-9A-Fa-f]+|[0-9]+)u?\)?",
        source,
        re.MULTILINE,
    )
    if not match:
        raise AssertionError(f"macro not found: {name}")
    return int(match.group(1), 0)


class Tlsr8251NvmLayoutTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cfg = text(FLASH_CFG)
        cls.safe = text(FLASH_SAFE)
        cls.cold = text(COLD_HDR)

    def test_512k_partition_map(self):
        self.assertEqual(macro_int(self.cfg, "FLASH_ADDR_LAYOUT_512K_SOFT_PROTECT"), 0x40000)
        self.assertEqual(macro_int(self.cfg, "FLASH_ADDR_LAYOUT_512K_RUN_KV_BASE"), 0x42000)
        self.assertEqual(macro_int(self.cfg, "FLASH_ADDR_LAYOUT_512K_LOG_BASE"), 0x46000)
        self.assertEqual(macro_int(self.cfg, "FLASH_ADDR_LAYOUT_512K_RUNTIME_BASE"), 0x56000)
        self.assertEqual(macro_int(self.cfg, "FLASH_ADDR_LAYOUT_512K_RESERVE_BASE"), 0x58000)
        self.assertEqual(macro_int(self.cfg, "FLASH_ADDR_LAYOUT_512K_RESERVE_END"), 0x74000)

    def test_partition_sector_counts(self):
        self.assertEqual(macro_int(self.cfg, "FLASH_ADDR_COLD_KV_SECTORS"), 2)
        self.assertEqual(macro_int(self.cfg, "FLASH_ADDR_RUN_KV_SECTORS"), 4)
        self.assertEqual(macro_int(self.cfg, "FLASH_ADDR_LOG_SECTORS"), 16)
        self.assertEqual(macro_int(self.cfg, "FLASH_ADDR_RUNTIME_SECTORS"), 2)
        self.assertEqual(macro_int(self.cold, "BMS_COLD_KV_SECTORS"), 2)

    def test_512k_partitions_are_contiguous_and_below_sdk_reserve(self):
        sector = macro_int(self.cfg, "FLASH_SECTOR_SIZE")
        cold = macro_int(self.cfg, "FLASH_ADDR_LAYOUT_512K_SOFT_PROTECT")
        soc = macro_int(self.cfg, "FLASH_ADDR_LAYOUT_512K_RUN_KV_BASE")
        log = macro_int(self.cfg, "FLASH_ADDR_LAYOUT_512K_LOG_BASE")
        runtime = macro_int(self.cfg, "FLASH_ADDR_LAYOUT_512K_RUNTIME_BASE")
        reserve = macro_int(self.cfg, "FLASH_ADDR_LAYOUT_512K_RESERVE_BASE")
        reserve_end = macro_int(self.cfg, "FLASH_ADDR_LAYOUT_512K_RESERVE_END")

        self.assertEqual(cold + 2 * sector, soc)
        self.assertEqual(soc + 4 * sector, log)
        self.assertEqual(log + 16 * sector, runtime)
        self.assertEqual(runtime + 2 * sector, reserve)
        self.assertEqual(reserve_end, 0x74000)

    def test_default_ota_contract_is_explicit(self):
        self.assertIn("MULTI_BOOT_ADDR_0x20000", self.cfg)
        self.assertIn("blc_flash_capacity == FLASH_SIZE_512K", self.cfg)
        self.assertIn("multi_boot_addr != MULTI_BOOT_ADDR_0x20000", self.cfg)

    def test_program_is_range_guarded_and_chunked(self):
        self.assertEqual(macro_int(self.safe, "FLASH_STORE_PROG_CHUNK"), 16)
        self.assertIn("flash_store_cfg_range_allowed(addr, len)", self.safe)
        self.assertIn("FLASH_STORE_PAGE_BYTES - page_off", self.safe)
        self.assertIn("flash_store_program_allowed_now()", self.safe)

    def test_erase_is_blocked_during_connection_and_ota(self):
        self.assertIn("ota_is_working", self.safe)
        self.assertIn("blc_ll_getCurrentState() == BLS_LINK_STATE_CONN", self.safe)
        self.assertIn("flash_store_cfg_range_allowed(addr, sector_size)", self.safe)


if __name__ == "__main__":
    unittest.main()
