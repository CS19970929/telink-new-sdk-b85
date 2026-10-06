from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


REPO_ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = REPO_ROOT / "bms_tools" / "bms.py"
WORKFLOW_PATH = REPO_ROOT / ".github" / "workflows" / "bms-ci.yml"
SPEC = importlib.util.spec_from_file_location("bms_tool", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
bms = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = bms
sys.path.insert(0, str(MODULE_PATH.parent))
SPEC.loader.exec_module(bms)
from static_analysis import StaticAnalysis
static = StaticAnalysis(bms)


class WorktreeJunctionTests(unittest.TestCase):
    def test_junction_is_stable_and_unique_per_worktree(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            first = Path(tmp) / "first"
            second = Path(tmp) / "second"
            first.mkdir()
            second.mkdir()

            self.assertEqual(bms._worktree_junction(first), bms._worktree_junction(first))
            self.assertNotEqual(bms._worktree_junction(first), bms._worktree_junction(second))
            self.assertRegex(
                bms._worktree_junction(first).name,
                r"^repo_[0-9a-f]{12}$",
            )


class GitProvenanceTests(unittest.TestCase):
    def test_clean_modified_and_unavailable_status_are_distinct(self):
        for returncode, output, expected in ((0, "", False), (0, " M app.c\n", True), (1, "", None)):
            with self.subTest(expected=expected):
                results = [mock.Mock(returncode=returncode, stdout=output),
                           mock.Mock(returncode=0, stdout="a" * 40),
                           mock.Mock(returncode=0, stdout="branch")]
                with mock.patch.object(bms.subprocess, "run", side_effect=results):
                    self.assertIs(bms._git_provenance()["dirty"], expected)

    def test_firmware_build_id_uses_first_32_bits_of_git_head(self):
        result = mock.Mock(returncode=0, stdout="12ab34cd" + "e" * 32 + "\n")
        with mock.patch.object(bms.subprocess, "run", return_value=result):
            self.assertEqual(bms._firmware_git_build_id(), "0x12ab34cdu")

    def test_firmware_build_id_is_zero_without_git_metadata(self):
        result = mock.Mock(returncode=1, stdout="")
        with mock.patch.object(bms.subprocess, "run", return_value=result):
            self.assertEqual(bms._firmware_git_build_id(), "0u")

    def test_firmware_dirty_flag_distinguishes_clean_and_modified_worktree(self):
        for output, expected in (("", 0), (" M vendor/ble_sample/app.c\n", 1)):
            with self.subTest(expected=expected):
                result = mock.Mock(returncode=0, stdout=output)
                with mock.patch.object(bms.subprocess, "run", return_value=result):
                    self.assertEqual(bms._firmware_git_dirty(), expected)


class ProductionBuildTests(unittest.TestCase):
    def test_owned_flags_and_explicit_profile(self):
        with mock.patch.multiple(bms, PRODUCT="d008", PRODUCTION=True, D008_PROFILE="24s-lfp"), \
             mock.patch.object(bms, "_firmware_git_build_id", return_value="0x12345678u"), \
             mock.patch.object(bms, "_firmware_git_dirty", return_value=0), \
             mock.patch.dict(bms.os.environ, {"EXTRA_DEFINES": ""}):
            flags = bms._effective_extra_defines()
            self.assertIn("-DBMS_PRODUCTION_BUILD=1", flags)
            self.assertIn("-DD008_PRODUCT_PROFILE=1", flags)
            with mock.patch.object(bms, "D008_PROFILE", None), self.assertRaises(SystemExit):
                bms._effective_extra_defines()
            with mock.patch.object(bms, "_firmware_git_dirty", return_value=1), self.assertRaises(SystemExit):
                bms._effective_extra_defines()
            for override in ("-DBMS_PRODUCTION_BUILD=0", "-UBMS_DIAG_BUILD_DIRTY", "-DBMS_DIAG_BUILD_ID=1", "-DD008_PRODUCT_PROFILE=3"):
                with mock.patch.dict(bms.os.environ, {"EXTRA_DEFINES": override}), self.assertRaises(SystemExit):
                    bms._effective_extra_defines()

    def test_unknown_git_is_not_clean(self):
        with mock.patch.object(bms.subprocess, "run", return_value=mock.Mock(returncode=1, stdout="")):
            self.assertEqual(bms._firmware_git_dirty(), 1)

    def test_child_arguments_keep_product_mode_and_profile(self):
        with mock.patch.multiple(bms, PRODUCTION=True, D008_PROFILE="20s-nmc"):
            self.assertEqual(bms._selection_args("d008"), ["--product", "d008", "--production", "--d008-profile", "20s-nmc"])


class ClientAssetPathTests(unittest.TestCase):
    def test_product_branch_does_not_require_legacy_qt_client(self) -> None:
        self.assertFalse((REPO_ROOT / "tools" / "BMSAssistantQt").exists())


class WorkflowSecurityTests(unittest.TestCase):
    def test_external_fork_prs_cannot_reach_tc32_runner(self) -> None:
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        tc32_job = workflow.split("  tc32-windows:\n", 1)[1].split("  tc32-production:\n", 1)[0]
        self.assertIn("vars.TELINK_TC32_CI_ENABLED == '1'", tc32_job)
        self.assertIn("github.event_name != 'pull_request'", tc32_job)
        self.assertIn(
            "github.event.pull_request.head.repo.full_name == github.repository",
            tc32_job,
        )
        self.assertIn(
            "runs-on: [self-hosted, Windows, X64, telink-tc32]",
            tc32_job,
        )


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
    def test_discovery_uses_product_backend_and_all_shared_sources(self):
        actual = bms._discover_managed_sources()
        self.assertIn("bms/core/bms_soc.c", actual)
        self.assertIn("bms/core/bms_parameter_access.c", actual)
        backend = "dvc1124" if bms.PRODUCT == "d008" else "sh3673510"
        self.assertTrue(any(path.startswith("bms/afe/" + backend + "/") for path in actual))
        self.assertFalse(any("vendor/ble_sample/" in path for path in actual))
        self.assertEqual(set(actual), set(bms._read_source_order()))

    def test_validation_rejects_unlisted_new_source(self) -> None:
        with self.assertRaisesRegex(bms.SourceOrderError, "unlisted new source"):
            bms._validate_source_order(["app.c"], ["app.c", "new_file.c"])

    def test_read_rejects_duplicate_case_collision(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            order = Path(tmp) / "source_order.txt"
            order.write_text("App.c\napp.c\n", encoding="utf-8")
            with self.assertRaisesRegex(bms.SourceOrderError, "case-colliding"):
                bms._read_source_order(order)

    def test_manifest_paths_are_real_files_and_repo_relative(self):
        for relative in bms._load_source_order_strict():
            self.assertFalse(Path(relative).is_absolute())
            self.assertTrue((REPO_ROOT / relative).is_file())



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
            {"env", "build", "compile", "link", "resources", "rebuild", "objcopy", "check-fw", "size", "map",
             "manifest", "verify", "baseline", "static", "flash-help", "ci", "sources"},
        )


class OutputPathTests(unittest.TestCase):
    def test_cli_outputs_are_external_and_separate_per_product(self):
        self.assertFalse(bms.BUILD_DIR.is_relative_to(REPO_ROOT))
        self.assertEqual(bms.BUILD_DIR.name, bms.PRODUCT)
        self.assertNotEqual(bms.BUILD_DIR, bms.IDE_BUILD_DIR)
        self.assertFalse(bms.RAW_BIN.is_relative_to(REPO_ROOT))
        self.assertTrue(bms.BIN.is_relative_to(REPO_ROOT / "firmware"))
        self.assertEqual(bms.MANIFEST.parent, bms.BIN.parent)

    def test_six_production_outputs_do_not_overwrite_each_other(self):
        outputs = set()
        cases = [("d008", profile) for profile in ("16s-lfp", "20s-nmc", "24s-lfp")]
        cases += [(product, None) for product in ("d011", "d013", "d014")]
        for product, profile in cases:
            argv = ["bms.py", "--product", product, "--production"]
            if profile:
                argv += ["--d008-profile", profile]
            module = importlib.util.module_from_spec(SPEC)
            with mock.patch.object(sys, "argv", argv):
                SPEC.loader.exec_module(module)
            self.assertEqual(module.FIRMWARE_DIR.parent.name, "production" + ("-" + profile if profile else ""))
            self.assertEqual(module.BIN.parent.name, product)
            outputs.add(module.BIN)
        self.assertEqual(len(outputs), 6)

    def test_only_checked_image_is_published_and_old_manifest_is_invalidated(self):
        payload = bytes(range(32))
        checked = payload + ((~bms._crc32(payload)) & 0xFFFFFFFF).to_bytes(4, "little")
        for valid in (True, False):
            with self.subTest(valid=valid), tempfile.TemporaryDirectory() as directory:
                temporary = Path(directory)
                build = temporary / "external"
                final = temporary / "project" / "firmware"
                build.mkdir()
                final.mkdir(parents=True)
                elf, raw = build / "firmware.elf", build / "firmware.raw.bin"
                binary, manifest = final / "firmware.bin", final / "fw_manifest.json"
                checker = temporary / "checker.exe"
                elf.touch()
                checker.touch()
                binary.write_bytes(b"previous image")
                manifest.write_text("previous manifest", encoding="utf8")
                with mock.patch.multiple(bms, PRODUCTION=False, ELF=elf, RAW_BIN=raw, BIN=binary,
                                         MANIFEST=manifest, BUILD_DIR=build, TL_CHECK_FW2=checker), \
                     mock.patch.object(bms, "_objcopy", side_effect=lambda source, target: target.write_bytes(b"raw")), \
                     mock.patch.object(bms, "_run_tl_check_fw", side_effect=lambda path: path.write_bytes(checked if valid else b"invalid")):
                    if valid:
                        bms._finalize_firmware()
                        self.assertEqual(binary.read_bytes(), checked)
                        self.assertFalse(manifest.exists())
                    else:
                        with self.assertRaises(SystemExit):
                            bms._finalize_firmware()
                        self.assertEqual(binary.read_bytes(), b"previous image")
                        self.assertEqual(manifest.read_text(encoding="utf8"), "previous manifest")



class StaticAnalysisPrimitiveTests(unittest.TestCase):
    def test_static_scope_accepts_only_ble_sample_paths(self) -> None:
        self.assertTrue(static._is_application_scope_path(
            "bms/app/app.c"))
        self.assertTrue(static._is_application_scope_path(
            "bms/platform/telink/flash_store_safe.h"))
        self.assertFalse(static._is_application_scope_path(
            f"{bms.SDK_SUBDIR}/vendor/common/app_common.c"))
        self.assertFalse(static._is_application_scope_path(
            f"{bms.SDK_SUBDIR}/drivers/B85/gpio.h"))

    def test_scope_exclusion_partition_never_suppresses_application(self) -> None:
        dependencies = {
            "bms/app/app.h",
            f"{bms.SDK_SUBDIR}/drivers/B85/gpio.h",
        }
        with tempfile.TemporaryDirectory() as directory:
            path, excluded = static._write_cppcheck_scope_exclusions(dependencies, Path(directory))
            self.assertEqual(excluded, [f"{bms.SDK_SUBDIR}/drivers/B85/gpio.h"])
            self.assertIn("gpio.h", path.read_text())
            self.assertNotIn("bms/app/app.h", path.read_text())


    def test_extracts_real_compile_settings_without_manual_flag_lists(self) -> None:
        command = (
            'tc32-elf-gcc -O2 -std=gnu99 -I"C:/sdk" '
            '-DPROJECT=1 -fshort-wchar -c -o"C:/out/app.o" "C:/sdk/app.c"'
        )
        with mock.patch.object(bms, "_tc32_tool", return_value=r"C:\tc32\tc32-elf-gcc.exe"), \
                mock.patch.object(bms, "_tool_version", return_value="GCC 4.5.1-tc32-1.3"):
            settings = static._extract_real_compile_settings(command)
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
            self.assertEqual(static._function_at_line(source, 5), "example_function")

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
        classification, status, _ = static._finding_classification(row, set())
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

            with mock.patch.object(static, "_resolve_artifact_runtime", return_value=Path("python")), \
                    mock.patch.object(bms.subprocess, "run", side_effect=fake_run):
                static._run_static_report_builder(
                    template, data_path, output_path, verification_dir)

            self.assertEqual(output_path.read_bytes(), b"completed-report")
            self.assertTrue((verification_dir / "verification.json").exists())
            self.assertEqual(data_path.read_text(encoding="utf-8"), "{}")
            self.assertFalse((run_dir / "node_modules").exists())


if __name__ == "__main__":
    unittest.main()
