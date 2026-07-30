#include "bms_fuse.h"
#include "bms_actuator.h"
#include "bms_safety_config.h"
#include <string.h>

#define BMS_FUSE_RECORD_VERSION 0x0101u
#define BMS_FUSE_CFG_VERSION    0x0101u

static bms_fuse_state_t g_state;
static bms_fuse_record_t g_record;
static bms_fuse_config_t g_cfg;
static uint32_t g_trigger_start_ms;
static uint16_t g_afe_status;
static uint16_t g_reset_reason;
static uint16_t g_param_version;

void bms_fuse_set_context(uint16_t afe_status, uint16_t reset_reason,
                          uint16_t param_version)
{
    g_afe_status = afe_status;
    g_reset_reason = reset_reason;
    g_param_version = param_version;
}

static uint32_t fuse_crc32(const uint8_t *data, uint32_t len)
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

static void fuse_config_prepare(void)
{
    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.version = BMS_FUSE_CFG_VERSION;
    g_cfg.length = sizeof(g_cfg);
    g_cfg.severe_hold_ms = BMS_FUSE_SEVERE_HOLD_MS;
    g_cfg.max_pulse_ms = BMS_FUSE_MAX_PULSE_MS;
    g_cfg.required_evidence_count = 2u;
    g_cfg.auto_trigger_enable = BMS_FUSE_AUTO_TRIGGER_ENABLE ? 1u : 0u;
    g_cfg.hardware_feedback_enable = BMS_FUSE_HW_FEEDBACK_ENABLE ? 1u : 0u;
    g_cfg.factory_test_enable = BMS_FUSE_FACTORY_TEST_ENABLE ? 1u : 0u;
    g_cfg.crc32 = 0u;
    g_cfg.crc32 = fuse_crc32((const uint8_t *)&g_cfg, sizeof(g_cfg));
}

static uint8_t fuse_config_valid(void)
{
    bms_fuse_config_t copy = g_cfg;
    uint32_t expected = copy.crc32;
    if (copy.version != BMS_FUSE_CFG_VERSION ||
        copy.length != sizeof(copy) ||
        copy.required_evidence_count < 2u ||
        copy.severe_hold_ms == 0u) return 0u;
    if (copy.auto_trigger_enable &&
        (!copy.hardware_feedback_enable || copy.max_pulse_ms == 0u)) return 0u;
    copy.crc32 = 0u;
    return fuse_crc32((const uint8_t *)&copy, sizeof(copy)) == expected;
}

static uint8_t fuse_record_valid(const bms_fuse_record_t *r)
{
    bms_fuse_record_t copy;
    uint32_t crc;
    if (r == 0 || r->version != BMS_FUSE_RECORD_VERSION ||
        r->length != sizeof(*r)) return 0u;
    copy = *r;
    crc = copy.crc32;
    copy.crc32 = 0u;
    return fuse_crc32((const uint8_t *)&copy, sizeof(copy)) == crc;
}

static uint8_t fuse_evidence_count(uint32_t mask)
{
    uint8_t count = 0u;
    while (mask) { count = (uint8_t)(count + (mask & 1u)); mask >>= 1; }
    return count;
}

static uint8_t fuse_save_state(bms_fuse_state_t state, uint32_t reason,
                               const bms_measurement_t *m, uint32_t now_ms)
{
    uint8_t i;
    memset(&g_record, 0, sizeof(g_record));
    g_record.version = BMS_FUSE_RECORD_VERSION;
    g_record.length = sizeof(g_record);
    g_record.state = state;
    g_record.reason = reason;
    g_record.fault_time_ms = now_ms;
    g_record.afe_status = g_afe_status;
    g_record.reset_reason = g_reset_reason;
    g_record.param_version = g_param_version;
    g_record.cell_min_mv = 0xFFFFu;
    if (m != 0) {
        for (i = 0u; i < m->series_count && i < BMS_CELL_MAX; ++i) {
            if (m->cell_mv[i] > g_record.cell_max_mv) g_record.cell_max_mv = m->cell_mv[i];
            if (m->cell_mv[i] < g_record.cell_min_mv) g_record.cell_min_mv = m->cell_mv[i];
        }
        g_record.pack_mv = m->pack_mv_adc;
        g_record.current_ma = m->current_ma;
        g_record.battery_temp_dC = m->battery_temp_dC;
        g_record.mos_temp_dC = m->mos_temp_dC;
    }
    g_record.crc32 = 0u;
    g_record.crc32 = fuse_crc32((const uint8_t *)&g_record, sizeof(g_record));
    return bms_fuse_persist_save(&g_record);
}

void bms_fuse_init(uint32_t now_ms)
{
    (void)now_ms;
    fuse_config_prepare();
    bms_fuse_hw_drive(0u);
    memset(&g_record, 0, sizeof(g_record));
    if (bms_fuse_persist_load(&g_record) && fuse_record_valid(&g_record) &&
        (g_record.state == FUSE_FIRED || g_record.state == FUSE_FAILED ||
         g_record.state == FUSE_TRIGGERING)) {
        g_state = (g_record.state == FUSE_FIRED) ? FUSE_FIRED : FUSE_FAILED;
        bms_set_global_inhibit(BMS_INHIBIT_FUSE_STATE, 1u);
        return;
    }
    if (!fuse_config_valid()) {
        g_state = FUSE_DISABLED;
        bms_set_global_inhibit(BMS_INHIBIT_PARAM_INVALID, 1u);
    } else {
        g_state = g_cfg.auto_trigger_enable ? FUSE_MONITORING : FUSE_DISABLED;
    }
}

void bms_fuse_update(const bms_fuse_input_t *in,
                     const bms_measurement_t *m, uint32_t now_ms)
{
    if (in == 0) return;
    switch (g_state) {
    case FUSE_DISABLED:
        bms_fuse_hw_drive(0u);
        break;
    case FUSE_MONITORING:
#if BMS_FUSE_AUTO_TRIGGER_ENABLE
        if (!in->factory_or_debug && in->environment_valid &&
            in->hardware_authorized && bms_fuse_hw_is_supported() &&
            bms_fuse_hw_driver_ok() && in->fet_off_failed &&
            fuse_evidence_count(in->evidence_mask) >= g_cfg.required_evidence_count &&
            in->severe_since_ms != 0u &&
            (uint32_t)(now_ms - in->severe_since_ms) >= g_cfg.severe_hold_ms &&
            g_cfg.max_pulse_ms > 0u) {
            if (fuse_save_state(FUSE_ARMED, in->evidence_mask, m, now_ms)) {
                g_state = FUSE_ARMED;
                bms_set_global_inhibit(BMS_INHIBIT_FUSE_STATE, 1u);
            }
        }
#endif
        break;
    case FUSE_ARMED:
        bms_set_global_inhibit(BMS_INHIBIT_FUSE_STATE, 1u);
        if (!in->environment_valid || !in->hardware_authorized ||
            fuse_evidence_count(in->evidence_mask) < g_cfg.required_evidence_count ||
            !in->fet_off_failed) {
            g_state = FUSE_FAILED;
            (void)fuse_save_state(FUSE_FAILED, in->evidence_mask, m, now_ms);
            break;
        }
        if (!fuse_save_state(FUSE_TRIGGERING, in->evidence_mask, m, now_ms)) {
            g_state = FUSE_FAILED;
            break;
        }
        g_trigger_start_ms = now_ms;
        g_state = FUSE_TRIGGERING;
        bms_fuse_hw_drive(1u);
        break;
    case FUSE_TRIGGERING:
        bms_set_global_inhibit(BMS_INHIBIT_FUSE_STATE, 1u);
        if (bms_fuse_hw_fired_feedback()) {
            bms_fuse_hw_drive(0u);
            g_state = FUSE_FIRED;
            (void)fuse_save_state(FUSE_FIRED, g_record.reason, m, now_ms);
        } else if ((uint32_t)(now_ms - g_trigger_start_ms) >= g_cfg.max_pulse_ms) {
            bms_fuse_hw_drive(0u);
            g_state = FUSE_FAILED;
            (void)fuse_save_state(FUSE_FAILED, g_record.reason, m, now_ms);
        }
        break;
    case FUSE_FIRED:
    case FUSE_FAILED:
    default:
        bms_fuse_hw_drive(0u);
        bms_set_global_inhibit(BMS_INHIBIT_FUSE_STATE, 1u);
        break;
    }
}

bms_fuse_state_t bms_fuse_get_state(void) { return g_state; }
const bms_fuse_record_t *bms_fuse_get_record(void) { return &g_record; }
const bms_fuse_config_t *bms_fuse_get_config(void) { return &g_cfg; }
