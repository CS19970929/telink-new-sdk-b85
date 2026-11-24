#include "bms_soc.h"
#include <assert.h>
#include <stdint.h>

static void init(bms_soc_t *soc, uint8_t chemistry, uint8_t shown, uint32_t now)
{
    bms_soc_config_t config = {{3500u, 2850u}, 600u, chemistry, 16u};
    bms_soc_checkpoint_t cp = {0};
    if (chemistry == BMS_SOC_CHEMISTRY_NMC)
        config.limits = (bms_soc_limits_t){4180u, 3100u};
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

static void display_survives_current_gaps(void)
{
    bms_soc_t soc;
    bms_soc_sample_t m = sample(0u, 4900, 3090u, 3120u);
    unsigned i;

    init(&soc, BMS_SOC_CHEMISTRY_LFP, 60u, 0u);
    (void)bms_soc_step(&soc, &m);
    for (i = 0u; i < 24u; ++i) {
        trace(&soc, &m, 1u, 201u);
        trace(&soc, &m, 1u, 401u);
    }
    assert(soc.state.shown == 60u && soc.display_ms == 4824u);

    m.current_valid = 0u;
    trace(&soc, &m, 1u, 201u);
    m.current_valid = 1u;
    trace(&soc, &m, 1u, 201u);
    assert(soc.state.shown == 60u);
    trace(&soc, &m, 1u, 201u);
    assert(soc.state.shown == 59u && soc.state.charge_as10 == 1295754u);
}

static void integrate_dvc_cc2_poll_cadence(void)
{
    const bms_soc_config_t config = {{3550u, 2600u}, 40u, BMS_SOC_CHEMISTRY_LFP, 16u};
    const bms_soc_checkpoint_t checkpoint = {
        .charge_as10 = 72000u, .capacity_0p1ah = 40u, .shown = 50u
    };
    const int32_t currents_ma[] = {-3000, 3000};
    unsigned direction;

    for (direction = 0u; direction < 2u; ++direction) {
        bms_soc_t soc;
        bms_soc_sample_t m = sample(0u, currents_ma[direction], 3310u, 3320u);
        uint32_t next_cc2_ms = 256u;
        unsigned i;

        bms_soc_init(&soc, &config, &checkpoint, 0u);
        (void)bms_soc_step(&soc, &m);
        for (i = 0u; i < 600u; ++i) {
            m.now_ms += 201u;
            m.voltage_fresh = m.now_ms >= next_cc2_ms;
            if (m.voltage_fresh) {
                m.current_time_ms = m.now_ms;
                next_cc2_ms += 256u;
            }
            (void)bms_soc_step(&soc, &m);
        }

        /* 129 个双轮 CC2 间隔各截去 2 ms，总计保留 120342 ms。 */
        assert(soc.state.charge_as10 == (direction == 0u ? 75610u : 68390u));
        assert(soc.state.discharge_as10 == (direction == 0u ? 0u : 3610u));
        assert(soc.carry_ma_ms == (direction == 0u ? -26000 : 26000));
    }
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
    assert(cp.endpoint_basis == (100u | (150u << 10) | (16u << 20) |
        ((uint32_t)BMS_SOC_CHEMISTRY_LFP << 25)));
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

static void lower_lfp_endpoint_checkpoint(void)
{
    bms_soc_t soc, reboot;
    bms_soc_config_t config = {{3750u, 2000u}, 600u, BMS_SOC_CHEMISTRY_LFP, 16u};
    bms_soc_limits_t defaults;
    bms_soc_sample_t m = sample(0u, 45000, 2000u, 2100u);

    bms_soc_defaults(config.chemistry, &defaults);
    assert(bms_soc_limits_valid(config.chemistry, &defaults));
    assert(bms_soc_limits_valid(config.chemistry, &config.limits));
    bms_soc_init(&soc, &config, 0, 0u);
    trace(&soc, &m, 27u, 200u);
    assert(soc.state.endpoint == BMS_SOC_EMPTY);
    assert(soc.state.endpoint_basis == (350u | (16u << 20) |
        ((uint32_t)BMS_SOC_CHEMISTRY_LFP << 25) | (1u << 27)));

    bms_soc_init(&reboot, &config, &soc.state, m.now_ms);
    assert(reboot.state.endpoint == BMS_SOC_EMPTY);
    config.limits.empty_mv = 2700u;
    bms_soc_init(&reboot, &config, &soc.state, m.now_ms);
    assert(reboot.state.endpoint == BMS_SOC_NO_ENDPOINT);
}

static void manual_soc_checkpoint(void)
{
    bms_soc_t soc, reboot;
    const uint8_t values[] = {0u, 55u, 99u, 100u};
    unsigned i;

    init(&soc, BMS_SOC_CHEMISTRY_LFP, 60u, 0u);
    soc.state.cycles = 7u;
    soc.state.discharge_as10 = 36000u;
    for (i = 0u; i < sizeof(values); ++i) {
        bms_soc_checkpoint_t checkpoint = bms_soc_manual_checkpoint(&soc, values[i]);
        assert(bms_soc_checkpoint_valid(&checkpoint));
        assert(soc.state.shown == 60u && soc.state.charge_as10 == 1296000u);
        assert(checkpoint.cycles == 7u && checkpoint.discharge_as10 == 36000u);
        bms_soc_init(&reboot, &soc.config, &checkpoint, 0u);
        assert(reboot.state.shown == values[i] && bms_soc_percent(&reboot) == values[i]);
        assert(reboot.state.endpoint == (values[i] == 100u ? BMS_SOC_FULL : BMS_SOC_NO_ENDPOINT));
    }
    bms_soc_sample_t discharge = sample(200u, 4900, 3290u, 3310u);
    (void)bms_soc_step(&reboot, &discharge);
    assert(reboot.state.endpoint == BMS_SOC_NO_ENDPOINT);
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
    display_survives_current_gaps();
    integrate_dvc_cc2_poll_cadence();
    full_and_resume();
    cutoff();
    empty_and_weak();
    rest_and_configuration();
    lower_lfp_endpoint_checkpoint();
    manual_soc_checkpoint();
    capacity_and_unconfirmed_bounds();
    return 0;
}
