#include "bms_diagnostics.h"
#include "bms_actuator.h"
#include "bms_safety_config.h"
#include <string.h>

#define BMS_DIAGNOSTIC_CFG_VERSION 0x0101u

static bms_diagnostic_config_t g_cfg;
static bms_measurement_t g_previous;
static uint32_t g_faults;
static uint32_t g_invalid_cell_mask;
static uint16_t g_cell_frozen_count[BMS_CELL_MAX];
static uint16_t g_current_frozen_count;
static uint8_t g_have_previous;
static uint8_t g_afe_config_valid;

static uint32_t bms_abs_i32(int32_t v)
{
    return (v < 0) ? ((uint32_t)(-(v + 1)) + 1u) : (uint32_t)v;
}
static uint32_t bms_diff_u32(uint32_t a, uint32_t b) { return (a > b) ? (a - b) : (b - a); }

static uint32_t diagnostic_crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t i;
    uint8_t bit;
    for (i = 0u; i < len; ++i) {
        crc ^= data[i];
        for (bit = 0u; bit < 8u; ++bit)
            crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
    }
    return ~crc;
}

void bms_diagnostics_config_finalize(bms_diagnostic_config_t *cfg)
{
    if (cfg == 0) return;
    cfg->version = BMS_DIAGNOSTIC_CFG_VERSION;
    cfg->length = (uint16_t)sizeof(*cfg);
    cfg->crc32 = 0u;
    cfg->crc32 = diagnostic_crc32((const uint8_t *)cfg, sizeof(*cfg));
}

uint8_t bms_diagnostics_config_validate(const bms_diagnostic_config_t *cfg)
{
    bms_diagnostic_config_t copy;
    uint32_t expected;
    if (cfg == 0 || cfg->version != BMS_DIAGNOSTIC_CFG_VERSION ||
        cfg->length != sizeof(*cfg)) return 0u;
    copy = *cfg;
    expected = copy.crc32;
    copy.crc32 = 0u;
    if (diagnostic_crc32((const uint8_t *)&copy, sizeof(copy)) != expected) return 0u;
    if (cfg->cell_min_mv < 500u || cfg->cell_max_mv > 6000u ||
        cfg->cell_min_mv >= cfg->cell_max_mv || cfg->cell_jump_mv == 0u ||
        cfg->pack_cross_max_mv == 0u || cfg->cell_sum_max_error_mv == 0u ||
        cfg->current_jump_ma == 0u || cfg->current_zero_drift_ma == 0u ||
        cfg->temp_jump_dC == 0u || cfg->ntc_short_mv >= cfg->ntc_open_mv ||
        cfg->frozen_frames == 0u || cfg->expected_series == 0u ||
        cfg->expected_series > BMS_CELL_MAX) return 0u;
    return 1u;
}

void bms_diagnostics_init(const bms_diagnostic_config_t *cfg)
{
    memset(&g_previous, 0, sizeof(g_previous));
    memset(g_cell_frozen_count, 0, sizeof(g_cell_frozen_count));
    g_current_frozen_count = 0u;
    g_faults = 0u;
    g_invalid_cell_mask = 0u;
    g_have_previous = 0u;
    g_afe_config_valid = 0u;
    memset(&g_cfg, 0, sizeof(g_cfg));
    if (bms_diagnostics_config_validate(cfg)) {
        g_cfg = *cfg;
    } else {
        /* An invalid diagnostic policy is itself a blocking configuration. */
        g_cfg.expected_series = 0u;
        g_cfg.board_valid = 0u;
        g_cfg.cell_map_valid = 0u;
        g_cfg.params_valid = 0u;
        g_cfg.rsense_valid = 0u;
    }
}

void bms_diagnostics_set_afe_config_valid(uint8_t valid)
{
    g_afe_config_valid = valid ? 1u : 0u;
}

uint32_t bms_diagnostics_update(const bms_measurement_t *m, uint32_t now_ms)
{
    uint32_t f = 0u;
    uint32_t sum = 0u;
    uint32_t required_mask;
    uint8_t i;

    if (m == 0) return BMS_DIAG_SAMPLE_STALE;
    g_invalid_cell_mask = 0u;
    if ((m->series_count != g_cfg.expected_series) ||
        (m->series_count == 0u) || (m->series_count > BMS_CELL_MAX)) {
        f |= BMS_DIAG_CELL_COUNT | BMS_DIAG_BOARD_SERIES;
    }
    required_mask = (m->series_count <= BMS_CELL_MAX && m->series_count != 0u) ?
                    ((1UL << m->series_count) - 1UL) : 0u;
    if ((m->cell_valid_mask & required_mask) != required_mask) f |= BMS_DIAG_CELL_COUNT;

    for (i = 0u; i < m->series_count && i < BMS_CELL_MAX; ++i) {
        uint16_t v = m->cell_mv[i];
        sum += v;
        if (v < g_cfg.cell_min_mv || v > g_cfg.cell_max_mv) {
            f |= BMS_DIAG_CELL_RANGE;
            g_invalid_cell_mask |= (1UL << i);
        }
        if (g_have_previous && m->timestamp_ms != g_previous.timestamp_ms) {
            if (bms_diff_u32(v, g_previous.cell_mv[i]) > g_cfg.cell_jump_mv) {
                f |= BMS_DIAG_CELL_JUMP;
                g_invalid_cell_mask |= (1UL << i);
            }
            if (v == g_previous.cell_mv[i]) {
                if (g_cell_frozen_count[i] < 0xFFFFu) ++g_cell_frozen_count[i];
                if (g_cell_frozen_count[i] >= g_cfg.frozen_frames) f |= BMS_DIAG_CELL_FROZEN;
            } else {
                g_cell_frozen_count[i] = 0u;
            }
        }
    }

    if (m->afe_frame_valid &&
        (m->pack_mv_afe == 0u ||
         bms_diff_u32(sum, m->pack_mv_afe) > g_cfg.cell_sum_max_error_mv))
        f |= BMS_DIAG_CELL_SUM_AFE;
    if (m->pack_adc_valid &&
        bms_diff_u32(sum, m->pack_mv_adc) > g_cfg.cell_sum_max_error_mv) f |= BMS_DIAG_CELL_SUM_ADC;
    if (m->pack_mv_adc < ((uint32_t)g_cfg.cell_min_mv * m->series_count) ||
        m->pack_mv_adc > ((uint32_t)g_cfg.cell_max_mv * m->series_count)) f |= BMS_DIAG_PACK_RANGE;
    if (m->pack_mv_afe && m->pack_mv_adc &&
        bms_diff_u32(m->pack_mv_afe, m->pack_mv_adc) > g_cfg.pack_cross_max_mv) f |= BMS_DIAG_PACK_CROSS;

    if (m->current_raw == 0x0000u || m->current_raw == 0xFFFFu) f |= BMS_DIAG_CURRENT_SAT;
    if (g_have_previous && m->timestamp_ms != g_previous.timestamp_ms) {
        if (bms_abs_i32(m->current_ma - g_previous.current_ma) > g_cfg.current_jump_ma)
            f |= BMS_DIAG_CURRENT_JUMP;
        if (m->current_raw == g_previous.current_raw) {
            if (g_current_frozen_count < 0xFFFFu) ++g_current_frozen_count;
            if (g_current_frozen_count >= g_cfg.frozen_frames) f |= BMS_DIAG_CURRENT_FROZEN;
        } else g_current_frozen_count = 0u;
        if (bms_abs_i32((int32_t)m->battery_temp_dC - g_previous.battery_temp_dC) > g_cfg.temp_jump_dC ||
            bms_abs_i32((int32_t)m->mos_temp_dC - g_previous.mos_temp_dC) > g_cfg.temp_jump_dC)
            f |= BMS_DIAG_TEMP_JUMP;
    }
    if (!m->charger_present && !m->load_requested &&
        bms_abs_i32(m->current_ma) > g_cfg.current_zero_drift_ma) f |= BMS_DIAG_CURRENT_ZERO;
    if (m->charger_present && m->current_ma < -(int32_t)g_cfg.current_zero_drift_ma)
        f |= BMS_DIAG_CURRENT_DIRECTION;

    if (m->battery_ntc_mv >= g_cfg.ntc_open_mv || m->mos_ntc_mv >= g_cfg.ntc_open_mv)
        f |= BMS_DIAG_TEMP_OPEN;
    if (m->battery_ntc_mv <= g_cfg.ntc_short_mv || m->mos_ntc_mv <= g_cfg.ntc_short_mv)
        f |= BMS_DIAG_TEMP_SHORT;
    if ((uint32_t)(now_ms - m->afe_timestamp_ms) > BMS_SAMPLE_STALE_MS ||
        (uint32_t)(now_ms - m->adc_timestamp_ms) > BMS_SAMPLE_STALE_MS)
        f |= BMS_DIAG_SAMPLE_STALE;
    if (!m->afe_frame_valid) f |= BMS_DIAG_AFE_COMM;
    if (!g_afe_config_valid) f |= BMS_DIAG_AFE_CONFIG;
    if (!g_cfg.board_valid) f |= BMS_DIAG_BOARD_SERIES;
    if (!g_cfg.cell_map_valid) f |= BMS_DIAG_CELL_MAP;
    if (!g_cfg.params_valid) f |= BMS_DIAG_PARAM_CRC;
    if (!g_cfg.rsense_valid) f |= BMS_DIAG_RSENSE_CONFIG;

    g_faults = f;
    g_previous = *m;
    g_have_previous = 1u;

    bms_set_global_inhibit(BMS_INHIBIT_SAMPLE_INVALID,
                           (f & (BMS_DIAG_CELL_RANGE | BMS_DIAG_CELL_JUMP |
                                 BMS_DIAG_CELL_COUNT | BMS_DIAG_PACK_RANGE |
                                 BMS_DIAG_TEMP_OPEN | BMS_DIAG_TEMP_SHORT |
                                 BMS_DIAG_SAMPLE_STALE | BMS_DIAG_AFE_COMM)) != 0u);
    bms_set_global_inhibit(BMS_INHIBIT_AFE_CONFIG, (f & BMS_DIAG_AFE_CONFIG) != 0u);
    bms_set_global_inhibit(BMS_INHIBIT_PARAM_INVALID, (f & BMS_DIAG_PARAM_CRC) != 0u);
    bms_set_global_inhibit(BMS_INHIBIT_DIAGNOSTIC,
                           (f & (BMS_DIAG_BOARD_SERIES | BMS_DIAG_RSENSE_CONFIG |
                                 BMS_DIAG_CELL_MAP | BMS_DIAG_CELL_SUM_AFE |
                                 BMS_DIAG_CELL_SUM_ADC | BMS_DIAG_PACK_CROSS)) != 0u);
    return f;
}

uint32_t bms_diagnostics_faults(void) { return g_faults; }
uint32_t bms_diagnostics_blocking_faults(void)
{
    return g_faults & (BMS_DIAG_CELL_RANGE | BMS_DIAG_CELL_JUMP |
        BMS_DIAG_CELL_COUNT | BMS_DIAG_CELL_SUM_AFE |
        BMS_DIAG_CELL_SUM_ADC | BMS_DIAG_PACK_RANGE |
        BMS_DIAG_PACK_CROSS | BMS_DIAG_TEMP_OPEN | BMS_DIAG_TEMP_SHORT |
        BMS_DIAG_SAMPLE_STALE | BMS_DIAG_AFE_COMM | BMS_DIAG_AFE_CONFIG |
        BMS_DIAG_BOARD_SERIES | BMS_DIAG_CELL_MAP | BMS_DIAG_PARAM_CRC |
        BMS_DIAG_RSENSE_CONFIG);
}
uint32_t bms_diagnostics_invalid_cell_mask(void) { return g_invalid_cell_mask; }
