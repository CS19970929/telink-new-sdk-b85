#include <assert.h>
#include "bms_soc_eta.h"

static void settle(bms_soc_eta_t *eta, bms_soc_eta_input_t *input)
{
    bms_soc_eta_reset(eta);
    for (unsigned i = 0; i < 151; ++i) bms_soc_eta_update(eta, input);
}

int main(void)
{
    bms_soc_eta_t eta;
    /* 100 Ah 电池剩余 40 Ah，以 10 A 放电：240 分钟。 */
    bms_soc_eta_input_t input = {10000, 40u * 36000u, 100u * 36000u,
                                 200, BMS_SOC_ETA_DIR_DISCHARGE, 0, 0};
    settle(&eta, &input);
    assert(eta.eta_valid && eta.time_to_empty_min == 240u);
    assert(eta.time_to_full_min == BMS_SOC_ETA_MINUTES_INVALID);
    input.current_ma = -10000;
    input.direction = BMS_SOC_ETA_DIR_CHARGE;
    settle(&eta, &input);
    assert(eta.eta_valid && eta.time_to_full_min == 360u);
    input.endpoint_active = 1;
    bms_soc_eta_update(&eta, &input);
    assert(!eta.eta_valid && eta.eta_state == BMS_SOC_ETA_LOW_CONFIDENCE);
    input.endpoint_active = 0;
    input.near_full = 1;
    input.current_ma = -1000;
    for (unsigned i = 0; i < 200; ++i) bms_soc_eta_update(&eta, &input);
    assert(!eta.eta_valid); /* 满电收尾电流不能外推成可信 ETA。 */
    input.direction = BMS_SOC_ETA_DIR_NONE;
    bms_soc_eta_update(&eta, &input);
    assert(eta.eta_state == BMS_SOC_ETA_INVALID);
    input.direction = BMS_SOC_ETA_DIR_DISCHARGE;
    input.current_ma = 1;
    input.deadband_ma = 0;
    settle(&eta, &input);
    assert(!eta.eta_valid); /* 极小电流与零死区不能造成除零。 */
    input.current_ma = INT32_MAX;
    input.near_full = 0;
    settle(&eta, &input);
    assert(eta.eta_valid && eta.time_to_empty_min == 0u);
    return 0;
}
