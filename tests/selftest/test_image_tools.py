from __future__ import annotations

import sys
import tempfile
import unittest
import json
import re
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SDK = REPO / "tc_ble_single_sdk-V3.4.2.8_Patch_0001" / "tc_ble_single_sdk"
TOOLS = SDK / "script" / "selftest"
PROJECT = SDK / "project" / "tlsr_tc32" / "B85" / "825x_ble_sample"
sys.path.insert(0, str(TOOLS))

from image_manifest import crc32, symbol_address, unpack_manifest  # noqa: E402
from verify_image_crc import verify  # noqa: E402


class ImageToolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.elf = PROJECT / "tc_ble_single_sdk_B85.elf"
        cls.image = PROJECT / "825x_ble_sample.bin"
        cls.nm = Path(r"C:\TelinkSDK\opt\tc32\bin\tc32-elf-nm.exe")
        if not (cls.elf.exists() and cls.image.exists() and cls.nm.exists()):
            raise unittest.SkipTest("run scripts/build_selftest.ps1 first")

    def test_standard_crc32_vector(self) -> None:
        self.assertEqual(crc32(b"123456789"), 0xCBF43926)

    def test_built_image_and_telink_trailer(self) -> None:
        result = verify(self.elf, self.image, self.nm)
        self.assertTrue(result["ok"], result)

    def test_one_bit_corruption_is_detected(self) -> None:
        original = bytearray(self.image.read_bytes())
        offset = symbol_address(self.elf, self.nm)
        manifest = unpack_manifest(original, offset)
        original[manifest["range0_start"] + 8] ^= 1
        with tempfile.TemporaryDirectory() as directory:
            corrupted = Path(directory) / "corrupted.bin"
            corrupted.write_bytes(original)
            result = verify(self.elf, corrupted, self.nm)
        self.assertFalse(result["image_crc_ok"])
        self.assertFalse(result["telink_crc_ok"])
        self.assertFalse(result["ok"])

    def test_fault_injection_production_defaults(self) -> None:
        cfg = (SDK / "vendor" / "ble_sample" / "bms_selftest" / "bms_selftest_cfg.h").read_text(encoding="utf-8")
        self.assertIn("#define BMS_DIAG_FAULT_INJECT_ENABLE              0", cfg)
        self.assertIn("#define BMS_FAULT_INJECT_MASK                     0u", cfg)
        diag = (SDK / "vendor" / "ble_sample" / "bms_selftest" / "bms_diag.c").read_text(encoding="utf-8")
        self.assertNotIn("BMS_Diag_Write", diag)

    def test_manifest_ranges_and_flash_slot_bounds(self) -> None:
        data = self.image.read_bytes()
        offset = symbol_address(self.elf, self.nm)
        manifest = unpack_manifest(data, offset)
        self.assertEqual(offset, manifest["range0_start"] + manifest["range0_length"])
        self.assertEqual(offset + 64, manifest["range1_start"])
        self.assertLessEqual(manifest["range1_start"] + manifest["range1_length"], manifest["image_size"])
        self.assertLess(manifest["image_size"], 0x1F000)
        self.assertGreater(manifest["range0_length"], 0)
        self.assertGreater(manifest["range1_length"], 0)

    def test_map_layout_and_required_symbols(self) -> None:
        report = json.loads((PROJECT / "selftest_layout_report.json").read_text(encoding="utf-8"))
        self.assertTrue(report["ok"], report)
        self.assertTrue(all(report["checks"].values()), report)
        nm_text = __import__("subprocess").check_output([self.nm, "-n", self.elf], text=True)
        for symbol in (
            "BMS_SelfTest_EarlyBoot",
            "BMS_SelfTest_Tc32CpuRegPatternAsm",
            "BMS_SelfTest_Tc32ControlFlowAsm",
            "_bms_manifest_start_",
            "_bms_stack_guard_start_",
        ):
            self.assertIn(symbol, nm_text)

    def test_control_flow_algorithm_and_assembly_are_linked(self) -> None:
        flow = (SDK / "vendor" / "ble_sample" / "bms_selftest" / "bms_selftest_controlflow.c").read_text(encoding="utf-8")
        cpu = (SDK / "vendor" / "ble_sample" / "bms_selftest" / "bms_selftest_cpu.c").read_text(encoding="utf-8")
        assembly = (SDK / "vendor" / "ble_sample" / "bms_selftest" / "bms_selftest_cpu_tc32.S").read_text(encoding="utf-8")
        self.assertIn("flow->counter ^ flow->inverse", flow)
        self.assertIn("BMS_SelfTest_Tc32ControlFlowAsm", cpu)
        self.assertIn(".global BMS_SelfTest_Tc32ControlFlowAsm", assembly)
        self.assertRegex(assembly, r"tadd\s+r0, #1[\s\S]*tadd\s+r0, #2[\s\S]*tadd\s+r0, #4")

    def test_inverse_pair_algorithm(self) -> None:
        for value in (0x0000, 0x0001, 0x55AA, 0xFFFF):
            inverse = (~value) & 0xFFFF
            self.assertEqual((value ^ inverse) & 0xFFFF, 0xFFFF)
            self.assertNotEqual((value ^ (inverse ^ 1)) & 0xFFFF, 0xFFFF)
        failsafe = (SDK / "vendor" / "ble_sample" / "bms_selftest" / "bms_failsafe.c").read_text(encoding="utf-8")
        self.assertIn("*value ^ *inverse", failsafe)
        self.assertIn("BMS_FAULT_INTERNAL_DATA", failsafe)

    def test_parameter_dual_sector_selection_model(self) -> None:
        def select(sectors: list[tuple[bool, bool, int]]) -> tuple[int, int] | None:
            chosen = None
            for index, (valid, active, generation) in enumerate(sectors):
                if valid and active and (chosen is None or generation >= chosen[1]):
                    chosen = (index, generation)
            return chosen

        self.assertEqual(select([(True, True, 4), (True, True, 5)]), (1, 5))
        self.assertEqual(select([(False, True, 9), (True, True, 5)]), (1, 5))
        self.assertIsNone(select([(False, True, 9), (True, False, 5)]))
        kv = (SDK / "vendor" / "ble_sample" / "flash_kv32.c").read_text(encoding="utf-8")
        self.assertIn("kv_select_active_sector", kv)
        self.assertIn("info.generation >= *active_generation", kv)

    def test_fault_codes_state_and_safe_output_gates(self) -> None:
        types = (SDK / "vendor" / "ble_sample" / "bms_selftest" / "bms_selftest_types.h").read_text(encoding="utf-8")
        values = [int(value) for value in re.findall(r"BMS_FAULT_[A-Z_]+\s*=\s*(\d+)", types)]
        self.assertEqual(values, list(range(18)))
        failsafe = (SDK / "vendor" / "ble_sample" / "bms_selftest" / "bms_failsafe.c").read_text(encoding="utf-8")
        self.assertIn("BMS_Port_ForceDangerousOutputsOff();", failsafe)
        app = (SDK / "vendor" / "ble_sample" / "app.c").read_text(encoding="utf-8")
        self.assertGreaterEqual(app.count("BMS_FailSafe_AllowOutputs()"), 6)
        for line in app.splitlines():
            if re.search(r"gpio_write\(RF_EN_PIN,\s*1\)", line) and not line.lstrip().startswith("//"):
                self.assertIn("BMS_FailSafe_AllowOutputs()", line)

    def test_build_guards_and_crc_field_position(self) -> None:
        cfg = (SDK / "vendor" / "ble_sample" / "bms_selftest" / "bms_selftest_cfg.h").read_text(encoding="utf-8")
        self.assertIn("#if BMS_DIAG_FAULT_INJECT_ENABLE && !BMS_DIAG_TEST_BUILD", cfg)
        self.assertIn("#if !BMS_DIAG_FAULT_INJECT_ENABLE && (BMS_FAULT_INJECT_MASK != 0u)", cfg)
        data = self.image.read_bytes()
        offset = symbol_address(self.elf, self.nm)
        manifest = unpack_manifest(data, offset)
        self.assertEqual(manifest["header_size"], 64)
        self.assertEqual(manifest["polynomial"], 0xEDB88320)


if __name__ == "__main__":
    unittest.main()
