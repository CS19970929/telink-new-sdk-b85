"""同领域检查直接执行各场景；场景状态独立，断言与故障注入保留。"""
from __future__ import annotations

def check_sw_protection_contract_check():
    print("CHECK sw_protection_contract_check", flush=True)
    #!/usr/bin/env python3
    """Static contract for the AFE-independent D008/D011/D013 software protection core."""
    from pathlib import Path
    from project_paths import Sources, host_includes, selected_source

    ROOT = Path(__file__).resolve().parents[1]
    HERE = Sources(ROOT)
    source = (HERE / "bms_sw_protection.c").read_text(encoding="utf-8", errors="ignore")
    header = (HERE / "bms_sw_protection.h").read_text(encoding="utf-8", errors="ignore")
    backend = (HERE / "bms_afe_backend.h").read_text(encoding="utf-8", errors="ignore")

    for token in (
        "BMS_SW_PROTECTION_LEVEL_COUNT     3u",
        "BMS_SW_PROTECTION_FILTER_COUNT    12u",
        "trip_count",
        "recover_count",
        "unMdlFault_First",
        "unMdlFault_Second",
        "unMdlFault_Third",
        "u16VcellOvp_First",
        "u16VcellOvp_Second",
        "u16VcellOvp_Third",
        "u16VcellUvp_First",
        "u16VbusOvp_First",
        "u16VbusUvp_First",
        "u16IchgOcp_First",
        "u16IdsgOcp_First",
        "u16TChgOTp_First",
        "u16TchgUTp_First",
        "u16TdischgOTp_First",
        "u16TdischgUTp_First",
        "u16TmosOTp_First",
        "u16VdeltaOvp_First",
        "BMS_ERROR_TEMP_BREAK",
        "bms_sw_protection_record_fault_edges",
    ):
        if token not in source and token not in header:
            raise AssertionError(f"missing common protection invariant: {token}")

    for forbidden in ("DVC1124_", "SH3673510_", "SH3673520_", "gpio_", "ReadReg", "WriteReg"):
        if forbidden in source:
            raise AssertionError(f"common protection leaked backend detail: {forbidden}")

    if "p->u16SocLow_" in source:
        raise AssertionError("legacy SOC protection must remain out until semantics are specified")

    # Recover applies only to Third. First/Second alarms clear outside their own
    # threshold after filtering; equality remains active (no boundary oscillation).
    for token in (
        "bms_sw_high_recovery_valid",
        "bms_sw_low_recovery_valid",
        "return !third || recover < third;",
        "return !third || recover > third;",
        "(value < trip) : (value > trip)",
        "level == 2u",
        "if (!bms_protection_params_valid())",
        "bms_sw_protection_clear();",
    ):
        if token not in source:
            raise AssertionError(f"software protection fail-safe missing: {token}")

    # Battery OTP/UTP and MOS OTP have different sensor ownership. One invalid NTC
    # must not erase the other sensor's protection filters/fault state.
    for token in (
        "if (temperature_enabled && inputs->battery_temp_valid)",
        "if (temperature_enabled && inputs->mos_temp_required && inputs->mos_temp_valid)",
        "bms_sw_filter_reset(&s_filter[level][BMS_SW_F_MOS_OT])",
        "bms_sw_filter_reset(&s_filter[level][BMS_SW_F_CHG_OT])",
    ):
        if token not in source:
            raise AssertionError(f"independent temperature validity missing: {token}")
    if "if (temp_valid)" in source:
        raise AssertionError("battery and MOS temperature protections must not share one combined validity gate")


    # Charge/discharge battery-temperature faults must not start from temperature
    # alone. New charge faults require charge current, new discharge faults require
    # discharge current. Once active, recovery must remain temperature-driven so the
    # current dropping to zero after FET shutdown cannot immediately clear the fault.
    for token in (
        "static uint8_t bms_sw_temp_filter_update",
        "charge_current_present = (measurements->charge_a10 > 0u)",
        "discharge_current_present = (measurements->discharge_a10 > 0u)",
        "if (!state->active && !trip_enabled)",
        "state->trip_count = 0u;",
        "charge_current_present, inputs->battery_temp_max",
        "charge_current_present, inputs->battery_temp_min",
        "discharge_current_present, inputs->battery_temp_max",
        "discharge_current_present, inputs->battery_temp_min",
    ):
        if token not in source:
            raise AssertionError(f"directional temperature trigger gate missing: {token}")

    battery_block = source.split("if (temperature_enabled && inputs->battery_temp_valid)", 1)[1].split("else", 1)[0]
    if battery_block.count("bms_sw_temp_filter_update") != 4:
        raise AssertionError("all four battery temperature faults must use the directional trigger gate")

    mos_block = source.split("if (temperature_enabled && inputs->mos_temp_required && inputs->mos_temp_valid)", 1)[1].split("else", 1)[0]
    if "bms_sw_temp_filter_update" in mos_block:
        raise AssertionError("power-MOS OTP must remain independent of charge/discharge current direction")
    if "bms_sw_filter_update" not in mos_block:
        raise AssertionError("power-MOS OTP filter missing")

    if "BMS_AFE_BACKEND_DVC1124" in backend and "#define BMS_AFE_BACKEND BMS_AFE_BACKEND_DVC1124" in backend:
        dvc = (HERE / "dvc1124_bms.c").read_text(encoding="utf-8", errors="ignore")
        for token in ("bms_sw_protection_update_groups(&sw, DVC1124_SW_PROTECT_ENABLE,", "dvc_merge_hw_faults(alarm);", "bms_sw_protection_record_fault_edges();"):
            if token not in dvc:
                raise AssertionError(f"D008 integration missing: {token}")
        if "dvc_publish_faults(alarm, &cfg);" in dvc:
            raise AssertionError("D008 still executes legacy per-backend software protection")

    if "BMS_AFE_BACKEND_SH3673510" in backend and "#define BMS_AFE_BACKEND BMS_AFE_BACKEND_SH3673510" in backend:
        sh = (HERE / "sh3673510_bms.c").read_text(encoding="utf-8", errors="ignore")
        for token in ("bms_sw_protection_update(&sw);", "bms_sw_protection_clear();", "bms_sw_protection_record_fault_edges();"):
            if token not in sh:
                raise AssertionError(f"SH3673510 integration missing: {token}")
        if "update_faults();" in sh:
            raise AssertionError("SH3673510 still executes legacy per-backend software protection")
        if sh.index("bms_sw_protection_update(&sw);") > sh.index("merge_hw_protection_faults(&status);"):
            raise AssertionError("software protection must run before hardware flags are merged")
        if sh.index("merge_hw_protection_faults(&status);") > sh.index("bms_sw_protection_record_fault_edges();"):
            raise AssertionError("fault history must observe merged software + hardware Third state")

    print("Unified software protection contract: PASS")

def check_common_feature_policy_contract_check():
    print("CHECK common_feature_policy_contract_check", flush=True)
    #!/usr/bin/env python3
    from pathlib import Path
    from project_paths import Sources, host_includes, selected_source

    ROOT = Path(__file__).resolve().parents[1]
    APP = Sources(ROOT)

    def text(name: str) -> str:
        path = APP / name
        if not path.exists(): raise AssertionError(f"missing {path}")
        return path.read_text(encoding="utf-8", errors="replace")

    features_h=text("bms_features.h"); features_c=text("bms_features.c"); board_h=text("bms_board.h"); board_c=text("bms_board.c"); guard_c=text("bms_afe_guard.c"); afe_h=text("bms_afe.h"); config_h=text("bms_config_store.h"); config_c=text("bms_config_store.c"); modbus=text("modbus_rtu.c")
    assert "#define BMS_HEATER_START_TEMP_X10 400u" in features_h
    assert "#define BMS_HEATER_STOP_TEMP_X10 450u" in features_h
    assert "bms_afe_get_charge_source_present" in afe_h
    assert "bms_afe_get_charge_source_present" in guard_c
    assert "charge_source_present" in features_c
    assert "BMS_HEATER_START_TEMP_X10" in config_c and "BMS_HEATER_STOP_TEMP_X10" in config_c
    assert "#define BMS_BALANCE_START_DELTA_MV_DEFAULT 50u" in features_h
    assert "#define BMS_BALANCE_STOP_DELTA_MV_DEFAULT 30u" in features_h
    assert "openwire_fault_latched" in features_c and "openwire_suspected" in features_c
    assert "balance_voltage_trusted" in features_c and "balance_sample_plausible" in features_c
    assert "u16VdeltaOvp_First" not in features_c
    assert "bms_board_heater_set" in board_h and "bms_board_charge_source_present" in board_h
    assert "bms_board_heater_allowed" in board_h and "bms_board_balance_supported" in board_h
    assert "bms_board_heater_allowed()" in features_c and "bms_board_balance_supported()" in features_c
    assert "service_balance" in features_c and "apply_balance_mask(0u)" in features_c
    assert "service_openwire" in features_c and "bms_afe_openwire_start" in features_c and "bms_afe_openwire_poll" in features_c
    charge_block=guard_c.index("bms_features_outputs_blocked"); fet_write=guard_c.index("AFE_FETS",charge_block)
    assert charge_block < fet_write
    assert "bms_features_charge_direction_blocked()" in text("sh3673510_bms.c")
    assert "bms_features_service();" in guard_c and "bms_features_on_afe_invalid();" in guard_c
    for symbol in ("bms_afe_get_feature_snapshot","bms_afe_get_charge_source_present","bms_afe_set_balance_mask","bms_afe_get_balance_mask","bms_afe_openwire_start","bms_afe_openwire_poll"): assert symbol in afe_h

    # Communication-loss fail-safe contract. SH36735xx WDT is ~32 s in the
    # production profile, so the common guard must leave the SPI bus silent for a
    # full 35 s margin before a single bounded recovery attempt.
    assert "#define BMS_AFE_COMM_FAILS_BEFORE_SILENCE    2u" in guard_c
    assert "#define BMS_AFE_FAILSAFE_WAIT_SAMPLES 175u" in guard_c
    assert "if (s_guard.test_shutdown_hold || service_failsafe_wait()) return;" in guard_c
    # 校验静默等待分支在任何总线动作之前返回，不依赖注释语言。
    wait = guard_c.split("static uint8_t service_failsafe_wait", 1)[1].split(
        "static uint8_t apply_requested", 1)[0]
    waiting = wait.split("if (s_guard.failsafe_wait_samples != 0u)", 1)[1].split(
        "s_guard.bus_silenced = 0u;", 1)[0]
    assert "--s_guard.failsafe_wait_samples;" in waiting and "return 1u;" in waiting
    assert "AFE_" not in waiting
    assert wait.index("return 1u;") < wait.index("AFE_INIT();")
    assert "if (s_guard.comm_failures == 0u) best_effort_shutdown();" in guard_c
    assert "s_guard.bus_silenced = 1u;" in guard_c
    assert "bms_afe_bus_access_allowed" in afe_h and "bms_afe_bus_access_allowed" in guard_c
    apply=guard_c.split("static uint8_t apply_requested",1)[1].split("static void note_invalid",1)[0]
    assert "if (s_guard.comm_inhibit || s_guard.bus_silenced || s_guard.test_shutdown_hold) return 1u;" in apply
    sleep=guard_c.split("uint8_t bms_afe_sleep",1)[1].split("#else",1)[0]
    assert sleep.index("if (s_guard.bus_silenced || s_guard.test_shutdown_hold) return 0u;") < sleep.index("AFE_SLEEP()")
    setfets=guard_c.split("uint8_t bms_afe_set_fets",1)[1].split("void bms_afe_get_requested_fets",1)[0]
    assert "if (s_guard.comm_inhibit || s_guard.bus_silenced || s_guard.test_shutdown_hold) return 1u;" in setfets

    sh_bms=text("sh3673510_bms.c")
    for forbidden in (
        "static void apply_heater",
        "static void apply_balance",
        "SH3510_REINIT_TRIGGER",
        "SH3510_REINIT_COOLDOWN",
    ):
        assert forbidden not in sh_bms, forbidden

    sample = sh_bms.split("void sh3673510_bms_afe_sample(void)", 1)[1].split(
        "uint8_t sh3673510_bms_afe_apply_protection_config", 1
    )[0]
    for forbidden in (
        "apply_heater();",
        "apply_balance();",
        "sh3510_apply_requested_fets();",
        "sh3673510_control_init()",
    ):
        assert forbidden not in sample, forbidden

    assert "comm_fault_latched" in guard_c
    assert "else if (s_guard.comm_fault_latched)" in guard_c

    sh=text("sh3673510_feature_backend.c")
    for token in (
        "SH3673520_SCONF3_OWD_EN_MASK","SH3673520_SCONF3_OWD_TRG_MASK",
        "SH3673520_REG_FLAG3","SH3673520_REG_OWDH","0x000AAAAA","0x00055555",
        "SH_FEATURE_BALANCE_REFRESH_SAMPLES 100u","sh_read_balance_mask"
    ): assert token in sh, token
    for token in ("SH3673520_REG_VCHGRH", "SH3510_CHARGER_ON_MV", "SH3510_CHARGER_OFF_MV",
                  "sample_release_evidence", "sh3673510_backend_get_charge_source_present"):
        assert token in sh_bms, token
    assert "sh3673510_backend_get_charge_source_present" not in sh

    for token in ("balance_enable","balance_start_mv","balance_start_delta_mv","balance_stop_delta_mv"):
        assert token in config_h and token in config_c and token in features_c
    assert "#define BMS_CONFIG_SCHEMA_VERSION        3u" in config_c
    assert "#define BMS_CONFIG_USER_BYTES            54u" in config_c
    parameters=text("bms_parameter_access.c")
    assert "BMS_PARAM_REG_HEATER_ENABLE" in parameters and "BMS_PARAM_REG_BALANCE_ENABLE" in parameters
    assert "bms_parameter_write" in modbus

    sh_bms=text("sh3673510_bms.c")
    assert "SH3673520_BSTATUS2_DSGING_MASK" in sh_bms
    assert "SH3673520_BSTATUS2_CHGING_MASK" in sh_bms
    assert "s_fet_command_valid" in sh_bms
    # 相同有效 FET 命令必须在再次写硬件之前返回，避免干扰自主保护。
    fets = sh_bms.split("static uint8_t sh3510_apply_requested_fets", 1)[1].split(
        "static void publish_hw_status", 1)[0]
    cached = fets.split("if (s_fet_command_valid &&", 1)[1].split(
        "if (!sh3673510_control_set_fets", 1)[0]
    assert "s_last_charge_command == charge_on" in cached
    assert "s_last_discharge_command == discharge_on" in cached
    assert "return 1u;" in cached
    assert fets.index("if (s_fet_command_valid &&") < fets.index(
        "sh3673510_control_set_fets(charge_on, discharge_on)")

    print("common feature policy contract: PASS")

def check_afe_hw_access_contract_check():
    print("CHECK afe_hw_access_contract_check", flush=True)
    #!/usr/bin/env python3
    from pathlib import Path
    from project_paths import Sources, host_includes, selected_source
    ROOT=Path(__file__).resolve().parents[1]
    HERE = Sources(ROOT)
    def text(n): return (HERE/n).read_text(encoding='utf-8')
    a=text('bms_afe_hw_access.c'); h=text('bms_afe_hw_access.h'); m=text('modbus_rtu.c'); p=text('bms_afe_hw_profile.c'); c=text('bms_afe_hw_modbus.h')
    assert 'BMS_AFE_HW_ACCESS_MODBUS_FUNC       0x42u' in h
    assert 'BMS_AFE_HW_ACCESS_UNLOCK_MAGIC      0x41464548UL' in h
    assert 'BMS_AFE_HW_ACCESS_TIMEOUT_SECONDS   60u' in h
    assert 'bms_afe_hw_access_is_active()' in p
    assert 'BMS_AFE_HW_ERROR_AUTH' in p
    assert 'BMS_AFE_HW_APPLY_INCONSISTENT' in p
    assert 'afe_hw_profile_rollback' in p
    assert 'bms_afe_hw_access_close();' in p
    assert 'BMS_AFE_HW_EFFECTIVE_REG_BASE            0x2540u' in c
    assert 'BMS_AFE_HW_META_INTERFACE_VERSION        0x252Bu' in c
    assert 'bms_afe_hw_profile_get_effective' in p
    assert 'g_bms_protection_params' not in m[m.index('static u8 afe_hw_profile_write_block'):m.index('static int dvc_comm_is_semantic') if 'static int dvc_comm_is_semantic' in m[m.index('static u8 afe_hw_profile_write_block'):] else m.index('static u16 read_fault_history_reg')]
    print('AFE hardware access/transaction/effective-profile contract: PASS')

def check_soc_contract_check():
    print("CHECK soc_contract_check", flush=True)
    #!/usr/bin/env python3
    from pathlib import Path
    from project_paths import Sources, host_includes, selected_source
    import unittest

    ROOT = Path(__file__).resolve().parents[1]
    MOD = Sources(ROOT)
    C = (MOD / "bms_soc.c").read_text(encoding="utf-8", errors="ignore")
    APP = (MOD / "app.c").read_text(encoding="utf-8", errors="ignore")
    H = (MOD / "bms_soc.h").read_text(encoding="utf-8", errors="ignore")
    PROFILE = (MOD / "bms_soc_profile.h").read_text(encoding="utf-8", errors="ignore")
    DEFS = (MOD / "bms_soc_defs.h").read_text(encoding="utf-8", errors="ignore")
    CONFIG_C = (MOD / "bms_config_store.c").read_text(encoding="utf-8", errors="ignore")
    CONFIG_H = (MOD / "bms_config_store.h").read_text(encoding="utf-8", errors="ignore")
    STATE_C = (MOD / "bms_state_store.c").read_text(encoding="utf-8", errors="ignore")
    STATE_H = (MOD / "bms_state_store.h").read_text(encoding="utf-8", errors="ignore")

    DIAG_H = (MOD / "bms_diag.h").read_text(encoding="utf-8")

    class SocContract(unittest.TestCase):
        def test_dual_chemistry_profiles_are_data_not_algorithm(self):
            self.assertIn("BMS_SOC_CHEMISTRY_LFP", DEFS)
            self.assertIn("BMS_SOC_CHEMISTRY_NMC", DEFS)
            self.assertIn("g_soc_ocv_lfp", PROFILE)
            self.assertIn("g_soc_ocv_nmc", PROFILE)
            self.assertIn("BMS_SOC_PROFILE_GENERIC_LFP_VERSION", PROFILE)
            self.assertIn("BMS_SOC_PROFILE_GENERIC_NMC_VERSION", PROFILE)
            self.assertNotIn("static const soc_ocv_point_t g_soc_ocv_lfp", C)
            self.assertNotIn("static const soc_ocv_point_t g_soc_ocv_nmc", C)
            self.assertIn("SOC_AUTO_LFP_OVP_MAX_MV              3900u", C)

        def test_product_chemistry_and_profile_are_config_fields(self):
            self.assertIn("u32 battery_chemistry", CONFIG_H)
            self.assertIn("u32 soc_profile_id", CONFIG_H)
            self.assertIn("BMS_CONFIG_SYSTEM_WORDS          5u", CONFIG_C)
            self.assertIn("bms_config_put_u32le", CONFIG_C)
            self.assertIn("system->battery_chemistry = BMS_PRODUCT_CHEMISTRY", CONFIG_C)
            self.assertIn("system->soc_profile_id = BMS_PRODUCT_SOC_PROFILE_ID", CONFIG_C)
            self.assertIn("bms_config_store_get_system", CONFIG_C)
            self.assertIn("bms_soc_configure", C)
            self.assertIn("soc_load_persisted_product_config", C)
            self.assertNotIn("BMS_COLD_SYSTEM_KEY_BASE", CONFIG_C)

        def test_explicit_profile_wins_and_mismatches_are_rejected(self):
            self.assertIn("soc_profile_from_id(g_soc_config.profile_id)", C)
            self.assertIn("profile_id == BMS_SOC_PROFILE_GENERIC_NMC", C)
            self.assertIn("profile_id == BMS_SOC_PROFILE_GENERIC_LFP", C)
            self.assertIn("soc_product_config_valid", C)
            # 读取当前配置域，不通过旧键值存储迁移产品身份。
            load = C.split("static void soc_load_persisted_product_config(void)", 1)[1].split(
                "void bms_soc_get_diag", 1)[0]
            self.assertIn("bms_config_store_get_soc(&g_soc_config)", load)
            self.assertNotIn("SOC_KV_KEY_", load)
            self.assertIn("bms_config_store_set_soc(config)", C)

        def test_coulomb_integration_and_deadband(self):
            self.assertIn("SOC_INTEGRAL_PERIOD_MS              200u", C)
            self.assertIn("SOC_CURRENT_DEADBAND_MA_DEFAULT     200u", C)
            self.assertIn("soc_current_direction", C)
            self.assertIn("g_soc_integral_tick_remainder", C)

        def test_ocv_requires_ten_minutes_and_uses_band(self):
            self.assertIn("SOC_OCV_REST_PREPARE_SECONDS        600u", C)
            self.assertIn("SOC_OCV_ERROR_BAND_PERCENT          5u", C)
            self.assertIn("g_soc_runtime.ocv_low", C)
            self.assertIn("g_soc_runtime.ocv_high", C)
            self.assertIn("soc_step_down_to(g_soc_runtime.ocv_high)", C)
            ocv_fn = C[C.index("static uint8_t soc_idle_ocv_tracking"):C.index("static uint16_t soc_discharge_natural_1pct_ticks")]
            self.assertNotIn("soc_step_up_to", ocv_fn)

        def test_display_soc_is_separate(self):
            self.assertIn("static uint8_t g_soc_display_soc", C)
            self.assertIn("SOC_DISPLAY_STEP_TICKS              SOC_TICKS_PER_SECOND", C)
            self.assertIn("g_bms_report.SocElement.u16Soc = get_soc_display();", C)

        def test_endpoints_and_lfp_terminal_knee_are_chemistry_specific(self):
            self.assertIn("g_bms_report.unMdlFault_Third.bits.b1CellOvp", APP)
            self.assertIn("g_bms_report.unMdlFault_Third.bits.b1CellUvp", APP)
            self.assertIn("150u, 100u, 50u, 20u", PROFILE)
            self.assertIn("300u, 200u, 150u, 50u", PROFILE)

        def test_upward_calibration_requires_confirmed_charging_full_anchor(self):
            start = C.index("static uint8_t soc_apply_full_anchor(void)")
            end = C.index("static uint8_t soc_apply_forced_empty_anchor(void)", start)
            full_fn = C[start:end]
            self.assertIn("(VCELLMAX >= full_mv) && (VCELLMIN >= full_min)", full_fn)
            self.assertIn("(g_soc_input.cell_delta_mv <= g_soc_profile->full_cell_delta_max_mv) && isCHG()", full_fn)
            self.assertIn("if (isCHG() && g_soc_input.third_cell_ovp)", full_fn)
            self.assertNotIn("&& !isDSG()", full_fn)

        def test_soc_low_faults_are_implemented_without_mos_policy(self):
            self.assertIn("soc_update_low_faults", C)
            self.assertIn("soc_fault_reg(level)->bits.b1SocLow", C)
            self.assertIn("u16SocLow_First", C)
            self.assertNotIn("b1SocLow ||", C)

        def test_cycle_soh_has_no_learning_state(self):
            self.assertNotIn("soc_learning_", C)
            self.assertNotIn("BMS_SOC_LEARNING", H)
            self.assertNotIn("bms_state_store_write_learning", STATE_C)
            self.assertNotIn("learned_capacity", STATE_H)
            self.assertIn("bms_soh_from_cycle", C)
            self.assertIn("storage_record_save", STATE_C)

        def test_diag_reports_profile_identity_and_version(self):
            self.assertIn("bms_soc_diag_t", H)
            self.assertIn("profile_id", DIAG_H)
            self.assertIn("profile_version", DIAG_H)
            self.assertIn("diag->profile_id = g_soc_profile->profile_id", C)
            self.assertIn("diag->profile_version = g_soc_profile->profile_version", C)

    if __name__ == "__main__":
        assert unittest.TextTestRunner(verbosity=2).run(unittest.TestSuite(unittest.defaultTestLoader.loadTestsFromTestCase(c) for c in (SocContract,))).wasSuccessful()

def check_bms_diag_contract_check():
    print("CHECK bms_diag_contract_check", flush=True)
    #!/usr/bin/env python3
    """Static contract for the shared SH3673510 diagnostics window."""

    from pathlib import Path
    from project_paths import Sources, host_includes, selected_source


    ROOT = Path(__file__).resolve().parents[1]
    BLE = Sources(ROOT)


    def require(text: str, token: str, source: str) -> None:
        if token not in text:
            raise AssertionError(f"{source}: missing {token}")


    def main() -> int:
        header = (BLE / "bms_diag.h").read_text(encoding="utf-8")
        source = (BLE / "bms_diag.c").read_text(encoding="utf-8") + (BLE / "bms_storage_platform_telink.c").read_text(encoding="utf8")
        app = (BLE / "app.c").read_text(encoding="utf-8")
        modbus = (BLE / "modbus_rtu.c").read_text(encoding="utf-8")
        order = (ROOT / "bms/products" / BLE.product / "sources.txt").read_text(encoding="utf-8")
        build = (ROOT / "bms_tools" / "build.mk").read_text(encoding="utf-8")

        for token in ("0x2A00u", "0x2B00u", "0x2E00u", "BMS_DIAG_RUNTIME_VERSION 3u"):
            require(header, token, "bms_diag.h")
        for token in ("s_words[0] = 0x4447u", "bms_diag_boot_word(14u, bms_afe_hw_profile_expected_model())",
                      "bms_diag_boot_word(15u, 0x8251u)", "BMS_DIAG_BUILD_ID"):
            require(source, token, "bms_diag.c")
        for token in ("bms_diag_init();", "bms_diag_freeze_boot();",
                      "bms_diag_poll_runtime("):
            require(app, token, "app.c")
        require(modbus, "bms_diag_read(reg, qty, &rsp[3])", "modbus_rtu.c")
        if modbus.count("bms_diag_overlaps") < 3:
            raise AssertionError("diagnostic window must reject both single and multiple writes")
        require(order, "bms/core/bms_diag.c", "source_order.txt")
        require(build, "-DMCU_STARTUP_8251", "build.mk")
        require(build, "$(EXTRA_DEFINES)", "build.mk")
        print("SH3673510 diagnostics contract PASS")
        return 0


    if __name__ == "__main__":
        assert main() in (None, 0)

if __name__ == "__main__":
    check_sw_protection_contract_check()
    check_common_feature_policy_contract_check()
    check_afe_hw_access_contract_check()
    check_soc_contract_check()
    check_bms_diag_contract_check()
