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

    for token in (
        "BMS_SW_PROTECTION_LEVEL_COUNT     3u",
        "BMS_SW_F_COUNT",
        "static uint16_t s_filter_count",
        "fault_first",
        "fault_second",
        "fault_third",
        "cell_ovp_first_mv",
        "cell_ovp_second_mv",
        "cell_ovp_third_mv",
        "cell_uvp_first_mv",
        "pack_ovp_first_10mv",
        "pack_uvp_first_10mv",
        "charge_ocp_first_a10",
        "discharge_ocp_first_a10",
        "charge_otp_first_x10",
        "charge_utp_first_x10",
        "discharge_otp_first_x10",
        "discharge_utp_first_x10",
        "mos_otp_first_x10",
        "cell_delta_first_mv",
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
        "s_filter_count[level][BMS_SW_F_MOS_OT] = 0u",
        "s_filter_count[level][BMS_SW_F_CHG_OT] = 0u",
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
        "charge_current_present = (measurements->charge_current_a10 > 0u)",
        "discharge_current_present = (measurements->discharge_current_a10 > 0u)",
        "if (!active && !trip_enabled)",
        "*count = 0u;",
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

    dvc = (HERE / "dvc1124_bms.c").read_text(encoding="utf-8")
    update = dvc.index("bms_sw_protection_update_groups(&sw, DVC1124_SW_PROTECT_ENABLE,")
    merge = dvc.index("dvc_merge_hw_faults(alarm);", update)
    assert merge < dvc.index("bms_sw_protection_record_fault_edges();", merge)

    print("Unified software protection contract: PASS")

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
    assert 'BMS_AFE_HW_APPLY_ROLLBACK_OK' in p
    assert 'BMS_AFE_HW_ERROR_ROLLBACK' in p
    assert 'bms_afe_hw_access_close();' in p
    assert 'BMS_AFE_HW_EFFECTIVE_REG_BASE            0x2540u' in c
    assert 'BMS_AFE_HW_META_INTERFACE_VERSION        0x252Bu' in c
    assert 'bms_afe_hw_profile_get_effective' in p
    assert 'g_bms_protection_params' not in m[m.index('static u8 afe_hw_profile_write_block'):m.index('static int dvc_comm_is_semantic') if 'static int dvc_comm_is_semantic' in m[m.index('static u8 afe_hw_profile_write_block'):] else m.index('static u16 read_fault_history_reg')]
    print('AFE hardware access/transaction/effective-profile contract: PASS')

if __name__ == "__main__":
    check_sw_protection_contract_check()
    check_afe_hw_access_contract_check()
