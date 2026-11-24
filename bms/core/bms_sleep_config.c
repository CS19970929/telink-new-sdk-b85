/* 休眠业务默认、参数校验与纯策略；持久数据由 Config 拥有。 */
#include "bms_sleep_config.h"
#include "bms_product.h"
#include "bms_timing.h"
#include <string.h>

#define SLEEP_LOW_ACTIVE 0x01u
#define SLEEP_NORMAL_ACTIVE 0x02u
#define SLEEP_MS_PER_MINUTE 60000u

#if BMS_SLEEP_LOW_SECONDS < 60u || BMS_SLEEP_LOW_SECONDS % 60u != 0u || \
    BMS_SLEEP_LOW_SECONDS > BMS_SLEEP_NORMAL_SECONDS || \
    BMS_SLEEP_NORMAL_SECONDS % 60u != 0u || BMS_SLEEP_NORMAL_SECONDS / 60u > 65535u || \
    BMS_SLEEP_LOW_CELL_MV < 1000u || BMS_SLEEP_LOW_CELL_MV >= BMS_SLEEP_NORMAL_CELL_MV || \
    BMS_SLEEP_NORMAL_CELL_MV > 4500u
#error "Sleep defaults require ordered voltages and whole-minute delays in 1..65535"
#endif

void bms_sleep_config_defaults(bms_sleep_config_t *config)
{
    if (!config) return;
    config->low_cell_mv = BMS_SLEEP_LOW_CELL_MV;
    config->low_delay_minutes = BMS_SLEEP_LOW_SECONDS / 60u;
    config->normal_cell_mv = BMS_SLEEP_NORMAL_CELL_MV;
    config->normal_delay_minutes = BMS_SLEEP_NORMAL_SECONDS / 60u;
    config->afe_delay_minutes = BMS_SLEEP_AFE_FIXED_MINUTES;
}

int bms_sleep_config_valid(const bms_sleep_config_t *config)
{
    return config && config->low_cell_mv >= 1000u &&
        config->low_cell_mv < config->normal_cell_mv && config->normal_cell_mv <= 4500u &&
        config->low_delay_minutes != 0u &&
        config->low_delay_minutes <= config->normal_delay_minutes &&
        config->afe_delay_minutes == BMS_SLEEP_AFE_FIXED_MINUTES;
}

static uint32_t sleep_elapsed_limit(uint32_t elapsed, uint32_t increment, uint32_t limit)
{
    if (elapsed >= limit || increment >= limit - elapsed) return limit;
    return elapsed + increment;
}

static void sleep_select_reason(bms_sleep_status_t *status, uint8_t reason,
                                        uint32_t elapsed_ms, uint32_t delay_ms)
{
    if (status->reason == BMS_SLEEP_REASON_NONE ||
        delay_ms - elapsed_ms <= status->delay_ms - status->elapsed_ms) {
        status->reason = reason;
        status->elapsed_ms = elapsed_ms;
        status->delay_ms = delay_ms;
    }
}

/* 纯业务策略：输入均为已验证快照，不读取时间、Flash、AFE 或 MCU 外设。 */
void bms_sleep_policy_step(bms_sleep_policy_t *state, const bms_sleep_config_t *config,
                           const bms_sleep_input_t *input, uint32_t elapsed_ms,
                           bms_sleep_status_t *status)
{
    const uint32_t low_delay = (uint32_t)config->low_delay_minutes * SLEEP_MS_PER_MINUTE;
    const uint32_t normal_delay = (uint32_t)config->normal_delay_minutes * SLEEP_MS_PER_MINUTE;
    const uint32_t afe_delay = BMS_SLEEP_AFE_FIXED_MINUTES * SLEEP_MS_PER_MINUTE;
    const uint32_t overdischarge_delay = BMS_SLEEP_OVERDISCHARGE_SECONDS * 1000u;
    const int32_t rest_limit_ma = (int32_t)BMS_CURRENT_UNRELIABLE_MAX_MA;
    const uint8_t charging = input->current_valid && input->current_ma < -BMS_SLEEP_OVERDISCHARGE_CHARGE_MA;
    uint8_t conditions = 0u;
    uint8_t continuous;
    uint8_t overdischarge_timing;

    memset(status, 0, sizeof(*status));
    if (input->valid && input->current_valid && input->current_ma >= -rest_limit_ma &&
        input->current_ma <= rest_limit_ma) {
        if (input->cell_min_mv < config->low_cell_mv)
            conditions |= SLEEP_LOW_ACTIVE;
        if (input->cell_min_mv < config->normal_cell_mv)
            conditions |= SLEEP_NORMAL_ACTIVE;
    }
    /* 只累计两次低压静置样本之间的时间，漏采后重新开始。 */
    continuous = elapsed_ms <= BMS_SAMPLE_MAX_POLL_GAP_MS ? conditions & state->voltage_conditions : 0u;
    state->voltage_conditions = conditions;
    state->low_voltage_ms = (continuous & SLEEP_LOW_ACTIVE) ?
        sleep_elapsed_limit(state->low_voltage_ms, elapsed_ms, low_delay) : 0u;
    state->normal_voltage_ms = (continuous & SLEEP_NORMAL_ACTIVE) ?
        sleep_elapsed_limit(state->normal_voltage_ms, elapsed_ms, normal_delay) : 0u;
    state->afe_error_ms = input->afe_fault && !charging ?
        sleep_elapsed_limit(state->afe_error_ms, elapsed_ms, afe_delay) : 0u;
    /* fault_third 已合并软件/AFE 欠压。通信恢复重初始化不能冒充电池恢复。 */
    if (input->overdischarge_fault)
        state->overdischarge_active = 1u;
    else if (input->valid && !input->afe_fault &&
             input->cell_min_mv >= input->cell_uvp_recover_mv &&
             input->pack_voltage_10mv >= input->pack_uvp_recover_10mv)
        state->overdischarge_active = 0u;
    overdischarge_timing = state->overdischarge_active && !charging;
    state->overdischarge_ms = overdischarge_timing && state->overdischarge_timing ?
        sleep_elapsed_limit(state->overdischarge_ms, elapsed_ms, overdischarge_delay) : 0u;
    state->overdischarge_timing = overdischarge_timing;

    if (conditions & SLEEP_LOW_ACTIVE)
        sleep_select_reason(status, BMS_SLEEP_REASON_LOW, state->low_voltage_ms, low_delay);
    if (conditions & SLEEP_NORMAL_ACTIVE)
        sleep_select_reason(status, BMS_SLEEP_REASON_NORMAL, state->normal_voltage_ms, normal_delay);
    if (input->afe_fault && !charging)
        sleep_select_reason(status, BMS_SLEEP_REASON_AFE, state->afe_error_ms, afe_delay);
    if (overdischarge_timing)
        sleep_select_reason(status, BMS_SLEEP_REASON_OVERDISCHARGE, state->overdischarge_ms, overdischarge_delay);
}
