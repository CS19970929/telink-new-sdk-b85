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
    bms_sleep_input_t input = {
        .cell_min_mv = 2700u, .pack_voltage_10mv = 2000u,
        .cell_uvp_recover_mv = 3100u, .pack_uvp_recover_10mv = 2400u,
        .valid = 1u, .current_valid = 1u
    };
    memset(&state, 0, sizeof(state));
    const uint16_t thresholds[] = {2700u, 2900u};
    const uint8_t reasons[] = {BMS_SLEEP_REASON_LOW, BMS_SLEEP_REASON_NORMAL};
    for (unsigned tier = 0u; tier < 2u; ++tier) {
        memset(&state, 0, sizeof(state));
        input.cell_min_mv = thresholds[tier];
        input.current_ma = 200;
        bms_sleep_policy_step(&state, &config, &input, 200u, &status);
        assert(status.reason == reasons[tier] && status.elapsed_ms == 0u);
        bms_sleep_policy_step(&state, &config, &input, 200u, &status);
        assert(status.elapsed_ms == 200u);
        input.current_ma = 201;
        bms_sleep_policy_step(&state, &config, &input, 200u, &status);
        assert(status.reason == BMS_SLEEP_REASON_NONE);
        input.current_ma = -200;
        bms_sleep_policy_step(&state, &config, &input, 200u, &status);
        assert(status.reason == reasons[tier] && status.elapsed_ms == 0u);
        input.current_ma = -201;
        bms_sleep_policy_step(&state, &config, &input, 200u, &status);
        assert(status.reason == BMS_SLEEP_REASON_NONE);
        input.current_ma = 0;
        bms_sleep_policy_step(&state, &config, &input, 200u, &status);
        input.current_valid = 0u;
        bms_sleep_policy_step(&state, &config, &input, 200u, &status);
        assert(status.reason == BMS_SLEEP_REASON_NONE);
        input.current_valid = 1u;
        bms_sleep_policy_step(&state, &config, &input, 200u, &status);
        uint32_t delay = status.delay_ms;
        assert(status.reason == reasons[tier] && status.elapsed_ms == 0u);
        for (uint32_t ms = 400u; ms < delay; ms += 400u)
            bms_sleep_policy_step(&state, &config, &input, 400u, &status);
        assert(status.elapsed_ms == delay - 400u);
        bms_sleep_policy_step(&state, &config, &input, 400u, &status);
        assert(status.elapsed_ms == delay);
        bms_sleep_policy_step(&state, &config, &input, 401u, &status);
        assert(status.elapsed_ms == 0u);
    }

    memset(&state, 0, sizeof(state));
    input.overdischarge_fault = 1u;
    input.current_ma = -1000;
    bms_sleep_policy_step(&state, &config, &input, 200u, &status);
    bms_sleep_policy_step(&state, &config, &input, 179000u, &status);
    assert(status.reason == BMS_SLEEP_REASON_OVERDISCHARGE && status.elapsed_ms == 179000u);
    input.current_ma = -1001;
    bms_sleep_policy_step(&state, &config, &input, 1000u, &status);
    assert(status.reason == BMS_SLEEP_REASON_NONE && state.overdischarge_active && state.overdischarge_ms == 0u);
    input.current_ma = 0;
    bms_sleep_policy_step(&state, &config, &input, 200u, &status);
    assert(status.reason == BMS_SLEEP_REASON_OVERDISCHARGE && status.elapsed_ms == 0u);
    input.overdischarge_fault = 0u;
    input.valid = 0u;
    input.current_valid = 0u;
    bms_sleep_policy_step(&state, &config, &input, UINT32_MAX, &status);
    assert(state.overdischarge_ms == 180000u && !state.afe_error_ms);
    input.afe_fault = 1u;
    bms_sleep_policy_step(&state, &config, &input, 1800000u, &status);
    assert(state.afe_error_ms == 1800000u);
    input.afe_fault = 0u;
    assert(status.elapsed_ms == status.delay_ms);
    input.valid = 1u;
    input.cell_min_mv = 3300u;
    input.pack_voltage_10mv = 2500u;
    bms_sleep_policy_step(&state, &config, &input, 0u, &status);
    assert(!state.overdischarge_active && status.reason == BMS_SLEEP_REASON_NONE);
    return 0;
}
