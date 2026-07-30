#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../bms_measurement.h"
#include "../bms_actuator.h"
#include "../bms_sw_protection.h"
#include "../bms_diagnostics.h"
#include "../bms_fet_monitor.h"
#include "../bms_fuse.h"
#include "../bms_supervisor.h"

static bms_actuator_state_t hw_state;
static unsigned hw_writes;
static unsigned log_count;
static unsigned fuse_drive_count;
static unsigned fuse_drive_active;
static unsigned fuse_feedback;
static bms_fuse_record_t persisted;
static unsigned persisted_valid;

uint8_t bms_actuator_hw_apply(const bms_actuator_state_t *state)
{
    hw_state = *state;
    ++hw_writes;
    return 1u;
}
void bms_safety_log_transition(uint16_t code, uint8_t active, const bms_measurement_t *m)
{
    (void)code; (void)active; (void)m; ++log_count;
}
uint8_t bms_fet_hw_feedback_supported(bms_fet_path_t path) { (void)path; return 0u; }
uint8_t bms_fet_hw_is_off(bms_fet_path_t path) { (void)path; return 0u; }
uint8_t bms_fuse_hw_is_supported(void) { return 1u; }
uint8_t bms_fuse_hw_authorized(void) { return 1u; }
uint8_t bms_fuse_hw_fired_feedback(void) { return (uint8_t)fuse_feedback; }
uint8_t bms_fuse_hw_driver_ok(void) { return 1u; }
void bms_fuse_hw_drive(uint8_t active)
{
    ++fuse_drive_count;
    fuse_drive_active = active;
}
uint8_t bms_fuse_persist_load(bms_fuse_record_t *record)
{
    if (!persisted_valid) return 0u;
    *record = persisted;
    return 1u;
}
uint8_t bms_fuse_persist_save(const bms_fuse_record_t *record)
{
    persisted = *record;
    persisted_valid = 1u;
    return 1u;
}

static bms_measurement_t valid_measurement(uint32_t now)
{
    bms_measurement_t m;
    unsigned i;
    memset(&m, 0, sizeof(m));
    m.series_count = 10u;
    m.cell_valid_mask = 0x3FFu;
    for (i = 0; i < 10; ++i) m.cell_mv[i] = 3500u;
    m.pack_mv_afe = 35000u;
    m.pack_mv_adc = 35000u;
    m.current_raw = 0x1234u;
    m.battery_temp_dC = 250;
    m.mos_temp_dC = 300;
    m.battery_ntc_mv = 1650u;
    m.mos_ntc_mv = 1650u;
    m.afe_frame_valid = 1u;
    m.pack_adc_valid = 1u;
    m.current_valid = 1u;
    m.battery_temp_valid = 1u;
    m.mos_temp_valid = 1u;
    m.afe_timestamp_ms = now;
    m.adc_timestamp_ms = now;
    m.timestamp_ms = now;
    return m;
}

static bms_diagnostic_config_t valid_diag_config(void)
{
    bms_diagnostic_config_t d;
    memset(&d, 0, sizeof(d));
    d.cell_min_mv = 1000u; d.cell_max_mv = 5000u; d.cell_jump_mv = 500u;
    d.pack_cross_max_mv = 2000u; d.cell_sum_max_error_mv = 2000u;
    d.current_jump_ma = 60000u; d.current_zero_drift_ma = 2000u;
    d.temp_jump_dC = 300u; d.ntc_open_mv = 3250u; d.ntc_short_mv = 50u;
    d.frozen_frames = 3u; d.expected_series = 10u;
    d.board_valid = 1u; d.cell_map_valid = 1u;
    d.params_valid = 1u; d.rsense_valid = 1u;
    bms_diagnostics_config_finalize(&d);
    return d;
}

static void clear_safe_inhibits(void)
{
    bms_set_global_inhibit(BMS_INHIBIT_INIT | BMS_INHIBIT_HW_UNVERIFIED, 0u);
    bms_set_charge_inhibit(BMS_INHIBIT_INIT | BMS_INHIBIT_HW_UNVERIFIED, 0u);
    bms_set_discharge_inhibit(BMS_INHIBIT_INIT | BMS_INHIBIT_HW_UNVERIFIED, 0u);
    bms_actuator_set_safety_ready(1u);
}

static void test_measurement_and_actuator(void)
{
    bms_measurement_t m = valid_measurement(100u);
    bms_measurement_init();
    assert(!bms_measurement_is_fresh_and_complete(100u));                         /* 1 */
    bms_measurement_publish(&m);
    assert(bms_measurement_is_fresh_and_complete(100u));                          /* 2 */
    assert(!bms_measurement_is_fresh_and_complete(3000u));                        /* 3 */
    bms_measurement_note_wakeup(3000u);
    assert(!bms_measurement_is_fresh_and_complete(3000u));                        /* 4 */

    bms_actuator_init();
    assert(!hw_state.ctlc_on && !hw_state.charge_on && !hw_state.discharge_on);   /* 5 */
    clear_safe_inhibits();
    bms_actuator_request_charge(1u);
    bms_actuator_request_discharge(1u);
    bms_actuator_update(0u);
    assert(!hw_state.ctlc_on);                                                    /* 6 */
    bms_actuator_update(2001u);
    assert(hw_state.ctlc_on && hw_state.charge_on && hw_state.discharge_on);       /* 7 */
    bms_set_charge_inhibit(BMS_INHIBIT_CELL_OV, 1u);
    bms_actuator_update(2002u);
    assert(!hw_state.charge_on && hw_state.discharge_on);                         /* 8 */
    bms_set_discharge_inhibit(BMS_INHIBIT_CELL_UV, 1u);
    bms_actuator_update(2003u);
    assert(!hw_state.charge_on && !hw_state.discharge_on && hw_state.ctlc_on);     /* 9 */
    bms_set_global_inhibit(BMS_INHIBIT_SHORT, 1u);
    bms_actuator_update(2004u);
    assert(!hw_state.ctlc_on);                                                    /* 10 */
    bms_set_global_inhibit(BMS_INHIBIT_SHORT, 0u);
    bms_actuator_update(3000u);
    assert(!hw_state.ctlc_on);                                                    /* 11 */
    bms_actuator_update(5005u);
    assert(hw_state.ctlc_on);                                                     /* 12 */
}

static void test_protection(void)
{
    bms_sw_protection_config_t cfg;
    bms_measurement_t m = valid_measurement(0u);
    bms_sw_protection_defaults(&cfg);
    assert(bms_sw_protection_config_validate(&cfg));                              /* 13 */
    cfg.item[BMS_PROT_CELL_OV].recover_value = cfg.item[BMS_PROT_CELL_OV].enter_value;
    bms_sw_protection_config_finalize(&cfg);
    assert(!bms_sw_protection_config_validate(&cfg));                             /* 14 */
    bms_sw_protection_defaults(&cfg);
    bms_actuator_init(); clear_safe_inhibits();
    bms_sw_protection_init(&cfg);
    m.cell_mv[0] = 3800u;
    bms_sw_protection_update(&m, 0u);
    assert(bms_sw_protection_runtime(BMS_PROT_CELL_OV)->state == BMS_PROT_ENTER_DELAY); /* 15 */
    bms_sw_protection_update(&m, 101u);
    assert(bms_sw_protection_active_mask() & (1UL << BMS_PROT_CELL_OV));           /* 16 */
    assert(bms_get_charge_inhibit() & BMS_INHIBIT_CELL_OV);                       /* 17 */
    m.cell_mv[0] = 3400u;
    bms_sw_protection_update(&m, 102u);
    assert(bms_sw_protection_runtime(BMS_PROT_CELL_OV)->state == BMS_PROT_RECOVERY_DELAY); /* 18 */
    bms_sw_protection_update(&m, 1200u);
    assert(!(bms_get_charge_inhibit() & BMS_INHIBIT_CELL_OV));                    /* 19 */
    m.cell_mv[0] = 2900u;
    m.mos_temp_dC = 1000;
    bms_sw_protection_update(&m, 1300u);
    bms_sw_protection_update(&m, 2400u);
    assert((bms_sw_protection_active_mask() & (1UL << BMS_PROT_CELL_UV)) != 0u);  /* 20 */
    assert((bms_sw_protection_active_mask() & (1UL << BMS_PROT_MOS_OT)) != 0u);  /* 21 */
    assert(bms_get_global_inhibit() & BMS_INHIBIT_MOS_OT);                        /* 22 */
    assert(log_count >= 4u);                                                      /* 23 */
}

static void force_protection_input(bms_measurement_t *m,
                                   const bms_sw_protection_config_t *cfg,
                                   bms_protection_id_t id)
{
    int32_t trip = cfg->item[id].trip_when_high ?
                   (cfg->item[id].enter_value + 1) :
                   (cfg->item[id].enter_value - 1);
    switch (id) {
    case BMS_PROT_CELL_OV:
    case BMS_PROT_CELL_UV:
        m->cell_mv[0] = (uint16_t)trip;
        break;
    case BMS_PROT_PACK_OV:
    case BMS_PROT_PACK_UV:
        m->pack_mv_adc = (uint32_t)trip;
        break;
    case BMS_PROT_CHG_OC:
        m->current_ma = trip;
        break;
    case BMS_PROT_DSG_OC1:
    case BMS_PROT_DSG_OC2:
    case BMS_PROT_SHORT:
        m->current_ma = -trip;
        break;
    case BMS_PROT_CHG_OT:
    case BMS_PROT_CHG_UT:
    case BMS_PROT_DSG_OT:
    case BMS_PROT_DSG_UT:
        m->battery_temp_dC = (int16_t)trip;
        break;
    case BMS_PROT_MOS_OT:
        m->mos_temp_dC = (int16_t)trip;
        break;
    default:
        break;
    }
}

static void test_all_protection_inputs_and_actions(void)
{
    bms_sw_protection_config_t cfg;
    unsigned id;
    bms_sw_protection_defaults(&cfg);
    for (id = 0u; id < BMS_PROT_COUNT; ++id) {
        bms_measurement_t m = valid_measurement(100u);
        uint32_t active;
        bms_actuator_init();
        clear_safe_inhibits();
        bms_sw_protection_init(&cfg);
        force_protection_input(&m, &cfg, (bms_protection_id_t)id);
        bms_sw_protection_update(&m, 100u);
        bms_sw_protection_update(&m, 101u + cfg.item[id].enter_delay_ms);
        active = bms_sw_protection_active_mask();
        assert((active & (1UL << id)) != 0u);
        if (cfg.item[id].inhibit_charge)
            assert((bms_get_charge_inhibit() & cfg.item[id].inhibit_reason) != 0u);
        if (cfg.item[id].inhibit_discharge)
            assert((bms_get_discharge_inhibit() & cfg.item[id].inhibit_reason) != 0u);
        if (cfg.item[id].global_shutdown)
            assert((bms_get_global_inhibit() & cfg.item[id].inhibit_reason) != 0u);
    }
}

static void test_fault_isolation(void)
{
    bms_sw_protection_config_t cfg;
    bms_measurement_t m = valid_measurement(0u);
    bms_sw_protection_defaults(&cfg);
    bms_actuator_init();
    clear_safe_inhibits();
    bms_sw_protection_init(&cfg);
    m.cell_mv[0] = 3800u;
    m.current_ma = 25000;
    bms_sw_protection_update(&m, 0u);
    bms_sw_protection_update(&m, 1001u);
    assert((bms_get_charge_inhibit() & BMS_INHIBIT_CELL_OV) != 0u);
    assert((bms_get_charge_inhibit() & BMS_INHIBIT_CHG_OC) != 0u);
    m.cell_mv[0] = 3400u;
    bms_sw_protection_update(&m, 1002u);
    bms_sw_protection_update(&m, 2103u);
    assert((bms_get_charge_inhibit() & BMS_INHIBIT_CELL_OV) == 0u);
    assert((bms_get_charge_inhibit() & BMS_INHIBIT_CHG_OC) != 0u);

    m = valid_measurement(0u);
    bms_actuator_init();
    clear_safe_inhibits();
    bms_sw_protection_init(&cfg);
    m.current_ma = -25000;
    bms_sw_protection_update(&m, 0u);
    bms_sw_protection_update(&m, 501u);
    assert((bms_get_discharge_inhibit() & BMS_INHIBIT_DSG_OC1) != 0u);
    assert((bms_get_discharge_inhibit() & BMS_INHIBIT_DSG_OC2) != 0u);
    m.current_ma = -9000;
    bms_sw_protection_update(&m, 502u);
    bms_sw_protection_update(&m, 2603u);
    assert((bms_get_discharge_inhibit() & BMS_INHIBIT_DSG_OC1) != 0u);
    assert((bms_get_discharge_inhibit() & BMS_INHIBIT_DSG_OC2) == 0u);
}

static void test_diagnostics(void)
{
    bms_diagnostic_config_t d = valid_diag_config();
    bms_measurement_t m = valid_measurement(10u);
    assert(bms_diagnostics_config_validate(&d));
    d.crc32 ^= 1u;
    assert(!bms_diagnostics_config_validate(&d));
    d = valid_diag_config();
    bms_actuator_init();
    bms_diagnostics_init(&d);
    bms_diagnostics_set_afe_config_valid(1u);
    assert(bms_diagnostics_update(&m, 10u) == 0u);                                /* 24 */
    m.cell_mv[2] = 700u; m.timestamp_ms++;
    assert(bms_diagnostics_update(&m, 11u) & BMS_DIAG_CELL_RANGE);                /* 25 */
    assert(bms_diagnostics_invalid_cell_mask() & (1UL << 2));                     /* 26 */
    m = valid_measurement(20u); m.pack_mv_adc = 40000u;
    assert(bms_diagnostics_update(&m, 20u) & BMS_DIAG_CELL_SUM_ADC);              /* 27 */
    m = valid_measurement(21u); m.pack_mv_afe = 0u;
    assert(bms_diagnostics_update(&m, 21u) & BMS_DIAG_CELL_SUM_AFE);
    m = valid_measurement(30u); m.battery_ntc_mv = 3290u;
    assert(bms_diagnostics_update(&m, 30u) & BMS_DIAG_TEMP_OPEN);                 /* 28 */
    m = valid_measurement(40u); m.mos_ntc_mv = 20u;
    assert(bms_diagnostics_update(&m, 40u) & BMS_DIAG_TEMP_SHORT);                /* 29 */
    m = valid_measurement(50u); m.afe_frame_valid = 0u;
    assert(bms_diagnostics_update(&m, 50u) & BMS_DIAG_AFE_COMM);                  /* 30 */
    m = valid_measurement(60u); m.series_count = 9u;
    assert(bms_diagnostics_update(&m, 60u) & BMS_DIAG_BOARD_SERIES);              /* 31 */
    m = valid_measurement(70u); m.current_ma = -3000; m.charger_present = 1u;
    assert(bms_diagnostics_update(&m, 70u) & BMS_DIAG_CURRENT_DIRECTION);         /* 32 */
    m = valid_measurement(80u);
    assert(bms_diagnostics_update(&m, 3000u) & BMS_DIAG_SAMPLE_STALE);            /* 33 */
    bms_diagnostics_set_afe_config_valid(0u);
    m = valid_measurement(90u);
    assert(bms_diagnostics_update(&m, 90u) & BMS_DIAG_AFE_CONFIG);                /* 34 */

    d = valid_diag_config(); bms_diagnostics_init(&d);
    bms_diagnostics_set_afe_config_valid(1u);
    m = valid_measurement(100u);
    (void)bms_diagnostics_update(&m, 100u);
    m.timestamp_ms = 101u; m.cell_mv[1] = 4100u;
    assert(bms_diagnostics_update(&m, 101u) & BMS_DIAG_CELL_JUMP);

    d = valid_diag_config(); bms_diagnostics_init(&d);
    bms_diagnostics_set_afe_config_valid(1u);
    m = valid_measurement(200u);
    (void)bms_diagnostics_update(&m, 200u);
    m.timestamp_ms = 201u; (void)bms_diagnostics_update(&m, 201u);
    m.timestamp_ms = 202u; (void)bms_diagnostics_update(&m, 202u);
    m.timestamp_ms = 203u;
    assert(bms_diagnostics_update(&m, 203u) & BMS_DIAG_CELL_FROZEN);
    assert(bms_diagnostics_faults() & BMS_DIAG_CURRENT_FROZEN);

    d = valid_diag_config(); bms_diagnostics_init(&d);
    bms_diagnostics_set_afe_config_valid(1u);
    m = valid_measurement(300u); m.current_raw = 0u;
    assert(bms_diagnostics_update(&m, 300u) & BMS_DIAG_CURRENT_SAT);
    m = valid_measurement(301u); m.current_ma = 3000;
    assert(bms_diagnostics_update(&m, 301u) & BMS_DIAG_CURRENT_ZERO);

    d = valid_diag_config(); d.cell_map_valid = 0u;
    d.params_valid = 0u; d.rsense_valid = 0u;
    bms_diagnostics_config_finalize(&d);
    bms_diagnostics_init(&d); bms_diagnostics_set_afe_config_valid(1u);
    m = valid_measurement(400u);
    assert(bms_diagnostics_update(&m, 400u) & BMS_DIAG_PARAM_CRC);
    assert(bms_diagnostics_faults() & BMS_DIAG_RSENSE_CONFIG);
    assert(bms_diagnostics_faults() & BMS_DIAG_CELL_MAP);
}

static void test_fet_monitor(void)
{
    bms_measurement_t m = valid_measurement(0u);
    m.current_ma = 5000;
    bms_actuator_init();
    bms_fet_monitor_init();
    bms_fet_monitor_update(&m, 0u, 1u, 0u);
    assert(bms_fet_monitor_get(BMS_FET_CHARGE)->state == FET_OFF_REQUESTED);       /* 35 */
    bms_fet_monitor_update(&m, 1u, 1u, 0u);
    bms_fet_monitor_update(&m, 501u, 1u, 0u);
    bms_fet_monitor_update(&m, 502u, 1u, 0u);
    bms_fet_monitor_update(&m, 503u, 1u, 0u);
    bms_fet_monitor_update(&m, 504u, 1u, 0u);
    assert(bms_fet_monitor_any_failed());                                         /* 36 */
    assert(bms_get_global_inhibit() & BMS_INHIBIT_FET_OFF_FAILED);                /* 37 */

    m = valid_measurement(1000u);
    bms_actuator_init(); clear_safe_inhibits();
    bms_actuator_request_charge(1u);
    bms_actuator_update(0u);
    bms_actuator_update(2001u);
    bms_fet_monitor_init();
    bms_fet_monitor_update(&m, 2001u, 1u, 0u);
    bms_set_charge_inhibit(BMS_INHIBIT_CELL_OV, 1u);
    bms_actuator_update(2002u);
    bms_fet_monitor_update(&m, 2002u, 0u, 0u);
    assert(bms_fet_monitor_get(BMS_FET_CHARGE)->state == FET_OFF_REQUESTED);
    bms_fet_monitor_update(&m, 2003u, 0u, 0u);
    bms_fet_monitor_update(&m, 2503u, 0u, 0u);
    bms_fet_monitor_update(&m, 2504u, 0u, 0u);
    assert(bms_fet_monitor_get(BMS_FET_CHARGE)->state == FET_OFF_CONFIRMED);
}

static void test_supervisor(void)
{
    bms_diagnostic_config_t d = valid_diag_config();
    bms_sw_protection_config_t cfg;
    bms_measurement_t m;
    unsigned i;

    bms_actuator_init();
    bms_measurement_init();
    bms_diagnostics_init(&d);
    bms_diagnostics_set_afe_config_valid(1u);
    bms_sw_protection_defaults(&cfg);
    bms_sw_protection_init(&cfg);
    persisted_valid = 0u;
    bms_fuse_init(0u);
    bms_supervisor_init(0u, 0u, 0u);
    assert(bms_supervisor_state() == BMS_START_IO_SAFE);                          /* 38 */
    bms_supervisor_begin_afe_startup();
    assert(bms_supervisor_state() == BMS_START_AFE_WAIT_READY);
    bms_supervisor_note_afe_ready(1u);
    assert(bms_supervisor_state() == BMS_START_AFE_CONFIG);                       /* 39 */
    bms_supervisor_note_afe_config(1u);
    assert(bms_supervisor_state() == BMS_START_AFE_VERIFY);                       /* 40 */
    bms_supervisor_note_afe_verify(1u);
    assert(bms_supervisor_state() == BMS_START_SAMPLE_VALIDATE);                  /* 41 */
    m = valid_measurement(500u);
    bms_measurement_publish(&m);
    (void)bms_diagnostics_update(&m, 500u);
    bms_supervisor_update(500u);
    bms_supervisor_update(501u);
    bms_supervisor_update(502u);
    assert(bms_supervisor_state() == BMS_START_SAMPLE_VALIDATE);
    for (i = 1u; i <= 3u; ++i) {
        m = valid_measurement(i * 1000u);
        m.cell_mv[0] = (uint16_t)(m.cell_mv[0] + i);
        m.pack_mv_afe += i;
        m.pack_mv_adc += i;
        m.current_raw = (uint16_t)(m.current_raw + i);
        bms_measurement_publish(&m);
        (void)bms_diagnostics_update(&m, i * 1000u);
        bms_sw_protection_update(&m, i * 1000u);
        bms_supervisor_update(i * 1000u);
    }
    assert(bms_supervisor_state() == BMS_START_READY);                            /* 42 */
    assert(bms_supervisor_safety_init_ok());                                      /* 43 */
    bms_supervisor_heartbeat(BMS_TASK_AFE_SAMPLE, 3000u);
    bms_supervisor_heartbeat(BMS_TASK_MCU_ADC, 3000u);
    bms_supervisor_heartbeat(BMS_TASK_PROTECTION, 3000u);
    bms_supervisor_heartbeat(BMS_TASK_DIAGNOSTICS, 3000u);
    bms_supervisor_heartbeat(BMS_TASK_ACTUATOR, 3000u);
    bms_supervisor_heartbeat(BMS_TASK_FET_MONITOR, 3000u);
    bms_supervisor_heartbeat(BMS_TASK_FUSE, 3000u);
    bms_supervisor_heartbeat(BMS_TASK_SAFETY_LOG, 3000u);
    assert(bms_supervisor_watchdog_allowed(3001u));                               /* 44 */
    assert(!bms_supervisor_watchdog_allowed(6000u));                              /* 45 */
    bms_supervisor_require_revalidation();
    assert(!bms_supervisor_safety_init_ok() &&
           bms_supervisor_state() == BMS_START_SAMPLE_VALIDATE);                  /* 46 */
    bms_supervisor_init(0u, 0u, 0u);
    bms_supervisor_note_afe_ready(0u);
    assert(bms_supervisor_state() == BMS_START_LOCKED);                           /* 47 */
    bms_supervisor_init(0u, 0u, 0u);
    bms_supervisor_note_afe_ready(1u);
    bms_supervisor_note_afe_config(0u);
    assert(bms_supervisor_state() == BMS_START_LOCKED);
    bms_supervisor_init(0u, 0u, 0u);
    bms_supervisor_note_afe_ready(1u);
    bms_supervisor_note_afe_config(1u);
    bms_supervisor_note_afe_verify(0u);
    assert(bms_supervisor_state() == BMS_START_LOCKED);
    bms_supervisor_init(0u, 0xFFFFu, 1u);
    assert(bms_get_global_inhibit() & BMS_INHIBIT_WDT_RESET);                     /* 48 */
}

static void test_fuse(void)
{
    bms_measurement_t m = valid_measurement(0u);
    bms_fuse_input_t in;
    memset(&in, 0, sizeof(in));
    persisted_valid = 0u; fuse_feedback = 0u;
    bms_fuse_init(0u);
    assert(bms_fuse_get_config()->version != 0u);
    assert(bms_fuse_get_config()->length == sizeof(bms_fuse_config_t));
#ifdef TEST_FUSE_AUTO
    assert(bms_fuse_get_state() == FUSE_MONITORING);                              /* 38 */
    in.evidence_mask = 1u; in.fet_off_failed = 1u; in.environment_valid = 1u;
    in.hardware_authorized = 1u; in.severe_since_ms = 1u;
    bms_fuse_update(&in, &m, 20000u);
    assert(bms_fuse_get_state() == FUSE_MONITORING);                              /* 39 */
    in.evidence_mask = 3u;
    bms_fuse_update(&in, &m, 20000u);
    assert(bms_fuse_get_state() == FUSE_ARMED);                                   /* 40 */
    assert(persisted_valid);                                                      /* 41 */
    bms_fuse_update(&in, &m, 20001u);
    assert(bms_fuse_get_state() == FUSE_TRIGGERING && fuse_drive_active);          /* 42 */
    bms_fuse_update(&in, &m, 20102u);
    assert(bms_fuse_get_state() == FUSE_FAILED && !fuse_drive_active);             /* 43 */
    bms_fuse_init(30000u);
    assert(bms_fuse_get_state() == FUSE_FAILED);                                  /* 44 */
    persisted_valid = 0u; fuse_feedback = 1u;
    bms_fuse_init(0u);
    bms_fuse_update(&in, &m, 20000u);
    bms_fuse_update(&in, &m, 20001u);
    bms_fuse_update(&in, &m, 20002u);
    assert(bms_fuse_get_state() == FUSE_FIRED);                                   /* 45 */
    bms_fuse_init(30000u);
    assert(bms_fuse_get_state() == FUSE_FIRED);                                   /* 46 */
#else
    assert(bms_fuse_get_state() == FUSE_DISABLED);                                /* 38 */
    in.evidence_mask = 3u; in.fet_off_failed = 1u; in.environment_valid = 1u;
    in.hardware_authorized = 1u; in.severe_since_ms = 1u;
    bms_fuse_update(&in, &m, 20000u);
    assert(bms_fuse_get_state() == FUSE_DISABLED);                                /* 39 */
    assert(!fuse_drive_active);                                                   /* 40 */
#endif
}

int main(void)
{
    test_measurement_and_actuator();
    test_protection();
    test_all_protection_inputs_and_actions();
    test_fault_isolation();
    test_diagnostics();
    test_fet_monitor();
    test_supervisor();
    test_fuse();
    printf("bms_safety_host_test: PASS (%s)\n",
#ifdef TEST_FUSE_AUTO
           "auto-fuse enabled test build"
#else
           "production defaults"
#endif
    );
    return 0;
}
