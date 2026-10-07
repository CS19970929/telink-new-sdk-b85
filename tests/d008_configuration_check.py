"""同领域检查直接执行各场景；场景状态独立，断言与故障注入保留。"""
from __future__ import annotations

def check_d008_20s_profile_contract_check():
    print("CHECK d008_20s_profile_contract_check", flush=True)
    #!/usr/bin/env python3
    """D008 20S-NMC compile-profile and DVC channel-mask safety contracts."""
    from pathlib import Path
    from project_paths import Sources, host_includes, selected_source
    import re

    ROOT = Path(__file__).resolve().parents[1]
    HERE = Sources(ROOT)
    profile = (HERE / 'd008_product_profile.h').read_text(encoding='utf-8')
    dvc = (HERE / 'dvc1124.c').read_text(encoding='utf-8')
    cfg = (HERE / 'dvc1124_project_config.h').read_text(encoding='utf-8')
    boot = (HERE / 'dvc1124_boot.c').read_text(encoding='utf-8')
    store = dvc + boot
    service = (HERE / 'dvc1124_config_service.c').read_text(encoding='utf-8')

    assert '#define D008_PRODUCT_PROFILE_20S_NMC  2u' in profile
    block = profile.split('#elif (D008_PRODUCT_PROFILE == D008_PRODUCT_PROFILE_20S_NMC)', 1)[1].split('#else', 1)[0]
    assert '#define D008_PRODUCT_CELL_COUNT       20u' in block
    assert 'BMS_SOC_CHEMISTRY_NMC' in block
    assert 'BMS_SOC_PROFILE_GENERIC_NMC' in block
    assert '#define DVC1124_DEFAULT_CELL_COUNT           D008_PRODUCT_CELL_COUNT' in cfg
    assert 'DVC1124_SetCellCount((uint8_t)DVC1124_DEFAULT_CELL_COUNT)' in dvc

    # Reproduce the driver mask algorithm: for 20S, only channels 21..24 are masked.
    def masks(cell_count: int):
        out = [0, 0, 0]
        for cell in range(5, 25):
            if cell <= cell_count:
                continue
            if cell >= 17:
                out[0] |= 1 << (cell - 17)
            elif cell >= 9:
                out[1] |= 1 << (cell - 9)
            else:
                out[2] |= 1 << (cell - 1)
        return out
    assert masks(20) == [0xF0, 0x00, 0x00]
    assert masks(24) == [0x00, 0x00, 0x00]
    assert 'for (cell = 5u; cell <= DVC1124_MAX_CELLS; ++cell)' in dvc

    # D008 board invariants are compile-time owned and re-applied after every init.
    assert 'ConfigStoreLoad' not in store and 'ConfigStoreRestore' not in store
    assert 'DVC1124_DEFAULT_HIGH_SIDE_FET_MASK' in dvc
    assert 'DVC1124_EncodeCurrentWake(DVC1124_CURRENT_WAKE_THRESHOLD_UV' in store
    assert 'DVC1124_DEFAULT_DSG_MASK_POLICY' in store
    assert 'DVC1124_DEFAULT_CHG_MASK_POLICY' in store
    assert 'DVC1124_ApplyProjectOperatingConfig()' in store
    assert 's_project_config_pending = 1u' in store
    assert 'DVC1124_CFG_ERR_READ_ONLY' in service
    assert 'DVC1124_ConfigStoreSave' not in service

    # Open-wire results are raw measurements only; no unverified open-wire trip rule.
    assert 'DVC1124_OpenWireBegin' in dvc and 'DVC1124_OpenWirePoll' in dvc
    ow = dvc.split('void DVC1124_OpenWirePoll', 1)[1].split('void DVC1124_OpenWireGetResult', 1)[0]
    assert 'cell_mv' in ow and 'OPENWIRE_READY' in ow
    assert 'bms_error_raise' not in ow

    # Balancing is armed by request and renewed below the ~60s hardware auto-clear.
    assert '#define DVC_BALANCE_REFRESH_INTERVAL_US 45000000u' in dvc
    assert 'DVC1124_BalanceService' in dvc
    assert 's_openwire_result.state == DVC1124_OPENWIRE_WAITING' in dvc

    print('D008 20S/product safety completion contract: PASS')

def check_d008_profile_selection_host_check():
    print("CHECK d008_profile_selection_host_check", flush=True)
    from pathlib import Path
    from project_paths import Sources, host_includes, selected_source
    import tempfile,subprocess,os,shlex
    ROOT=Path(__file__).resolve().parents[1]
    APP = Sources(ROOT)
    with tempfile.TemporaryDirectory(prefix='d008-profile-') as d:
        p=Path(d)/'profile.c'
        for selector,count in ((None,16),(1,24),(2,20),(3,16)):
            p.write_text('#include "d008_product_profile.h"\n#if D008_PRODUCT_CELL_COUNT != %d\n#error inconsistent_profile\n#endif\nint main(void){return 0;}\n'%count)
            subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c99',*host_includes(ROOT),str(p),'-o',str(Path(d)/'profile.exe')]+([] if selector is None else ['-DD008_PRODUCT_PROFILE=%d'%selector]),check=True)
    print('PASS default 16S and explicit 24S/20S/16S identities')

    with tempfile.TemporaryDirectory(prefix='d008-release-profile-') as d:
        p=Path(d)/'profile.c'
        p.write_text('#include "d008_product_profile.h"\n')
        base=shlex.split(os.environ.get('CC','cc'))+['-E','-DBMS_PRODUCTION_BUILD=1',*host_includes(ROOT),str(p)]
        assert subprocess.run(base,capture_output=True).returncode != 0
        for selector in (1,2,3):
            subprocess.run(base+['-DD008_PRODUCT_PROFILE=%d'%selector],check=True,stdout=subprocess.DEVNULL)
    print('PASS production rejects implicit D008 profile; all three explicit profiles accepted')

def check_d008_framework_contract_check():
    print("CHECK d008_framework_contract_check", flush=True)
    #!/usr/bin/env python3
    """D008/DVC1124 architecture, safety and configuration-ownership contracts."""

    import re
    import unittest
    from pathlib import Path
    from project_paths import Sources, host_includes, selected_source

    ROOT = Path(__file__).resolve().parents[1]
    SRC = Sources(ROOT)


    def read(name):
        return selected_source(SRC / name) if name=="app.c" else (SRC / name).read_text(encoding="utf-8", errors="ignore")


    def macro_literal(text, name):
        m = re.search(
            rf"^\s*#define\s+{re.escape(name)}\s+\(?\s*(0x[0-9A-Fa-f]+|[0-9]+)u?\s*\)?",
            text,
            re.MULTILINE,
        )
        if not m:
            raise AssertionError(f"literal macro not found: {name}")
        return int(m.group(1), 0)


    class D008FrameworkContract(unittest.TestCase):
        @classmethod
        def setUpClass(cls):
            cls.backend_h = read("bms_afe_backend.h") + read("bms_product_config.h")
            cls.afe_h = read("bms_afe.h") + read("bms_afe_driver.h")
            cls.guard = read("bms_afe_guard.c")
            cls.dvc = read("dvc1124.c")
            cls.dvc_bms = read("dvc1124_bms.c")
            cls.fixed_backend = read("dvc1124.c") + read("dvc1124_boot.c")
            cls.service = read("dvc1124_config_service.c")
            cls.project = read("dvc1124_project_config.h")
            cls.product = read("d008_product_profile.h")
            cls.features = read("bms_features.c")
            cls.param = read("bms_parameters.c")
            cls.app = read("app.c")
            cls.app_config = read("app_config.h")
            cls.parameter_access = read("bms_parameter_access.c")
            cls.conf = read("bms_product_conf.h")
            cls.hw_profile = read("bms_afe_hw_profile.c")

        def test_backend_defaults_to_dvc1124(self):
            self.assertIn("BMS_AFE_BACKEND_DVC1124", self.backend_h)
            self.assertRegex(self.backend_h, r"#define\s+BMS_AFE_BACKEND\s+1")
            self.assertIn("dvc1124_backend_sample", self.afe_h)

        def test_guard_comm_loss_uses_hardware_watchdog_silence(self):
            self.assertEqual(macro_literal(self.guard, "BMS_AFE_VALID_SNAPSHOT_RELEASE_COUNT"), 3)
            self.assertEqual(macro_literal(self.guard, "BMS_AFE_COMM_FAILS_BEFORE_SILENCE"), 2)
            self.assertEqual(macro_literal(self.guard, "BMS_AFE_FAILSAFE_WAIT_SAMPLES"), 25)
            self.assertIn("s_guard.comm_inhibit = 1u;", self.guard)
            self.assertIn("s_guard.bus_silenced = 1u;", self.guard)
            self.assertIn("if (s_guard.test_shutdown_hold || service_failsafe_wait()) return;", self.guard)
            self.assertIn("if (s_guard.comm_failures == 0u) best_effort_shutdown();", self.guard)
            self.assertIn("AFE_INIT();", self.guard)
            self.assertNotIn("BMS_AFE_REINIT_TRIGGER", self.guard)
            self.assertNotIn("BMS_AFE_REINIT_COOLDOWN_SAMPLES", self.guard)
            self.assertIn("valid_snapshot_streak", self.guard)
            self.assertIn("bms_features_on_afe_invalid", self.guard)
            self.assertIn("bms_afe_bus_access_allowed", self.guard)

        def test_guard_owns_shutdown_wake_test_lifecycle(self):
            for token in (
                "test_shutdown_hold",
                "bms_afe_test_enter_shutdown",
                "bms_afe_test_wake",
                "if (s_guard.test_shutdown_hold || service_failsafe_wait()) return;",
                "AFE_TEST_SHUTDOWN()",
                "AFE_INIT();",
                "s_guard.valid_snapshot_streak = 0u;",
            ):
                self.assertIn(token, self.guard)
            self.assertNotIn("DVC1124_AFE_WakeupFromShutdown", self.fixed_backend)

        def test_guard_keeps_requested_state_separate_from_feedback(self):
            self.assertIn("requested_charge_on", self.guard)
            self.assertIn("requested_discharge_on", self.guard)
            self.assertIn("bms_afe_get_requested_fets", self.guard)
            set_fets = re.search(
                r"(?s)uint8_t\s+bms_afe_set_fets\s*\([^)]*\)\s*\{.*?\n\}",
                self.guard,
            )
            self.assertIsNotNone(set_fets)
            self.assertNotIn("b1Status_MOS_CHG =", set_fets.group(0))
            self.assertNotIn("b1Status_MOS_DSG =", set_fets.group(0))

        def test_mos_report_is_owned_by_dvc_feedback(self):
            self.assertRegex(
                self.dvc,
                r"b1Status_MOS_CHG\s*=\s*\(data\[DVC1124_REG_CC2_L_FLAGS\]\s*&\s*DVC1124_CC2_CHGF_MASK\)",
            )
            self.assertRegex(
                self.dvc,
                r"b1Status_MOS_DSG\s*=\s*\(data\[DVC1124_REG_CC2_L_FLAGS\]\s*&\s*DVC1124_CC2_DSGF_MASK\)",
            )
            self.assertNotRegex(self.app, r"b1Status_MOS_(?:CHG|DSG)\s*=")
            self.assertNotRegex(self.dvc_bms, r"b1Status_MOS_(?:CHG|DSG)\s*=")

        def test_common_port_policy_uses_auto_diode_without_off_pulse(self):
            policy = self.dvc_bms.split("static uint8_t dvc_apply_common_port_fet_state", 1)[1]
            policy = policy.split("void DVC1124_BmsApp_AFEGet", 1)[0]
            self.assertIn("DVC1124_FET_DRIVE_AUTO_DIODE", policy)
            self.assertIn("dvc_set_fet_modes_if_changed", policy)
            self.assertNotIn("DVC1124_SetMosState", policy)
            self.assertNotIn("DVC1124_WriteRegisterFieldSafe", policy)

        def test_normal_app_requests_both_common_port_fets(self):
            fn = self.app.split("void mos_update(void)", 1)[1]
            fn = fn.split("static void board_init", 1)[0]
            self.assertIn("bms_afe_set_fets(1u, 1u)", fn)
            self.assertNotIn("IsKeyWakeupActive", fn)
            self.assertNotIn("Runtime_GetMode()", fn)

        def test_app_has_no_legacy_mcu_adc_or_cert_temperature_path(self):
            self.assertNotIn("app_adc_multi_sample", self.app)
            self.assertNotIn("iSheldTemp_10K_mcu", self.app)
            self.assertNotIn("battery_check.h", self.app)
            self.assertNotIn("user_battery_power_check", self.app)
            self.assertNotIn("battery_clear_adc_setting_flag", self.app)
            self.assertNotIn("_UL_RENZHENG_ENABLE_", self.app)
            self.assertNotIn("_UL_RENZHENG_ENABLE_", self.conf)
            self.assertNotIn("BMS_FAULT_MOS_OTP_THIRD", self.app)
            self.assertIn("gpio_write(RF_EN_PIN, 0);", self.app)

        def test_dvc_is_single_temperature_owner_for_protection_and_reporting(self):
            sample = self.dvc_bms.split("void DVC1124_BmsApp_AFEGet", 1)[1]
            sample = sample.split("uint8_t dvc1124_backend_set_fets", 1)[0]
            self.assertIn("dvc_get_battery_temperature_range", sample)
            self.assertIn("DVC1124_DEFAULT_MOS_NTC_GP", sample)
            self.assertIn("dvc_publish_temperature_report(&sw);", sample)
            self.assertIn("bms_sw_protection_update_groups(&sw, DVC1124_SW_PROTECT_ENABLE,", sample)
            self.assertIn("DVC1124_DEFAULT_BATTERY_NTC_GP       2u", self.project)
            self.assertIn("DVC1124_DEFAULT_BATTERY_NTC2_GP      3u", self.project)
            self.assertIn("DVC1124_DEFAULT_MOS_NTC_GP           4u", self.project)
            self.assertIn("u16Temperature[ENV_TEMP3]", self.dvc_bms)
            self.assertIn("u16Temperature[MOS_TEMP1]", self.dvc_bms)
            self.assertIn("u16TempMin = sw->battery_temp_min", self.dvc_bms)
            self.assertIn("u16TempMax = sw->battery_temp_max", self.dvc_bms)

        def test_protection_switches_default_to_production(self):
            self.assertEqual(macro_literal(self.project, "DVC1124_SW_PROTECT_ENABLE"), 1)
            self.assertEqual(macro_literal(self.project, "DVC1124_HW_PROTECT_ENABLE"), 1)
            self.assertIn("DVC1124 protection enable macros must be 0 or 1", self.project)

        def test_hw_off_really_disables_dvc_autonomous_protection(self):
            block = self.dvc.split("static uint8_t dvc_disable_threshold_protection", 1)[1]
            block = block.split("#endif", 1)[0]
            self.assertIn("DVC1124_SetShortCircuitProtection(0u, 0u)", block)
            self.assertIn("DVC1124_REG_BODY_DIODE, 0u", block)
            self.assertIn("DVC1124_I2C_WDT_OFF", block)
            self.assertIn("DVC1124_REG_DSG_MASK, 0xFFu", block)
            self.assertIn("DVC1124_REG_CHG_MASK, 0xFFu", block)

        def test_sw_off_clears_software_managed_fault_state(self):
            sample = self.dvc_bms.split("void DVC1124_BmsApp_AFEGet", 1)[1]
            sample = sample.split("uint8_t dvc1124_backend_set_fets", 1)[0]
            self.assertIn("DVC1124_SW_TEMP_PROTECT_ENABLE", sample)
            self.assertIn("bms_sw_protection_update_groups(&sw, DVC1124_SW_PROTECT_ENABLE,", sample)
            self.assertNotIn("bms_sw_protection_clear();", sample)
            self.assertLess(sample.index("dvc_publish_temperature_report(&sw);"),
                            sample.index("bms_sw_protection_update_groups"))

        def test_fixed_dvc_operating_config_has_no_flash_owner(self):
            for token in (
                "flash_kv32",
                "ConfigStoreLoad",
                "ConfigStoreSave",
                "ConfigStoreRestore",
                "ConfigStoreCapture",
            ):
                self.assertNotIn(token, self.fixed_backend)
            self.assertIn("DVC1124_ApplyProjectOperatingConfig", self.fixed_backend)

        def test_fixed_config_is_reapplied_after_afe_reset(self):
            self.assertIn("DVC1124_AFE_Reset();", self.fixed_backend)
            self.assertIn("DVC1124_UpdataAfeConfig();", self.fixed_backend)
            self.assertIn("s_project_config_pending", self.fixed_backend)
            self.assertIn("DVC1124_ApplyProjectOperatingConfig()", self.fixed_backend)

        def test_fixed_semantic_window_is_diagnostic_only(self):
            self.assertNotIn("dvc1124_boot.h", self.service)
            self.assertNotIn("DVC1124_ConfigStore", self.service)
            self.assertIn("DVC1124_CFG_ERR_READ_ONLY", self.service)
            self.assertIn("bms_afe_bus_access_allowed", self.service)
            raw = self.service.split("DVC1124_ConfigServiceWriteRaw", 1)[1]
            self.assertIn("DVC1124_CFG_ERR_READ_ONLY", raw)
            self.assertNotIn("DVC1124_WriteRegisters", raw)

        def test_fail_safe_watchdog_policy_is_project_compile_time(self):
            self.assertEqual(macro_literal(self.project, "DVC1124_I2C_WATCHDOG_SECONDS"), 4)
            self.assertEqual(macro_literal(self.project, "DVC1124_I2C_TIMEOUT_CLOSE_CHG"), 1)
            self.assertEqual(macro_literal(self.project, "DVC1124_I2C_TIMEOUT_CLOSE_DSG"), 1)
            self.assertNotIn("DVC1124_I2C_WATCHDOG_SECONDS", self.product)
            self.assertIn("DVC1124_DSGMASK_DWM_MASK", self.fixed_backend)
            self.assertIn("DVC1124_CHGMASK_CWM_MASK", self.fixed_backend)

        def test_body_diode_policy_remains_common_port_compile_time(self):
            self.assertEqual(macro_literal(self.project, "DVC1124_BODY_DIODE_THRESHOLD_UV"), 80)
            self.assertIn("DVC1124_DSGMASK_DBDM_MASK", self.project)
            self.assertIn("DVC1124_CHGMASK_CBDM_MASK", self.project)
            self.assertIn("DVC1124_BODY_DIODE_THRESHOLD_UV", self.fixed_backend)

        def test_only_protection_profiles_remain_runtime_persistent_for_afe(self):
            self.assertIn("bms_afe_hw_profile_get(&hw)", self.service)
            self.assertIn("bms_afe_hw_profile_get", self.hw_profile)
            self.assertIn("dvc1124_backend_apply_protection_config", self.fixed_backend)
            fn = self.fixed_backend.split("uint8_t dvc1124_backend_apply_protection_config", 1)[1]
            self.assertIn("DVC1124_ApplyProtectionConfig()", fn)

        def test_product_profile_still_selects_physical_cell_count_and_soc_identity(self):
            self.assertEqual(macro_literal(self.product, "D008_PRODUCT_PROFILE_24S_LFP"), 1)
            self.assertEqual(macro_literal(self.product, "D008_PRODUCT_PROFILE_20S_NMC"), 2)
            self.assertEqual(macro_literal(self.product, "D008_PRODUCT_PROFILE_16S_LFP"), 3)
            self.assertIn("#define D008_PRODUCT_PROFILE D008_PRODUCT_PROFILE_16S_LFP", self.product)
            self.assertIn("D008 production requires an explicit D008_PRODUCT_PROFILE", self.product)
            self.assertIn("D008_PRODUCT_CELL_COUNT       16u", self.product)
            self.assertIn("D008_PRODUCT_CELL_COUNT       20u", self.product)
            self.assertIn("D008_PRODUCT_CELL_COUNT       24u", self.product)
            self.assertIn('D008_PRODUCT_PROFILE_NAME     "D008-16S-LFP"', self.product)
            self.assertIn("DVC1124_DEFAULT_CELL_COUNT           D008_PRODUCT_CELL_COUNT", self.project)

        def test_production_build_blocks_debug_and_protection_isolation(self):
            self.assertIn("BMS_PRODUCTION_BUILD", self.app_config)
            self.assertIn("Production build requires a nonzero Git diagnostic build ID", self.app_config)
            self.assertIn("Production build requires a clean Git worktree", self.app_config)
            self.assertIn("Production build forbids __TEST_SOC__ command hooks", self.app_config)
            self.assertIn("Production build forbids current-test, debug GPIO and UART debug output", self.app_config)
            self.assertIn("Production build requires SDK flash protection", self.app_config)
            self.assertIn("Production build requires watchdog", self.app_config)
            self.assertIn("Production build requires software, hardware and temperature protection enabled", self.project)
            self.assertIn("sensitive_factory_write_allowed", self.parameter_access)
            self.assertNotIn("Runtime_GetMode()", self.parameter_access)
            self.assertIn("bms_afe_hw_access_is_active()", self.parameter_access)
            self.assertGreaterEqual(self.parameter_access.count("sensitive_factory_write_allowed()"), 3)

        def test_no_legacy_parameter_migration(self):
            self.assertNotIn("param_apply_d008_product_identity_if_unset", self.param)
            self.assertNotIn("param_migrate_temperature_protection_v1", self.param)
            self.assertIn("bms_config_store_validate_startup()", self.param)

        def test_invalid_software_protection_params_block_outputs(self):
            self.assertIn("static uint8_t s_protection_params_valid", self.param)
            self.assertIn("uint8_t bms_protection_params_valid(void)", self.param)
            self.assertIn("s_protection_params_valid = 0u;", self.param)
            self.assertIn("s_protection_params_valid = 1u;", self.param)
            self.assertIn("!bms_protection_params_valid()", self.features)

        def test_startup_update_failure_has_separate_gate(self):
            self.assertIn("s_protection_params_valid && s_storage_startup_valid", self.param)
            self.assertIn("s_storage_startup_valid = 0u", self.param)
            self.assertNotIn("param_upgrade_mark_epoch", self.param)

        def test_openwire_and_balance_safety_gate_remain(self):
            self.assertIn("bms_afe_openwire_start", self.features)
            self.assertIn("apply_balance_mask(0u)", self.features)
            self.assertIn("openwire_suspected", self.features)
            self.assertIn("balance_voltage_trusted", self.features)
            self.assertNotIn("u16VdeltaOvp_First", self.features)
            self.assertIn("DVC1124_OpenWireBegin", self.dvc)
            self.assertIn("DVC1124_BalanceService", self.dvc)

        def test_source_order_still_contains_backend_lifecycle_unit(self):
            order = (ROOT / "bms/products/d008/sources.txt").read_text(encoding="utf-8")
            self.assertIn("bms/afe/dvc1124/dvc1124_boot.c", order)
            self.assertIn("bms/afe/dvc1124/dvc1124_config_service.c", order)


    if __name__ == "__main__":
        assert unittest.TextTestRunner(verbosity=2).run(unittest.TestSuite(unittest.defaultTestLoader.loadTestsFromTestCase(c) for c in (D008FrameworkContract,))).wasSuccessful()

def check_d008_documentation_contract_check():
    print("CHECK d008_documentation_contract_check", flush=True)
    #!/usr/bin/env python3
    """Keep D008 AFE product documentation aligned with compile-time policy."""
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[1]
    DOCS = ROOT / "docs"

    product = (DOCS / "D008_PRODUCT_REFERENCE.md").read_text(encoding="utf-8")
    afe = (DOCS / "AFE_HARDWARE_PROTECTION_V2.md").read_text(encoding="utf-8")
    guide = (DOCS / "CONFIGURATION_AND_BUILD_GUIDE.md").read_text(encoding="utf-8")
    readme = (ROOT / "README.md").read_text(encoding="utf-8")
    agents = (ROOT / "AGENTS.md").read_text(encoding="utf-8")

    for token in (
        "GP1 | heater NTC",
        "GP2 | battery NTC #1",
        "GP3 | battery NTC #2",
        "GP4 | power MOS NTC",
        "GP5 | CHG low-side",
        "GP6 | DSG low-side",
        "Rsense | RS1..RS10 = 10 × 2 mΩ",
        "I2C watchdog | **4 s**",
        "Body diode threshold | **80 µV**",
        "每次 AFE reset/init 后重新下发",
    ):
        if token not in product:
            raise AssertionError(f"D008 product documentation drift: {token}")

    truth = "feature/windows-afe-hw-protection-editor-v2"
    for name, text in (("AGENTS.md", agents), ("README", readme), ("AFE hardware doc", afe), ("build guide", guide)):
        if truth not in text or "bms-tool-windows/" not in text:
            raise AssertionError(f"Windows source-of-truth missing from {name}")

    for name, text in (("README", readme), ("AFE hardware doc", afe), ("build guide", guide)):
        if "tools/BMSAssistantQt" in text:
            raise AssertionError(f"retired Qt client still presented as usable in {name}")

    print("D008 documentation contract: PASS")

if __name__ == "__main__":
    check_d008_20s_profile_contract_check()
    check_d008_profile_selection_host_check()
    check_d008_framework_contract_check()
    check_d008_documentation_contract_check()
