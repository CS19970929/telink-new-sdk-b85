/* 真实休眠 Core TU，无 MCU/AFE/SDK：相同输入在任何平台作出相同决策。 */
#include <assert.h>
#include <string.h>
#include "bms_sleep_config.h"
#include "bms_storage_platform.h"

const storage_port_t *bms_storage_platform_port(void) { return 0; }
int bms_storage_platform_region(bms_storage_domain_t domain, storage_region_t *region)
{
    (void)domain;
    (void)region;
    assert(0); /* 策略测试不允许访问持久化端口。 */
    return 0;
}
int main(void)
{
    bms_sleep_config_t config = {2800u, 60u, 3000u, 1440u, 30u};
    bms_sleep_policy_t state;
    bms_sleep_status_t status;
    bms_sleep_input_t input = {0, 2700u, 2000u, 3100u, 2400u, 1u, 0u, 0u};
    memset(&state, 0, sizeof(state));
    bms_sleep_policy_step(&state, &config, &input, 3599u, &status);
    assert(status.reason == BMS_SLEEP_REASON_LOW && status.elapsed_ms == 3599000u);
    input.valid = 0u; /* 失效帧不能证明低压恢复。 */
    input.cell_min_mv = 3300u;
    bms_sleep_policy_step(&state, &config, &input, 1u, &status);
    assert(status.reason == BMS_SLEEP_REASON_LOW && status.elapsed_ms == status.delay_ms);
    input.valid = 1u;
    bms_sleep_policy_step(&state, &config, &input, 0u, &status);
    assert(status.reason == BMS_SLEEP_REASON_NONE && !state.low_voltage_seconds);
    input.cell_min_mv = 2900u;
    input.current_ma = -200;
    bms_sleep_policy_step(&state, &config, &input, 100u, &status);
    assert(status.reason == BMS_SLEEP_REASON_NORMAL);
    input.current_ma = -201;
    bms_sleep_policy_step(&state, &config, &input, 0u, &status);
    assert(status.reason == BMS_SLEEP_REASON_NONE);
    input.overdischarge_fault = 1u;
    bms_sleep_policy_step(&state, &config, &input, 179u, &status);
    assert(status.reason == BMS_SLEEP_REASON_OVERDISCHARGE && status.elapsed_ms == 179000u);
    input.overdischarge_fault = 0u;
    input.valid = 0u;
    bms_sleep_policy_step(&state, &config, &input, UINT32_MAX, &status);
    assert(state.overdischarge_seconds == 180u && state.afe_error_seconds == 1800u);
    assert(status.elapsed_ms == status.delay_ms);
    input.valid = 1u;
    input.cell_min_mv = 3300u;
    input.pack_voltage_10mv = 2500u;
    bms_sleep_policy_step(&state, &config, &input, 0u, &status);
    assert(!state.overdischarge_active && status.reason == BMS_SLEEP_REASON_NONE);
    return 0;
}
