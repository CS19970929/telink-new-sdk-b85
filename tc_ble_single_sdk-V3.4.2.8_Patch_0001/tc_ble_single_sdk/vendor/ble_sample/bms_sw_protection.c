#include "bms_sw_protection.h"
#include "bms_actuator.h"
#include <string.h>

#define BMS_SW_CFG_VERSION 0x0101u

static bms_sw_protection_config_t g_cfg;
static bms_protection_runtime_t g_rt[BMS_PROT_COUNT];
static uint32_t g_active_mask;
static uint16_t g_primary_fault;

static uint32_t bms_crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t i;
    uint8_t bit;
    for (i = 0u; i < len; ++i) {
        crc ^= data[i];
        for (bit = 0u; bit < 8u; ++bit) {
            crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
        }
    }
    return ~crc;
}

static void bms_cfg_finish(bms_sw_protection_config_t *cfg)
{
    cfg->version = BMS_SW_CFG_VERSION;
    cfg->length = (uint16_t)sizeof(*cfg);
    cfg->crc32 = 0u;
    cfg->crc32 = bms_crc32((const uint8_t *)cfg, (uint32_t)sizeof(*cfg));
}

void bms_sw_protection_config_finalize(bms_sw_protection_config_t *cfg)
{
    if (cfg != 0) bms_cfg_finish(cfg);
}

static void set_cfg(bms_protection_cfg_t *p, int32_t enter, int32_t recover,
                    uint32_t enter_ms, uint32_t recover_ms, uint32_t reason,
                    uint16_t code, uint8_t high, uint8_t chg, uint8_t dsg,
                    uint8_t global, uint8_t latch, uint8_t fet, uint8_t fuse)
{
    memset(p, 0, sizeof(*p));
    p->enter_value = enter;
    p->recover_value = recover;
    p->enter_delay_ms = enter_ms;
    p->recover_delay_ms = recover_ms;
    p->inhibit_reason = reason;
    p->fault_code = code;
    p->trip_when_high = high;
    p->inhibit_charge = chg;
    p->inhibit_discharge = dsg;
    p->global_shutdown = global;
    p->auto_recover = latch ? 0u : 1u;
    p->latch = latch;
    p->fet_verify = fet;
    p->fuse_eligible = fuse;
    p->severity = fuse ? BMS_SEVERITY_FUSE_ELIGIBLE :
                  (latch ? BMS_SEVERITY_LATCHED :
                  (global ? BMS_SEVERITY_GLOBAL_SHUTDOWN : BMS_SEVERITY_PROTECTION));
}

void bms_sw_protection_defaults(bms_sw_protection_config_t *cfg)
{
    if (cfg == 0) return;
    memset(cfg, 0, sizeof(*cfg));
    /* Conservative fallback only; firmware replaces these from validated params. */
    set_cfg(&cfg->item[BMS_PROT_CELL_OV], 3750, 3500, 100, 1000, BMS_INHIBIT_CELL_OV, 0x1101, 1, 1, 0, 0, 0, 1, 1);
    set_cfg(&cfg->item[BMS_PROT_CELL_UV], 3000, 3100, 1000, 1000, BMS_INHIBIT_CELL_UV, 0x1102, 0, 0, 1, 0, 0, 1, 0);
    set_cfg(&cfg->item[BMS_PROT_PACK_OV], 36500, 35000, 100, 1000, BMS_INHIBIT_PACK_OV, 0x1103, 1, 1, 0, 0, 0, 1, 1);
    set_cfg(&cfg->item[BMS_PROT_PACK_UV], 29000, 30000, 1000, 1000, BMS_INHIBIT_PACK_UV, 0x1104, 0, 0, 1, 0, 0, 1, 0);
    set_cfg(&cfg->item[BMS_PROT_CHG_OC], 20000, 10000, 100, 1000, BMS_INHIBIT_CHG_OC, 0x1201, 1, 1, 0, 0, 0, 1, 1);
    set_cfg(&cfg->item[BMS_PROT_DSG_OC1], 10000, 8000, 500, 1000, BMS_INHIBIT_DSG_OC1, 0x1202, 1, 0, 1, 0, 0, 1, 0);
    set_cfg(&cfg->item[BMS_PROT_DSG_OC2], 20000, 10000, 100, 2000, BMS_INHIBIT_DSG_OC2, 0x1203, 1, 0, 1, 1, 0, 1, 1);
    set_cfg(&cfg->item[BMS_PROT_SHORT], 500000, 10000, 0, 0, BMS_INHIBIT_SHORT, 0x1204, 1, 0, 1, 1, 1, 1, 1);
    set_cfg(&cfg->item[BMS_PROT_CHG_OT], 550, 500, 100, 1000, BMS_INHIBIT_CHG_OT, 0x1301, 1, 1, 0, 0, 0, 1, 1);
    set_cfg(&cfg->item[BMS_PROT_CHG_UT], 0, 30, 100, 1000, BMS_INHIBIT_CHG_UT, 0x1302, 0, 1, 0, 0, 0, 1, 0);
    set_cfg(&cfg->item[BMS_PROT_DSG_OT], 600, 500, 100, 1000, BMS_INHIBIT_DSG_OT, 0x1303, 1, 0, 1, 0, 0, 1, 1);
    set_cfg(&cfg->item[BMS_PROT_DSG_UT], -200, -100, 100, 1000, BMS_INHIBIT_DSG_UT, 0x1304, 0, 0, 1, 0, 0, 1, 0);
    set_cfg(&cfg->item[BMS_PROT_MOS_OT], 950, 800, 100, 2000, BMS_INHIBIT_MOS_OT, 0x1305, 1, 1, 1, 1, 0, 1, 1);
    bms_cfg_finish(cfg);
}

uint8_t bms_sw_protection_config_validate(const bms_sw_protection_config_t *cfg)
{
    bms_sw_protection_config_t copy;
    uint32_t expected;
    uint8_t i;

    if ((cfg == 0) || (cfg->version != BMS_SW_CFG_VERSION) ||
        (cfg->length != sizeof(*cfg))) return 0u;
    copy = *cfg;
    expected = copy.crc32;
    copy.crc32 = 0u;
    if (bms_crc32((const uint8_t *)&copy, sizeof(copy)) != expected) return 0u;
    for (i = 0u; i < BMS_PROT_COUNT; ++i) {
        if (cfg->item[i].enter_delay_ms > 600000u ||
            cfg->item[i].recover_delay_ms > 600000u) return 0u;
        if (cfg->item[i].trip_when_high) {
            if (cfg->item[i].recover_value >= cfg->item[i].enter_value) return 0u;
        } else if (cfg->item[i].recover_value <= cfg->item[i].enter_value) {
            return 0u;
        }
    }
    if (cfg->item[BMS_PROT_DSG_OC2].enter_value <= cfg->item[BMS_PROT_DSG_OC1].enter_value ||
        cfg->item[BMS_PROT_SHORT].enter_value <= cfg->item[BMS_PROT_DSG_OC2].enter_value) return 0u;
    return 1u;
}

void bms_sw_protection_init(const bms_sw_protection_config_t *cfg)
{
    memset(g_rt, 0, sizeof(g_rt));
    g_active_mask = 0u;
    g_primary_fault = 0u;
    if (bms_sw_protection_config_validate(cfg)) g_cfg = *cfg;
    else bms_sw_protection_defaults(&g_cfg);
}

static int32_t bms_input(bms_protection_id_t id, const bms_measurement_t *m)
{
    uint8_t i;
    uint16_t min_cell = 0xFFFFu;
    uint16_t max_cell = 0u;
    for (i = 0u; i < m->series_count && i < BMS_CELL_MAX; ++i) {
        if (m->cell_mv[i] < min_cell) min_cell = m->cell_mv[i];
        if (m->cell_mv[i] > max_cell) max_cell = m->cell_mv[i];
    }
    switch (id) {
    case BMS_PROT_CELL_OV: return max_cell;
    case BMS_PROT_CELL_UV: return min_cell;
    case BMS_PROT_PACK_OV:
    case BMS_PROT_PACK_UV: return (int32_t)m->pack_mv_adc;
    case BMS_PROT_CHG_OC: return (m->current_ma > 0) ? m->current_ma : 0;
    case BMS_PROT_DSG_OC1:
    case BMS_PROT_DSG_OC2:
    case BMS_PROT_SHORT: return (m->current_ma < 0) ? -m->current_ma : 0;
    case BMS_PROT_CHG_OT:
    case BMS_PROT_CHG_UT:
    case BMS_PROT_DSG_OT:
    case BMS_PROT_DSG_UT: return m->battery_temp_dC;
    case BMS_PROT_MOS_OT: return m->mos_temp_dC;
    default: return 0;
    }
}

static void bms_apply_action(const bms_protection_cfg_t *p, uint8_t active)
{
    if (p->inhibit_charge) bms_set_charge_inhibit(p->inhibit_reason, active);
    if (p->inhibit_discharge) bms_set_discharge_inhibit(p->inhibit_reason, active);
    if (p->global_shutdown) bms_set_global_inhibit(p->inhibit_reason, active);
}

static uint8_t bms_enter(const bms_protection_cfg_t *p, int32_t value)
{
    return p->trip_when_high ? (value >= p->enter_value) : (value <= p->enter_value);
}

static uint8_t bms_recover(const bms_protection_cfg_t *p, int32_t value)
{
    return p->trip_when_high ? (value <= p->recover_value) : (value >= p->recover_value);
}

void bms_sw_protection_update(const bms_measurement_t *m, uint32_t now_ms)
{
    uint8_t i;
    uint32_t new_active = 0u;
    uint8_t best_severity = 0u;

    if (m == 0) return;
    g_primary_fault = 0u;
    for (i = 0u; i < BMS_PROT_COUNT; ++i) {
        bms_protection_cfg_t *p = &g_cfg.item[i];
        bms_protection_runtime_t *r = &g_rt[i];
        int32_t value = bms_input((bms_protection_id_t)i, m);
        uint8_t trip = bms_enter(p, value);
        uint8_t clear = bms_recover(p, value);

        switch (r->state) {
        case BMS_PROT_NORMAL:
            if (trip) { r->state = BMS_PROT_ENTER_DELAY; r->state_since_ms = now_ms; }
            break;
        case BMS_PROT_ENTER_DELAY:
            if (!trip) r->state = BMS_PROT_NORMAL;
            else if ((uint32_t)(now_ms - r->state_since_ms) >= p->enter_delay_ms) {
                r->state = p->latch ? BMS_PROT_LATCHED : BMS_PROT_ACTIVE;
                r->state_since_ms = now_ms;
                bms_apply_action(p, 1u);
                bms_safety_log_transition(p->fault_code, 1u, m);
            }
            break;
        case BMS_PROT_ACTIVE:
            if (p->auto_recover && clear) {
                r->state = BMS_PROT_RECOVERY_DELAY;
                r->state_since_ms = now_ms;
            }
            break;
        case BMS_PROT_RECOVERY_DELAY:
            if (!clear) r->state = BMS_PROT_ACTIVE;
            else if ((uint32_t)(now_ms - r->state_since_ms) >= p->recover_delay_ms) {
                r->state = BMS_PROT_NORMAL;
                bms_apply_action(p, 0u);
                bms_safety_log_transition(p->fault_code, 0u, m);
            }
            break;
        case BMS_PROT_LATCHED:
        default:
            break;
        }
        if ((r->state == BMS_PROT_ACTIVE) || (r->state == BMS_PROT_RECOVERY_DELAY) ||
            (r->state == BMS_PROT_LATCHED)) {
            new_active |= (1UL << i);
            if ((g_primary_fault == 0u) || (p->severity > best_severity)) {
                best_severity = p->severity;
                g_primary_fault = p->fault_code;
            }
        }
    }
    g_active_mask = new_active;
}

uint32_t bms_sw_protection_active_mask(void) { return g_active_mask; }
uint16_t bms_sw_protection_primary_fault(void) { return g_primary_fault; }
const bms_protection_runtime_t *bms_sw_protection_runtime(bms_protection_id_t id)
{
    return ((uint8_t)id < BMS_PROT_COUNT) ? &g_rt[id] : 0;
}
const bms_sw_protection_config_t *bms_sw_protection_config(void) { return &g_cfg; }
