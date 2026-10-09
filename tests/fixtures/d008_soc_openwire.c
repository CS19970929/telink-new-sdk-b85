static bms_feature_state_t s_feature;
static unsigned poll_count, rounds, diagnostic_frames;
static int poll_result, poll_stuck, start_ok = 1;
uint32_t bms_diag_tick(void) { return tick; }
void bms_diag_openwire(const uint16_t *words) { (void)words; }
static uint8_t bms_afe_openwire_stop(void) { return 1u; }
static uint8_t openwire_hard_fault(void) { return (uint8_t)afe_error; }
static uint8_t apply_balance_mask(uint32_t mask) { return 1u; }
static void update_charge_session(void) { s_feature.charge_session_active = (uint8_t)charge_session_active; }
static void update_balance_voltage_trust(const bms_afe_feature_snapshot_t *s) {}
static void service_heater(const bms_afe_feature_snapshot_t *s) { s_feature.heater_on = (uint8_t)heater_on; }
static void service_balance(const bms_afe_feature_snapshot_t *s) { s_feature.balance_active = (uint8_t)balance_active; }
static void bms_features_on_afe_invalid(void) { s_feature.openwire_suspected = 1u; }
static uint8_t bms_afe_openwire_start(void)
{
    poll_count = 0u;
    ++rounds;
    return (uint8_t)start_ok;
}
static bms_afe_diag_state_t bms_afe_openwire_poll(bms_afe_openwire_result_t *r)
{
    ++poll_count;
    if (poll_stuck || poll_count == 1u) return BMS_AFE_DIAG_BUSY;
    if (poll_result == 1) return BMS_AFE_DIAG_ERROR;
    r->valid = 1u;
    r->determinate = poll_result != 2;
    r->cell_count = 16u;
    r->open_cell_mask = poll_result == 3 ? 1u : 0u;
    return BMS_AFE_DIAG_READY;
}
/* FEATURE_SOURCE */
static bms_features_status_t feature_status(void)
{
 bms_features_status_t status;bms_features_get_status(&status);return status;
}


static void initialize_joint(void)
{
    setup(1u, 100u, 3196u);
    set_core_voltage(3196u, 3260u, 64u);
    memset(&s_feature, 0, sizeof(s_feature));
    s_feature.openwire_wait_tick = tick;
    s_feature.openwire_wait_ms = BMS_OPENWIRE_FIRST_IDLE_MS;
    rounds = diagnostic_frames = poll_count = 0u;
    poll_result = poll_stuck = 0;
    start_ok = 1;
    sample(1, -89, 1u);
}

static void joint_sample(uint32_t elapsed_32k)
{
    /* Guard ordering: acquire first, feature poll second, SOC last. A healthy
     * poll clears COW on the frame whose voltage is still diagnostic. */
    if (s_feature.openwire_active) {
        set_core_voltage(3100u, 3150u, 50u);
        ++diagnostic_frames;
    } else {
        set_core_voltage(3196u, 3260u, 64u);
    }
    bms_features_service();
    sample(1, -89, elapsed_32k);
}

static void prime_rest(void)
{
    initialize_joint();
    s_feature.openwire_wait_tick = tick;
    s_feature.openwire_wait_ms = 600000u;
    for (unsigned i = 0u; i < 510u; ++i) joint_sample(6400u);
    assert(g_soc_runtime.idle_stable_ticks > 500u);
    g_soc_runtime.ocv_down_ticks = 123u;
    s_feature.openwire_wait_ms = 0u;
    s_feature.openwire_idle_samples = BMS_OPENWIRE_FIRST_IDLE_SAMPLES;
    joint_sample(6400u);
    assert(g_soc_runtime.ocv_openwire_phase == SOC_OCV_OPENWIRE_PAUSED);
}

static void assert_reset(void)
{
    if (g_soc_runtime.idle_stable_ticks != 0u)
        fprintf(stderr, "unexpected retained rest=%lu phase=%u result=%d stuck=%d temp=%d heater=%d balance=%d afe=%d current=%ld session=%d\n",
                (unsigned long)g_soc_runtime.idle_stable_ticks,
                (unsigned)g_soc_runtime.ocv_openwire_phase, poll_result, poll_stuck,
                temp_valid, heater_on, balance_active, afe_error,
                (long)g_soc_input.current_ma, charge_session_active);
    assert(g_soc_runtime.idle_stable_ticks == 0u);
    assert(g_soc_runtime.ocv_down_ticks == 0u);
    assert(g_soc_runtime.ocv_openwire_phase == SOC_OCV_OPENWIRE_NONE);
}

int main(void)
{
    initialize_joint();
    unsigned corrections = 0u;
    for (unsigned i = 0u; i < 13500u; ++i) {
        joint_sample(6400u);
        if (g_soc_runtime.last_soc_action == BMS_SOC_ACTION_OCV_DOWN) ++corrections;
    }
    assert(rounds >= 8u && diagnostic_frames >= 16u);
    assert(get_soc_real() == 99u && get_soc_display() == 99u && corrections == 1u);
    assert(g_soc_runtime.idle_stable_ticks == soc_ocv_prepare_ticks());
    puts("PASS joint: repeated real Open-Wire policy, diagnostic voltage exclusion, 600s qualification and 1800s OCV_DOWN/display");

    prime_rest();
    uint32_t rest = g_soc_runtime.idle_stable_ticks;
    joint_sample(12800u);
    joint_sample(12800u);
    assert(!s_feature.openwire_active && feature_status().openwire_sample_active);
    assert(g_soc_runtime.idle_stable_ticks == rest && g_soc_runtime.ocv_down_ticks == 123u);
    for (unsigned i = 1u; i <= 3u; ++i) {
        joint_sample(12800u);
        assert(g_soc_runtime.ocv_openwire_confirm_samples == i);
        assert(g_soc_runtime.idle_stable_ticks == rest);
    }
    joint_sample(6400u);
    assert(g_soc_runtime.idle_stable_ticks == rest + 1u);
    assert(g_soc_runtime.ocv_down_ticks == 123u);
    puts("PASS pause: both timers frozen, final diagnostic frame excluded, three distinct 400ms frames required");

    for (int result = 1; result <= 3; ++result) {
        prime_rest(); poll_result = result;
        joint_sample(6400u); joint_sample(6400u);
        assert(s_feature.openwire_suspected); assert_reset();
    }
    prime_rest(); poll_stuck = 1;
    for (unsigned i = 0u; i < 15u; ++i) joint_sample(6400u);
    assert_reset();
    prime_rest(); sample(1, -89, 12801u); assert_reset();
    prime_rest(); sample(0, 0, 6400u); assert_reset();
    prime_rest(); temp_valid = 0; joint_sample(6400u); assert_reset();
    prime_rest(); heater_on = 1; joint_sample(6400u); assert_reset();
    prime_rest(); balance_active = 1; joint_sample(6400u); assert_reset();
    prime_rest(); afe_error = 1; joint_sample(6400u); assert_reset();
    prime_rest(); sample(1, 201, 6400u); assert_reset();
    prime_rest(); charge_session_active = 1; joint_sample(6400u); assert_reset();
    prime_rest(); simulated_reboot(); assert_reset();
    prime_rest(); joint_sample(6400u); joint_sample(6400u);
    set_core_voltage(3210u, 3274u, 64u); bms_features_service();
    sample(1, -89, 6400u); assert_reset();
    prime_rest(); joint_sample(6400u); joint_sample(6400u);
    set_core_voltage(3196u, 3300u, 104u); bms_features_service();
    sample(1, -89, 6400u); assert_reset();
    puts("PASS failure: suspected/confirmed/error/timeout, GAP/invalid, temperature/current/heater/balance/AFE/charger/reboot and changed voltage reset both timers");

    prime_rest();
    tick = UINT32_MAX - 9600u;
    g_soc_sample_tick_32k = tick;
    g_soc_observed_tick_32k = tick;
    s_feature.openwire_started_tick = tick;
    g_soc_runtime.ocv_openwire_started_32k = tick;
    for (unsigned i = 0u; i < 6u; ++i) joint_sample(6400u);
    assert(g_soc_runtime.ocv_openwire_phase == SOC_OCV_OPENWIRE_NONE);
    assert(g_soc_runtime.idle_stable_ticks > 500u);
    puts("PASS wrap: bounded COW pause and recovery survive 32k tick wrap");
    return 0;
}
