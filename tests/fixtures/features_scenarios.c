
bms_protection_params_t g_bms_protection_params;
static bms_user_params_t user;
static bms_afe_feature_snapshot_t snapshot;
static uint8_t snapshot_valid, heater, balance_fail, balance_off_fail, balance_read_fail, params_valid=1;
static unsigned balance_on_calls, balance_off_calls, balance_get_calls;
static unsigned charger_queries;
static uint8_t charger_present, charger_valid;
static unsigned fuse_count;
static uint8_t fuse_request;
static uint32_t balance, balance_dropped_mask;
static uint32_t test_tick;
uint32_t bms_time_now_32k(void){return test_tick;}
int bms_config_get_user(bms_user_params_t *p){*p=user;return 1;}
uint8_t bms_protection_params_valid(void){return params_valid;}
void bms_board_features_init(uint8_t fired){fuse_request=fired;}
void bms_board_heater_set(uint8_t on){heater=on;}
void bms_board_heater_fuse_fire(void){++fuse_count;fuse_request=1u;}
uint8_t bms_afe_get_charge_source_present(uint8_t *present){++charger_queries;*present=charger_present;return charger_valid;}
uint8_t bms_afe_get_feature_snapshot(bms_afe_feature_snapshot_t *s){if(!snapshot_valid){memset(s,0xa5,sizeof(*s));return 0;}*s=snapshot;return 1;}
uint8_t bms_afe_set_balance_mask(uint32_t mask){
    if(mask)++balance_on_calls;else ++balance_off_calls;
    if(balance_fail || (!mask && balance_off_fail))return 0;
    balance=mask & ~balance_dropped_mask;
    /* 与 DVC/SH 真实后端保持同一契约：成功必须完成回读并与请求一致。 */
    return !balance_read_fail && balance==mask;
}
uint8_t bms_afe_get_balance_mask(uint32_t *mask){++balance_get_calls;if(balance_read_fail)return 0;*mask=balance;return 1;}
/* FEATURES */
static void voltages(unsigned delta)
{
    for(unsigned i=0;i<BMS_AFE_FEATURE_MAX_CELLS;i++)
        g_bms_report.cell_voltage_mv[i]=i<snapshot.cell_count?3400:61001;
    g_bms_report.cell_voltage_mv[0]=(uint16_t)(3400+delta);
    g_bms_report.cell_min_mv=3400;
    g_bms_report.cell_max_mv=(uint16_t)(3400+delta);
    g_bms_report.cell_delta_mv=(uint16_t)delta;
}
static void reset(void)
{
    memset(&g_bms_report,0,sizeof(g_bms_report));
    for(unsigned i=0;i<BMS_ERROR_COUNT;i++)bms_error_clear((bms_error_id_t)i);
    memset(&g_bms_protection_params,0,sizeof(g_bms_protection_params));memset(&snapshot,0,sizeof(snapshot));
    params_valid=1;balance=0;balance_dropped_mask=0;balance_fail=balance_off_fail=balance_read_fail=0;
    balance_on_calls=balance_off_calls=balance_get_calls=0;fuse_count=0;fuse_request=0;
    charger_queries=0;charger_present=BMS_HEATER_REQUIRE_CHARGE_SOURCE_PRESENT;charger_valid=1;
    snapshot_valid=1;snapshot.cell_count=BMS_PRODUCT_CELL_COUNT;
    snapshot.battery_temp_valid=1;snapshot.heater_temp_valid=1;snapshot.mos_temp_valid=1;
    snapshot.battery_temp_min_x10=650;snapshot.battery_temp_max_x10=650;
    snapshot.heater_temp_x10=650;snapshot.mos_temp_x10=650;
    user.heater_enable=1;user.heater_start_x10=400;user.heater_stop_x10=450;
    user.balance_enable=1;user.balance_start_mv=3400;user.balance_start_delta_mv=50;user.balance_stop_delta_mv=30;
    g_bms_protection_params.charge_otp_recover_x10=850;g_bms_protection_params.charge_utp_recover_x10=450;g_bms_protection_params.mos_otp_recover_x10=1100;
    test_tick=0;
    memset(&s_feature,0,sizeof(s_feature)); /* 模拟 MCU 冷启动，区别于 AFE 重初始化。 */
    voltages(50);g_bms_report.charge_current_a10=10;bms_features_init();
}
static void step(unsigned n){while(n--){test_tick+=6400u;bms_features_service(1u);}}
int main(void)
{
    /* 缓存轮次不累计可信度，但已运行的均衡必须响应停止条件。 */
    reset();for(unsigned i=0;i<20;i++)bms_features_service(0u);assert(!balance_voltage_trusted());
    step(5);user.balance_enable=0;bms_features_service(0u);assert(!balance);
    /* 功能只报告自己的 MOS 阻断；参数失效仍停功能输出，MOS 由 guard 独立阻断。 */
    reset();params_valid=0;step(5);
    assert(!balance && !heater);
    /* 许可为零时持续禁止；AFE 重初始化也不能覆盖持久配置。 */
    reset();step(5);assert(balance_get_calls==0u); /* 成功的 ON/OFF 不再重复读硬件 */
    user.balance_enable=0;step(1);assert(!balance && balance_get_calls==0u);
    unsigned on_before=balance_on_calls;
    bms_features_init();step(20);assert(!balance && balance_on_calls==on_before);
    /* 恢复许可只恢复条件检测，压差不足仍然不启动。 */
    voltages(49);user.balance_enable=1;step(5);assert(!balance);
    voltages(50);step(1);assert(balance==(bms_board_balance_supported()?1u:0u));
    user.balance_enable=0;balance_off_fail=1;step(1);
    on_before=balance_on_calls;step(3);assert(balance_on_calls==on_before);
    balance_off_fail=0;step(1);assert(!balance);
    step(10);assert(!balance && balance_on_calls==on_before);
    /* 冷启动静置直接建立电压资格；不依赖过去充电，也不查询充电器。 */
    reset();g_bms_report.charge_current_a10=0;
    step(4);assert(!balance);
    step(1);assert(balance==(bms_board_balance_supported()?1u:0u));
    assert(!charger_queries && !heater);
    g_bms_report.discharge_current_a10=1;step(1);assert(!balance);
    g_bms_report.discharge_current_a10=0;step(1);
    assert(balance==(bms_board_balance_supported()?1u:0u));
    bms_features_stop();bms_features_init();step(4);assert(!balance);
    step(1);assert(balance==(bms_board_balance_supported()?1u:0u));
    reset();step(4);assert(!balance && !balance_voltage_trusted());
    step(1);assert(balance_voltage_trusted());
    assert(balance==(bms_board_balance_supported()?1u:0u));
    g_bms_report.charge_current_a10=0;step(1);
    assert(balance==(bms_board_balance_supported()?1u:0u));
    g_bms_report.discharge_current_a10=1;step(1);
    assert(!balance);
    g_bms_report.discharge_current_a10=0;g_bms_report.charge_current_a10=10;step(1);
    voltages(31);step(1);assert(balance==(bms_board_balance_supported()?1u:0u));
    voltages(30);step(1);assert(!balance);
    voltages(49);step(1);assert(!balance);voltages(50);step(1);
    assert(balance==(bms_board_balance_supported()?1u:0u));
    snapshot.mos_temp_x10=1100;step(1);assert(!balance);
    snapshot.mos_temp_x10=650;step(1);bms_error_raise(BMS_ERROR_DSG_SHORT);step(1);assert(!balance);
    bms_error_clear(BMS_ERROR_DSG_SHORT);step(1);balance_fail=1;step(1);assert(bms_error_get(BMS_ERROR_BALANCE));
    balance_fail=0;step(1);assert(!bms_error_get(BMS_ERROR_BALANCE));
    /* 不存在串位的 61001 不得进入 mask；真实有效串异常必须失去资格。 */
    g_bms_report.cell_voltage_mv[0]=61001;step(1);
    assert(!balance && !balance_voltage_trusted());
    voltages(50);step(5); /* 恢复有效电压后重新确认均衡资格。 */
    assert(balance==(bms_board_balance_supported()?1u:0u));
    /* 充电唤醒时 AFE 输出先关闭，重新取得有效样本后恢复均衡资格。 */
    reset();bms_features_stop();bms_features_init();step(5);
    assert(balance==(bms_board_balance_supported()?1u:0u));
    /* 真实短暂测量失效恢复后，连续有效样本重新建立均衡资格。 */
    bms_features_stop();
    step(5);assert(balance==(bms_board_balance_supported()?1u:0u));
    /* 一次超限电压跳变只拒绝当前帧；不使用旧基线无限阻断后续充电。 */
    reset();step(5);voltages(350);step(1);
    assert(!balance && !balance_voltage_trusted());
    step(4);assert(!balance);step(1);
    assert(balance==(bms_board_balance_supported()?1u:0u));
    reset();step(5);snapshot_valid=0;step(1);assert(!heater && !balance_voltage_trusted());
    /* 总线未知时保留最后读回，不能用软件请求 OFF 冒充硬件 OFF。 */
    assert(g_bms_report.balance_bits_low==(bms_board_balance_supported()?1u:0u));
    /* 10/3 mV 测试组：新通道不得借用其他通道的停止门槛。 */
    reset();user.balance_start_mv=3300;user.balance_start_delta_mv=10;user.balance_stop_delta_mv=3;
    voltages(10);g_bms_report.cell_voltage_mv[1]=3405;step(5);
    assert(balance==(bms_board_balance_supported()?1u:0u));
    step(1);assert(balance==(bms_board_balance_supported()?1u:0u));
    g_bms_report.cell_voltage_mv[1]=3410;step(1);
    assert(balance==(bms_board_balance_supported()?3u:0u));
    voltages(4);step(1);assert(balance==(bms_board_balance_supported()?1u:0u));
    voltages(3);step(1);assert(!balance);
    voltages(9);step(1);assert(!balance);
    /* 3300 mV 包含边界；3299 mV 即便压差足够也不能启动。 */
    for(unsigned i=0;i<snapshot.cell_count;i++)g_bms_report.cell_voltage_mv[i]=3290;
    g_bms_report.cell_voltage_mv[0]=3300;g_bms_report.cell_min_mv=3290;
    g_bms_report.cell_max_mv=3300;g_bms_report.cell_delta_mv=10;step(1);
    assert(balance==(bms_board_balance_supported()?1u:0u));
    g_bms_report.cell_voltage_mv[0]=3299;g_bms_report.cell_max_mv=3299;g_bms_report.cell_delta_mv=9;
    step(1);assert(!balance);
    /* ACK 成功但通道未应用：发布回读，报错并撤销继续均衡资格。 */
    reset();balance_dropped_mask=1u;step(5);
    assert(!balance && !g_bms_report.balance_bits_low);
    assert((bms_error_get(BMS_ERROR_BALANCE)!=0)==(bms_board_balance_supported()!=0));
    if (bms_board_balance_supported()) assert(balance_get_calls>0u); /* 错误后仍回读实际残留 */
    assert(!(reported_balance_mask()!=0u));
    balance_dropped_mask=0;step(1);assert(!bms_error_get(BMS_ERROR_BALANCE));
    reset();g_bms_report.cell_voltage_mv[1]=3450;step(5);
    balance_dropped_mask=2u;balance_off_fail=1u;step(1);
    assert(balance==(bms_board_balance_supported()?1u:0u));
    if(bms_board_balance_supported()) assert(s_feature.balance_off_pending); /* 应用失败只能重试OFF。 */
    assert((reported_balance_mask()!=0u)==bms_board_balance_supported()); /* 对外报告仍保留实际残留均衡。 */
    if(bms_board_balance_supported()) {
        unsigned on_calls=balance_on_calls;
        assert(s_feature.balance_off_pending);
        step(1);assert(balance_on_calls==on_calls && s_feature.balance_off_pending);
        balance_off_fail=0;balance_dropped_mask=0;step(1);
        assert(!balance && !s_feature.balance_off_pending && balance_on_calls==on_calls);
        step(1);assert(balance==3u && balance_on_calls>on_calls);
        /* 回读失败：即使 OFF 写入成功也不能假装已确认，报告保留最后读回。 */
        balance_read_fail=1;step(1);assert(s_feature.balance_off_pending);
        assert(g_bms_report.balance_bits_low==3u);
        on_calls=balance_on_calls;step(2);assert(balance_on_calls==on_calls);
        balance_read_fail=0;step(1);assert(!balance && !s_feature.balance_off_pending);
    }
    reset();user.balance_stop_delta_mv=0;step(5);voltages(0);step(1);
    assert(!balance); /* 零停止压差不能把最低电芯或所有电芯打开。 */
#if BMS_BALANCE_MAX_CELLS == 0 || BMS_BALANCE_MAX_CELLS >= BMS_PRODUCT_CELL_COUNT
    /* 无竞争时全部合格串入选，包括最后一串；保留串使用停止迟滞。 */
    reset();voltages(50);
    for(unsigned i=1;i+1<snapshot.cell_count;i++)g_bms_report.cell_voltage_mv[i]=3450;
    step(5);
    uint32_t all_high=(1uL << (snapshot.cell_count-1u))-1u;
    assert(balance==(bms_board_balance_supported()?all_high:0u));
    g_bms_report.cell_voltage_mv[snapshot.cell_count-1u]=3450;
    g_bms_report.cell_voltage_mv[1]=3400;step(1);
    assert(balance==(bms_board_balance_supported()?(((1uL << snapshot.cell_count)-1u)&~2u):0u));
#endif
#if BMS_BALANCE_MAX_CELLS == 2
    /* 两通道配置验证竞争、切换回差及同压顺序。 */
    reset();user.balance_start_mv=3300;user.balance_start_delta_mv=10;user.balance_stop_delta_mv=3;
    voltages(10);g_bms_report.cell_voltage_mv[1]=3420;g_bms_report.cell_voltage_mv[2]=3430;
    g_bms_report.cell_max_mv=3430;g_bms_report.cell_delta_mv=30;step(5);
    assert(balance==(bms_board_balance_supported()?6u:0u));
    g_bms_report.cell_voltage_mv[0]=3427;
    step(1);assert(balance==(bms_board_balance_supported()?6u:0u)); /* 高出恰好 7 mV 不抢占。 */
    g_bms_report.cell_voltage_mv[0]=3428;
    step(1);assert(balance==(bms_board_balance_supported()?5u:0u)); /* 高出 8 mV 替换仍合格的旧串。 */
    g_bms_report.cell_voltage_mv[1]=3428;
    step(1);assert(balance==(bms_board_balance_supported()?5u:0u)); /* 同压不来回切换。 */
    g_bms_report.cell_voltage_mv[1]=3435;g_bms_report.cell_max_mv=3435;g_bms_report.cell_delta_mv=35;
    step(1);assert(balance==(bms_board_balance_supported()?5u:0u)); /* 反向替换也须超过回差。 */
    g_bms_report.cell_voltage_mv[1]=3436;g_bms_report.cell_max_mv=3436;g_bms_report.cell_delta_mv=36;
    step(1);assert(balance==(bms_board_balance_supported()?6u:0u));
    g_bms_report.cell_voltage_mv[1]=3403;g_bms_report.cell_max_mv=3430;g_bms_report.cell_delta_mv=30;
    step(1);assert(balance==(bms_board_balance_supported()?5u:0u)); /* 到停止压差立即退出。 */
    /* 回差带中的旧串可继续，但抢占失去名额后须重新满足启动门槛。 */
    reset();user.balance_start_mv=3300;user.balance_start_delta_mv=10;user.balance_stop_delta_mv=3;
    voltages(10);g_bms_report.cell_voltage_mv[1]=3420;
    g_bms_report.cell_max_mv=3420;g_bms_report.cell_delta_mv=20;step(5);
    assert(balance==(bms_board_balance_supported()?3u:0u));
    g_bms_report.cell_voltage_mv[0]=3404;step(1);
    assert(balance==(bms_board_balance_supported()?3u:0u));
    g_bms_report.cell_voltage_mv[2]=3412;step(1);
    assert(balance==(bms_board_balance_supported()?6u:0u));
    g_bms_report.cell_voltage_mv[2]=3403;step(1);
    assert(balance==(bms_board_balance_supported()?2u:0u)); /* 3404 mV 不能借停止门槛重启。 */
    reset();voltages(50);g_bms_report.cell_voltage_mv[1]=3450;g_bms_report.cell_voltage_mv[2]=3450;
    step(5);assert(balance==(bms_board_balance_supported()?3u:0u)); /* 同压按串号，相邻串可选。 */
#endif
#if BMS_BALANCE_MAX_CELLS == 5 && BMS_PRODUCT_CELL_COUNT == 16
    /* D008 截图回归：先建立旧五串，再发布截图电压，必须让更高电压串抢占。 */
    reset();user.balance_start_mv=3300;user.balance_start_delta_mv=30;user.balance_stop_delta_mv=15;
    for(unsigned i=0;i<snapshot.cell_count;i++)g_bms_report.cell_voltage_mv[i]=3368;
    g_bms_report.cell_voltage_mv[0]=3406;g_bms_report.cell_voltage_mv[1]=3400;
    g_bms_report.cell_voltage_mv[2]=3402;g_bms_report.cell_voltage_mv[12]=3400;
    g_bms_report.cell_voltage_mv[15]=3658;
    g_bms_report.cell_min_mv=3368;g_bms_report.cell_max_mv=3658;g_bms_report.cell_delta_mv=290;
    step(5);assert(balance==(bms_board_balance_supported()?0x9007u:0u));
    const uint16_t screenshot_mv[16]={3406,3381,3402,3403,3368,3368,3428,3460,
                                      3424,3376,3443,3399,3393,3371,3467,3658};
    memcpy(g_bms_report.cell_voltage_mv,screenshot_mv,sizeof(screenshot_mv));
    step(1);assert(balance==(bms_board_balance_supported()?0xC4C0u:0u)); /* 7/8/11/15/16 串；2 串退出。 */
    step(3);assert(balance==(bms_board_balance_supported()?0xC4C0u:0u));
#endif
#if BMS_BALANCE_TEMP_STOP_X10 == 1000 && BMS_BALANCE_TEMP_RESUME_X10 == 900
    reset();step(5);snapshot.mos_temp_x10=1000;step(1);
    assert(!balance && s_feature.balance_temperature_paused);
    snapshot.mos_temp_x10=950;step(1);assert(!balance && s_feature.balance_temperature_paused);
    bms_features_init();assert(s_feature.balance_temperature_paused); /* AFE reset 不解除热暂停。 */
    snapshot.mos_temp_x10=900;step(5);assert(!balance && s_feature.balance_temperature_paused);
    snapshot.mos_temp_valid=0;snapshot.mos_temp_x10=899;step(1);
    assert(!balance && s_feature.balance_temperature_paused);
    snapshot.mos_temp_valid=1;step(1);
    assert(!s_feature.balance_temperature_paused && balance==(bms_board_balance_supported()?1u:0u));
    /* 其他业务门禁阻断期间仍记录过热；即使原温度门槛禁用也有效。 */
    user.balance_enable=0;g_bms_protection_params.charge_otp_recover_x10=0;
    snapshot.battery_temp_max_x10=1000;step(1);assert(s_feature.balance_temperature_paused);
    snapshot.battery_temp_max_x10=950;user.balance_enable=1;step(1);assert(!balance);
    snapshot_valid=0;step(1);assert(s_feature.balance_temperature_paused);
    snapshot_valid=1;snapshot.battery_temp_max_x10=899;step(1);
    assert(!s_feature.balance_temperature_paused); /* 降温可解除，其他资格仍需重新建立。 */
#endif
    reset();snapshot.battery_temp_min_x10=399;step(1);
    if(bms_board_heater_supported()){
        assert(s_feature.heater_state==BMS_HEATER_ARMING && !heater && bms_features_charge_direction_blocked());
        step(3);assert(!heater);g_bms_report.charge_current_a10=0;step(1);assert(heater);
        snapshot.battery_temp_min_x10=449;step(1);assert(heater);
        snapshot.battery_temp_min_x10=450;step(1);assert(!heater);
        snapshot.battery_temp_min_x10=399;step(1);assert(!heater);
        assert(s_feature.heater_state==(BMS_HEATER_REQUIRE_CHARGE_SOURCE_PRESENT?BMS_HEATER_ARMING:BMS_HEATER_IDLE));
        g_bms_report.charge_current_a10=10;step(1);assert(s_feature.heater_state==BMS_HEATER_ARMING);
        snapshot.battery_temp_min_x10=450;step(1);assert(!heater && s_feature.heater_state==BMS_HEATER_IDLE);
        snapshot.battery_temp_min_x10=399;
        g_bms_report.charge_current_a10=10;step(1);g_bms_report.charge_current_a10=0;step(1);assert(heater);
        /* 同时报告充放电时放电优先，直接停止加热和均衡。 */
        g_bms_report.charge_current_a10=10;g_bms_report.discharge_current_a10=1;
        step(1);assert(!heater && !balance);
        if(bms_board_heater_fuse_supported()){
            snapshot.heater_temp_x10=bms_board_heater_off_fault_temp_x10();
            unsigned n=(bms_board_heater_off_fault_confirm_ms()+199u)/200u;
            if(n==0)n=1;
            step(n-1);assert(!fuse_count);step(1);assert(fuse_count==1 && s_feature.heater_fuse_fired);
            step(n+1);assert(fuse_count==1);
            bms_features_init();step(n+1);
            assert(fuse_count==1 && fuse_request && s_feature.heater_fuse_fired && !heater);
        }
    }else{g_bms_report.charge_current_a10=0;step(10);assert(!heater && !fuse_count);}
    /* 超过旧周期的空闲运行不再暂停均衡；仍依赖真实回读和温度资格。 */
    reset();step(5);g_bms_report.charge_current_a10=0;
    for(unsigned i=0;i<1600u;i++) {
        step(1);assert(balance==(bms_board_balance_supported()?1u:0u));
    }
    /* 充电器在位可零流启动；D011持续检查并在拔除/未知时停止。 */
    reset();g_bms_report.charge_current_a10=0;snapshot.battery_temp_min_x10=399;
    charger_present=1;charger_valid=0;step(5);assert(!heater);
    assert(s_feature.heater_state==BMS_HEATER_IDLE);
    charger_valid=1;step(1);
    if(bms_board_heater_supported()) {
        assert(s_feature.heater_state==BMS_HEATER_ARMING);
        unsigned queries=charger_queries;step(1);assert(heater);
        assert(charger_queries==queries+(BMS_HEATER_REQUIRE_CHARGE_SOURCE_PRESENT?1u:0u));
#if BMS_HEATER_REQUIRE_CHARGE_SOURCE_PRESENT
        charger_present=0;step(1);assert(!heater && s_feature.heater_state==BMS_HEATER_IDLE);
        charger_present=1;step(1);step(1);assert(heater);
        charger_valid=0;step(1);assert(!heater && s_feature.heater_state==BMS_HEATER_IDLE);
        /* ARMING中拔除亦不能开启加热。 */
        charger_valid=1;step(1);assert(s_feature.heater_state==BMS_HEATER_ARMING);
        charger_present=0;step(1);assert(!heater && s_feature.heater_state==BMS_HEATER_IDLE);
#endif
    } else assert(!heater && !charger_queries);
    printf("PASS cells=%u heater=%u fuse=%u balance=%u：边界/迟滞/资格/故障/冷启动静置\n",
           (unsigned)BMS_PRODUCT_CELL_COUNT,bms_board_heater_supported(),bms_board_heater_fuse_supported(),bms_board_balance_supported());
    return 0;
}
