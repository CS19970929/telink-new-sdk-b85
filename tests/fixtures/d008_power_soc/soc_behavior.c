/* 同一生产 SOC 函数体在三种诊断宏下的业务输出；不以诊断缓存作为期望值。 */
static void init_case(uint8_t chemistry, uint8_t seed)
{
    bms_state_store_data_t state = {seed, 0u, 5u};
    memset(&g_bms_report, 0, sizeof(g_bms_report));
    memset(&g_bms_system_status, 0, sizeof(g_bms_system_status));
    stored_profile.battery_chemistry = chemistry;
    stored_profile.soc_profile_id = chemistry;
    g_bms_protection_params.cell_ovp_third_mv = chemistry == 1u ? 3650u : 4250u;
    g_bms_protection_params.cell_uvp_third_mv = 2500u;
    soc_param_lib_init(&state);
    assert(get_soc_real() == seed && g_bms_report.soc.soc_percent == seed);
}

int main(void)
{
    puts("chemistry,case,step,soc,display,capacity,full,cycle,soh,faults");
    for (unsigned chemistry = 1u; chemistry <= 2u; ++chemistry) {
        for (unsigned scenario = 0u; scenario < 5u; ++scenario) {
            uint8_t seed = scenario == 0u ? 90u : (scenario == 1u ? 97u : 40u);
            init_case((uint8_t)chemistry, seed);
            bms_soc_sample_t sample = {0};
            sample.timestamp_32k = UINT32_MAX - 12800u;
            sample.timestamp_ms = UINT32_MAX - 400u;
            sample.sample_valid = sample.voltage_valid = sample.temperature_valid = 1u;
            sample.temperature_min_x10 = sample.temperature_max_x10 = 650u;
            for (unsigned n = 0u; n < (scenario == 0u ? 36000u : 2400u); ++n) {
                uint16_t mid = chemistry == 1u ? 3330u : 3800u;
                sample.timestamp_32k += 6400u;
                sample.timestamp_ms += 200u;
                sample.cell_min_mv = mid;
                sample.cell_max_mv = mid + 10u;
                sample.cell_delta_mv = 10u;
                sample.current_ma = 0;
                sample.sample_valid = 1u;
                sample.third_cell_uvp = sample.third_cell_ovp = 0u;
                if (scenario == 1u) { /* 整包满充资格与显示到 100%。 */
                    sample.cell_min_mv = chemistry == 1u ? 3500u : 4200u;
                    sample.cell_max_mv = sample.cell_min_mv + 10u;
                    sample.current_ma = -500;
                    sample.third_cell_ovp = 1u;
                } else if (scenario == 2u) { /* UV 锁存期间开始充电，不能抹掉已充容量。 */
                    sample.cell_min_mv = 2400u;
                    sample.cell_max_mv = 2410u;
                    sample.third_cell_uvp = 1u;
                    sample.current_ma = n < 100u ? 1000 : -5000;
                } else if (scenario == 3u) { /* 连续静置空电。 */
                    sample.cell_min_mv = sample.cell_max_mv = 2400u;
                    sample.cell_delta_mv = 0u;
                } else if (scenario == 4u) { /* 方向转换、缺帧、长间隔、重复时间与重启。 */
                    sample.current_ma = n % 400u < 200u ? 5000 : -5000;
                    if (n % 173u == 0u) sample.sample_valid = 0u;
                    if (n % 211u == 0u) { sample.timestamp_ms += BMS_SOC_MAX_SAMPLE_GAP_MS + 1u; sample.timestamp_32k += (BMS_SOC_MAX_SAMPLE_GAP_MS + 1u)*32u; }
                    if (n == 1200u) {
                        bms_state_store_data_t saved = {get_soc_real(), g_bms_soc.discharge_fraction_percent,
                                                       g_bms_soc.cycle_count};
                        soc_param_lib_init(&saved);
                    }
                }
                bms_soc_process_sample(&sample);
                if (scenario == 4u && n % 127u == 0u) bms_soc_process_sample(&sample);
                assert(get_soc_real() <= 100u && g_bms_report.soc.soc_percent <= 100u);
                assert(g_bms_soc.remaining_capacity_as10 <= g_bms_soc.effective_capacity_as10);
                printf("%u,%u,%u,%u,%u,%lu,%lu,%lu,%u,%u\n", chemistry, scenario, n,
                       get_soc_real(), g_bms_report.soc.soc_percent,
                       (unsigned long)g_bms_soc.remaining_capacity_as10,
                       (unsigned long)g_bms_soc.effective_capacity_as10,
                       (unsigned long)g_bms_soc.cycle_count, g_bms_soc.soh,
                       g_bms_report.fault_third.all);
            }
            if (scenario == 1u) assert(get_soc_real() == 100u && g_bms_report.soc.soc_percent == 100u);
            if (scenario == 2u) assert(g_bms_soc.remaining_capacity_as10 != 0u);
            if (scenario == 3u) assert(get_soc_real() == 0u && g_bms_report.soc.soc_percent == 0u);
        }
    }
#if BMS_SOC_BOARD_TEST_ENABLE
    /* 板测独立启用时，RAM 结果记录和结束后的业务状态恢复仍可工作。 */
    init_case(1u, 43u);
    bms_soc_state_t saved = g_bms_soc;
    bms_soc_sample_t board = {0};
    board.sample_valid = board.voltage_valid = board.temperature_valid = 1u;
    board.temperature_min_x10 = board.temperature_max_x10 = 650u;
    for (unsigned n = 0u; n <= SOC_BOARD_CASE_COUNT * 100u; ++n) {
        board.timestamp_32k = n * 6400u;
        board.timestamp_ms = n * 200u;
        bms_soc_board_test_prepare(&board, board.timestamp_32k);
        if (bms_soc_board_test_active()) {
            bms_soc_process_sample(&board);
            bms_soc_board_test_note();
        }
    }
    assert(!bms_soc_board_test_active());
    assert(get_soc_real() == saved.soc_estimate_percent && g_bms_report.soc.soc_percent == 43u);
    for (unsigned i = 0u; i < SOC_BOARD_CASE_COUNT; ++i) assert(s_board_words[i][0] & 1u);
#endif
    return 0;
}
