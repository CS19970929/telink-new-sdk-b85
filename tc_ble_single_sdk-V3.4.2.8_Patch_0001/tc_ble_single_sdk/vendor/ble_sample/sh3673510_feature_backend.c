#include "bms_afe.h"
#include "bms_state.h"
#include "sh3673510_project_config.h"
#include "sh3673510_control.h"
#include "sh3673520.h"
#include "sh3673520_reg.h"
#include <string.h>

#define SH_FEATURE_BALANCE_REFRESH_SAMPLES 100u /* 20 s @ 200 ms, below 30.38 s HW timeout */

static uint32_t s_balance_requested;
static uint32_t s_balance_effective;
static uint16_t s_balance_refresh_count;

static uint8_t s_ow_busy;
static uint8_t s_ow_seen_odd;
static uint8_t s_ow_seen_even;
static uint8_t s_ow_attempts;
static uint32_t s_ow_mask;

static uint32_t sh_valid_cell_mask(void)
{
    return (1uL << SH3673510_D011_CELL_COUNT) - 1uL;
}

static uint8_t sh_ntc_valid(int32_t raw)
{
    uint32_t ohm = 0u;
    return (SH3673520_NtcRawToOhm(raw, &ohm) == SH3673520_OK && ohm != 0u) ? 1u : 0u;
}

uint8_t sh3673510_backend_get_feature_snapshot(bms_afe_feature_snapshot_t *out)
{
    sh3673520_temperature_raw_t raw;
    uint16_t t1, t2;

    if (out == 0) return 0u;
    memset(out, 0, sizeof(*out));
    if (!SH3673520_IsReady()) return 0u;
    if (SH3673520_ReadTemperatures(&raw) != SH3673520_OK) return 0u;

    out->valid = 1u;
    out->cell_count = SH3673510_D011_CELL_COUNT;
    out->battery_temp_valid = (uint8_t)(sh_ntc_valid(raw.external_raw[SH3673510_D011_BAT_NTC1_INDEX]) &&
                                        sh_ntc_valid(raw.external_raw[SH3673510_D011_BAT_NTC2_INDEX]));
    out->heater_temp_valid = sh_ntc_valid(raw.external_raw[SH3673510_D011_HEATER_NTC_INDEX]);
    out->mos_temp_valid = sh_ntc_valid(raw.external_raw[SH3673510_D011_MOS_NTC_INDEX]);

    t1 = g_stCellInfoReport.u16Temperature[AFE1_TEMP1];
    t2 = g_stCellInfoReport.u16Temperature[AFE1_TEMP2];
    out->battery_temp_min_x10 = (t1 < t2) ? t1 : t2;
    out->battery_temp_max_x10 = (t1 > t2) ? t1 : t2;
    out->heater_temp_x10 = g_stCellInfoReport.u16Temperature[AFE1_TEMP3];
    out->mos_temp_x10 = g_stCellInfoReport.u16Temperature[MOS_TEMP1];
    return 1u;
}

uint8_t sh3673510_backend_set_balance_mask(uint32_t cell_mask)
{
    cell_mask &= sh_valid_cell_mask();
    if (cell_mask == 0u) {
        s_balance_refresh_count = 0u;
        if (s_balance_requested != 0u || s_balance_effective != 0u) {
            if (!sh3673510_control_set_balance(0u)) return 0u;
        }
        s_balance_requested = 0u;
        s_balance_effective = 0u;
        return 1u;
    }

    if (s_balance_refresh_count < SH_FEATURE_BALANCE_REFRESH_SAMPLES)
        ++s_balance_refresh_count;

    if (cell_mask != s_balance_requested ||
        s_balance_refresh_count >= SH_FEATURE_BALANCE_REFRESH_SAMPLES) {
        if (!sh3673510_control_set_balance((uint16_t)cell_mask)) return 0u;
        s_balance_requested = cell_mask;
        s_balance_effective = cell_mask;
        s_balance_refresh_count = 0u;
    }
    return 1u;
}

uint8_t sh3673510_backend_get_balance_mask(uint32_t *cell_mask)
{
    if (cell_mask == 0) return 0u;
    *cell_mask = s_balance_effective;
    return 1u;
}

static uint8_t sh_ow_enable(uint8_t enable)
{
    uint8_t v, verify;
    if (SH3673520_ReadReg(SH3673520_REG_SCONF3, &v) != SH3673520_OK) return 0u;
    if (enable) v |= SH3673520_SCONF3_OWD_EN_MASK;
    else v &= (uint8_t)~SH3673520_SCONF3_OWD_EN_MASK;
    v &= (uint8_t)~SH3673520_SCONF3_OWD_TRG_MASK;
    if (SH3673520_WriteReg(SH3673520_REG_SCONF3, v) != SH3673520_OK) return 0u;
    if (SH3673520_ReadReg(SH3673520_REG_SCONF3, &verify) != SH3673520_OK) return 0u;
    return ((verify & SH3673520_SCONF3_OWD_EN_MASK) ==
            (v & SH3673520_SCONF3_OWD_EN_MASK)) ? 1u : 0u;
}

static uint8_t sh_ow_trigger(void)
{
    uint8_t v;
    if (SH3673520_ReadReg(SH3673520_REG_SCONF3, &v) != SH3673520_OK) return 0u;
    v |= (uint8_t)(SH3673520_SCONF3_OWD_EN_MASK | SH3673520_SCONF3_OWD_TRG_MASK);
    /* OWD_TRG is command-like/self-clearing; do not require readback=1. */
    return (SH3673520_WriteReg(SH3673520_REG_SCONF3, v) == SH3673520_OK) ? 1u : 0u;
}

static void sh_ow_abort(void)
{
    (void)sh_ow_enable(0u);
    s_ow_busy = 0u;
    s_ow_seen_odd = 0u;
    s_ow_seen_even = 0u;
    s_ow_attempts = 0u;
    s_ow_mask = 0u;
}

uint8_t sh3673510_backend_openwire_start(void)
{
    if (s_ow_busy || !SH3673520_IsReady()) return 0u;
    s_ow_seen_odd = 0u;
    s_ow_seen_even = 0u;
    s_ow_attempts = 0u;
    s_ow_mask = 0u;
    if (!sh_ow_enable(1u) || !sh_ow_trigger()) { sh_ow_abort(); return 0u; }
    s_ow_busy = 1u;
    return 1u;
}

bms_afe_diag_state_t sh3673510_backend_openwire_poll(bms_afe_openwire_result_t *out)
{
    uint8_t flag3;
    uint8_t data[3];
    uint32_t raw;
    uint32_t valid_mask = sh_valid_cell_mask();
    uint8_t i;

    if (!s_ow_busy) return BMS_AFE_DIAG_IDLE;
    /* FLAG3.OWD_FLG is read-clear. OWD_IND is valid in the same read. */
    if (SH3673520_ReadReg(SH3673520_REG_FLAG3, &flag3) != SH3673520_OK) {
        sh_ow_abort(); return BMS_AFE_DIAG_ERROR;
    }
    if ((flag3 & SH3673520_FLAG3_OWD_FLG_MASK) == 0u) return BMS_AFE_DIAG_BUSY;
    if (SH3673520_ReadRegs(SH3673520_REG_OWDH, data, 3u) != SH3673520_OK) {
        sh_ow_abort(); return BMS_AFE_DIAG_ERROR;
    }

    raw = (((uint32_t)data[0] & 0x0Fu) << 16) | ((uint32_t)data[1] << 8) | data[2];
    if (flag3 & SH3673520_FLAG3_OWD_IND_MASK) {
        s_ow_mask |= raw & 0x000AAAAAu;
        s_ow_seen_odd = 1u;
    } else {
        s_ow_mask |= raw & 0x00055555u;
        s_ow_seen_even = 1u;
    }
    ++s_ow_attempts;

    if (s_ow_seen_odd && s_ow_seen_even) {
        if (out != 0) {
            memset(out, 0, sizeof(*out));
            out->valid = 1u;
            out->determinate = 1u;
            out->cell_count = SH3673510_D011_CELL_COUNT;
            out->open_cell_mask = s_ow_mask & valid_mask;
            for (i = 0u; i < SH3673510_D011_CELL_COUNT; ++i)
                out->diagnostic_cell_mv[i] = g_stCellInfoReport.u16VCell[i];
        }
        (void)sh_ow_enable(0u);
        s_ow_busy = 0u;
        s_ow_attempts = 0u;
        return BMS_AFE_DIAG_READY;
    }

    if (s_ow_attempts >= 3u || !sh_ow_trigger()) {
        sh_ow_abort(); return BMS_AFE_DIAG_ERROR;
    }
    return BMS_AFE_DIAG_BUSY;
}
