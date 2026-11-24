from __future__ import annotations

import importlib.util
import os
import shlex
import sys
import subprocess
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
import android_ota
import hashlib
import json
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
             mock.patch.object(bms, "_selected_build_options", return_value={}), \
             mock.patch.dict(bms.os.environ, {"EXTRA_DEFINES": ""}):
            flags = bms._effective_extra_defines()
            self.assertIn("-DBMS_PRODUCTION_BUILD=1", flags)
            self.assertIn("-DD008_PRODUCT_PROFILE=1", flags)
            with mock.patch.object(bms, "D008_PROFILE", None), self.assertRaises(SystemExit):
                bms._effective_extra_defines()
            with mock.patch.object(bms, "_firmware_git_dirty", return_value=1):
                self.assertIn("-DBMS_BUILD_DIRTY=1", bms._effective_extra_defines())
            for override in ("-DBMS_PRODUCTION_BUILD=0", "-UBMS_BUILD_DIRTY", "-DBMS_BUILD_ID=1", "-DD008_PRODUCT_PROFILE=3", "-DBMS_D008_SCD_POLICY_APPROVED=1", "-DBMS_PRODUCT_RELEASE_APPROVED=1"):
                with mock.patch.dict(bms.os.environ, {"EXTRA_DEFINES": override}), self.assertRaises(SystemExit):
                    bms._effective_extra_defines()

    def test_unknown_git_is_not_clean(self):
        with mock.patch.object(bms.subprocess, "run", return_value=mock.Mock(returncode=1, stdout="")):
            self.assertEqual(bms._firmware_git_dirty(), 1)

    def test_child_arguments_keep_product_mode_and_profile(self):
        with mock.patch.multiple(bms, PRODUCTION=True, D008_PROFILE="20s-nmc"):
            self.assertEqual(bms._selection_args(), ["--product", "d008", "--production", "--d008-profile", "20s-nmc"])


class ClientAssetPathTests(unittest.TestCase):
    def test_product_branch_does_not_require_legacy_qt_client(self) -> None:
        self.assertFalse((REPO_ROOT / "tools" / "BMSAssistantQt").exists())


class WorkflowSecurityTests(unittest.TestCase):
    def test_ci_uses_no_image_link_and_all_assemblies(self):
        job = WORKFLOW_PATH.read_text(encoding="utf-8").split("  tc32:\n", 1)[1]
        self.assertNotIn("self-hosted", job)
        self.assertIn("16s-lfp, 20s-nmc, 24s-lfp, 23s-lfp, custom", job)
        self.assertIn('link --jobs 4', job)
        self.assertNotIn('rebuild', job)


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
             "manifest", "verify", "baseline", "static", "flash-help", "ci", "sources", "test", "release"},
        )


    def test_release_runs_image_manifest_verify_in_order(self):
        calls = []
        with mock.patch.object(bms, "PRODUCTION", True), mock.patch.object(bms, "cmd_rebuild", side_effect=lambda a:calls.append("rebuild")), mock.patch.object(bms, "cmd_manifest", side_effect=lambda a:calls.append("manifest")), mock.patch.object(bms, "cmd_verify", side_effect=lambda a:calls.append("verify") or 0):
            self.assertEqual(bms.cmd_release(bms.argparse.Namespace(jobs=4)), 0)
            self.assertEqual(calls, ["rebuild", "manifest", "verify"])

    def test_pending_approval_stops_before_clean_or_image_generation(self):
        for command in (bms.cmd_build, bms.cmd_rebuild, bms._finalize_firmware):
            with self.subTest(command=command.__name__), mock.patch.object(bms, "_require_release_approval", side_effect=SystemExit(1)), mock.patch.object(bms, "_invoke_make") as make, mock.patch.object(bms, "_run") as run:
                with self.assertRaises(SystemExit):
                    if command == bms._finalize_firmware:command()
                    else:command(bms.argparse.Namespace(jobs=4))
                make.assert_not_called()
                run.assert_not_called()


class OutputPathTests(unittest.TestCase):
    def test_cli_outputs_are_external_and_separate_per_product(self):
        self.assertFalse(bms.BUILD_DIR.is_relative_to(REPO_ROOT))
        self.assertEqual(bms.BUILD_DIR.name, bms.PRODUCT)
        self.assertNotEqual(bms.BUILD_DIR, bms.IDE_BUILD_DIR)
        self.assertFalse(bms.RAW_BIN.is_relative_to(REPO_ROOT))
        self.assertTrue(bms.BIN.is_relative_to(REPO_ROOT / "firmware"))
        self.assertEqual(bms.MANIFEST.parent, bms.BIN.parent)

    def test_five_production_outputs_do_not_overwrite_each_other(self):
        outputs = set()
        product = 'd008'
        for profile in ('16s-lfp', '20s-nmc', '23s-lfp', '24s-lfp', 'custom'):
            argv = ["bms.py", "--product", product, "--production", "--d008-profile", profile]
            module = importlib.util.module_from_spec(SPEC)
            # 本用例只验证输出路径隔离，不应受当前源码 OTA 内容摘要约束。
            with mock.patch.object(sys, "argv", argv), \
                 mock.patch.object(bms.build_options, "verify_source",
                                   side_effect=bms.build_options.read_source):
                SPEC.loader.exec_module(module)
            self.assertEqual(module.FIRMWARE_DIR.parent.name,
                             module.build_options.variant('production', product, profile,
                                 module.build_options.read_source(REPO_ROOT, product, profile)))
            self.assertEqual(module.BIN.parent.name, product)
            outputs.add(module.BIN)
        self.assertEqual(len(outputs), 5)

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
                     mock.patch.object(bms, "_require_release_approval"), \
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
        with tempfile.TemporaryDirectory() as directory, mock.patch.object(bms, "_junc", side_effect=Path):
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


class BuildOptionsTests(unittest.TestCase):
    def test_source_configuration_is_recorded_without_environment_parameter_flags(self):
        with mock.patch.multiple(bms, PRODUCT='d008', PRODUCTION=False), \
             mock.patch.object(bms, '_firmware_git_build_id', return_value='0x12345678u'), \
             mock.patch.object(bms, '_firmware_git_dirty', return_value=0), \
             mock.patch.dict(bms.os.environ, {'EXTRA_DEFINES': ''}):
            configuration = bms._build_configuration()
            self.assertEqual(configuration['build_options'], bms.build_options.read_source(REPO_ROOT, 'd008'))
            flags = bms._effective_extra_defines()
            for macro in bms.build_options.MACROS.values():
                self.assertNotIn('-D' + macro + '=', flags)
            self.assertNotIn('-DBMS_OTA_UPDATE_MASK=', flags)
            self.assertNotIn('-DBMS_OTA_UPDATE_ID_', flags)
            with mock.patch.dict(bms.os.environ, {'BMS_BUILD_OPTIONS': '{}'}), self.assertRaises(SystemExit):
                bms._selected_build_options()
            for macro in (*bms.build_options.MACROS.values(), 'BMS_OTA_UPDATE_MASK',
                          'BMS_OTA_UPDATE_ID_5', 'BMS_OTA_UPDATE_SW', 'BMS_BUILD_PARAMETERS_REVISION'):
                with mock.patch.dict(bms.os.environ, {'EXTRA_DEFINES': '-D' + macro + '=2'}):
                    with self.assertRaises(SystemExit):
                        bms._effective_extra_defines()

    def test_validation_rejects_invalid_types_ranges_and_conflicting_profiles(self):
        for options in ({'capacity_0p1ah': True}, {'capacity_0p1ah': '120'},
                        {'capacity_0p1ah': 6554}, {'soc_state_revision': -1},
                        {'parameters_revision': 65536}, {'chemistry': 'auto'}, [], {'unknown': 1}):
            with self.subTest(options=options), self.assertRaises(ValueError):
                bms.build_options.normalize(options, 'd008', None)
        with self.assertRaises(ValueError):
            bms.build_options.normalize({'chemistry': 'lfp'}, 'd008', '20s-nmc')
        for product, cells, chemistry in (('d008', 3, 'lfp'), ('d008', 25, 'lfp'),
                                           ('d008', 24, 'nmc'), ('d011', 11, 'lfp'), ('d008', 3, 'nmc')):
            with self.subTest(product=product, cells=cells), self.assertRaises(ValueError):
                bms.build_options.normalize({'cell_count': cells, 'chemistry': chemistry}, product,
                                            'custom' if product == 'd008' else None)

        selected = bms.build_options.normalize({'afe_model': 'dvc1124', 'cell_count': 20, 'chemistry': 'nmc'}, 'd008', '20s-nmc')
        self.assertEqual(selected['afe_model'], 'dvc1124')
        self.assertEqual(bms.build_options.variant('development', 'd008', '20s-nmc', selected), 'development-dvc1124-20s-nmc')
        for product, model, cells in (('d008', 'sh3673510', 20), ('d008', 'sh3673520', 21), ('d008', 'sh3673520', 10)):
            with self.subTest(model=model, cells=cells), self.assertRaises(ValueError):
                bms.build_options.normalize({'afe_model': model, 'cell_count': cells, 'chemistry': 'lfp'}, product,
                                            'custom' if product == 'd008' else None)
        for product, profile, chemistry in (('d008', '20s-nmc', 'nmc'),
                                            ('d008', '16s-lfp', 'lfp'), ('d008', '23s-lfp', 'lfp')):
            self.assertEqual(bms.build_options.normalize({'chemistry': chemistry, 'capacity_0p1ah': 6553,
                'update_groups': ['business'], 'update_id': '0000' + '1234' * 5}, product, profile)['capacity_0p1ah'], 6553)

    def test_ota_selection_requires_a_generated_identifier_and_preserve_has_zero_mask(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            copy_configuration(root)
            bms.build_options.save_source(root, 'd008', {'update_groups': []})
            self.assertNotIn('update_id', bms.build_options.read_source(root, 'd008'))
        self.assertNotIn('update_id', bms.build_options.normalize({'update_groups': []}, 'd008', None))
        for options in ({'update_groups': ['sw']}, {'update_id': '0000' + '1234' * 5},
                        {'update_groups': ['events'], 'update_id': '0000' + '1234' * 5},
                        {'update_groups': ['sw', 'sw'], 'update_id': '0000' + '1234' * 5},
                        {'update_groups': ['sw'], 'update_id': '0' * 24},
                        {'update_groups': ['sw'], 'update_id': '1111' + '1234' * 5},
                        {'update_groups': ['factory_reset', 'sw'], 'update_id': '0000' + '1234' * 5}):
            with self.subTest(options=options), self.assertRaises(ValueError):
                bms.build_options.normalize(options, 'd008', None)
        token = bms.build_options.new_update_id()
        self.assertEqual(len(token), 24)
        self.assertEqual(bms.build_options.normalize({'update_groups': ['sw'], 'update_id': token}, 'd008', None)['update_id'], token)
        reset = bms.build_options.normalize({'update_groups': ['factory_reset'], 'update_id': token}, 'd008', None)
        self.assertEqual(reset['update_groups'], ['factory_reset'])



def copy_configuration(root):
    import shutil
    shutil.copytree(REPO_ROOT / 'bms/products', root / 'bms/products')
    for relative in bms.build_options.DEFAULT_INPUTS:
        target = root / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(REPO_ROOT / relative, target)
    for name in ('build_options.py', 'android_ota.py'):
        target = root / 'bms_tools' / name
        target.parent.mkdir(exist_ok=True)
        shutil.copy2(REPO_ROOT / 'bms_tools' / name, target)
    (root / '.gitignore').write_text('/firmware/\n__pycache__/\n', encoding='utf-8')
    subprocess.run(['git', 'init', '-q', str(root)], check=True, capture_output=True)
    commit_configuration(root)


def commit_configuration(root):
    subprocess.run(['git', 'add', '-A'], cwd=root, check=True, capture_output=True)
    subprocess.run(['git', '-c', 'user.name=配置回归', '-c', 'user.email=fixture@example.invalid',
                    'commit', '-qm', '保存测试配置'], cwd=root, check=True, capture_output=True)


class SourceConfigurationTests(unittest.TestCase):
    def test_soc_curve_source_change_invalidates_saved_parameter_package(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            copy_configuration(root)
            path = root / 'bms/core/bms_soc.c'
            if not path.exists():
                path.write_bytes((REPO_ROOT / 'bms/core/bms_soc.c').read_bytes())
            old = bms.build_options.save_source(root, 'd008', {'update_groups': ['soc']})
            path.write_bytes(path.read_bytes() + b'\n/* OCV curve edit */\n')
            with self.assertRaisesRegex(ValueError, '不匹配'):
                bms.build_options.verify_source(root, 'd008')
            new = bms.build_options.save_source(root, 'd008', {})
            self.assertNotEqual(old['configuration']['update_id'], new['configuration']['update_id'])
            self.assertNotEqual(old['parameter_inputs_sha256'], new['parameter_inputs_sha256'])

    def test_assembly_changes_preserve_explicit_update_selection(self):
        for product in bms.build_options.PRODUCTS:
            for factory in (False, True):
                with self.subTest(product=product, factory=factory), tempfile.TemporaryDirectory() as tmp:
                    root = Path(tmp)
                    copy_configuration(root)
                    old = bms.build_options.save_source(root, product, {
                        'update_groups': ['factory_reset'] if factory else ['business']})['configuration']
                    chemistry = 'nmc' if old['chemistry'] == 'lfp' else 'lfp'
                    saved = bms.build_options.save_source(root, product, {'chemistry': chemistry})['configuration']
                    expected = ['factory_reset'] if factory else ['business']
                    self.assertEqual(saved['update_groups'], expected)
                    self.assertEqual(bms.build_options.verify_source(root, product), saved)

    def test_engineering_profile_validates_the_saved_source_package(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            copy_configuration(root)
            saved = bms.build_options.save_source(root, 'd008', {'update_groups': ['sw']})
            for profile, cells in (('16s-lfp', 16), ('20s-nmc', 20), ('24s-lfp', 24)):
                selected = bms.build_options.verify_source(root, 'd008', profile)
                self.assertEqual(selected['cell_count'], cells)
                self.assertEqual(selected['update_id'], saved['configuration']['update_id'])
            path = root/'bms/products/d008/d008_protection_config.h'
            path.write_text(path.read_text(encoding='utf-8') + '\n/* 摘要故障注入 */\n', encoding='utf-8')
            with self.assertRaisesRegex(ValueError, '不匹配'):
                bms.build_options.verify_source(root, 'd008', '16s-lfp')

    def test_manual_group_selection_can_be_saved_before_an_identifier_exists(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            copy_configuration(root)
            bms.build_options.save_source(root, 'd008', {'update_groups': []})
            path = root/'bms/products/d008/d008_protection_config.h'
            text = bms.build_options._replace(path.read_text(encoding='utf-8'), 'BMS_OTA_UPDATE_SW', '1u')
            path.write_text(text, encoding='utf-8')
            self.assertEqual(bms.build_options.read_source(root, 'd008', for_edit=True)['update_groups'], ['sw'])
            with self.assertRaises(ValueError):
                bms.build_options.verify_source(root, 'd008')
            saved = bms.build_options.save_source(root, 'd008', {})
            self.assertEqual(bms.build_options.verify_source(root, 'd008'), saved['configuration'])
            self.assertIn('update_id', saved['configuration'])

    def test_d008_save_is_readable_and_only_changes_the_selected_source(self):
        cases = [('d008', 'dvc1124', 23)]
        for product, model, cells in cases:
            with self.subTest(product=product), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                copy_configuration(root)
                before = {p.relative_to(root).as_posix(): p.read_bytes() for p in (root/'bms/products').rglob('*.h')}
                requested = dict(afe_model=model, cell_count=cells, chemistry='nmc', capacity_0p1ah=731,
                                 update_groups=['sw', 'afe', 'business', 'soc'])
                result = bms.build_options.save_source(root, product, requested)
                actual = bms.build_options.verify_source(root, product)
                self.assertEqual({key: actual[key] for key in requested}, requested)
                self.assertEqual(len(result['changed_files']), 2)
                self.assertTrue(all('/' + product + '/' in p for p in result['changed_files']))
                for relative, original in before.items():
                    if relative not in result['changed_files']:
                        self.assertEqual((root / relative).read_bytes(), original)
                with self.assertRaises(ValueError):
                    bms.build_options.require_clean_source(root)
                commit_configuration(root)
                bms.build_options.require_clean_source(root)
                self.assertEqual(bms.build_options.verify_source(root, product), actual)

    def test_identifier_is_fixed_until_a_new_package_is_saved(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            copy_configuration(root)
            options = {'update_groups': ['business'], 'capacity_0p1ah': 200}
            first = bms.build_options.save_source(root, 'd008', options)
            second = bms.build_options.save_source(root, 'd008', options)
            self.assertEqual(first['configuration'], second['configuration'])
            self.assertEqual(second['changed_files'], [])
            third = bms.build_options.save_source(root, 'd008', options, new_package=True)
            self.assertNotEqual(first['configuration']['update_id'], third['configuration']['update_id'])
            self.assertEqual(first['parameter_inputs_sha256'], third['parameter_inputs_sha256'])

    def test_manual_default_edit_requires_a_new_identifier_and_history_rejects_reuse(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            copy_configuration(root)
            old = bms.build_options.save_source(root, 'd008', {'update_groups': ['business']}, new_package=True)
            commit_configuration(root)
            path = root / 'bms/products/d008/d008_protection_config.h'
            capacity = old['configuration']['capacity_0p1ah']
            text = bms.build_options._replace(path.read_text(encoding='utf-8'),
                    'BMS_PRODUCT_DEFAULT_CAPACITY_0P1AH', str(201 if capacity == 200 else 200) + 'u')
            path.write_text(text, encoding='utf-8')
            with self.assertRaises(ValueError):
                bms.build_options.verify_source(root, 'd008')
            new = bms.build_options.save_source(root, 'd008', {})
            self.assertNotEqual(old['configuration']['update_id'], new['configuration']['update_id'])
            text = path.read_text(encoding='utf-8')
            for index in range(6):
                identifier = old['configuration']['update_id']
                text = bms.build_options._replace(text, f'BMS_OTA_UPDATE_ID_{index}',
                    f'0x{identifier[index*4:index*4+4]}u')
            path.write_text(text, encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'Git'):
                bms.build_options.verify_source(root, 'd008')

    def test_invalid_save_leaves_all_source_bytes_unchanged_and_preserve_clears_policy(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            copy_configuration(root)
            bms.build_options.save_source(root, 'd008', {'update_groups': ['sw']})
            paths = [p for p in (root/'bms/products').rglob('*.h')]
            before = {p: p.read_bytes() for p in paths}
            for invalid in ({'cell_count': 24, 'chemistry': 'nmc'}, {'capacity_0p1ah': 6554},
                            {'afe_model': 'sh3673520'}, {'update_groups': ['events']}):
                with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                    bms.build_options.save_source(root, 'd008', invalid)
                self.assertEqual({p: p.read_bytes() for p in paths}, before)
            kept = bms.build_options.save_source(root, 'd008', {'update_groups': []})
            self.assertNotIn('update_id', kept['configuration'])
            self.assertEqual(kept['parameter_inputs_sha256'], '0'*64)
            bms.build_options.verify_source(root, 'd008')

    def test_failed_second_file_write_rolls_back_without_losing_the_previous_source(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            copy_configuration(root)
            paths = list((root/'bms/products/d008').glob('*.h'))
            before = {p: p.read_bytes() for p in paths}
            original_write = Path.write_bytes
            failed = False
            def write(path, data):
                nonlocal failed
                if path.name == 'd008_product_profile.h' and not failed:
                    failed = True
                    raise OSError('injected write failure')
                return original_write(path, data)
            with mock.patch.object(Path, 'write_bytes', write), self.assertRaises(OSError):
                bms.build_options.save_source(root, 'd008', {'capacity_0p1ah': 200, 'cell_count': 9})
            self.assertEqual({p: p.read_bytes() for p in paths}, before)

    def test_all_update_group_combinations_and_custom_assemblies_round_trip_source(self):
        import os
        import shlex
        import shutil
        compiler = os.environ.get('CC') or shutil.which('cc')
        self.assertTrue(compiler, '配置到实际 C 预处理验证需要 CC/cc')
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            copy_configuration(root)
            groups = list(bms.build_options.UPDATE_GROUPS)
            for mask in range(32):
                selected = [g for i, g in enumerate(groups) if mask & (1 << i)]
                result = bms.build_options.save_source(root, 'd008', {'update_groups': selected})
                self.assertEqual(result['configuration'].get('update_groups', []), selected)
                self.assertEqual(bms.build_options.verify_source(root, 'd008'), result['configuration'])
                expected_mask = sum(bms.build_options.UPDATE_GROUPS[group] for group in selected)
                identifier = result['configuration'].get('update_id', '0'*24)
                code = '#include "bms_product.h"\n#include "bms_update_policy.h"\n'
                code += f'#if BMS_OTA_UPDATE_MASK != {expected_mask}\n#error source_update_mask_mismatch\n#endif\n'
                for index in range(6):
                    code += f'#if BMS_OTA_UPDATE_ID_{index} != 0x{identifier[index*4:index*4+4]}\n#error source_update_id_mismatch\n#endif\n'
                actual = subprocess.run([*shlex.split(compiler), '-E', '-P', '-x', 'c',
                    '-I', str(root/'bms/products/d008'), '-I', str(root/'bms/products'),
                    '-I', str(root/'bms/core'), '-I', str(REPO_ROOT/'bms/core'), '-'],
                    input=code, capture_output=True, text=True, timeout=15)
                self.assertEqual(actual.returncode, 0, actual.stderr)
            reset = bms.build_options.save_source(root, 'd008', {'update_groups': ['factory_reset']})
            self.assertEqual(reset['configuration']['update_groups'], ['factory_reset'])
            self.assertEqual(bms.build_options.verify_source(root, 'd008'), reset['configuration'])
            self.assertEqual(bms.build_options.UPDATE_GROUPS['factory_reset'], 512)
            count = 0
            for product in bms.build_options.PRODUCTS:
                models = ('dvc1124',) if product == 'd008' else ('sh3673510', 'sh3673520')
                for model in models:
                    for chemistry in ('lfp', 'nmc'):
                        maximum = (24 if chemistry == 'lfp' else 23) if product == 'd008' else (10 if model == 'sh3673510' else 20)
                        for cells in range(4, maximum+1):
                            result = bms.build_options.save_source(root, product, dict(
                                afe_model=model, chemistry=chemistry, cell_count=cells, update_groups=[]))
                            self.assertEqual(result['configuration']['cell_count'], cells)
                            self.assertEqual(result['configuration']['chemistry'], chemistry)
                            expected_chemistry = 1 if chemistry == 'lfp' else 2
                            code = '#include "bms_product.h"\n'
                            code += f'#if BMS_PRODUCT_CELL_COUNT != {cells} || BMS_PRODUCT_CHEMISTRY != {expected_chemistry}\n#error source_configuration_mismatch\n#endif\n'
                            code += f'#if BMS_PRODUCT_DEFAULT_CAPACITY_0P1AH != {result["configuration"]["capacity_0p1ah"]}\n#error source_capacity_mismatch\n#endif\n'
                            if product != 'd008':
                                code += f'#if BMS_BUILD_AFE_MODEL != {model.removeprefix("sh367")}\n#error source_afe_mismatch\n#endif\n'
                            actual = subprocess.run([*shlex.split(compiler), '-E', '-P', '-x', 'c',
                                '-I', str(root/'bms/products'/product), '-I', str(root/'bms/core'), '-'],
                                input=code, capture_output=True, text=True, timeout=15)
                            self.assertEqual(actual.returncode, 0, actual.stderr)
                            count += 1
            self.assertEqual(count, 41)


@unittest.skipUnless(sys.platform == 'win32', 'Windows 选择菜单')
class AndroidOtaWorkflowTests(unittest.TestCase):
    def create_files(self, root, product='d008', mode='production'):
        copy_configuration(root)
        profile = bms.build_options.source_profile(root) if product == 'd008' else None
        options = bms.build_options.read_source(root, product, profile)
        variant = bms.build_options.variant(mode, product, profile, options)
        firmware = root/'firmware'/variant/product/'825x_ble_sample.bin'
        firmware.parent.mkdir(parents=True)
        firmware.write_bytes(b'fixture-only-not-a-firmware-image')
        sender = root/'Sender.exe'
        sender.write_bytes(b'fixture-only-not-an-executable')
        data = {'product': product, 'bin': str(firmware), 'size_bytes': firmware.stat().st_size,
                'sha256': hashlib.sha256(firmware.read_bytes()).hexdigest(),
                'configuration': {'product': product, 'production': mode=='production', 'build_mode': mode,
                    'd008_profile': profile, 'build_options': options,
                    'parameter_inputs_sha256': bms.build_options.parameter_inputs_hash(root, product, options)}}
        manifest = firmware.with_name('fw_manifest.json')
        manifest.write_text(json.dumps(data), encoding='utf-8')
        commit_configuration(root)
        return firmware, manifest, data, sender

    def runner(self, responses=None):
        return mock.patch.object(android_ota, 'subprocess',
            mock.Mock(run=mock.Mock(side_effect=responses, return_value=mock.Mock(returncode=0))))

    def test_d008_two_modes_build_verify_and_send_without_parameter_overrides(self):
        for product in bms.build_options.PRODUCTS:
            for mode in ('development', 'production'):
                with self.subTest(product=product, mode=mode), tempfile.TemporaryDirectory() as tmp:
                    root = Path(tmp)
                    firmware, _, _, sender = self.create_files(root, product, mode)
                    with self.runner() as process, mock.patch('builtins.print'):
                        self.assertEqual(android_ota.run_workflow(product, sender, root=root, mode=mode), 0)
                    calls = process.run.call_args_list
                    self.assertEqual(len(calls), 4)
                    self.assertEqual(calls[0].args[0][-3:], ['build', '--jobs', '4'])
                    self.assertEqual([call.args[0][-1] for call in calls[1:3]], ['manifest', 'verify'])
                    self.assertEqual(calls[-1].args[0], [str(sender), '--firmware', str(firmware), '--auto-ota'])
                    self.assertTrue(all('BMS_BUILD_OPTIONS' not in call.kwargs['env'] for call in calls))

    def test_every_failed_build_stage_and_failed_sender_is_reported(self):
        for failed_stage in range(4):
            with self.subTest(stage=failed_stage), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                _, _, _, sender = self.create_files(root)
                responses = [mock.Mock(returncode=0) for _ in range(failed_stage)]
                responses += [mock.Mock(returncode=7, stdout='failure', stderr='failure')]
                with self.runner(responses) as process, mock.patch('builtins.print'):
                    self.assertEqual(android_ota.run_workflow('d008', sender, root=root), 7)
                self.assertEqual(process.run.call_count, failed_stage+1)

    def test_wrong_identity_configuration_path_hash_or_size_never_sends(self):
        for fault in ('product', 'profile', 'mode', 'options', 'config_hash', 'path', 'bytes', 'size'):
            with self.subTest(fault=fault), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                firmware, manifest, data, sender = self.create_files(root)
                if fault == 'product': data['product'] = 'd011'
                elif fault == 'profile': data['configuration']['d008_profile'] = '20s-nmc'
                elif fault == 'mode': data['configuration']['build_mode'] = 'development'
                elif fault == 'options': data['configuration']['build_options']['capacity_0p1ah'] = 500
                elif fault == 'config_hash': data['configuration']['parameter_inputs_sha256'] = '0'*64
                elif fault == 'path': data['bin'] = str(root/'old.bin')
                elif fault == 'bytes': firmware.write_bytes(b'wrong')
                else: data['size_bytes'] += 1
                manifest.write_text(json.dumps(data), encoding='utf-8')
                with self.runner() as process, mock.patch('builtins.print'), self.assertRaises(ValueError):
                    android_ota.run_workflow('d008', sender, root=root)
                self.assertEqual(process.run.call_count, 3)

    def test_dirty_source_builds_and_only_environment_or_argument_overrides_stop(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            _, _, _, sender = self.create_files(root)
            for extra in ({'options': {'capacity_0p1ah': 200}}, {'options': {}}):
                with self.runner() as process, self.assertRaises(ValueError):
                    android_ota.run_workflow('d008', sender, root=root, **extra)
                process.run.assert_not_called()
            with mock.patch.dict(android_ota.os.environ, {'BMS_BUILD_OPTIONS': '{}'}), self.runner() as process:
                with self.assertRaises(ValueError):
                    android_ota.run_workflow('d008', sender, root=root)
                process.run.assert_not_called()
            (root/'unrelated.txt').write_text('dirty')
            with self.runner() as process, mock.patch('builtins.print'):
                self.assertEqual(android_ota.run_workflow('d008', sender, root=root), 0)
            self.assertEqual(process.run.call_count, 4)

    def test_send_only_revalidates_and_build_only_does_not_require_sender(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            _, _, _, sender = self.create_files(root)
            with self.runner() as process, mock.patch('builtins.print'):
                self.assertEqual(android_ota.run_workflow('d008', sender, root=root, send_only=True), 0)
            self.assertEqual(process.run.call_count, 2)
            self.assertEqual(process.run.call_args_list[0].args[0][-1], 'verify')
            sender.unlink()
            commit_configuration(root)
            with self.runner() as process, mock.patch('builtins.print'):
                self.assertEqual(android_ota.run_workflow('d008', sender, root=root, build_only=True), 0)
            self.assertEqual(process.run.call_count, 3)

    def test_failed_send_only_verify_and_invalid_combinations_do_not_send(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            _, _, _, sender = self.create_files(root)
            with self.runner([mock.Mock(returncode=8)]) as process, mock.patch('builtins.print'):
                self.assertEqual(android_ota.run_workflow('d008', sender, root=root, send_only=True), 8)
            self.assertEqual(process.run.call_count, 1)
            with self.assertRaises(ValueError):
                android_ota.run_workflow('d008', sender, root=root, send_only=True, build_only=True)
            with self.assertRaises(ValueError):
                android_ota.run_workflow('d008', sender, root=root, mode='invalid')


if __name__ == "__main__":
    unittest.main()
