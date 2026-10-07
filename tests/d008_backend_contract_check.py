"""同领域检查直接执行各场景；场景状态独立，断言与故障注入保留。"""
from __future__ import annotations

def check_dvc1124_config_quick_check():
    print("CHECK dvc1124_config_quick_check", flush=True)
    #!/usr/bin/env python3
    import re
    import unittest
    from pathlib import Path
    from project_paths import Sources, host_includes, selected_source

    ROOT = Path(__file__).resolve().parents[1]
    SRC = Sources(ROOT)


    def read(name):
        return (SRC / name).read_text(encoding="utf-8", errors="strict")


    def macro_literal(text, name):
        m = re.search(
            rf"^\s*#define\s+{re.escape(name)}\s+\(?\s*(0x[0-9A-Fa-f]+|[0-9]+)u?\s*\)?",
            text,
            re.MULTILINE,
        )
        if not m:
            raise AssertionError(f"literal macro not found: {name}")
        return int(m.group(1), 0)


    class RegisterTruthTests(unittest.TestCase):
        @classmethod
        def setUpClass(cls):
            cls.reg = read("dvc1124_reg.h")
            cls.hdr = read("dvc1124.h")
            cls.driver = read("dvc1124.c")

        def test_critical_register_truth(self):
            self.assertEqual(macro_literal(self.reg, "DVC1124_OC2_ENABLE_MASK"), 0x40)
            self.assertEqual(macro_literal(self.reg, "DVC1124_SCD_ENABLE_MASK"), 0x40)
            self.assertEqual(macro_literal(self.reg, "DVC1124_DSGMASK_DWM_MASK"), 0x08)
            self.assertEqual(macro_literal(self.reg, "DVC1124_CHGMASK_CWM_MASK"), 0x80)
            self.assertEqual(macro_literal(self.reg, "DVC1124_I2C_WDT_TIME_MASK"), 0x07)

        def test_watchdog_encodings_match_v12_mapping(self):
            self.assertRegex(self.reg, r"DVC1124_I2C_WDT_OFF\s*=\s*0u")
            self.assertRegex(self.reg, r"DVC1124_I2C_WDT_4S\s*=\s*4u")
            self.assertRegex(self.reg, r"DVC1124_I2C_WDT_8S\s*=\s*5u")
            self.assertRegex(self.reg, r"DVC1124_I2C_WDT_16S\s*=\s*6u")
            self.assertRegex(self.reg, r"DVC1124_I2C_WDT_32S\s*=\s*7u")

        def test_read_clear_registers_remain_protected(self):
            self.assertIn("DVC1124_RegReadHasSideEffect", self.hdr)
            self.assertIn("DVC1124_REG_STATUS", self.hdr)
            self.assertIn("DVC1124_REG_CORE_OT", self.hdr)

        def test_hw_off_still_disables_autonomous_sources(self):
            block = self.driver.split("static uint8_t dvc_disable_threshold_protection", 1)[1]
            block = block.split("#endif", 1)[0]
            self.assertIn("DVC1124_I2C_WDT_OFF", block)
            self.assertIn("DVC1124_REG_DSG_MASK, 0xFFu", block)
            self.assertIn("DVC1124_REG_CHG_MASK, 0xFFu", block)
            self.assertIn("DVC1124_REG_BODY_DIODE, 0u", block)


    class CompileTimeOwnershipTests(unittest.TestCase):
        @classmethod
        def setUpClass(cls):
            cls.product = read("d008_product_profile.h")
            cls.project = (read("bms_product.h") + read("dvc1124_product_defaults.h"))
            cls.backend = read("dvc1124.c") + read("dvc1124_boot.c")
            cls.service = read("dvc1124_config_service.c")
            cls.service_hdr = read("dvc1124_config_service.h")

        def test_project_fail_safe_defaults_are_compile_time(self):
            self.assertEqual(macro_literal(self.project, "DVC1124_I2C_WATCHDOG_SECONDS"), 4)
            self.assertEqual(macro_literal(self.project, "DVC1124_I2C_TIMEOUT_CLOSE_CHG"), 1)
            self.assertEqual(macro_literal(self.project, "DVC1124_I2C_TIMEOUT_CLOSE_DSG"), 1)
            self.assertNotIn("DVC1124_I2C_WATCHDOG_SECONDS", self.product)

        def test_project_config_keeps_body_diode_and_mask_policy_named(self):
            self.assertEqual(macro_literal(self.project, "DVC1124_BODY_DIODE_THRESHOLD_UV"), 80)
            self.assertIn("DVC1124_DSGMASK_DBDM_MASK", self.project)
            self.assertIn("DVC1124_CHGMASK_CBDM_MASK", self.project)

        def test_legacy_store_file_no_longer_owns_flash(self):
            forbidden = (
                "flash_kv32",
                "flash_store_cfg",
                "DVC1124_ConfigStoreLoad",
                "DVC1124_ConfigStoreSave",
                "DVC1124_ConfigStoreRestore",
                "DVC1124_ConfigStoreCapture",
            )
            for token in forbidden:
                self.assertNotIn(token, self.backend)

        def test_every_afe_init_reapplies_firmware_policy(self):
            self.assertIn("DVC1124_AFE_Reset();", self.backend)
            self.assertIn("DVC1124_UpdataAfeConfig();", self.backend)
            self.assertIn("DVC1124_ApplyProjectOperatingConfig", self.backend)
            self.assertIn("s_project_config_pending", self.backend)
            self.assertNotIn("ConfigStoreRestore", self.backend)

        def test_compile_time_apply_covers_previous_persistent_fields(self):
            expected = (
                "DVC1124_DEFAULT_CC1_WORK_TIME",
                "DVC1124_DEFAULT_CC1_SLEEP_WAKE_TIME",
                "DVC1124_CHARGE_PUMP_VOLTAGE_CODE",
                "DVC1124_DEFAULT_CELL_MEASUREMENT_MASK",
                "DVC1124_DEFAULT_CELL_VOLTAGE_SIGNED",
                "DVC1124_DEFAULT_VADC_ENABLE",
                "DVC1124_DEFAULT_VADC_SYNC_WITH_CC2",
                "DVC1124_DEFAULT_VADC_PERIOD",
                "DVC1124_DEFAULT_VADC_TIME",
                "DVC1124_GP1_DEFAULT_MODE",
                "DVC1124_GP6_DEFAULT_MODE",
                "DVC1124_DEFAULT_V3P3_SLEEP_ENABLE",
                "DVC1124_DEFAULT_V3P3_WORK_ENABLE",
                "DVC1124_DEFAULT_TIMED_WAKE",
                "DVC1124_DEFAULT_INTERRUPT_MASK",
                "DVC1124_DEFAULT_DSG_PULLDOWN_STRENGTH",
                "DVC1124_CURRENT_WAKE_THRESHOLD_UV",
                "DVC1124_BODY_DIODE_THRESHOLD_UV",
                "DVC1124_I2C_WATCHDOG_SECONDS",
                "DVC1124_I2C_TIMEOUT_CLOSE_CHG",
                "DVC1124_I2C_TIMEOUT_CLOSE_DSG",
                "DVC1124_DEFAULT_CORE_OT_CODE",
            )
            for token in expected:
                self.assertIn(token, self.backend)
            self.assertIn("DVC1124_ApplyProjectOperatingConfig()", self.backend)

        def test_hw_off_compile_path_does_not_reenable_watchdog(self):
            self.assertIn("#if DVC1124_HW_PROTECT_ENABLE", self.backend)
            self.assertIn("wdt = DVC1124_I2C_WDT_OFF;", self.backend)
            self.assertIn("dsg_mask = 0xFFu;", self.backend)
            self.assertIn("chg_mask = 0xFFu;", self.backend)
            self.assertIn("body_diode_code = 0u;", self.backend)

        def test_semantic_fixed_config_is_read_only(self):
            self.assertNotIn("dvc1124_boot.h", self.service)
            self.assertNotIn("DVC1124_ConfigStore", self.service)
            self.assertIn("DVC1124_CFG_ERR_READ_ONLY", self.service)
            # 固定策略禁止写入应由代码分支证明，不检查说明文字。
            write = self.service.split("DVC1124_ConfigServiceWrite(", 1)[1].split(
                "DVC1124_ConfigServiceReadRaw(", 1)[0]
            self.assertIn("if (dvc_cfg_fixed_field(field) ||", write)
            self.assertIn("return DVC1124_CFG_ERR_READ_ONLY;", write)
            self.assertIn("DVC1124_ConfigServiceWrite", self.service_hdr)

        def test_raw_register_mirror_is_read_only(self):
            raw_write = self.service.split("static dvc1124_config_result_t", 1)[-1]
            self.assertIn("DVC1124_ConfigServiceWriteRaw", self.service)
            self.assertIn("DVC1124_CFG_ERR_READ_ONLY", raw_write)
            self.assertNotIn("ConfigStore", raw_write)


    class ProtectionOwnershipTests(unittest.TestCase):
        @classmethod
        def setUpClass(cls):
            cls.service = read("dvc1124_config_service.c")
            cls.modbus = read("modbus_rtu.c")
            cls.hw_profile = read("bms_afe_hw_profile.c")
            cls.backend = read("dvc1124.c") + read("dvc1124_boot.c")

        def test_requested_afe_protection_still_comes_from_hw_profile(self):
            self.assertIn("bms_afe_hw_profile_get(&hw)", self.service)
            for field in (
                "hw.cov_mv",
                "hw.cuv_mv",
                "hw.ocd1_a10",
                "hw.occ1_a10",
                "hw.ocd2_a10",
                "hw.occ2_a10",
                "hw.sc_a10",
            ):
                self.assertIn(field, self.service)

        def test_effective_afe_protection_is_read_from_dvc(self):
            for reg in (
                "DVC1124_REG_COV_H",
                "DVC1124_REG_CUV_H",
                "DVC1124_REG_OCD1_THR",
                "DVC1124_REG_OCC1_THR",
                "DVC1124_REG_OCD2",
                "DVC1124_REG_OCC2",
                "DVC1124_REG_SCD",
            ):
                self.assertIn(reg, self.service)

        def test_hw_profile_atomic_transaction_remains_persistent_owner(self):
            self.assertIn("afe_hw_profile_write_block", self.modbus)
            self.assertIn("bms_afe_hw_profile_set(&candidate)", self.hw_profile)
            self.assertIn("bms_afe_apply_protection_config()", self.hw_profile)
            self.assertIn("bms_afe_hw_profile_get(&verify)", self.hw_profile)

        def test_backend_runtime_apply_only_delegates_protection(self):
            fn = self.backend.split("uint8_t dvc1124_backend_apply_protection_config", 1)[1]
            self.assertIn("DVC1124_ApplyProtectionConfig()", fn)
            self.assertNotIn("ConfigStore", fn)


    if __name__ == "__main__":
        assert unittest.TextTestRunner(verbosity=2).run(unittest.TestSuite(unittest.defaultTestLoader.loadTestsFromTestCase(c) for c in (RegisterTruthTests,CompileTimeOwnershipTests,ProtectionOwnershipTests,))).wasSuccessful()

def check_d008_common_port_fet_contract_check():
    print("CHECK d008_common_port_fet_contract_check", flush=True)
    #!/usr/bin/env python3
    import re
    import unittest
    from pathlib import Path
    from project_paths import Sources, host_includes, selected_source

    ROOT = Path(__file__).resolve().parents[1]
    SRC = Sources(ROOT)

    def read(name):
        return (SRC / name).read_text(encoding="utf-8", errors="strict")

    class D008CommonPortFetContract(unittest.TestCase):
        def test_normal_operation_requests_both_fets(self):
            app = read("app.c")
            m = re.search(r"void mos_update\(void\)\n\{(.*?)\n\}\n", app, re.S)
            self.assertIsNotNone(m)
            body = m.group(1)
            self.assertIn("bms_afe_set_fets(1u, 1u)", body)
            self.assertNotIn("IsChargerWakeupActive", body)
            self.assertNotIn("IsKeyWakeupActive", body)
            self.assertNotIn("charge_mos_status", body)
            self.assertNotIn("discharge_mos_status", body)
            self.assertNotIn("Runtime_GetMode()", body)

        def test_dvc_body_diode_recovery_is_compile_time_policy(self):
            cfg = (read("bms_product.h") + read("dvc1124_product_defaults.h"))
            backend = read("dvc1124.c") + read("dvc1124_boot.c")
            self.assertRegex(cfg, r"#define\s+DVC1124_BODY_DIODE_THRESHOLD_UV\s+80u")
            self.assertIn("DVC1124_DSGMASK_DBDM_MASK", cfg)
            self.assertIn("DVC1124_CHGMASK_CBDM_MASK", cfg)
            self.assertIn("DVC1124_BODY_DIODE_THRESHOLD_UV", backend)
            self.assertIn("DVC1124_EncodeBodyDiode", backend)

        def test_body_diode_policy_has_no_operating_config_flash_owner(self):
            backend = read("dvc1124.c") + read("dvc1124_boot.c")
            self.assertNotIn("ConfigStoreLoad", backend)
            self.assertNotIn("ConfigStoreSave", backend)
            self.assertNotIn("flash_kv32", backend)

        def test_one_sided_protection_uses_auto_diode_without_hard_off_transition(self):
            bms = read("dvc1124_bms.c")
            policy = bms.split("static uint8_t dvc_apply_common_port_fet_state", 1)[1]
            policy = policy.split("void DVC1124_BmsApp_AFEGet", 1)[0]
            self.assertIn("charge_blocked && !discharge_blocked", policy)
            self.assertIn("discharge_blocked && !charge_blocked", policy)
            self.assertGreaterEqual(policy.count("DVC1124_FET_DRIVE_AUTO_DIODE"), 2)
            self.assertIn("dvc_set_fet_modes_if_changed(charge_mode, discharge_mode)", policy)
            self.assertNotIn("DVC1124_SetMosState", policy)
            self.assertNotIn("DVC1124_WriteRegisterFieldSafe", policy)
            self.assertNotIn("effective_charge", policy)
            self.assertNotIn("effective_discharge", policy)

        def test_heater_directional_block_uses_auto_diode_not_guard_hard_off(self):
            guard = read("bms_afe_guard.c")
            bms = read("dvc1124_bms.c")
            self.assertIn("bms_features_outputs_blocked()", guard)
            self.assertNotIn("bms_features_charge_direction_blocked()", guard)
            charge = bms.split("static uint8_t dvc_charge_blocked", 1)[1].split("static uint8_t dvc_discharge_blocked", 1)[0]
            self.assertIn("bms_features_charge_direction_blocked()", charge)
            policy = bms.split("static uint8_t dvc_apply_common_port_fet_state", 1)[1]
            policy = policy.split("void DVC1124_BmsApp_AFEGet", 1)[0]
            self.assertIn("charge_blocked && !discharge_blocked", policy)
            self.assertIn("DVC1124_FET_DRIVE_AUTO_DIODE", policy)

        def test_steady_auto_diode_mode_does_not_rewrite_r81_every_200ms(self):
            bms = read("dvc1124_bms.c")
            helper = bms.split("static uint8_t dvc_set_fet_modes_if_changed", 1)[1]
            helper = helper.split("static uint8_t dvc_apply_common_port_fet_state", 1)[0]
            self.assertIn("DVC1124_ReadRegisters(DVC1124_REG_FET_CTRL", helper)
            self.assertIn("DVC1124_FET_CHGC_MASK", helper)
            self.assertIn("DVC1124_FET_DSGC_MASK", helper)
            same_mode_return = re.search(
                r"if\s*\(.*?charge_mode.*?discharge_mode.*?\)\s*\{\s*return\s+1u;",
                helper,
                re.S,
            )
            self.assertIsNotNone(same_mode_return)
            self.assertEqual(helper.count("DVC1124_WriteRegisterSafe(DVC1124_REG_FET_CTRL"), 1)

        def test_single_r81_write_encodes_final_chg_and_dsg_modes(self):
            bms = read("dvc1124_bms.c")
            helper = bms.split("static uint8_t dvc_set_fet_modes_if_changed", 1)[1]
            helper = helper.split("static uint8_t dvc_apply_common_port_fet_state", 1)[0]
            self.assertIn("DVC1124_FIELD_PREP(DVC1124_FET_CHGC_MASK", helper)
            self.assertIn("DVC1124_FIELD_PREP(DVC1124_FET_DSGC_MASK", helper)
            self.assertNotIn("DVC1124_WriteRegisterFieldSafe", helper)

        def test_controlled_off_is_preserved_but_comm_loss_uses_wdt(self):
            guard = read("bms_afe_guard.c")
            bms = read("dvc1124_bms.c")

            # When communication is healthy, an explicit 0/0 request still maps to
            # the true DVC OFF/OFF command; open-wire can also request controlled off.
            policy = bms.split("static uint8_t dvc_apply_common_port_fet_state", 1)[1]
            policy = policy.split("void DVC1124_BmsApp_AFEGet", 1)[0]
            self.assertIn("dvc1124_fet_drive_t charge_mode = DVC1124_FET_DRIVE_OFF", policy)
            self.assertIn("dvc1124_fet_drive_t discharge_mode = DVC1124_FET_DRIVE_OFF", policy)
            self.assertIn("bms_afe_openwire_start", guard)
            self.assertIn("if (!AFE_FETS(0u, 0u)) return 0u;", guard)

            # Communication loss is different: make only one best-effort off
            # attempt, then keep the bus silent so the DVC hardware WDT can fire.
            self.assertIn("static void best_effort_shutdown(void)", guard)
            self.assertIn("if (s_guard.comm_failures == 0u) best_effort_shutdown();", guard)
            self.assertIn("s_guard.bus_silenced = 1u;", guard)
            self.assertIn("if (s_guard.test_shutdown_hold || service_failsafe_wait()) return;", guard)
            self.assertIn("if (s_guard.comm_inhibit || s_guard.bus_silenced || s_guard.test_shutdown_hold) return 1u;", guard)
            self.assertNotIn("BMS_AFE_REINIT_TRIGGER", guard)

    if __name__ == "__main__":
        assert unittest.TextTestRunner(verbosity=2).run(unittest.TestSuite(unittest.defaultTestLoader.loadTestsFromTestCase(c) for c in (D008CommonPortFetContract,))).wasSuccessful()

def check_d008_boot_zero_current_contract_check():
    print("CHECK d008_boot_zero_current_contract_check", flush=True)
    """D008 boot zero-current calibration contract checks."""
    from pathlib import Path
    from project_paths import Sources, host_includes, selected_source

    ROOT = Path(__file__).resolve().parents[1]
    BASE = Sources(ROOT)

    driver = (BASE / "dvc1124.c").read_text(encoding="utf-8")
    header = (BASE / "dvc1124.h").read_text(encoding="utf-8")
    config = (BASE / "bms_product.h").read_text(encoding="utf-8") + (BASE / "dvc1124_product_defaults.h").read_text(encoding="utf-8")
    backend = (BASE / "dvc1124_boot.c").read_text(encoding="utf-8")
    conf = (BASE / "bms_soc.h").read_text(encoding="utf-8")

    assert "#define DVC1124_BOOT_ZERO_ENABLE              1u" in config
    assert "#define DVC1124_BOOT_ZERO_SAMPLE_INTERVAL_MS  270u" in config
    assert "DVC1124_BootCurrentZeroCalibrate" in header
    assert "DVC1124_StartCadcCalibration()" in driver
    assert driver.count("!dvc_boot_zero_wait_fresh_cc2()") == 2
    assert "DVC1124_FET_PDSGC_MASK" in driver and "DVC1124_FET_PCHGC_MASK" in driver
    assert "DVC1124_CC2_DSGF_MASK" in driver and "DVC1124_CC2_CHGF_MASK" in driver
    assert "factory_current_ma = bms_config_calibrate_current(current_ma);" in driver
    assert "current_ma = dvc_apply_boot_zero(factory_current_ma);" in driver
    assert "#define BMS_CURRENT_UNRELIABLE_MAX_MA 200u" in conf

    init_pos = backend.index("DVC1124_UpdataAfeConfig();")
    zero_pos = backend.index("(void)DVC1124_BootCurrentZeroCalibrate();")
    assert init_pos < zero_pos

    zero_fn = driver[driver.index("uint8_t DVC1124_BootCurrentZeroCalibrate(void)"):]
    assert "bms_config_set_user" not in zero_fn
    assert "storage_record_save" not in zero_fn

    print("PASS D008 boot zero-current calibration is init-only, FET-safe, RAM-only, deadband-preserving")

if __name__ == "__main__":
    check_dvc1124_config_quick_check()
    check_d008_common_port_fet_contract_check()
    check_d008_boot_zero_current_contract_check()
