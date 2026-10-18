/* 休眠业务默认、参数校验与纯策略；持久数据由 Config 拥有。 */
#include "bms_sleep_config.h"
#include "bms_product.h"
#include <string.h>

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
                                        uint32_t elapsed_seconds, uint32_t delay_seconds)
{
    uint32_t elapsed_ms = elapsed_seconds * 1000u;
    uint32_t delay_ms = delay_seconds * 1000u;
    if (status->reason == BMS_SLEEP_REASON_NONE ||
        delay_ms - elapsed_ms <= status->delay_ms - status->elapsed_ms) {
        status->reason = reason;
        status->elapsed_ms = elapsed_ms;
        status->delay_ms = delay_ms;
    }
}

/* 纯业务策略：输入均为已验证快照，不读取时间、Flash、AFE 或 MCU 外设。 */
void bms_sleep_policy_step(bms_sleep_policy_t *state, const bms_sleep_config_t *config,
                           const bms_sleep_input_t *input, uint32_t elapsed_sec,
                           bms_sleep_status_t *status)
{
    uint8_t conditions = state->voltage_conditions;
    /* 数据暂不可用不等于正式 AFE 故障；持续失效由 guard 确认。 */
    uint8_t afe_fault = input->afe_fault;
    uint32_t low_delay, normal_delay, afe_delay;
    low_delay = (uint32_t)config->low_delay_minutes * 60u;
    normal_delay = (uint32_t)config->normal_delay_minutes * 60u;
    afe_delay = BMS_SLEEP_AFE_FIXED_MINUTES * 60u;
    memset(status, 0, sizeof(*status));
    if (input->valid) {
        conditions = 0u;
        if (input->cell_min_mv < config->low_cell_mv)
            conditions |= BMS_SLEEP_VOLTAGE_LOW_ACTIVE;
        if (input->cell_min_mv < config->normal_cell_mv &&
            input->current_ma >= -BMS_SLEEP_CHARGE_CURRENT_MA)
            conditions |= BMS_SLEEP_VOLTAGE_NORMAL_ACTIVE;
    }
    /* 下档不排除充电，上档只在无可靠充电时累计；保持原有两条件资格。 */
    state->voltage_conditions = conditions;
    state->low_voltage_seconds = (conditions & BMS_SLEEP_VOLTAGE_LOW_ACTIVE) ?
        sleep_elapsed_limit(state->low_voltage_seconds, elapsed_sec, low_delay) : 0u;
    state->normal_voltage_seconds = (conditions & BMS_SLEEP_VOLTAGE_NORMAL_ACTIVE) ?
        sleep_elapsed_limit(state->normal_voltage_seconds, elapsed_sec,
                             normal_delay) : 0u;
    state->afe_error_seconds = afe_fault ?
        sleep_elapsed_limit(state->afe_error_seconds, elapsed_sec,
                             afe_delay) : 0u;
    /* fault_third 已合并软件/AFE 欠压。通信恢复重初始化不能冒充电池恢复。 */
    if (input->overdischarge_fault)
        state->overdischarge_active = 1u;
    else if (input->valid && !afe_fault &&
             input->cell_min_mv >= input->cell_uvp_recover_mv &&
             input->pack_voltage_10mv >= input->pack_uvp_recover_10mv)
        state->overdischarge_active = 0u;
    state->overdischarge_seconds = state->overdischarge_active ?
        sleep_elapsed_limit(state->overdischarge_seconds, elapsed_sec,
                             BMS_SLEEP_OVERDISCHARGE_SECONDS) : 0u;

    if ((conditions & BMS_SLEEP_VOLTAGE_LOW_ACTIVE))
        sleep_select_reason(status, BMS_SLEEP_REASON_LOW,
            state->low_voltage_seconds, low_delay);
    if ((conditions & BMS_SLEEP_VOLTAGE_NORMAL_ACTIVE))
        sleep_select_reason(status, BMS_SLEEP_REASON_NORMAL,
            state->normal_voltage_seconds, normal_delay);
    if (afe_fault)
        sleep_select_reason(status, BMS_SLEEP_REASON_AFE,
            state->afe_error_seconds, afe_delay);
    if (state->overdischarge_active)
        sleep_select_reason(status, BMS_SLEEP_REASON_OVERDISCHARGE,
            state->overdischarge_seconds, BMS_SLEEP_OVERDISCHARGE_SECONDS);
}
