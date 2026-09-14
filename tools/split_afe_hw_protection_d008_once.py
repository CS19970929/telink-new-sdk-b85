#!/usr/bin/env python3
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
HERE = ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001" / "tc_ble_single_sdk" / "vendor" / "ble_sample"
D011_REF = "origin/feature/sh3673510-d011-bms"


def git_copy(rel: str) -> None:
    data = subprocess.check_output(["git", "show", f"{D011_REF}:{rel}"])
    dst = ROOT / rel
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(data)


for rel in (
    "tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/bms_afe_hw_profile.c",
    "tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/bms_afe_hw_profile.h",
    "tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/bms_cold_kv_store.c",
    "tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/bms_cold_kv_store.h",
    "tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/bms_sw_protection.c",
    "tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/bms_sw_protection.h",
):
    git_copy(rel)

# DVC profile validation: SCD physical current must be representable by 10mV steps.
p = HERE / "bms_afe_hw_profile.c"
s = p.read_text(encoding="utf-8")
needle = '''    sense_uv = ((u32)p->occ2_a10 * DVC1124_DEFAULT_SHUNT_UOHM) / 10u;\n    if ((p->enable_mask & BMS_AFE_HW_EN_OCC2) && sense_uv > 256000u) return 0u;\n'''
insert = needle + '''    sense_uv = ((u32)p->sc_a10 * DVC1124_DEFAULT_SHUNT_UOHM) / 10u;\n    if ((p->enable_mask & BMS_AFE_HW_EN_SC) &&\n        (sense_uv < 10000u || sense_uv > 630000u)) return 0u;\n'''
if needle in s and 'sense_uv < 10000u || sense_uv > 630000u' not in s:
    s = s.replace(needle, insert, 1)
p.write_text(s, encoding="utf-8")

# DVC register programming now consumes the independent hardware profile.
p = HERE / "dvc1124.c"
s = p.read_text(encoding="utf-8")
if '#include "bms_afe_hw_profile.h"' not in s:
    # dvc1124.c has param.h in all current D008 baselines.
    s = s.replace('#include "param.h"\n', '#include "param.h"\n#include "bms_afe_hw_profile.h"\n', 1)

new_apply = r'''static uint8_t dvc_apply_protection_from_params(void)
{
    bms_afe_hw_profile_t hw;
    dvc1124_config_t cfg;
    uint16_t cov_mv;
    uint16_t cuv_mv;
    uint16_t request;
    uint32_t cov_req_dly;
    uint32_t cuv_req_dly;
    uint32_t ocd1_req_dly;
    uint32_t ocd2_req_dly;
    uint32_t occ1_req_dly;
    uint32_t occ2_req_dly;
    uint32_t sc_sense_uv;
    uint16_t sc_mv;
    uint8_t buf[2];
    uint8_t code;
    uint16_t code12;
    uint16_t actual;
    uint8_t ok = 1u;

    if (!bms_afe_hw_profile_get(&hw)) return 0u;
    DVC1124_GetConfig(&cfg);
    if (cfg.shunt_uohm == 0u) return 0u;

    cov_mv = (hw.enable_mask & BMS_AFE_HW_EN_COV) ? hw.cov_mv : 0u;
    cuv_mv = (hw.enable_mask & BMS_AFE_HW_EN_CUV) ? hw.cuv_mv : 0u;
    cov_req_dly = hw.cov_delay_ms;
    cuv_req_dly = hw.cuv_delay_ms;
    ocd1_req_dly = hw.ocd1_delay_ms;
    ocd2_req_dly = hw.ocd2_delay_ms;
    occ1_req_dly = hw.occ1_delay_ms;
    occ2_req_dly = hw.occ2_delay_ms;

    memset(&s_applied, 0, sizeof(s_applied));

    if (cov_mv == 0u) {
        code12 = 0u;
    } else {
        if ((cov_mv < 501u) || (cov_mv > 4595u)) return 0u;
        code12 = (uint16_t)(cov_mv - 500u);
        s_applied.cov_mv = (uint16_t)(code12 + 500u);
    }
    code = dvc_voltage_delay_code(cov_req_dly, &actual);
    s_applied.cov_delay_ms = actual;
    dvc_note_quant(DVC_QUANT_COV_DLY, cov_req_dly, actual);
    buf[0] = (uint8_t)(code12 >> 4);
    buf[1] = (uint8_t)(((code12 & 0x0Fu) << 4) | code);
    ok &= dvc_write_verified_block(DVC1124_REG_COV_H, buf, 2u);

    if (cuv_mv == 0u) {
        code12 = 0u;
    } else {
        if (cuv_mv > 4095u) return 0u;
        code12 = cuv_mv;
        s_applied.cuv_mv = code12;
    }
    code = dvc_voltage_delay_code(cuv_req_dly, &actual);
    s_applied.cuv_delay_ms = actual;
    dvc_note_quant(DVC_QUANT_CUV_DLY, cuv_req_dly, actual);
    buf[0] = (uint8_t)(code12 >> 4);
    buf[1] = (uint8_t)(((code12 & 0x0Fu) << 4) | code);
    ok &= dvc_write_verified_block(DVC1124_REG_CUV_H, buf, 2u);

    request = (hw.enable_mask & BMS_AFE_HW_EN_OCD1) ? hw.ocd1_a10 : 0u;
    code = dvc_current_to_oc1_code(request, &actual);
    s_applied.ocd1_a_x10 = actual;
    dvc_note_quant(DVC_QUANT_OCD1_THR, request, actual);
    ok &= dvc_write_verified(DVC1124_REG_OCD1_THR, code);

    request = (hw.enable_mask & BMS_AFE_HW_EN_OCC1) ? hw.occ1_a10 : 0u;
    code = dvc_current_to_oc1_code(request, &actual);
    s_applied.occ1_a_x10 = actual;
    dvc_note_quant(DVC_QUANT_OCC1_THR, request, actual);
    ok &= dvc_write_verified(DVC1124_REG_OCC1_THR, code);

    code = dvc_linear_delay_code(ocd1_req_dly, 8u, &actual);
    s_applied.ocd1_delay_ms = actual;
    dvc_note_quant(DVC_QUANT_OCD1_DLY, ocd1_req_dly, actual);
    ok &= dvc_write_verified(DVC1124_REG_OCD1_DLY, code);

    code = dvc_linear_delay_code(occ1_req_dly, 8u, &actual);
    s_applied.occ1_delay_ms = actual;
    dvc_note_quant(DVC_QUANT_OCC1_DLY, occ1_req_dly, actual);
    ok &= dvc_write_verified(DVC1124_REG_OCC1_DLY, code);

    request = (hw.enable_mask & BMS_AFE_HW_EN_OCD2) ? hw.ocd2_a10 : 0u;
    if (request == 0u) {
        ok &= dvc_update_reg(DVC1124_REG_OCD2,
                             (uint8_t)(DVC1124_OC2_ENABLE_MASK | DVC1124_OC2_THRESHOLD_MASK), 0u);
    } else {
        code = dvc_current_to_oc2_code(request, &actual);
        s_applied.ocd2_a_x10 = actual;
        dvc_note_quant(DVC_QUANT_OCD2_THR, request, actual);
        ok &= dvc_update_reg(DVC1124_REG_OCD2,
                             (uint8_t)(DVC1124_OC2_ENABLE_MASK | DVC1124_OC2_THRESHOLD_MASK),
                             (uint8_t)(DVC1124_OC2_ENABLE_MASK | code));
    }

    request = (hw.enable_mask & BMS_AFE_HW_EN_OCC2) ? hw.occ2_a10 : 0u;
    if (request == 0u) {
        ok &= dvc_update_reg(DVC1124_REG_OCC2,
                             (uint8_t)(DVC1124_OC2_ENABLE_MASK | DVC1124_OC2_THRESHOLD_MASK), 0u);
    } else {
        code = dvc_current_to_oc2_code(request, &actual);
        s_applied.occ2_a_x10 = actual;
        dvc_note_quant(DVC_QUANT_OCC2_THR, request, actual);
        ok &= dvc_update_reg(DVC1124_REG_OCC2,
                             (uint8_t)(DVC1124_OC2_ENABLE_MASK | DVC1124_OC2_THRESHOLD_MASK),
                             (uint8_t)(DVC1124_OC2_ENABLE_MASK | code));
    }

    code = dvc_linear_delay_code(ocd2_req_dly, 4u, &actual);
    s_applied.ocd2_delay_ms = actual;
    dvc_note_quant(DVC_QUANT_OCD2_DLY, ocd2_req_dly, actual);
    ok &= dvc_write_verified(DVC1124_REG_OCD2_DLY, code);

    code = dvc_linear_delay_code(occ2_req_dly, 4u, &actual);
    s_applied.occ2_delay_ms = actual;
    dvc_note_quant(DVC_QUANT_OCC2_DLY, occ2_req_dly, actual);
    ok &= dvc_write_verified(DVC1124_REG_OCC2_DLY, code);

    sc_mv = 0u;
    if (hw.enable_mask & BMS_AFE_HW_EN_SC) {
        sc_sense_uv = ((uint32_t)hw.sc_a10 * cfg.shunt_uohm) / 10u;
        /* Floor to a 10mV code so the effective hardware trip is never above
         * the requested physical-current threshold. */
        sc_mv = (uint16_t)((sc_sense_uv / 10000u) * 10u);
        if (sc_mv < 10u || sc_mv > 630u) return 0u;
    }
    ok &= DVC1124_SetShortCircuitProtection(sc_mv, hw.sc_delay_us);
    return ok;
}
'''
s, n = re.subn(r'static uint8_t dvc_apply_protection_from_params\(void\)\s*\{.*?\n\}\n\nstatic void dvc_note_comm_result',
                  new_apply + '\nstatic void dvc_note_comm_result', s, count=1, flags=re.S)
if n != 1:
    raise SystemExit(f'dvc apply replacement count={n}')
p.write_text(s, encoding="utf-8")

# DVC hardware-latch recovery uses independent recovery values/times.
p = HERE / "dvc1124_bms.c"
s = p.read_text(encoding="utf-8")
if '#include "bms_afe_hw_profile.h"' not in s:
    s = s.replace('#include "bms_sw_protection.h"\n', '#include "bms_sw_protection.h"\n#include "bms_afe_hw_profile.h"\n', 1)
helper = r'''static uint8_t dvc_recovery_stable(uint8_t condition, uint16_t stable_ms, uint16_t *count)
{
    uint16_t required;
    if (count == 0) return 0u;
    if (!condition) { *count = 0u; return 0u; }
    required = (uint16_t)(((uint32_t)stable_ms + DVC_BMS_SAMPLE_PERIOD_MS - 1u) /
                          DVC_BMS_SAMPLE_PERIOD_MS);
    if (required == 0u) required = 1u;
    if (*count < required) ++(*count);
    return (*count >= required) ? 1u : 0u;
}

'''
if 'static uint8_t dvc_recovery_stable' not in s:
    pos = s.find('static uint8_t dvc_clear_recovered_hw_latches')
    if pos < 0: raise SystemExit('DVC recovery anchor missing')
    s = s[:pos] + helper + s[pos:]
new_recovery = r'''static uint8_t dvc_clear_recovered_hw_latches(uint8_t alarm)
{
    static uint16_t cov_count;
    static uint16_t cuv_count;
    static uint16_t occ_count;
    static uint16_t ocd_count;
    bms_afe_hw_profile_t hw;
    uint8_t clear_mask = 0u;
    uint8_t verify;

    if (!bms_afe_hw_profile_get(&hw)) return alarm;

    if (alarm & DVC1124_ALARM_COV_MASK) {
        if (dvc_recovery_stable((uint8_t)(g_stCellInfoReport.u16VCellMax <= hw.cov_recover_mv),
                                hw.cov_recover_ms, &cov_count))
            clear_mask |= DVC1124_ALARM_COV_MASK;
    } else cov_count = 0u;

    if (alarm & DVC1124_ALARM_CUV_MASK) {
        if (dvc_recovery_stable((uint8_t)(g_stCellInfoReport.u16VCellMin >= hw.cuv_recover_mv),
                                hw.cuv_recover_ms, &cuv_count))
            clear_mask |= DVC1124_ALARM_CUV_MASK;
    } else cuv_count = 0u;

    if (alarm & (DVC1124_ALARM_OCC1_MASK | DVC1124_ALARM_OCC2_MASK)) {
        if (dvc_recovery_stable((uint8_t)(g_stCellInfoReport.u16Ichg <= hw.occ_recover_a10),
                                hw.occ_recover_ms, &occ_count))
            clear_mask |= (uint8_t)(alarm & (DVC1124_ALARM_OCC1_MASK | DVC1124_ALARM_OCC2_MASK));
    } else occ_count = 0u;

    if (alarm & (DVC1124_ALARM_OCD1_MASK | DVC1124_ALARM_OCD2_MASK)) {
        if (dvc_recovery_stable((uint8_t)(g_stCellInfoReport.u16IDischg <= hw.ocd_recover_a10),
                                hw.ocd_recover_ms, &ocd_count))
            clear_mask |= (uint8_t)(alarm & (DVC1124_ALARM_OCD1_MASK | DVC1124_ALARM_OCD2_MASK));
    } else ocd_count = 0u;

    /* SCD remains hardware-latched until D008 has a hardware-verified
     * load-removal/recovery policy. */
    if (clear_mask == 0u) return alarm;
    if (!DVC1124_ClearAlarmFlags(clear_mask)) return alarm;
    if (!DVC1124_ReadRegisters(DVC1124_REG_ALARM, &verify, 1u)) return alarm;
    return verify;
}
'''
s, n = re.subn(r'static uint8_t dvc_clear_recovered_hw_latches\(uint8_t alarm\)\s*\{.*?\n\}\n\nstatic void dvc_merge_hw_faults',
                  new_recovery + '\nstatic void dvc_merge_hw_faults', s, count=1, flags=re.S)
if n != 1:
    raise SystemExit(f'DVC recovery replacement count={n}')
p.write_text(s, encoding="utf-8")

# Existing DVC 0x2840 requested fields become a compatibility read view of the
# independent profile. Writes to this legacy partial window are disabled; the
# common 0x2500 block is the only mutable HW protection transaction.
p = HERE / "dvc1124_config_service.c"
s = p.read_text(encoding="utf-8")
if '#include "bms_afe_hw_profile.h"' not in s:
    s = s.replace('#include "bms_cold_kv_store.h"\n', '#include "bms_cold_kv_store.h"\n#include "bms_afe_hw_profile.h"\n', 1)
new_read = r'''static dvc1124_config_result_t dvc_cfg_read_requested_protection(
    dvc1124_config_field_t field,
    u32 *value)
{
    bms_afe_hw_profile_t hw;
    dvc1124_config_t device;
    u32 sense_uv;

    if (value == NULL) return DVC1124_CFG_ERR_VALUE;
    if (!bms_afe_hw_profile_get(&hw)) return DVC1124_CFG_ERR_STORE;
    DVC1124_GetConfig(&device);

    switch (field)
    {
    case DVC1124_CFG_REQ_COV_MV:        *value = hw.cov_mv; return DVC1124_CFG_OK;
    case DVC1124_CFG_REQ_COV_DELAY_MS:  *value = hw.cov_delay_ms; return DVC1124_CFG_OK;
    case DVC1124_CFG_REQ_CUV_MV:        *value = hw.cuv_mv; return DVC1124_CFG_OK;
    case DVC1124_CFG_REQ_CUV_DELAY_MS:  *value = hw.cuv_delay_ms; return DVC1124_CFG_OK;
    case DVC1124_CFG_REQ_OCD1_X10A:     *value = hw.ocd1_a10; return DVC1124_CFG_OK;
    case DVC1124_CFG_REQ_OCD1_DELAY_MS: *value = hw.ocd1_delay_ms; return DVC1124_CFG_OK;
    case DVC1124_CFG_REQ_OCC1_X10A:     *value = hw.occ1_a10; return DVC1124_CFG_OK;
    case DVC1124_CFG_REQ_OCC1_DELAY_MS: *value = hw.occ1_delay_ms; return DVC1124_CFG_OK;
    case DVC1124_CFG_REQ_OCD2_X10A:     *value = hw.ocd2_a10; return DVC1124_CFG_OK;
    case DVC1124_CFG_REQ_OCD2_DELAY_MS: *value = hw.ocd2_delay_ms; return DVC1124_CFG_OK;
    case DVC1124_CFG_REQ_OCC2_X10A:     *value = hw.occ2_a10; return DVC1124_CFG_OK;
    case DVC1124_CFG_REQ_OCC2_DELAY_MS: *value = hw.occ2_delay_ms; return DVC1124_CFG_OK;
    case DVC1124_CFG_REQ_SCD_MV:
        if (device.shunt_uohm == 0u) return DVC1124_CFG_ERR_VALUE;
        sense_uv = ((u32)hw.sc_a10 * device.shunt_uohm) / 10u;
        *value = sense_uv / 1000u;
        return DVC1124_CFG_OK;
    case DVC1124_CFG_REQ_SCD_DELAY_US:  *value = hw.sc_delay_us; return DVC1124_CFG_OK;
    default: return DVC1124_CFG_ERR_ADDRESS;
    }
}
'''
s, n = re.subn(r'static dvc1124_config_result_t dvc_cfg_read_requested_protection\(.*?\n\}\n\ndvc1124_config_result_t DVC1124_ConfigServiceRead',
                  new_read + '\ndvc1124_config_result_t DVC1124_ConfigServiceRead', s, count=1, flags=re.S)
if n != 1: raise SystemExit(f'DVC requested-read replacement count={n}')
# Make the whole old requested semantic range read-only.
old = '''    if ((u8)field >= (u8)DVC1124_CFG_REQ_COV_MV &&\n        (u8)field <= (u8)DVC1124_CFG_REQ_OCC2_DELAY_MS)\n        return dvc_cfg_write_bms_protection(field, value);\n\n    return dvc_cfg_write_afe_field(field, value);\n'''
new = '''    if ((u8)field >= (u8)DVC1124_CFG_REQ_COV_MV &&\n        (u8)field <= (u8)DVC1124_CFG_REQ_SCD_DELAY_US)\n        return DVC1124_CFG_ERR_READ_ONLY;\n\n    return dvc_cfg_write_afe_field(field, value);\n'''
if old not in s: raise SystemExit('DVC config write routing anchor missing')
s = s.replace(old, new, 1)
# Raw SCD register writes no longer own persistent protection state.
s = s.replace('''    case DVC1124_REG_SCD:\n        code = (u8)(raw & DVC1124_SCD_THRESHOLD_MASK);\n        if ((raw & DVC1124_SCD_ENABLE_MASK) == 0u)\n        {\n            cfg->scd_threshold_mv = 0u;\n            cfg->scd_delay_us = 0u;\n        }\n        else\n        {\n            if (code == 0u) return DVC1124_CFG_ERR_VALUE;\n            cfg->scd_threshold_mv = (u16)code * 10u;\n        }\n        break;\n    case DVC1124_REG_SCD_DLY:\n        if (cfg->scd_threshold_mv == 0u && raw != 0u) return DVC1124_CFG_ERR_VALUE;\n        cfg->scd_delay_us = (u16)(((u32)raw * 781u + 50u) / 100u);\n        break;\n''',
'''    case DVC1124_REG_SCD:\n    case DVC1124_REG_SCD_DLY:\n        return DVC1124_CFG_ERR_FORBIDDEN;\n''', 1)
p.write_text(s, encoding="utf-8")

# The legacy DVC operating-config store must not overwrite SCD after the common
# HW profile has been applied during AFE reset/reinitialization.
p = HERE / "dvc1124_config_store.c"
s = p.read_text(encoding="utf-8")
s = s.replace('''    ok &= DVC1124_SetCoreOtThresholdCode(cfg->core_ot_code);\n    ok &= DVC1124_SetShortCircuitProtection(cfg->scd_threshold_mv,\n                                             cfg->scd_delay_us);\n    ok &= DVC1124_ApplyOperatingConfig(&cfg->operating);\n''',
'''    ok &= DVC1124_SetCoreOtThresholdCode(cfg->core_ot_code);\n    /* SCD is owned by bms_afe_hw_profile; this legacy store must not overwrite it. */\n    ok &= DVC1124_ApplyOperatingConfig(&cfg->operating);\n''', 1)
p.write_text(s, encoding="utf-8")

# Common 0x2500 hardware profile Modbus window; software writes remain software-only.
p = HERE / "modbus_rtu.h"
s = p.read_text(encoding="utf-8")
if 'BMS_AFE_HW_PROFILE_REG_BASE' not in s:
    s += '\n#define BMS_AFE_HW_PROFILE_REG_BASE  0x2500u\n#define BMS_AFE_HW_PROFILE_WORDS     35u\n#define BMS_AFE_HW_PROFILE_REG_COUNT 40u\n'
p.write_text(s, encoding="utf-8")

p = HERE / "modbus_rtu.c"
s = p.read_text(encoding="utf-8")
if '#include "bms_afe_hw_profile.h"' not in s:
    s = s.replace('#include "bms_state.h"\n', '#include "bms_state.h"\n#include "bms_sw_protection.h"\n#include "bms_afe_hw_profile.h"\n#include "dvc1124.h"\n#include "dvc1124_project_config.h"\n', 1)
if 'static u16 u16be(const u8 *p);' not in s:
    s = s.replace('static u16 read_ascii_string_reg', 'static u16 u16be(const u8 *p);\nstatic u16 read_ascii_string_reg', 1)
helpers = r'''static int afe_hw_profile_is_reg(u16 reg)
{
    return (reg >= BMS_AFE_HW_PROFILE_REG_BASE &&
            reg < (u16)(BMS_AFE_HW_PROFILE_REG_BASE + BMS_AFE_HW_PROFILE_REG_COUNT));
}

static u16 afe_hw_profile_read_reg(u16 reg)
{
    bms_afe_hw_profile_t p;
    u16 offset = (u16)(reg - BMS_AFE_HW_PROFILE_REG_BASE);
    if (offset < BMS_AFE_HW_PROFILE_WORDS) {
        if (!bms_afe_hw_profile_get(&p)) return 0xFFFFu;
        return ((const u16 *)&p)[offset];
    }
    switch (offset) {
    case 35u: return bms_afe_hw_profile_capabilities();
    case 36u: return bms_afe_hw_profile_get(&p) ? 1u : 0u;
    case 37u: return DVC1124_DEFAULT_SHUNT_UOHM;
    case 38u: return DVC1124_DEFAULT_CELL_COUNT;
    case 39u: return DVC1124_I2C_WATCHDOG_SECONDS;
    default: return 0xFFFFu;
    }
}

static u8 afe_hw_profile_write_block(const u8 *pdata, u16 qty)
{
    bms_afe_hw_profile_t before;
    bms_afe_hw_profile_t candidate;
    u16 i;
    if (pdata == 0 || qty != BMS_AFE_HW_PROFILE_WORDS) return MB_EX_ILLEGAL_VALUE;
    if (!bms_afe_hw_profile_get(&before)) return MB_EX_DEVICE_FAILURE;
    candidate = before;
    for (i = 0u; i < qty; ++i)
        ((u16 *)&candidate)[i] = u16be(&pdata[(u32)i * 2u]);
    if (!bms_afe_hw_profile_validate(&candidate)) return MB_EX_ILLEGAL_VALUE;
    if (!bms_afe_hw_profile_set(&candidate)) return MB_EX_DEVICE_FAILURE;
    if (!bms_afe_apply_protection_config()) {
        (void)bms_afe_hw_profile_set(&before);
        (void)bms_afe_apply_protection_config();
        return MB_EX_DEVICE_FAILURE;
    }
    return 0u;
}

'''
if 'static int afe_hw_profile_is_reg' not in s:
    pos = s.find('static int dvc_comm_is_semantic')
    s = s[:pos] + helpers + s[pos:]
s = s.replace('static u16 read_reg(u16 reg)\n{\n    u16 val;\n',
              'static u16 read_reg(u16 reg)\n{\n    u16 val;\n\n    if (afe_hw_profile_is_reg(reg)) return afe_hw_profile_read_reg(reg);\n', 1)
s = s.replace('static u8 write_reg(u16 reg, u16 val)\n{\n',
              'static u8 write_reg(u16 reg, u16 val)\n{\n    if (afe_hw_profile_is_reg(reg)) return MB_EX_ILLEGAL_ADDRESS;\n', 1)
s, n = re.subn(r'static u8 commit_protection_update\(const struct PRT_E2ROM_PARAS \*previous\)\s*\{.*?\n\}',
'''static u8 commit_protection_update(const struct PRT_E2ROM_PARAS *previous)\n{\n    if (!bms_sw_protection_validate_params(&g_tParam.protect)) {\n        g_tParam.protect = *previous;\n        return MB_EX_ILLEGAL_VALUE;\n    }\n    if (!SaveParam()) {\n        g_tParam.protect = *previous;\n        return MB_EX_DEVICE_FAILURE;\n    }\n    return 0u;\n}''', s, count=1, flags=re.S)
if n != 1: raise SystemExit(f'D008 commit protection replacement count={n}')
# Common atomic block has priority over DVC semantic multi-write rejection.
needle = '''        /*\n         * DVC safety configuration is transactional per semantic field today.\n         * Reject multi-field writes instead of accepting a half-updated AFE\n         * when a later field fails. Use 0x06 until batch commit is implemented.\n         */\n        if (qty > 1u && dvc_comm_range_contains(reg, qty))\n            return modbus_exception(addr, func, MB_EX_ILLEGAL_VALUE, rsp, rsp_len);\n\n        previous_protect = g_tParam.protect;\n        pdata = &req[7];\n'''
replacement = '''        pdata = &req[7];\n        if (reg == BMS_AFE_HW_PROFILE_REG_BASE) {\n            if (qty != BMS_AFE_HW_PROFILE_WORDS)\n                return modbus_exception(addr, func, MB_EX_ILLEGAL_VALUE, rsp, rsp_len);\n            exception = afe_hw_profile_write_block(pdata, qty);\n            if (exception != 0u)\n                return modbus_exception(addr, func, exception, rsp, rsp_len);\n            if (addr == 0x00u) return 0;\n            rsp[0] = addr; rsp[1] = func; put_u16be(&rsp[2], reg); put_u16be(&rsp[4], qty);\n            crc = mb_crc16(rsp, 6u); rsp[6] = (u8)(crc & 0xFFu); rsp[7] = (u8)(crc >> 8); *rsp_len = 8u;\n            return 1;\n        }\n        if (afe_hw_profile_is_reg(reg) || afe_hw_profile_is_reg((u16)(reg + qty - 1u)))\n            return modbus_exception(addr, func, MB_EX_ILLEGAL_ADDRESS, rsp, rsp_len);\n\n        /* Legacy DVC semantic multi-write remains intentionally non-atomic. */\n        if (qty > 1u && dvc_comm_range_contains(reg, qty))\n            return modbus_exception(addr, func, MB_EX_ILLEGAL_VALUE, rsp, rsp_len);\n\n        previous_protect = g_tParam.protect;\n'''
if needle not in s: raise SystemExit('D008 0x10 anchor missing')
s = s.replace(needle, replacement, 1)
p.write_text(s, encoding="utf-8")

# Dedicated D008 contracts.
t = ROOT / "tests" / "afe_hw_profile_contract_check.py"
t.write_text(r'''#!/usr/bin/env python3
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
HERE=ROOT/'tc_ble_single_sdk-V3.4.2.8_Patch_0001'/'tc_ble_single_sdk'/'vendor'/'ble_sample'
def text(n): return (HERE/n).read_text(encoding='utf-8')
p=text('bms_afe_hw_profile.c'); kv=text('bms_cold_kv_store.c'); d=text('dvc1124.c'); b=text('dvc1124_bms.c'); c=text('dvc1124_config_service.c'); store=text('dvc1124_config_store.c'); m=text('modbus_rtu.c')
assert 'BMS_COLD_AFE_HW_KEY_BASE   0x5000u' in kv
assert 'BMS_AFE_HW_MODEL_DVC1124' in p
assert 'sense_uv < 10000u || sense_uv > 630000u' in p
apply=d[d.index('static uint8_t dvc_apply_protection_from_params'):d.index('static void dvc_note_comm_result')]
assert 'g_tParam.protect' not in apply
assert 'bms_afe_hw_profile_get(&hw)' in apply
assert 'hw.sc_a10' in apply
assert 'g_tParam.protect.u16VcellOvp_Rcv' not in b
assert 'hw.cov_recover_mv' in b and 'hw.ocd_recover_a10' in b
assert 'return DVC1124_CFG_ERR_READ_ONLY;' in c
assert 'SCD is owned by bms_afe_hw_profile' in store
commit=m[m.index('static u8 commit_protection_update'):m.index('u16 mb_crc16')]
assert 'bms_afe_apply_protection_config' not in commit
assert 'qty != BMS_AFE_HW_PROFILE_WORDS' in m
print('D008 independent AFE hardware protection profile contract: PASS')
''', encoding="utf-8")

print('D008 AFE hardware protection split staged')
