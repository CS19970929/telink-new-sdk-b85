from __future__ import annotations

import importlib.util
import tempfile
import unittest
from pathlib import Path
from unittest import mock


REPO_ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = REPO_ROOT / "bms_tools" / "bms.py"
SPEC = importlib.util.spec_from_file_location("bms_tool", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
bms = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(bms)

class ToolchainEnvironmentTests(unittest.TestCase):
    def test_canonicalises_windows_path_key(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            env = {"Path": r"C:\Windows\System32"}
            with mock.patch.object(bms, "DEFAULT_TC32_DIR", Path(tmp)):
                with mock.patch.object(bms.shutil, "which", return_value=None):
                    result = bms._ensure_toolchain_env(env)

            self.assertNotIn("Path", result)
            self.assertIn("PATH", result)
            self.assertEqual(
                result["PATH"],
                str(Path(tmp)) + bms.os.pathsep + r"C:\Windows\System32",
            )

    def test_does_not_prepend_when_tool_is_already_resolvable(self) -> None:
        env = {"Path": r"C:\toolchain;C:\Windows"}
        with mock.patch.object(bms.shutil, "which", return_value=r"C:\toolchain\tc32-elf-gcc.exe"):
            result = bms._ensure_toolchain_env(env)
        self.assertNotIn("Path", result)
        self.assertEqual(result["PATH"], r"C:\toolchain;C:\Windows")

    def test_tc32_tool_prefers_pinned_absolute_path(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            expected = Path(tmp) / "tc32-elf-size.exe"
            expected.touch()
            with mock.patch.object(bms, "DEFAULT_TC32_DIR", Path(tmp)):
                actual = bms._tc32_tool("tc32-elf-size")
        self.assertEqual(actual, str(expected))


class IntegrityPrimitiveTests(unittest.TestCase):
    def test_crc32_known_vector(self) -> None:
        self.assertEqual(bms._crc32(b"123456789"), 0xCBF43926)

    def test_telink_crc_trailer_contract(self) -> None:
        payload = b"firmware payload"
        payload_crc = bms._crc32(payload)
        trailer = ((~payload_crc) & 0xFFFFFFFF).to_bytes(4, "little")
        details = bms._telink_crc_details(payload + trailer)
        self.assertTrue(details["valid"])
        self.assertEqual(details["payload_crc32"], payload_crc)
        self.assertEqual(details["whole_image_crc32_residue"], 0xFFFFFFFF)

    def test_telink_crc_rejects_corruption(self) -> None:
        payload = b"firmware payload"
        trailer = ((~bms._crc32(payload)) & 0xFFFFFFFF).to_bytes(4, "little")
        corrupted = bytearray(payload + trailer)
        corrupted[0] ^= 0x01
        self.assertFalse(bms._telink_crc_details(bytes(corrupted))["valid"])

    def test_raw_image_has_no_telink_trailer(self) -> None:
        self.assertFalse(bms._telink_crc_details(b"raw firmware image")["valid"])


class SourceOrderTests(unittest.TestCase):
    def test_discovery_is_case_sensitive_and_interleaves_c_and_assembly(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            sdk = Path(tmp)
            app = sdk / "vendor" / "ble_sample"
            common = sdk / "common"
            nested = app / "feature"
            nested.mkdir(parents=True)
            common.mkdir(parents=True)
            for path in (app / "SocEnhance.c", app / "app.c", nested / "worker.c",
                         common / "div_mod.S", common / "sdk_version.c"):
                path.touch()
            groups = ((Path("vendor/ble_sample"), True), (Path("common"), False))
            with mock.patch.object(bms, "SDK_DIR", sdk), \
                    mock.patch.object(bms, "SOURCE_GROUPS", groups):
                actual = bms._discover_managed_sources()
        self.assertEqual(actual, [
            "vendor/ble_sample/SocEnhance.c",
            "vendor/ble_sample/app.c",
            "vendor/ble_sample/feature/worker.c",
            "common/div_mod.S",
            "common/sdk_version.c",
        ])

    def test_validation_rejects_unlisted_new_source(self) -> None:
        with self.assertRaisesRegex(bms.SourceOrderError, "unlisted new source"):
            bms._validate_source_order(["app.c"], ["app.c", "new_file.c"])

    def test_read_rejects_duplicate_case_collision(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            order = Path(tmp) / "source_order.txt"
            order.write_text("App.c\napp.c\n", encoding="utf-8")
            with self.assertRaisesRegex(bms.SourceOrderError, "case-colliding"):
                bms._read_source_order(order)

    def test_parse_ide_order_follows_makefile_include_and_objs(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            sdk = root / "sdk"
            ide = root / "ide"
            app = sdk / "vendor" / "ble_sample"
            common = sdk / "common"
            app.mkdir(parents=True)
            common.mkdir(parents=True)
            ide.joinpath("vendor", "ble_sample").mkdir(parents=True)
            ide.joinpath("common").mkdir(parents=True)
            (app / "SocEnhance.c").touch()
            (app / "app.c").touch()
            (common / "div_mod.S").touch()
            (ide / "makefile").write_text(
                "-include vendor/ble_sample/subdir.mk\n-include common/subdir.mk\n",
                encoding="utf-8",
            )
            (ide / "vendor" / "ble_sample" / "subdir.mk").write_text(
                "OBJS += \\\n./vendor/ble_sample/SocEnhance.o \\\n./vendor/ble_sample/app.o \n\n",
                encoding="utf-8",
            )
            (ide / "common" / "subdir.mk").write_text(
                "OBJS += \\\n./common/div_mod.o \n\n", encoding="utf-8"
            )
            groups = ((Path("vendor/ble_sample"), True), (Path("common"), False))
            with mock.patch.object(bms, "SDK_DIR", sdk), \
                    mock.patch.object(bms, "SOURCE_GROUPS", groups):
                actual = bms._parse_ide_source_order(ide)
        self.assertEqual(actual, [
            "vendor/ble_sample/SocEnhance.c",
            "vendor/ble_sample/app.c",
            "common/div_mod.S",
        ])


class CommandSurfaceTests(unittest.TestCase):
    def test_only_development_toolchain_commands_are_exposed(self) -> None:
        parser = bms.build_parser()
        subparsers = next(
            action for action in parser._actions
            if isinstance(action, bms.argparse._SubParsersAction)
        )
        commands = set(subparsers.choices)
        self.assertEqual(
            commands,
            {"env", "build", "rebuild", "objcopy", "check-fw", "size", "map",
             "manifest", "verify", "baseline", "static", "flash-help", "ci", "sources"},
        )


class OutputPathTests(unittest.TestCase):
    def test_cli_outputs_are_inside_project_and_separate_from_ide(self) -> None:
        self.assertEqual(bms.BUILD_DIR.parent, bms.PROJ_DIR)
        self.assertEqual(bms.BUILD_DIR.name, "825x_ble_sample_cli")
        self.assertEqual(bms.IDE_BUILD_DIR.name, "825x_ble_sample")
        self.assertNotEqual(bms.BUILD_DIR, bms.IDE_BUILD_DIR)


class StaticAnalysisPrimitiveTests(unittest.TestCase):
    def test_static_scope_accepts_only_ble_sample_paths(self) -> None:
        self.assertTrue(bms._is_application_scope_path(
            f"{bms.SDK_SUBDIR}/vendor/ble_sample/app.c"))
        self.assertTrue(bms._is_application_scope_path(
            f"{bms.SDK_SUBDIR}/vendor/ble_sample/flash_store_safe.h"))
        self.assertFalse(bms._is_application_scope_path(
            f"{bms.SDK_SUBDIR}/vendor/common/app_common.c"))
        self.assertFalse(bms._is_application_scope_path(
            f"{bms.SDK_SUBDIR}/drivers/B85/gpio.h"))

    def test_scope_exclusion_partition_never_suppresses_application(self) -> None:
        dependencies = {
            f"{bms.SDK_SUBDIR}/vendor/ble_sample/app.h",
            f"{bms.SDK_SUBDIR}/drivers/B85/gpio.h",
        }
        excluded = sorted(
            path for path in dependencies if not bms._is_application_scope_path(path)
        )
        self.assertEqual(excluded, [f"{bms.SDK_SUBDIR}/drivers/B85/gpio.h"])

    def test_extracts_real_compile_settings_without_manual_flag_lists(self) -> None:
        command = (
            'tc32-elf-gcc -O2 -std=gnu99 -I"C:/sdk" '
            '-DPROJECT=1 -fshort-wchar -c -o"C:/out/app.o" "C:/sdk/app.c"'
        )
        with mock.patch.object(bms, "_tc32_tool", return_value=r"C:\tc32\tc32-elf-gcc.exe"), \
                mock.patch.object(bms, "_tool_version", return_value="GCC 4.5.1-tc32-1.3"):
            settings = bms._extract_real_compile_settings(command)
        self.assertEqual(settings["c_standard"], "gnu99")
        self.assertEqual(settings["include_paths"], ["C:/sdk"])
        self.assertEqual(settings["defines"], ["PROJECT=1"])
        self.assertIn("-fshort-wchar", settings["compile_flags"])

    def test_function_locator_handles_multiline_c_function(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "sample.c"
            source.write_text(
                "static int\nexample_function(int value)\n{\n"
                "    if (value) {\n        return value;\n    }\n    return 0;\n}\n",
                encoding="utf-8",
            )
            self.assertEqual(bms._function_at_line(source, 5), "example_function")

    def test_unmodified_sdk_style_is_not_auto_approved_or_auto_deviated(self) -> None:
        row = {
            "id": "unusedFunction",
            "severity": "style",
            "misra_rule": "",
            "locations": [{
                "file": f"{bms.SDK_SUBDIR}/drivers/B85/gpio.h",
                "line": 10,
            }],
        }
        classification, status, _ = bms._finding_classification(row, set())
        self.assertEqual(classification, "SDK问题")
        self.assertEqual(status, "待评审（SDK）")
        self.assertNotIn("批准", status)

    def test_report_runtime_is_isolated_from_static_evidence_directory(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            run_dir = root / "run"
            run_dir.mkdir()
            template = root / "template.xlsx"
            template.write_bytes(b"template")
            data_path = run_dir / "report_data.json"
            data_path.write_text("{}", encoding="utf-8")
            output_path = run_dir / "report.xlsx"
            verification_dir = run_dir / "report_previews"

            def fake_run(command, **_kwargs):
                temp_output = Path(command[4])
                temp_verification = Path(command[5])
                temp_output.write_bytes(b"completed-report")
                temp_verification.mkdir(parents=True)
                (temp_verification / "verification.json").write_text(
                    '{"formula_errors": 0}', encoding="utf-8")
                return mock.Mock(returncode=0, stdout="ok", stderr="")

            with mock.patch.object(bms, "_resolve_artifact_runtime", return_value=Path("python")), \
                    mock.patch.object(bms.subprocess, "run", side_effect=fake_run):
                bms._run_static_report_builder(
                    template, data_path, output_path, verification_dir)

            self.assertEqual(output_path.read_bytes(), b"completed-report")
            self.assertTrue((verification_dir / "verification.json").exists())
            self.assertEqual(data_path.read_text(encoding="utf-8"), "{}")
            self.assertFalse((run_dir / "node_modules").exists())


if __name__ == "__main__":
    unittest.main()
