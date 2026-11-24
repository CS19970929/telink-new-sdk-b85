#include "bms_soc.h"
#include <assert.h>
#include <stdint.h>

static void init(bms_soc_t *soc, uint8_t chemistry, uint8_t shown, uint32_t now)
{
    bms_soc_config_t config = {{0, 0}, 600u, chemistry, 16u};
    bms_soc_checkpoint_t cp = {0};
    bms_soc_defaults(chemistry, &config.limits);
    cp.capacity_0p1ah = config.capacity_0p1ah;
    cp.charge_as10 = (uint32_t)shown * 21600u;
    cp.shown = shown;
    bms_soc_init(soc, &config, &cp, now);
}

static bms_soc_sample_t sample(uint32_t now, int32_t ma, uint16_t min_mv, uint16_t max_mv)
{
    bms_soc_sample_t input = {0};
    input.now_ms = input.current_time_ms = now;
    input.current_ma = ma;
    input.min_mv = min_mv;
    input.max_mv = max_mv;
    input.mean_mv = (uint16_t)((min_mv + max_mv) / 2u);
    input.temperature_x10 = 650u;
    input.current_valid = input.voltage_valid = input.voltage_fresh = input.resting_allowed = 1u;
    return input;
}

static void trace(bms_soc_t *soc, bms_soc_sample_t *m, unsigned count, uint32_t step)
{
    unsigned i;
    for (i = 0; i < count; ++i) {
        uint8_t before = soc->state.shown;
        m->now_ms += step;
        m->current_time_ms = m->now_ms;
        (void)bms_soc_step(soc, m);
        assert(soc->state.shown <= before + 1u);
        assert(before <= soc->state.shown + 1u);
        if (m->current_ma > 200) assert(soc->state.shown <= before);
        if (m->current_ma < -200) assert(soc->state.shown >= before);
    }
}

static void integration(void)
{
    bms_soc_t soc;
    uint32_t start = UINT32_MAX - 199u;
    bms_soc_sample_t m = sample(start, 199, 3250u, 3270u);
    init(&soc, BMS_SOC_CHEMISTRY_LFP, 60u, start);
    assert(!bms_soc_step(&soc, &m));
    trace(&soc, &m, 5u, 200u);
    assert(soc.state.charge_as10 == 1295999u && soc.carry_ma_ms == 99000);
    assert(soc.state.discharge_as10 == 1u);
    (void)bms_soc_step(&soc, &m);
    assert(soc.carry_ma_ms == 99000);
    m.current_ma = -199;
    trace(&soc, &m, 1u, 200u);
    assert(soc.state.charge_as10 == 1295998u); /* 上一读数覆盖截至反向样本的区间。 */
    trace(&soc, &m, 6u, 200u);
    assert(soc.state.charge_as10 == 1296000u && soc.carry_ma_ms == 0);
    m.current_valid = 0;
    trace(&soc, &m, 1u, 200u);
    m.current_valid = 1;
    trace(&soc, &m, 1u, 200u);
    trace(&soc, &m, 1u, 401u);
    assert(soc.state.charge_as10 == 1296000u);
    m.current_time_ms -= 1000u;
    m.now_ms += 200u;
    assert(!bms_soc_step(&soc, &m) && !soc.previous_valid);
}

static void full_and_resume(void)
{
    bms_soc_t soc, reboot;
    bms_soc_sample_t m = sample(0u, -1500, 3440u, 3520u);
    bms_soc_config_t config;
    bms_soc_checkpoint_t cp;
    init(&soc, BMS_SOC_CHEMISTRY_LFP, 82u, 0u);
    (void)bms_soc_step(&soc, &m);
    trace(&soc, &m, 149u, 200u);
    assert(soc.state.endpoint == BMS_SOC_NO_ENDPOINT && soc.state.shown <= 86u);
    trace(&soc, &m, 2u, 200u);
    assert(soc.state.endpoint == BMS_SOC_FULL && bms_soc_percent(&soc) == 100u);
    assert(soc.state.shown < 100u);
    m.current_ma = 0;
    trace(&soc, &m, 10u, 200u);
    cp = soc.state;
    config = soc.config;
    bms_soc_init(&reboot, &config, &cp, m.now_ms);
    trace(&reboot, &m, 1u, 200u);
    assert(reboot.state.shown == cp.shown);
    trace(&reboot, &m, 500u, 200u);
    assert(reboot.state.shown == 100u && reboot.state.endpoint == BMS_SOC_FULL);
    m.current_ma = 1000;
    trace(&reboot, &m, 1u, 200u);
    assert(reboot.state.endpoint == BMS_SOC_NO_ENDPOINT);
    trace(&reboot, &m, 25u, 200u);
    assert(reboot.state.shown == 99u);
}

static void cutoff(void)
{
    bms_soc_t soc;
    bms_soc_sample_t m = sample(0u, -10000, 3380u, 3600u);
    init(&soc, BMS_SOC_CHEMISTRY_LFP, 58u, 0u);
    trace(&soc, &m, 160u, 200u);
    assert(soc.state.endpoint == BMS_SOC_NO_ENDPOINT);
    m.min_mv = 3440u;
    m.max_mv = 3510u;
    trace(&soc, &m, 12u, 200u);
    m.charge_cutoff = 1;
    m.current_ma = 0;
    m.voltage_fresh = 0u;
    trace(&soc, &m, 1u, 200u);
    m.voltage_fresh = 1u;
    trace(&soc, &m, 1u, 200u);
    assert(soc.state.endpoint == BMS_SOC_FULL);
}

static void empty_and_weak(void)
{
    bms_soc_t soc;
    bms_soc_sample_t m = sample(0u, 45000, 2850u, 3040u);
    init(&soc, BMS_SOC_CHEMISTRY_LFP, 43u, 0u);
    trace(&soc, &m, 2u, 200u);
    m.min_mv = 2950u;
    trace(&soc, &m, 25u, 200u);
    assert(soc.state.endpoint == BMS_SOC_NO_ENDPOINT);
    m.min_mv = 2820u;
    trace(&soc, &m, 27u, 200u);
    assert(soc.state.endpoint == BMS_SOC_EMPTY && soc.state.charge_as10 == 0u);
    m.current_ma = 0;
    trace(&soc, &m, 225u, 200u);
    assert(soc.state.shown == 0u);

    init(&soc, BMS_SOC_CHEMISTRY_LFP, 25u, 0u);
    m = sample(0u, 36000, 2800u, 3250u);
    trace(&soc, &m, 6u, 200u);
    m.now_ms += 200u;
    m.current_time_ms = m.now_ms;
    m.current_ma = 0;
    m.discharge_cutoff = 1;
    assert(bms_soc_step(&soc, &m));
    m.line_check_active = 1;
    trace(&soc, &m, 6u, 200u);
    m.line_check_active = 0;
    m.line_check_passed = 0;
    trace(&soc, &m, 1u, 200u);
    assert(soc.state.endpoint == BMS_SOC_NO_ENDPOINT);

    init(&soc, BMS_SOC_CHEMISTRY_LFP, 25u, 0u);
    m = sample(0u, 36000, 2800u, 3250u);
    trace(&soc, &m, 6u, 200u);
    m.now_ms += 200u;
    m.current_time_ms = m.now_ms;
    m.current_ma = 0;
    m.discharge_cutoff = 1;
    assert(bms_soc_step(&soc, &m));
    m.line_check_active = 1;
    trace(&soc, &m, 6u, 200u);
    m.line_check_active = 0;
    m.line_check_passed = 1;
    trace(&soc, &m, 1u, 200u);
    assert(soc.state.endpoint == BMS_SOC_EMPTY);
}

static void rest_and_configuration(void)
{
    bms_soc_t soc, reboot;
    bms_soc_config_t config;
    bms_soc_sample_t m = sample(0u, 0, 3270u, 3300u);
    init(&soc, BMS_SOC_CHEMISTRY_LFP, 90u, 0u);
    trace(&soc, &m, 18001u, 400u);
    assert(soc.state.shown == 90u && bms_soc_percent(&soc) == 90u);
    init(&soc, BMS_SOC_CHEMISTRY_NMC, 91u, 0u);
    m = sample(0u, 0, 3740u, 3760u);
    trace(&soc, &m, 13499u, 400u);
    assert(bms_soc_percent(&soc) == 91u);
    trace(&soc, &m, 2u, 400u);
    assert(bms_soc_percent(&soc) == 90u);
    assert(soc.state.shown == 91u);
    config = soc.config;
    config.capacity_0p1ah = 1000u;
    config.limits.full_mv = 4170;
    config.limits.empty_mv = 3150;
    assert(bms_soc_limits_valid(config.chemistry, &config.limits));
    bms_soc_init(&reboot, &config, &soc.state, m.now_ms);
    bms_soc_reconfigure(&soc, &config);
    assert(soc.state.charge_as10 == reboot.state.charge_as10 && soc.state.charge_as10 == 3240000u);
    assert(soc.state.shown == 91u && reboot.state.shown == 91u);
    config.limits.full_mv = 4201u;
    assert(!bms_soc_limits_valid(config.chemistry, &config.limits));
}

static void capacity_and_unconfirmed_bounds(void)
{
    bms_soc_t soc;
    bms_soc_sample_t m = sample(0u, 2000000, 3150u, 3200u);
    bms_soc_config_t config = {{3500u, 2850u}, 1u, BMS_SOC_CHEMISTRY_LFP, 16u};
    bms_soc_checkpoint_t cp = {3600u, 0u, 0u, 1u, 65534u, 99u, 0u, 0u};
    bms_soc_init(&soc, &config, &cp, 0u);
    trace(&soc, &m, 1250u, 400u);
    assert(soc.state.cycles == UINT16_MAX && soc.state.discharge_as10 < 3600u);
    assert(soc.state.charge_as10 == 0u && soc.state.shown == 1u);
    m.current_ma = -2000000;
    trace(&soc, &m, 1250u, 400u);
    assert(soc.state.charge_as10 == 3600u && soc.state.shown == 99u);
}

int main(void)
{
    integration();
    full_and_resume();
    cutoff();
    empty_and_weak();
    rest_and_configuration();
    capacity_and_unconfirmed_bounds();
    return 0;
}
