#include "dvc1124_config_store.h"
#include "bms_afe.h"

#include "dvc1124_project_config.h"
#include <string.h>

/*
 * This file keeps its legacy name only because the Telink project has a locked
 * source order.  It no longer owns a Flash KV store.
 *
 * Fixed DVC operating/board policy is compile-time firmware configuration and
 * is re-applied after every AFE reset.  Only software-protection parameters and
 * bms_afe_hw_profile remain persistent runtime protection data.
 */
static uint8_t s_project_config_pending = 1u;

static uint8_t dvc_project_wdt_code(uint8_t seconds,
                                    dvc1124_i2c_wdt_code_t *code)
{
    if (code == 0) return 0u;
    switch (seconds)
    {
    case 0u:  *code = DVC1124_I2C_WDT_OFF; return 1u;
    case 4u:  *code = DVC1124_I2C_WDT_4S; return 1u;
    case 8u:  *code = DVC1124_I2C_WDT_8S; return 1u;
    case 16u: *code = DVC1124_I2C_WDT_16S; return 1u;
    case 32u: *code = DVC1124_I2C_WDT_32S; return 1u;
    default: return 0u;
    }
}

static uint8_t dvc_project_encode_current_wake(uint16_t threshold_uv,
                                               uint8_t *code)
{
    if (code == 0) return 0u;
    if (threshold_uv == 0u)
    {
        *code = 0u;
        return 1u;
    }
    if ((threshold_uv < 10u) ||
        (threshold_uv > 2550u) ||
        ((threshold_uv % 10u) != 0u)) return 0u;
    *code = (uint8_t)(threshold_uv / 10u);
    return 1u;
}

static uint8_t dvc_project_encode_body_diode(uint16_t threshold_uv,
                                             uint8_t *code)
{
    if (code == 0) return 0u;
    if (threshold_uv == 0u)
    {
        *code = 0u;
        return 1u;
    }
    if ((threshold_uv < 40u) ||
        (threshold_uv > 10200u) ||
        ((threshold_uv % 40u) != 0u)) return 0u;
    *code = (uint8_t)(threshold_uv / 40u);
    return 1u;
}

static uint8_t dvc_project_apply_compile_time_config(void)
{
    dvc1124_operating_config_t cfg;
    dvc1124_i2c_wdt_code_t wdt;
    uint8_t current_wake_code;
    uint8_t body_diode_code;
    uint8_t dsg_mask;
    uint8_t chg_mask;
    uint8_t core_ot_code;
    uint8_t ok = 1u;

    memset(&cfg, 0, sizeof(cfg));

#if DVC1124_HW_PROTECT_ENABLE
    if (!dvc_project_wdt_code(DVC1124_I2C_WATCHDOG_SECONDS, &wdt)) return 0u;
    if (!dvc_project_encode_current_wake(DVC1124_CURRENT_WAKE_THRESHOLD_UV,
                                         &current_wake_code)) return 0u;
    if (!dvc_project_encode_body_diode(DVC1124_BODY_DIODE_THRESHOLD_UV,
                                       &body_diode_code)) return 0u;
    if (DVC1124_DEFAULT_CURRENT_WAKE_ENGINE_ENABLE &&
        (current_wake_code == 0u)) return 0u;

    dsg_mask = DVC1124_DEFAULT_DSG_MASK_POLICY;
    chg_mask = DVC1124_DEFAULT_CHG_MASK_POLICY;
#if DVC1124_I2C_TIMEOUT_CLOSE_DSG
    dsg_mask &= (uint8_t)~DVC1124_DSGMASK_DWM_MASK;
#else
    dsg_mask |= DVC1124_DSGMASK_DWM_MASK;
#endif
#if DVC1124_I2C_TIMEOUT_CLOSE_CHG
    chg_mask &= (uint8_t)~DVC1124_CHGMASK_CWM_MASK;
#else
    chg_mask |= DVC1124_CHGMASK_CWM_MASK;
#endif
    core_ot_code = DVC1124_DEFAULT_CORE_OT_CODE;
#else
    /* HW-off is an explicit bench/isolation mode: keep every autonomous
     * protection/fail-safe source disabled, matching dvc1124.c policy. */
    wdt = DVC1124_I2C_WDT_OFF;
    current_wake_code = 0u;
    body_diode_code = 0u;
    dsg_mask = 0xFFu;
    chg_mask = 0xFFu;
    core_ot_code = 0u;
#endif

    if (DVC1124_DEFAULT_DSG_PULLDOWN_STRENGTH > 30u) return 0u;
    if (DVC1124_DEFAULT_CORE_OT_CODE > 127u) return 0u;

    cfg.high_side_fet_mask = DVC1124_DEFAULT_HIGH_SIDE_FET_MASK;
    cfg.cadc_work_enable = DVC1124_DEFAULT_CADC_WORK_ENABLE;
#if DVC1124_HW_PROTECT_ENABLE
    cfg.current_wake_enable = DVC1124_DEFAULT_CURRENT_WAKE_ENGINE_ENABLE;
#else
    cfg.current_wake_enable = 0u;
#endif
    cfg.cc1_work_time = DVC1124_DEFAULT_CC1_WORK_TIME;
    cfg.cc1_sleep_wake_time = DVC1124_DEFAULT_CC1_SLEEP_WAKE_TIME;
    cfg.charge_pump_voltage = DVC1124_CHARGE_PUMP_VOLTAGE_CODE;
    cfg.cell_measurement_mask = DVC1124_DEFAULT_CELL_MEASUREMENT_MASK;
    cfg.cell_voltage_signed = DVC1124_DEFAULT_CELL_VOLTAGE_SIGNED;
    cfg.vadc_enable = DVC1124_DEFAULT_VADC_ENABLE;
    cfg.vadc_sync_with_cc2 = DVC1124_DEFAULT_VADC_SYNC_WITH_CC2;
    cfg.vadc_period = DVC1124_DEFAULT_VADC_PERIOD;
    cfg.vadc_time = DVC1124_DEFAULT_VADC_TIME;
    cfg.gp1_mode = DVC1124_GP1_DEFAULT_MODE;
    cfg.gp2_mode = DVC1124_GP2_DEFAULT_MODE;
    cfg.gp3_mode = DVC1124_GP3_DEFAULT_MODE;
    cfg.gp4_mode = DVC1124_GP4_DEFAULT_MODE;
    cfg.gp5_mode = DVC1124_GP5_DEFAULT_MODE;
    cfg.gp6_mode = DVC1124_GP6_DEFAULT_MODE;
    cfg.v3p3_sleep_enable = DVC1124_DEFAULT_V3P3_SLEEP_ENABLE;
    cfg.v3p3_work_enable = DVC1124_DEFAULT_V3P3_WORK_ENABLE;
    cfg.v3p3_timeout_restart = DVC1124_DEFAULT_V3P3_TIMEOUT_RESTART;
    cfg.i2c_watchdog = wdt;
    cfg.timed_wake = DVC1124_DEFAULT_TIMED_WAKE;
    cfg.interrupt_mask = DVC1124_DEFAULT_INTERRUPT_MASK;

    /* One semantic operating-config owner: firmware macros. */
    ok &= DVC1124_ApplyOperatingConfig(&cfg);
    ok &= DVC1124_WriteRegisterFieldSafe(DVC1124_REG_DSG_PULLDOWN,
                                          DVC1124_DPC_MASK,
                                          DVC1124_DPC_SHIFT,
                                          DVC1124_DEFAULT_DSG_PULLDOWN_STRENGTH);
    ok &= DVC1124_WriteRegisterSafe(DVC1124_REG_CURRENT_WAKE,
                                     current_wake_code);
    ok &= DVC1124_WriteRegisterSafe(DVC1124_REG_BODY_DIODE,
                                     body_diode_code);
    ok &= DVC1124_SetCoreOtThresholdCode(core_ot_code);
    ok &= DVC1124_WriteRegisterSafe(DVC1124_REG_DSG_MASK, dsg_mask);
    ok &= DVC1124_WriteRegisterSafe(DVC1124_REG_CHG_MASK, chg_mask);

    return ok ? 1u : 0u;
}

void bms_afe_init(void)
{
    s_project_config_pending = 1u;
    DVC1124_AFE_Reset();

    /* dvc1124.c applies the safety baseline and persistent HW protection
     * profile.  The first valid sample below then finalizes every fixed
     * operating field from compile-time product policy. */
    DVC1124_UpdataAfeConfig();
}

void bms_afe_sample(void)
{
    dvc1124_snapshot_t snapshot;

    DVC1124_BmsApp_AFEGet();

    if (!s_project_config_pending) return;

    DVC1124_GetSnapshot(&snapshot);
    if (!snapshot.valid) return;

    if (dvc_project_apply_compile_time_config())
    {
        s_project_config_pending = 0u;
        return;
    }

    /* Never release communication inhibit with a partially-applied fixed
     * product configuration.  Reset invalidates the snapshot; the common guard
     * will remain inhibited and retry through the normal re-init path. */
    DVC1124_AFE_Reset();
}

uint8_t bms_afe_apply_protection_config(void)
{
    /* Only protection parameters are runtime/Flash-owned. */
    return DVC1124_ApplyProtectionConfig();
}

void bms_afe_sleep(void)
{
    DVC1124_AFE_Sleep();
}
