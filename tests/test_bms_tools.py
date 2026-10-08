from __future__ import annotations

import importlib.util
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
             mock.patch.dict(bms.os.environ, {"EXTRA_DEFINES": ""}):
            flags = bms._effective_extra_defines()
            self.assertIn("-DBMS_PRODUCTION_BUILD=1", flags)
            self.assertIn("-DD008_PRODUCT_PROFILE=1", flags)
            with mock.patch.object(bms, "D008_PROFILE", None), self.assertRaises(SystemExit):
                bms._effective_extra_defines()
            with mock.patch.object(bms, "_firmware_git_dirty", return_value=1), self.assertRaises(SystemExit):
                bms._effective_extra_defines()
            for override in ("-DBMS_PRODUCTION_BUILD=0", "-UBMS_DIAG_BUILD_DIRTY", "-DBMS_DIAG_BUILD_ID=1", "-DD008_PRODUCT_PROFILE=3", "-DBMS_D008_SCD_POLICY_APPROVED=1", "-DBMS_D008_20S_NMC_PROTECTION_APPROVED=1", "-DBMS_D013_HW_CONFIG_APPROVED=1", "-DBMS_PRODUCT_RELEASE_APPROVED=1"):
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
    def test_sanitizer_folded_command_remains_one_shell_command(self):
        import shlex
        from tests.validation_catalog import CHECKS
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        job = workflow.split("  host-sanitizers:\n", 1)[1].split("  tc32-windows:\n", 1)[0]
        block = job.split("run: >-\n", 1)[1].split("\n      - uses:", 1)[0]
        lines = [line for line in block.splitlines() if line.strip()]
        indent = len(lines[0]) - len(lines[0].lstrip())
        self.assertTrue(all(len(line) - len(line.lstrip()) == indent for line in lines), "YAML additional indentation breaks the folded shell command")
        args = shlex.split(" ".join(line.strip() for line in lines))
        selected = [args[i + 1] for i, arg in enumerate(args) if arg == "--only"]
        self.assertEqual(len(selected), len(set(selected)))
        self.assertTrue(set(selected) <= set(CHECKS))

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
             "manifest", "verify", "baseline", "static", "flash-help", "ci", "sources", "test", "release"},
        )


    def test_host_test_selection_runs_once_for_all_products(self):
        for all_products in (False, True):
            with self.subTest(all_products=all_products), mock.patch.object(bms._selection, "all_products", all_products), mock.patch.object(bms.subprocess, "call", return_value=0) as child:
                self.assertEqual(bms.main(["test", "--output", "external-report"]), 0)
                command = child.call_args.args[0]
                self.assertEqual(child.call_count, 1)
                self.assertEqual("--product" in command, not all_products)
                self.assertEqual(command[-2:], ["--output", "external-report"])

    def test_missing_action_reports_parser_error_before_dispatch(self):
        with mock.patch.object(bms._selection, "all_products", True), mock.patch.object(bms, "_cli", []), mock.patch.object(bms.sys, "stderr"), mock.patch.object(bms.subprocess, "call") as child:
            with self.assertRaises(SystemExit) as caught:bms.main()
            self.assertEqual(caught.exception.code, 2)
            child.assert_not_called()

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


class AndroidOtaWorkflowTests(unittest.TestCase):
    def create_files(self, root, target, mode='production'):
        product, profile = android_ota.TARGETS[target]
        variant = mode + ('-' + profile if profile else '')
        firmware = root / 'firmware' / variant / product / '825x_ble_sample.bin'
        firmware.parent.mkdir(parents=True)
        firmware.write_bytes(b'old-or-new-fixture')
        data = {'product': product, 'bin': str(firmware),
                'size_bytes': firmware.stat().st_size,
                'sha256': hashlib.sha256(firmware.read_bytes()).hexdigest(),
                'configuration': {'product': product, 'production': mode == 'production',
                                  'build_mode': mode,
                                  'd008_profile': profile}}
        manifest = firmware.with_name('fw_manifest.json')
        manifest.write_text(json.dumps(data), encoding='utf-8')
        sender = root / 'Sender.exe'
        sender.write_bytes(b'not-an-executable')
        return firmware, manifest, data, sender

    def test_all_six_selections_build_verify_and_send_the_matching_image(self):
        for target, (product, profile) in android_ota.TARGETS.items():
            with self.subTest(target=target), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                firmware, _, _, sender = self.create_files(root, target)
                with mock.patch.object(android_ota.subprocess, 'run', return_value=mock.Mock(returncode=0)) as run, mock.patch('builtins.print'):
                    self.assertEqual(android_ota.run_workflow(target, sender, root=root), 0)
                commands = [call.args[0] for call in run.call_args_list]
                for command in commands[:3]:
                    self.assertEqual(command[command.index('--product') + 1], product)
                    self.assertIn('--production', command)
                    if profile:
                        self.assertEqual(command[command.index('--d008-profile') + 1], profile)
                    else:
                        self.assertNotIn('--d008-profile', command)
                self.assertEqual(commands[0][-3:], ['build', '--jobs', '4'])
                self.assertEqual([command[-1] for command in commands[1:3]], ['manifest', 'verify'])
                self.assertEqual(commands[3], [str(sender), '--firmware', str(firmware), '--auto-ota'])

    def test_each_failed_stage_stops_before_sending_an_existing_old_bin(self):
        for failed_stage in range(3):
            with self.subTest(stage=failed_stage), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                _, _, _, sender = self.create_files(root, 'd008-16s-lfp')
                responses = [mock.Mock(returncode=0) for _ in range(failed_stage)]
                responses.append(mock.Mock(returncode=7, stdout='failure', stderr='failure'))
                with mock.patch.object(android_ota.subprocess, 'run', side_effect=responses) as run, mock.patch('builtins.print'):
                    self.assertEqual(android_ota.run_workflow('d008-16s-lfp', sender, root=root), 7)
                self.assertEqual(run.call_count, failed_stage + 1)

    def test_wrong_product_profile_path_or_changed_bin_is_never_sent(self):
        for fault in ('product', 'profile', 'path', 'bytes'):
            with self.subTest(fault=fault), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                firmware, manifest, data, sender = self.create_files(root, 'd008-16s-lfp')
                if fault == 'product':
                    data['product'] = 'd014'
                elif fault == 'profile':
                    data['configuration']['d008_profile'] = '24s-lfp'
                elif fault == 'path':
                    data['bin'] = str(root / 'wrong.bin')
                else:
                    firmware.write_bytes(b'changed')
                manifest.write_text(json.dumps(data), encoding='utf-8')
                with mock.patch.object(android_ota.subprocess, 'run', return_value=mock.Mock(returncode=0)) as run, mock.patch('builtins.print'):
                    with self.assertRaises(ValueError):
                        android_ota.run_workflow('d008-16s-lfp', sender, root=root)
                self.assertEqual(run.call_count, 3)

    def test_missing_sender_fails_before_build_and_build_only_never_connects(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            _, _, _, sender = self.create_files(root, 'd008-16s-lfp')
            sender.unlink()
            with mock.patch.object(android_ota.subprocess, 'run', return_value=mock.Mock(returncode=0)) as run, mock.patch('builtins.print'):
                with self.assertRaises(ValueError):
                    android_ota.run_workflow('d008-16s-lfp', sender, root=root)
                run.assert_not_called()
                self.assertEqual(android_ota.run_workflow('d008-16s-lfp', sender, root=root, build_only=True), 0)
                self.assertEqual(run.call_count, 3)

    def test_sender_failure_remains_a_failure(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            _, _, _, sender = self.create_files(root, 'd008-16s-lfp')
            responses = [mock.Mock(returncode=code) for code in (0, 0, 0, 9)]
            with mock.patch.object(android_ota.subprocess, 'run', side_effect=responses), mock.patch('builtins.print'):
                self.assertEqual(android_ota.run_workflow('d008-16s-lfp', sender, root=root), 9)

    def test_shortcut_selections_match_the_workflow_targets(self):
        tasks = json.loads((REPO_ROOT / '.vscode/tasks.json').read_text(encoding='utf-8'))
        default = [task for task in tasks['tasks'] if isinstance(task.get('group'), dict)
                   and task['group'].get('isDefault')]
        self.assertEqual(len(default), 1)
        self.assertEqual(default[0]['label'], 'BMS: 选择配置编译并发送 OTA')
        self.assertEqual(default[0]['args'][-1], '${workspaceFolder}/bms_tools/select_firmware_build.ps1')
        ota = next(task for task in tasks['tasks'] if task['label'] == 'BMS: 编译并发送固件到 Android')
        self.assertEqual(ota['args'], ['bms_tools/android_ota.py', '--target', '${input:otaTarget}'])
        selection = next(item for item in tasks['inputs'] if item['id'] == 'otaTarget')
        self.assertEqual({item['value'] for item in selection['options']}, set(android_ota.TARGETS))

    def test_all_twelve_bin_configurations_use_separate_paths_and_the_selected_mode(self):
        for mode in ('production', 'development'):
            for target in android_ota.TARGETS:
                with self.subTest(mode=mode, target=target), tempfile.TemporaryDirectory() as tmp:
                    root = Path(tmp)
                    firmware, _, _, sender = self.create_files(root, target, mode)
                    sender.unlink()
                    with mock.patch.object(android_ota.subprocess, 'run', return_value=mock.Mock(returncode=0)) as run, mock.patch('builtins.print'):
                        self.assertEqual(android_ota.run_workflow(target, sender, root=root, mode=mode, build_only=True, rebuild=True), 0)
                    commands = [call.args[0] for call in run.call_args_list]
                    self.assertEqual(len(commands), 3)
                    self.assertEqual(commands[0][-3:], ['rebuild', '--jobs', '4'])
                    self.assertTrue(all(('--production' in command) == (mode == 'production') for command in commands))
                    self.assertTrue(firmware.is_file())

    def test_manifest_mode_mismatch_and_invalid_mode_stop_before_send(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            _, manifest, data, sender = self.create_files(root, 'd014')
            data['configuration']['build_mode'] = 'development'
            manifest.write_text(json.dumps(data), encoding='utf-8')
            with mock.patch.object(android_ota.subprocess, 'run', return_value=mock.Mock(returncode=0)) as run, mock.patch('builtins.print'):
                with self.assertRaises(ValueError):
                    android_ota.run_workflow('d014', sender, root=root)
                self.assertEqual(run.call_count, 3)
                run.reset_mock()
                with self.assertRaises(ValueError):
                    android_ota.run_workflow('d014', sender, root=root, mode='invalid')
                run.assert_not_called()

    def test_send_only_revalidates_without_rebuilding_and_requests_auto_ota(self):
        for mode in ('production', 'development'):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                firmware, _, _, sender = self.create_files(root, 'd008-16s-lfp', mode)
                with mock.patch.object(android_ota.subprocess, 'run', return_value=mock.Mock(returncode=0)) as run, mock.patch('builtins.print'):
                    self.assertEqual(android_ota.run_workflow('d008-16s-lfp', sender, root=root, mode=mode, send_only=True), 0)
                self.assertEqual(run.call_count, 2)
                self.assertEqual(run.call_args_list[0].args[0][-1], 'verify')
                self.assertEqual(run.call_args_list[1].args[0], [str(sender), '--firmware', str(firmware), '--auto-ota'])

    def test_send_only_verification_failure_never_calls_sender(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            _, _, _, sender = self.create_files(root, 'd014')
            with mock.patch.object(android_ota.subprocess, 'run', return_value=mock.Mock(returncode=8)) as run, mock.patch('builtins.print'):
                self.assertEqual(android_ota.run_workflow('d014', sender, root=root, send_only=True), 8)
                self.assertEqual(run.call_count, 1)
                run.reset_mock()
                with self.assertRaises(ValueError):
                    android_ota.run_workflow('d014', sender, root=root, send_only=True, build_only=True)
                run.assert_not_called()

    @unittest.skipUnless(sys.platform == 'win32', 'Windows 选择菜单')
    def test_picker_single_selection_builds_then_sends_and_failed_build_stops(self):
        script = REPO_ROOT / 'bms_tools/select_firmware_build.ps1'
        for code, expected_calls in ((0, 2), (7, 1)):
            with self.subTest(code=code):
                # 用 PowerShell 函数替代 Python 进程，验证真实菜单调度，不碰手机。
                source = '[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new(); '
                source += 'function python { Write-Output ("CALL:" + (ConvertTo-Json -InputObject @($args) -Compress)); '
                source += '$global:LASTEXITCODE = ' + str(code) + ' }; '
                source += "& '" + str(script).replace("'", "''") + "' -Configurations 'development:d014'"
                result = subprocess.run(['powershell.exe', '-NoProfile', '-Command', source],
                                        capture_output=True, encoding='utf-8', timeout=15)
                self.assertEqual(result.returncode, 0 if code == 0 else 1)
                calls = [json.loads(line[5:]) for line in result.stdout.splitlines() if line.startswith('CALL:')]
                self.assertEqual(len(calls), expected_calls)
                self.assertEqual(calls[0][-1], '--build-only')
                if code == 0:
                    self.assertEqual(calls[1], [str(REPO_ROOT / 'bms_tools/android_ota.py'), '--target', 'd014', '--mode', 'development', '--send-only'])

    @unittest.skipUnless(sys.platform == 'win32', 'Windows 选择菜单')
    def test_picker_batch_build_only_suppresses_sending_and_ota_selection(self):
        script = REPO_ROOT / 'bms_tools/select_firmware_build.ps1'
        source = '[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new(); '
        source += 'function python { Write-Output ("CALL:" + (ConvertTo-Json -InputObject @($args) -Compress)); $global:LASTEXITCODE = 0 }; '
        source += "& '" + str(script).replace("'", "''") + "' -BuildOnly -Configurations @('development:d014','production:d008-16s-lfp')"
        result = subprocess.run(['powershell.exe', '-NoProfile', '-Command', source],
                                capture_output=True, encoding='utf-8', timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        calls = [json.loads(line[5:]) for line in result.stdout.splitlines() if line.startswith('CALL:')]
        self.assertEqual(len(calls), 2)
        self.assertTrue(all(call[-1] == '--build-only' for call in calls))

    @unittest.skipUnless(sys.platform == 'win32', 'Windows 选择菜单')
    def test_batch_picker_plans_all_configurations_without_building_or_sending(self):
        script = REPO_ROOT / 'bms_tools/select_firmware_build.ps1'
        selections = [mode + ':' + target for mode in ('production', 'development')
                      for target in android_ota.TARGETS]
        # 参数都是受控常量；单引号加倍只用于仓库路径。
        source = "& '" + str(script).replace("'", "''") + "' -PlanOnly -Rebuild -Configurations @("
        source += ','.join("'" + selection + "'" for selection in selections) + ')'
        result = subprocess.run(['powershell.exe', '-NoProfile', '-Command',
                                 '[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new(); ' + source],
                                capture_output=True, encoding='utf-8', timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        commands = json.loads(result.stdout)
        self.assertEqual(len(commands), 12)
        for entry, selection in zip(commands, selections):
            mode, target = selection.split(':')
            self.assertEqual(entry['Configuration'], selection)
            self.assertEqual(entry['Arguments'], [str(REPO_ROOT / 'bms_tools/android_ota.py'), '--target', target,
                                                 '--mode', mode, '--build-only', '--rebuild'])

    @unittest.skipUnless(sys.platform == 'win32', 'Windows 选择菜单')
    def test_batch_picker_rejects_unknown_selection_before_starting_a_build(self):
        script = REPO_ROOT / 'bms_tools/select_firmware_build.ps1'
        result = subprocess.run(['powershell.exe', '-NoProfile', '-File', str(script),
                                 '-PlanOnly', '-Configurations', 'production:unknown'],
                                capture_output=True, encoding='utf-8', errors='replace', timeout=15)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Unknown build configuration', result.stderr)


if __name__ == "__main__":
    unittest.main()
