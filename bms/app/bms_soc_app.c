/* 应用负责 AFE 输入资格、线上报表及低电量告警，SOC 算法只处理数值。 */
#include "bms_soc_app.h"
#include "bms_config_store.h"
#include "bms_error.h"
#include "bms_product.h"
#include "bms_state.h"
#include "bms_state_store.h"
#include "bms_time.h"
#include <string.h>

#define SOC_REPORT_UNITS_PER_0P01AH 360u
#define SOC_BATTERY_TEMP_MIN_X10 450u
#define SOC_BATTERY_TEMP_MAX_X10 850u

static bms_soc_t s_soc;
static uint16_t s_low_count[3];

static bms_soc_config_t load_config(void)
{
    bms_soc_config_t config;
    bms_config_system_params_t system;

    config.chemistry = BMS_PRODUCT_CHEMISTRY;
    config.cell_count = BMS_PRODUCT_CELL_COUNT;
    config.capacity_0p1ah = BMS_PRODUCT_DEFAULT_CAPACITY_0P1AH;
    if (bms_config_store_get_system(&system) && system.capacity_factory != 0u &&
        system.capacity_factory <= BMS_SOC_CAPACITY_MAX_0P1AH)
        config.capacity_0p1ah = (uint16_t)system.capacity_factory;
    if (!bms_config_get_soc_limits(&config.limits)) bms_soc_defaults(config.chemistry, &config.limits);
    return config;
}

/* 循环 SOH 是既有估算报文，额定容量仍是计量分母。 */
static uint8_t soh_from_cycle(uint16_t cycle)
{
    if (cycle <= 80u) return 100u;
    if (cycle >= 800u) return 80u;
    if (cycle <= 500u) return (uint8_t)(100u - (uint32_t)(cycle - 80u) * 10u / 420u);
    return (uint8_t)(90u - (uint32_t)(cycle - 500u) * 10u / 300u);
}

static void publish_report(void)
{
    const bms_soc_checkpoint_t *cp = &s_soc.state;
    bms_soc_report_t *report = &g_bms_report.soc;

    report->soc_percent = cp->shown;
    report->cycle_count = cp->cycles;
    report->soh_percent = soh_from_cycle(cp->cycles);
    report->remaining_capacity_0p01ah = (uint16_t)(cp->charge_as10 / SOC_REPORT_UNITS_PER_0P01AH);
    report->nominal_capacity_0p01ah = cp->capacity_0p1ah * 10u;
    report->effective_capacity_0p01ah = report->nominal_capacity_0p01ah;
}

static void service_low_soc(uint8_t valid)
{
    bms_fault_reg_t *const levels[] = {&g_bms_report.fault_first, &g_bms_report.fault_second,
                                      &g_bms_report.fault_third};
    const bms_fault_code_t codes[] = {BMS_FAULT_SOC_LOW_FIRST, BMS_FAULT_SOC_LOW_SECOND,
                                      BMS_FAULT_SOC_LOW_THIRD};
    const uint16_t trip[] = {g_bms_protection_params.soc_low_first_percent,
                             g_bms_protection_params.soc_low_second_percent,
                             g_bms_protection_params.soc_low_third_percent};
    uint16_t recover = g_bms_protection_params.soc_low_recover_percent;
    uint16_t samples = (uint16_t)(((uint32_t)g_bms_protection_params.soc_low_filter_10ms * 10u +
                                  BMS_SAMPLE_PERIOD_MS - 1u) / BMS_SAMPLE_PERIOD_MS);
    uint8_t percent = bms_soc_percent(&s_soc);
    unsigned i;

    for (i = 0u; i < 3u; ++i) {
        uint8_t active = levels[i]->bits.soc_low;
        uint16_t release = recover > trip[i] ? recover : (trip[i] < 100u ? trip[i] + 1u : 100u);
        if (!trip[i] || trip[i] > 100u) levels[i]->bits.soc_low = 0u;
        else if (valid && (active ? percent >= release : percent <= trip[i])) {
            if (++s_low_count[i] < samples) continue;
            levels[i]->bits.soc_low = !active;
            if (!active) bms_fault_history_record(codes[i]);
        }
        s_low_count[i] = 0u;
    }
}

void bms_soc_app_init(void)
{
    bms_soc_checkpoint_t cp = bms_state_store_get();
    bms_soc_config_t config = load_config();

    bms_soc_init(&s_soc, &config, &cp, bms_time_ms());
    memset(s_low_count, 0, sizeof(s_low_count));
    publish_report();
}

void bms_soc_app_reload(void)
{
    bms_soc_config_t config = load_config();

    if (config.capacity_0p1ah != s_soc.config.capacity_0p1ah ||
        config.limits.full_mv != s_soc.config.limits.full_mv ||
        config.limits.empty_mv != s_soc.config.limits.empty_mv)
        bms_soc_reconfigure(&s_soc, &config);
    publish_report();
}

bms_soc_checkpoint_t bms_soc_app_checkpoint(void)
{
    bms_soc_checkpoint_t cp = s_soc.state;
    if (s_soc.initial_estimate_pending) cp.capacity_0p1ah = 0u;
    return cp;
}

uint8_t bms_soc_app_percent(void)
{
    return bms_soc_percent(&s_soc);
}

void bms_soc_app_sample(const bms_afe_aux_measurements_t *m)
{
    bms_soc_sample_t sample = {0};
    bms_afe_feature_snapshot_t features;
    const bms_fault_bits_t *fault = &g_bms_report.fault_third.bits;
    uint8_t qualified = 0u;

    sample.now_ms = bms_time_ms();
    sample.charge_cutoff = fault->cell_ovp || fault->pack_ovp;
    sample.discharge_cutoff = fault->cell_uvp || fault->pack_uvp;
    if (m) {
        sample.now_ms = m->sample_time_ms;
        sample.current_time_ms = m->current_time_ms;
        sample.current_ma = m->current_ma;
        sample.current_valid = m->current_qualified;
        sample.voltage_fresh = m->voltage_fresh;
        sample.line_check_active = m->voltage_check_active;
        sample.line_check_passed = m->voltage_calibration_valid;
        if (bms_afe_get_feature_snapshot(&features) && features.battery_temp_valid &&
            !fault->charge_ocp && !fault->discharge_ocp && !fault->charge_otp &&
            !fault->charge_utp && !fault->discharge_otp && !fault->discharge_utp &&
            !fault->mos_otp && !bms_error_get(BMS_ERROR_TEMP_BREAK) &&
            !bms_error_get(BMS_ERROR_CBC_DSG)) {
            uint32_t sum = 0u;
            unsigned i;

            for (i = 0u; i < BMS_PRODUCT_CELL_COUNT; ++i) {
                uint16_t mv = g_bms_report.cell_voltage_mv[i];
                if (mv < 2000u || mv > 4500u) break;
                sum += mv;
            }
            qualified = i == BMS_PRODUCT_CELL_COUNT;
            sample.min_mv = g_bms_report.cell_min_mv;
            sample.max_mv = g_bms_report.cell_max_mv;
            sample.mean_mv = (uint16_t)(sum / BMS_PRODUCT_CELL_COUNT);
            sample.temperature_x10 = (uint16_t)((features.battery_temp_min_x10 +
                                                  features.battery_temp_max_x10) / 2u);
            sample.voltage_valid = qualified;
            sample.resting_allowed = qualified && !fault->cell_delta_high &&
                !sample.charge_cutoff && !sample.discharge_cutoff &&
                !g_bms_system_status.bits.heater_status &&
                !g_bms_report.balance_bits_low && !g_bms_report.balance_bits_high &&
                features.battery_temp_min_x10 >= SOC_BATTERY_TEMP_MIN_X10 &&
                features.battery_temp_max_x10 <= SOC_BATTERY_TEMP_MAX_X10;
        }
    }
    if (bms_soc_step(&s_soc, &sample)) bms_afe_request_voltage_check();
    publish_report();
    service_low_soc(sample.current_valid);
}
