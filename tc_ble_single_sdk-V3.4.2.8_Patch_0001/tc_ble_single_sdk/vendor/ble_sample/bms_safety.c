#include "bms_safety.h"
#include "tl_common.h"
#include "drivers.h"
#include "app.h"
#include "conf.h"
#include "param.h"
#include "sci_upper.h"
#include "runtime.h"
#include "sh367309_datadeal.h"
#include "bms_measurement.h"
#include "bms_actuator.h"
#include "bms_sw_protection.h"
#include "bms_diagnostics.h"
#include "bms_fet_monitor.h"
#include "bms_fuse.h"
#include "bms_supervisor.h"
#include "bms_afe.h"
#include "bms_cold_kv_store.h"
#include "bms_safety_config.h"
#include "bms_safety_log.h"
#include <string.h>

extern struct stCell_Info g_stCellInfoReport;
extern uint32_t g_u32CS_Res_AFE;
extern uint8_t ota_is_working;

static uint8_t g_actuator_port_ready;
static bms_actuator_state_t g_last_hw_state;
static uint8_t g_last_hw_state_valid;
static uint32_t g_severe_since_ms;
static uint32_t g_last_diagnostics;
static bms_fet_monitor_state_t g_last_fet_state[BMS_FET_MONITOR_COUNT];
static bms_actuator_state_t g_last_logged_actuator;
static uint8_t g_last_logged_actuator_valid;
static bms_fuse_state_t g_last_fuse_state;
static uint32_t g_reset_streak;
static uint32_t g_ready_since_ms;
static uint8_t g_last_ota_active;
static uint32_t g_time_last_tick;
static uint32_t g_time_pending_tick;
static uint32_t g_time_ms;
static uint8_t g_time_ready;

uint32_t bms_safety_now_ms(void)
{
    uint32_t now_tick = clock_time();
    uint32_t elapsed_tick;
    uint32_t total_tick;
    if (!g_time_ready) {
        g_time_last_tick = now_tick;
        g_time_ready = 1u;
        return g_time_ms;
    }
    elapsed_tick = now_tick - g_time_last_tick;
    g_time_last_tick = now_tick;
    total_tick = g_time_pending_tick + elapsed_tick;
    g_time_ms += total_tick / CLOCK_SYS_CLOCK_1MS;
    g_time_pending_tick = total_tick % CLOCK_SYS_CLOCK_1MS;
    return g_time_ms;
}

static int16_t offset_temp_to_dC(uint16_t value)
{
    return (value >= 400u) ? (int16_t)(value - 400u) : (int16_t)(-(int16_t)(400u - value));
}

static uint8_t bms_cell_map_is_valid(void)
{
    uint16_t used = 0u;
    uint8_t i;
    if (SeriesNum == 0u || SeriesNum > BMS_CELL_MAX) return 0u;
    for (i = 0u; i < SeriesNum; ++i) {
        uint8_t channel = SeriesSelect_AFE1[SeriesNum - 1u][i];
        uint16_t bit;
        if (channel >= BMS_CELL_MAX) return 0u;
        bit = (uint16_t)(1u << channel);
        if (used & bit) return 0u;
        used |= bit;
    }
    return 1u;
}

static void build_protection_config(bms_sw_protection_config_t *cfg)
{
    const struct PRT_E2ROM_PARAS *p = &g_tParam.protect;
    bms_sw_protection_defaults(cfg);
    cfg->item[BMS_PROT_CELL_OV].enter_value = p->u16VcellOvp_First;
    cfg->item[BMS_PROT_CELL_OV].recover_value = p->u16VcellOvp_Rcv;
    cfg->item[BMS_PROT_CELL_OV].enter_delay_ms = p->u16VcellOvp_Filter;
    cfg->item[BMS_PROT_CELL_UV].enter_value = p->u16VcellUvp_First;
    cfg->item[BMS_PROT_CELL_UV].recover_value = p->u16VcellUvp_Rcv;
    cfg->item[BMS_PROT_CELL_UV].enter_delay_ms = p->u16VcellUvp_Filter;
    cfg->item[BMS_PROT_PACK_OV].enter_value = (int32_t)p->u16VbusOvp_Third * 10;
    cfg->item[BMS_PROT_PACK_OV].recover_value = (int32_t)p->u16VbusOvp_Rcv * 10;
    cfg->item[BMS_PROT_PACK_OV].enter_delay_ms = p->u16VbusOvp_Filter;
    cfg->item[BMS_PROT_PACK_UV].enter_value = (int32_t)p->u16VbusUvp_Third * 10;
    cfg->item[BMS_PROT_PACK_UV].recover_value = (int32_t)p->u16VbusUvp_Rcv * 10;
    cfg->item[BMS_PROT_PACK_UV].enter_delay_ms = p->u16VbusUvp_Filter;
    cfg->item[BMS_PROT_CHG_OC].enter_value = (int32_t)p->u16IchgOcp_Second * 100;
    cfg->item[BMS_PROT_CHG_OC].recover_value = (int32_t)p->u16IchgOcp_Rcv * 100;
    cfg->item[BMS_PROT_CHG_OC].enter_delay_ms = p->u16IchgOcp_Filter;
    cfg->item[BMS_PROT_DSG_OC1].enter_value = (int32_t)p->u16IdsgOcp_Second * 100;
    cfg->item[BMS_PROT_DSG_OC1].recover_value = (int32_t)p->u16IdsgOcp_Rcv * 100;
    cfg->item[BMS_PROT_DSG_OC1].enter_delay_ms = p->u16IdsgOcp_Filter;
    cfg->item[BMS_PROT_DSG_OC2].enter_value = (int32_t)p->u16IdsgOcp_Third * 100;
    cfg->item[BMS_PROT_DSG_OC2].recover_value = (int32_t)p->u16IdsgOcp_Rcv * 100;
    cfg->item[BMS_PROT_DSG_OC2].enter_delay_ms = p->u16IdsgOcp_Filter;
    cfg->item[BMS_PROT_SHORT].enter_value = (int32_t)AFE_ODC2 * 1000;
    cfg->item[BMS_PROT_SHORT].recover_value = (int32_t)p->u16IdsgOcp_Rcv * 100;

#define SET_TEMP(ID, FIELD, RCV, FILTER) do { \
    cfg->item[ID].enter_value = (int32_t)p->FIELD - 400; \
    cfg->item[ID].recover_value = (int32_t)p->RCV - 400; \
    cfg->item[ID].enter_delay_ms = p->FILTER; \
} while (0)
    SET_TEMP(BMS_PROT_CHG_OT, u16TChgOTp_Third, u16TChgOTp_Rcv, u16TChgOTp_Filter);
    SET_TEMP(BMS_PROT_CHG_UT, u16TchgUTp_Third, u16TchgUTp_Rcv, u16TchgUTp_Filter);
    SET_TEMP(BMS_PROT_DSG_OT, u16TdischgOTp_Third, u16TdischgOTp_Rcv, u16TdischgOTp_Filter);
    SET_TEMP(BMS_PROT_DSG_UT, u16TdischgUTp_Third, u16TdischgUTp_Rcv, u16TdischgUTp_Filter);
    SET_TEMP(BMS_PROT_MOS_OT, u16TmosOTp_Third, u16TmosOTp_Rcv, u16TmosOTp_Filter);
#undef SET_TEMP
    bms_sw_protection_config_finalize(cfg);
}

void bms_safety_init(uint32_t now_ms)
{
    bms_sw_protection_config_t protection_cfg;
    bms_diagnostic_config_t diag;
    uint8_t protection_cfg_valid;

    g_actuator_port_ready = 0u;
    g_last_hw_state_valid = 0u;
    g_severe_since_ms = 0u;
    g_last_diagnostics = 0u;
    memset(g_last_fet_state, 0, sizeof(g_last_fet_state));
    memset(&g_last_logged_actuator, 0, sizeof(g_last_logged_actuator));
    g_last_logged_actuator_valid = 0u;
    g_ready_since_ms = 0u;
    g_last_ota_active = ota_is_working ? 1u : 0u;
    bms_measurement_init();
    bms_actuator_init();
    bms_afe_init();
    build_protection_config(&protection_cfg);
    protection_cfg_valid = bms_sw_protection_config_validate(&protection_cfg);
    bms_sw_protection_init(&protection_cfg);
    memset(&diag, 0, sizeof(diag));
    diag.cell_min_mv = 1000u;
    diag.cell_max_mv = 5000u;
    diag.cell_jump_mv = 500u;
    diag.pack_cross_max_mv = 2000u;
    diag.cell_sum_max_error_mv = 2000u;
    diag.current_jump_ma = 200000u;
    diag.current_zero_drift_ma = 2000u;
    diag.temp_jump_dC = 300u;
    diag.ntc_open_mv = 3250u;
    diag.ntc_short_mv = 50u;
    diag.frozen_frames = 60u;
    diag.expected_series = SeriesNum;
    diag.board_valid = (FD_BMS_TYPE <= test_default && SeriesNum >= 5 && SeriesNum <= 16) ? 1u : 0u;
    diag.cell_map_valid = bms_cell_map_is_valid();
    diag.params_valid = (Param_IsTrusted() && protection_cfg_valid) ? 1u : 0u;
    diag.rsense_valid = ((CS_Res == 2) && (CS_Res_Num >= 1) && (CS_Res_Num <= 4) &&
                         (g_u32CS_Res_AFE == ((uint32_t)CS_Res_Num * 1000u / CS_Res))) ? 1u : 0u;
    bms_diagnostics_config_finalize(&diag);
    bms_diagnostics_init(&diag);
    bms_fet_monitor_init();
    {
        u32 boot_marker = 0u;
        g_reset_streak = 0u;
        (void)bms_cold_kv_store_get_control_value(BMS_COLD_CTRL_BOOT_IN_PROGRESS,
                                                   &boot_marker);
        (void)bms_cold_kv_store_get_control_value(BMS_COLD_CTRL_RESET_STREAK,
                                                   &g_reset_streak);
        if (boot_marker) {
            if (g_reset_streak < 0xFFFFFFFFu) ++g_reset_streak;
        } else {
            g_reset_streak = 0u;
        }
        (void)bms_cold_kv_store_set_control_value(BMS_COLD_CTRL_RESET_STREAK,
                                                   g_reset_streak);
        (void)bms_cold_kv_store_set_control_value(BMS_COLD_CTRL_BOOT_IN_PROGRESS, 1u);
    }
    bms_supervisor_init(now_ms, 0u, 1u); /* B85 code exposes no trustworthy reset-cause API. */
    bms_fuse_set_context((uint16_t)bms_afe_last_error(),
                         bms_supervisor_reset_reason(), g_tParam.ParamVer);
    bms_fuse_init(now_ms);
    g_last_fuse_state = bms_fuse_get_state();
    bms_safety_log_init();
    bms_safety_log_enqueue(BMS_SLOG_RESET_SUSPECT, 1u, 0, now_ms);
    if (!diag.params_valid)
        bms_safety_log_enqueue(BMS_SLOG_PARAM_CRC, 1u, 0, now_ms);
    if (!diag.board_valid || !diag.cell_map_valid || !diag.rsense_valid)
        bms_safety_log_enqueue(BMS_SLOG_BOARD_CONFIG, 1u, 0, now_ms);
    if (g_reset_streak >= 3u)
        bms_safety_log_enqueue(BMS_SLOG_RESET_SUSPECT,
                               (uint16_t)g_reset_streak, 0, now_ms);
}

uint8_t bms_safety_start_afe(void)
{
    bms_afe_error_t error;
    g_actuator_port_ready = 1u;
    bms_supervisor_begin_afe_startup();
    error = bms_afe_startup();
    bms_supervisor_note_afe_ready(error != BMS_AFE_ERR_READY && error != BMS_AFE_ERR_RESET);
    bms_supervisor_note_afe_config(error == BMS_AFE_OK);
    bms_supervisor_note_afe_verify(error == BMS_AFE_OK);
    if (error != BMS_AFE_OK) {
        uint16_t event = (error == BMS_AFE_ERR_RESET) ? BMS_SLOG_AFE_RESET_FAIL :
                         ((error == BMS_AFE_ERR_READY) ? BMS_SLOG_AFE_READY_FAIL :
                          BMS_SLOG_AFE_CONFIG_FAIL);
        bms_safety_log_enqueue(event, (uint16_t)error, bms_measurement_get(),
                               bms_safety_now_ms());
    }
    return error == BMS_AFE_OK;
}

void bms_safety_sample(uint8_t afe_valid, uint16_t battery_ntc_mv,
                       uint16_t mos_ntc_mv, uint32_t pack_adc_mv,
                       uint32_t now_ms)
{
    bms_measurement_t m;
    uint8_t i;

    memset(&m, 0, sizeof(m));
    m.series_count = SeriesNum;
    m.timestamp_ms = now_ms;
    m.afe_timestamp_ms = now_ms;
    m.adc_timestamp_ms = now_ms;
    m.afe_frame_valid = afe_valid ? 1u : 0u;
    m.pack_adc_valid = (pack_adc_mv >= 5000u && pack_adc_mv <= 80000u) ? 1u : 0u;
    m.current_valid = afe_valid ? 1u : 0u;
    m.battery_temp_valid = (battery_ntc_mv > 50u && battery_ntc_mv < 3250u) ? 1u : 0u;
    m.mos_temp_valid = (mos_ntc_mv > 50u && mos_ntc_mv < 3250u) ? 1u : 0u;
    m.pack_mv_adc = pack_adc_mv;
    m.battery_ntc_mv = battery_ntc_mv;
    m.mos_ntc_mv = mos_ntc_mv;
    m.battery_temp_dC = offset_temp_to_dC(g_stCellInfoReport.u16Temperature[8]);
    m.mos_temp_dC = offset_temp_to_dC(g_stCellInfoReport.u16Temperature[9]);
    m.current_ma = g_stCellInfoReport.u16Ichg ?
                   ((int32_t)g_stCellInfoReport.u16Ichg * 100) :
                   -((int32_t)g_stCellInfoReport.u16IDischg * 100);
    m.current_raw = ram_reg_309.Cadc;
    m.charger_present = IsChargerWakeupActive() ? 1u : 0u;
    m.load_requested = IsKeyWakeupActive() ? 1u : 0u;
    if (afe_valid) {
        for (i = 0u; i < SeriesNum; ++i) {
            m.cell_mv[i] = g_stCellInfoReport.u16VCell[i];
            if (m.cell_mv[i] >= 1000u && m.cell_mv[i] <= 5000u)
                m.cell_valid_mask |= (1UL << i);
        }
    }
    /*
     * Keep the AFE/report total separate from the locally recomputed cell sum
     * so diagnostics can detect corruption in that conversion/reporting path.
     * u16VCellTotle is stored in 10 mV units by DataLoad_CellVoltMaxMinFind.
     */
    m.pack_mv_afe = (uint32_t)g_stCellInfoReport.u16VCellTotle * 10u;
    bms_measurement_publish(&m);
    bms_afe_note_frame(afe_valid, now_ms);
    bms_supervisor_heartbeat(BMS_TASK_AFE_SAMPLE, now_ms);
    bms_supervisor_heartbeat(BMS_TASK_MCU_ADC, now_ms);
}

static uint32_t fuse_evidence(const bms_measurement_t *m)
{
    uint32_t active = bms_sw_protection_active_mask();
    uint32_t evidence = 0u;
    if (active & (1UL << BMS_PROT_CELL_OV)) evidence |= 1UL << 0;
    if (active & (1UL << BMS_PROT_PACK_OV)) evidence |= 1UL << 1;
    if (active & ((1UL << BMS_PROT_CHG_OT) | (1UL << BMS_PROT_DSG_OT) |
                  (1UL << BMS_PROT_MOS_OT))) evidence |= 1UL << 2;
    if (active & ((1UL << BMS_PROT_DSG_OC2) | (1UL << BMS_PROT_SHORT) |
                  (1UL << BMS_PROT_CHG_OC))) evidence |= 1UL << 3;
    if (!m->afe_frame_valid && m->pack_adc_valid) evidence |= 1UL << 4;
    return evidence;
}

void bms_safety_poll(uint32_t now_ms)
{
    const bms_measurement_t *m = bms_measurement_get();
    bms_fuse_input_t fuse_in;
    uint32_t evidence;
    uint32_t diagnostics;
    bms_fuse_state_t fuse_state;

    if (ota_is_working) {
        bms_set_global_inhibit(BMS_INHIBIT_OTA, 1u);
    } else {
        if (g_last_ota_active) {
            bms_measurement_invalidate_all(now_ms);
            bms_set_global_inhibit(BMS_INHIBIT_SAMPLE_INVALID, 1u);
            bms_supervisor_require_revalidation();
        }
        bms_set_global_inhibit(BMS_INHIBIT_OTA, 0u);
    }
    g_last_ota_active = ota_is_working ? 1u : 0u;

    bms_afe_poll(now_ms);
    if (bms_afe_take_revalidation_request()) {
        bms_measurement_invalidate_all(now_ms);
        bms_set_global_inhibit(BMS_INHIBIT_SAMPLE_INVALID, 1u);
        bms_supervisor_require_revalidation();
        bms_safety_log_enqueue(BMS_SLOG_AFE_CONFIG_DRIFT, 1u, m, now_ms);
        m = bms_measurement_get();
    }
    diagnostics = bms_diagnostics_update(m, now_ms);
    if ((diagnostics & ~g_last_diagnostics) != 0u) {
        bms_safety_log_enqueue(BMS_SLOG_SAMPLE_INVALID,
                               (uint16_t)(diagnostics & 0xFFFFu), m, now_ms);
        if ((diagnostics & ~g_last_diagnostics) & BMS_DIAG_AFE_COMM)
            bms_safety_log_enqueue(BMS_SLOG_AFE_COMM_FAIL, 1u, m, now_ms);
        if ((diagnostics & ~g_last_diagnostics) & BMS_DIAG_AFE_CONFIG)
            bms_safety_log_enqueue(BMS_SLOG_AFE_CONFIG_DRIFT, 1u, m, now_ms);
        if ((diagnostics & ~g_last_diagnostics) & BMS_DIAG_PARAM_CRC)
            bms_safety_log_enqueue(BMS_SLOG_PARAM_CRC, 1u, m, now_ms);
        if ((diagnostics & ~g_last_diagnostics) &
            (BMS_DIAG_BOARD_SERIES | BMS_DIAG_CELL_MAP |
             BMS_DIAG_RSENSE_CONFIG))
            bms_safety_log_enqueue(BMS_SLOG_BOARD_CONFIG, 1u, m, now_ms);
    }
    g_last_diagnostics = diagnostics;
    bms_supervisor_heartbeat(BMS_TASK_DIAGNOSTICS, now_ms);
    if (bms_measurement_is_fresh_and_complete(now_ms)) {
        bms_sw_protection_update(m, now_ms);
    }
    bms_supervisor_heartbeat(BMS_TASK_PROTECTION, now_ms);
    bms_supervisor_update(now_ms);
    if (bms_supervisor_safety_init_ok()) {
        if (g_ready_since_ms == 0u) g_ready_since_ms = now_ms;
        if ((uint32_t)(now_ms - g_ready_since_ms) >= 60000u) {
            (void)bms_cold_kv_store_set_control_value(BMS_COLD_CTRL_BOOT_IN_PROGRESS, 0u);
            (void)bms_cold_kv_store_set_control_value(BMS_COLD_CTRL_RESET_STREAK, 0u);
        }
    } else {
        g_ready_since_ms = 0u;
    }
    bms_actuator_update(now_ms);
    {
        const bms_actuator_state_t *actuator = bms_actuator_get_state();
        if (g_last_logged_actuator_valid &&
            actuator->ctlc_on != g_last_logged_actuator.ctlc_on) {
            bms_safety_log_enqueue(BMS_SLOG_CTLC_STATE, actuator->ctlc_on,
                                   m, now_ms);
        }
        g_last_logged_actuator = *actuator;
        g_last_logged_actuator_valid = 1u;
    }
    bms_supervisor_heartbeat(BMS_TASK_ACTUATOR, now_ms);
    bms_fet_monitor_update(m, now_ms, bms_afe_charge_fet_on(), bms_afe_discharge_fet_on());
    {
        uint8_t path;
        for (path = 0u; path < BMS_FET_MONITOR_COUNT; ++path) {
            bms_fet_monitor_state_t state =
                bms_fet_monitor_get((bms_fet_path_t)path)->state;
            if (state != g_last_fet_state[path]) {
                if (state == FET_OFF_REQUESTED)
                    bms_safety_log_enqueue(BMS_SLOG_FET_OFF_REQUEST, path, m, now_ms);
                else if (state == FET_OFF_CONFIRMED)
                    bms_safety_log_enqueue(BMS_SLOG_FET_OFF_OK, path, m, now_ms);
                else if (state == FET_OFF_FAILED)
                    bms_safety_log_enqueue(BMS_SLOG_FET_OFF_FAILED, path, m, now_ms);
                g_last_fet_state[path] = state;
            }
        }
    }
    bms_supervisor_heartbeat(BMS_TASK_FET_MONITOR, now_ms);

    evidence = fuse_evidence(m);
    if (evidence && bms_fet_monitor_any_failed()) {
        if (g_severe_since_ms == 0u) g_severe_since_ms = now_ms;
    } else g_severe_since_ms = 0u;
    memset(&fuse_in, 0, sizeof(fuse_in));
    fuse_in.evidence_mask = evidence;
    fuse_in.severe_since_ms = g_severe_since_ms;
    fuse_in.fet_off_failed = bms_fet_monitor_any_failed();
    fuse_in.environment_valid = bms_measurement_is_fresh_and_complete(now_ms) &&
                                Param_IsTrusted() && bms_afe_config_valid();
    fuse_in.factory_or_debug = (Runtime_GetMode() == MODE_FACTORY || ota_is_working) ? 1u : 0u;
    fuse_in.hardware_authorized = bms_fuse_hw_authorized();
    bms_fuse_set_context((uint16_t)bms_afe_last_error(),
                         bms_supervisor_reset_reason(), g_tParam.ParamVer);
    bms_fuse_update(&fuse_in, m, now_ms);
    fuse_state = bms_fuse_get_state();
    if (fuse_state != g_last_fuse_state) {
        uint16_t event = (fuse_state == FUSE_ARMED) ? BMS_SLOG_FUSE_ARMED :
                         ((fuse_state == FUSE_TRIGGERING) ? BMS_SLOG_FUSE_TRIGGER :
                         ((fuse_state == FUSE_FIRED) ? BMS_SLOG_FUSE_FIRED :
                          BMS_SLOG_FUSE_FAILED));
        bms_safety_log_enqueue(event, (uint16_t)fuse_state, m, now_ms);
        g_last_fuse_state = fuse_state;
    }
    bms_supervisor_heartbeat(BMS_TASK_FUSE, now_ms);
    bms_actuator_update(now_ms);
    bms_safety_log_poll();
    bms_supervisor_heartbeat(BMS_TASK_SAFETY_LOG, now_ms);
}

void bms_safety_prepare_sleep(uint32_t now_ms)
{
    bms_set_global_inhibit(BMS_INHIBIT_SAMPLE_INVALID, 1u);
    bms_actuator_update(now_ms);
    bms_measurement_invalidate_all(now_ms);
}

void bms_safety_note_wakeup(uint32_t now_ms)
{
    bms_measurement_note_wakeup(now_ms);
    bms_set_global_inhibit(BMS_INHIBIT_SAMPLE_INVALID, 1u);
    /*
     * AFE_Sleep sets the cached SLEEP bit.  Clear it and force a safe OFF
     * command to the AFE before any post-wakeup sample can be accepted.
     */
    SH367309_Reg_Store.REG_MTP_CONF.bits.SLEEP = 0u;
    g_last_hw_state_valid = 0u;
    bms_actuator_update(now_ms);
    bms_supervisor_require_revalidation();
}

uint8_t bms_safety_watchdog_allowed(uint32_t now_ms)
{
    return bms_supervisor_watchdog_allowed(now_ms);
}

void bms_safety_on_param_update(uint32_t now_ms)
{
    bms_sw_protection_config_t cfg;
    build_protection_config(&cfg);
    if (!bms_sw_protection_config_validate(&cfg)) {
        bms_set_global_inhibit(BMS_INHIBIT_PARAM_INVALID, 1u);
        bms_safety_log_enqueue(BMS_SLOG_PARAM_CRC, 1u, bms_measurement_get(), now_ms);
        return;
    }
    bms_sw_protection_init(&cfg);
    bms_afe_require_reconfigure();
    bms_set_global_inhibit(BMS_INHIBIT_AFE_CONFIG, 1u);
    bms_set_global_inhibit(BMS_INHIBIT_SAMPLE_INVALID, 1u);
    bms_measurement_invalidate_all(now_ms);
    bms_supervisor_require_revalidation();
    bms_actuator_update(now_ms);
}

uint8_t bms_safety_primary_fault_legacy(void)
{
    uint32_t active = bms_sw_protection_active_mask();
    if (bms_fuse_get_state() == FUSE_FIRED || bms_fuse_get_state() == FUSE_FAILED)
        return 0x0Fu;
    if (bms_fet_monitor_any_failed()) return 0x0Eu;
    if (active & (1UL << BMS_PROT_SHORT)) return 0x0Du;
    if (active & (1UL << BMS_PROT_DSG_OC2)) return 0x01u;
    if (active & (1UL << BMS_PROT_DSG_OC1)) return 0x02u;
    if (active & (1UL << BMS_PROT_CELL_OV)) return 0x07u;
    if (active & (1UL << BMS_PROT_PACK_OV)) return 0x07u;
    if (active & (1UL << BMS_PROT_CELL_UV)) return 0x06u;
    if (active & (1UL << BMS_PROT_PACK_UV)) return 0x06u;
    if (active & (1UL << BMS_PROT_CHG_OC)) return 0x08u;
    if (active & (1UL << BMS_PROT_MOS_OT)) return 0x0Cu;
    if (active & (1UL << BMS_PROT_CHG_OT)) return 0x04u;
    if (active & (1UL << BMS_PROT_DSG_OT)) return 0x05u;
    if (active & (1UL << BMS_PROT_CHG_UT)) return 0x03u;
    if (active & (1UL << BMS_PROT_DSG_UT)) return 0x09u;
    if (bms_get_global_inhibit() & (BMS_INHIBIT_AFE_CONFIG | BMS_INHIBIT_AFE_COMM))
        return 0x0Bu;
    if (bms_get_global_inhibit() & (BMS_INHIBIT_SAMPLE_INVALID |
                                    BMS_INHIBIT_PARAM_INVALID |
                                    BMS_INHIBIT_DIAGNOSTIC))
        return 0x0Au;
    return 0u;
}

uint8_t bms_actuator_hw_apply(const bms_actuator_state_t *state)
{
    u8 ok = 1u;
    if (state == 0) return 0u;
    if (g_last_hw_state_valid && memcmp(state, &g_last_hw_state, sizeof(*state)) == 0)
        return 1u;

    /* Closing is always applied before any AFE transaction. */
    if (!state->ctlc_on) gpio_write(AFE_CTL_PIN, 0);
    gpio_write(MCC_C_PIN, 0); /* Hardware purpose is unverified: fixed safe level. */
    if (g_actuator_port_ready) {
        SH367309_Reg_Store.REG_MTP_CONF.bits.IDLE = 0u;
        SH367309_Reg_Store.REG_MTP_CONF.bits.SLEEP = 0u;
        SH367309_Reg_Store.REG_MTP_CONF.bits.CADCON = 1u;
        SH367309_Reg_Store.REG_MTP_CONF.bits.CHGMOS = state->charge_on ? 1u : 0u;
        SH367309_Reg_Store.REG_MTP_CONF.bits.DSGMOS = state->discharge_on ? 1u : 0u;
        SH367309_Reg_Store.REG_MTP_CONF.bits.PCHMOS = state->precharge_on ? 1u : 0u;
        ok = MTPWrite(MTP_CONF, 1u, &SH367309_Reg_Store.REG_MTP_CONF.all);
    }
    if (ok && state->ctlc_on) gpio_write(AFE_CTL_PIN, 1);
    if (!ok) gpio_write(AFE_CTL_PIN, 0);
    g_last_hw_state = *state;
    g_last_hw_state_valid = ok ? 1u : 0u;
    return ok;
}

uint8_t bms_fet_hw_feedback_supported(bms_fet_path_t path) { (void)path; return 0u; }
uint8_t bms_fet_hw_is_off(bms_fet_path_t path) { (void)path; return 0u; }
uint8_t bms_fuse_hw_is_supported(void) { return BMS_FUSE_HW_FEEDBACK_ENABLE ? 1u : 0u; }
uint8_t bms_fuse_hw_authorized(void) { return 0u; }
uint8_t bms_fuse_hw_fired_feedback(void) { return 0u; }
uint8_t bms_fuse_hw_driver_ok(void) { return 0u; }
void bms_fuse_hw_drive(uint8_t active)
{
    (void)active;
    /* Never energize an unverified fuse driver. */
    gpio_write(RF_EN_PIN, 0);
}

uint8_t bms_fuse_persist_load(bms_fuse_record_t *record)
{
    u32 words[BMS_COLD_FUSE_WORD_COUNT];
    if (record == 0 || sizeof(*record) > sizeof(words)) return 0u;
    if (!bms_cold_kv_store_get_fuse_words(words, BMS_COLD_FUSE_WORD_COUNT)) return 0u;
    memcpy(record, words, sizeof(*record));
    return 1u;
}

uint8_t bms_fuse_persist_save(const bms_fuse_record_t *record)
{
    u32 words[BMS_COLD_FUSE_WORD_COUNT];
    if (record == 0 || sizeof(*record) > sizeof(words)) return 0u;
    memset(words, 0, sizeof(words));
    memcpy(words, record, sizeof(*record));
    return bms_cold_kv_store_set_fuse_words(words, BMS_COLD_FUSE_WORD_COUNT);
}

void bms_safety_log_transition(uint16_t fault_code, uint8_t active,
                               const bms_measurement_t *measurement)
{
    bms_safety_log_enqueue(fault_code, active, measurement,
                           measurement ? measurement->timestamp_ms : bms_safety_now_ms());
    switch (fault_code) {
    /*
     * Legacy bitfields remain owned by Fault_ChangeToMCU (AFE hardware state).
     * Software faults are exported through the safety masks/primary code; only
     * append the compatible historical event here so one software recovery
     * cannot erase a still-active AFE or sibling software fault.
     */
    case 0x1101: if (active) FaultWarnRecord2(CellOvp_Third); break;
    case 0x1102: if (active) FaultWarnRecord2(CellUvp_Third); break;
    case 0x1103: if (active) FaultWarnRecord2(BatOvp_Third); break;
    case 0x1104: if (active) FaultWarnRecord2(BatUvp_Third); break;
    case 0x1201: if (active) FaultWarnRecord2(IchgOcp_Third); break;
    case 0x1202:
    case 0x1203: if (active) FaultWarnRecord2(IdischgOcp_Third); break;
    case 0x1301: if (active) FaultWarnRecord2(CellChgOTp_Third); break;
    case 0x1302: if (active) FaultWarnRecord2(CellChgUTp_Third); break;
    case 0x1303: if (active) FaultWarnRecord2(CellDsgOTp_Third); break;
    case 0x1304: if (active) FaultWarnRecord2(CellDsgUTp_Third); break;
    case 0x1305: if (active) FaultWarnRecord2(MosOTp_Third); break;
    default: break;
    }
}
