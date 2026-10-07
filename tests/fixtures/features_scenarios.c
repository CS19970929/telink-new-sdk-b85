
bms_protection_params_t g_bms_protection_params;
static bms_user_params_t user;
static bms_afe_feature_snapshot_t snapshot;
static uint8_t heater, balance_fail, params_valid=1;
static unsigned fuse_count, ow_started;
static uint32_t balance;
static bms_afe_openwire_result_t ow_result;
static bms_afe_diag_state_t ow_state;
int bms_config_get_user(bms_user_params_t *p){*p=user;return 1;}
uint8_t bms_protection_params_valid(void){return params_valid;}
void bms_board_features_init(void){}
void bms_board_heater_set(uint8_t on){heater=on;}
void bms_board_heater_fuse_fire(void){++fuse_count;}
uint8_t bms_afe_get_charge_source_present(uint8_t *present){*present=0;return 1;}
uint8_t bms_afe_get_feature_snapshot(bms_afe_feature_snapshot_t *s){*s=snapshot;return 1;}
uint8_t bms_afe_set_balance_mask(uint32_t mask){if(balance_fail)return 0;balance=mask;return 1;}
uint8_t bms_afe_get_balance_mask(uint32_t *mask){*mask=balance;return 1;}
uint8_t bms_afe_openwire_start(void){++ow_started;return 1;}
bms_afe_diag_state_t bms_afe_openwire_poll(bms_afe_openwire_result_t *r){*r=ow_result;return ow_state;}
/* FEATURES */
static bms_features_status_t feature_status(void)
{
 bms_features_status_t status;bms_features_get_status(&status);return status;
}

static void voltages(unsigned delta)
{
    for(unsigned i=0;i<BMS_AFE_FEATURE_MAX_CELLS;i++)
        g_bms_report.u16VCell[i]=i<snapshot.cell_count?3400:61001;
    g_bms_report.u16VCell[0]=(uint16_t)(3400+delta);
    g_bms_report.u16VCellMin=3400;
    g_bms_report.u16VCellMax=(uint16_t)(3400+delta);
    g_bms_report.u16VCellDelta=(uint16_t)delta;
}
static void reset(void)
{
    memset(&g_bms_report,0,sizeof(g_bms_report));
    for(unsigned i=0;i<BMS_ERROR_COUNT;i++)bms_error_clear((bms_error_id_t)i);
    memset(&g_bms_protection_params,0,sizeof(g_bms_protection_params));memset(&snapshot,0,sizeof(snapshot));
    params_valid=1;balance=0;balance_fail=0;fuse_count=0;ow_started=0;
    ow_state=BMS_AFE_DIAG_BUSY;memset(&ow_result,0,sizeof(ow_result));
    snapshot.valid=1;snapshot.cell_count=BMS_PRODUCT_CELL_COUNT;
    snapshot.battery_temp_valid=1;snapshot.heater_temp_valid=1;snapshot.mos_temp_valid=1;
    snapshot.battery_temp_min_x10=650;snapshot.battery_temp_max_x10=650;
    snapshot.heater_temp_x10=650;snapshot.mos_temp_x10=650;
    user.heater_enable=1;user.heater_start_x10=400;user.heater_stop_x10=450;
    user.balance_enable=1;user.balance_start_mv=3400;user.balance_start_delta_mv=50;user.balance_stop_delta_mv=30;
    g_bms_protection_params.u16TChgOTp_Rcv=850;g_bms_protection_params.u16TchgUTp_Rcv=450;g_bms_protection_params.u16TmosOTp_Rcv=1100;
    voltages(50);g_bms_report.u16Ichg=10;bms_features_init();
}
static void step(unsigned n){while(n--)bms_features_service();}
int main(void)
{
    reset();step(4);assert(!balance && !feature_status().balance_voltage_trusted);
    step(1);assert(feature_status().balance_voltage_trusted);
    assert(balance==(bms_board_balance_supported()?1u:0u));
    voltages(30);step(1);assert(balance==(bms_board_balance_supported()?1u:0u));
    voltages(29);step(1);assert(!balance);
    voltages(49);step(1);assert(!balance);voltages(50);step(1);
    assert(balance==(bms_board_balance_supported()?1u:0u));
    snapshot.mos_temp_x10=1100;step(1);assert(!balance);
    snapshot.mos_temp_x10=650;step(1);bms_error_raise(BMS_ERROR_DSG_SHORT);step(1);assert(!balance);
    bms_error_clear(BMS_ERROR_DSG_SHORT);step(1);balance_fail=1;step(1);assert(bms_error_get(BMS_ERROR_BALANCE));
    balance_fail=0;step(1);assert(!bms_error_get(BMS_ERROR_BALANCE));
    /* 不存在串位的 61001 不得进入 mask；真实有效串异常必须失去资格。 */
    g_bms_report.u16VCell[0]=61001;step(1);assert(!balance && feature_status().openwire_suspected);
    reset();step(5);snapshot.valid=0;step(1);assert(!heater && !feature_status().balance_voltage_trusted);
    /* 总线未知时保留最后读回，不能用软件请求 OFF 冒充硬件 OFF。 */
    assert(g_bms_report.u16BalanceFlag1==(bms_board_balance_supported()?1u:0u));
    reset();snapshot.battery_temp_min_x10=399;step(1);
    if(bms_board_heater_supported() && bms_board_heater_allowed()){
        assert(feature_status().heater_state==BMS_HEATER_ARMING && !heater && bms_features_charge_direction_blocked());
        step(3);assert(!heater);g_bms_report.u16Ichg=0;step(1);assert(heater);
        snapshot.battery_temp_min_x10=449;step(1);assert(heater);
        snapshot.battery_temp_min_x10=450;step(1);assert(!heater);
        snapshot.battery_temp_min_x10=399;step(1);assert(!heater && feature_status().heater_state==BMS_HEATER_IDLE);
        g_bms_report.u16Ichg=10;step(1);g_bms_report.u16Ichg=0;step(1);assert(heater);
        g_bms_report.u16IDischg=1;step(1);assert(!heater);
        if(bms_board_heater_fuse_supported()){
            snapshot.heater_temp_x10=bms_board_heater_off_fault_temp_x10();
            unsigned n=(bms_board_heater_off_fault_confirm_ms()+199u)/200u;
            if(n==0)n=1;
            step(n-1);assert(!fuse_count);step(1);assert(fuse_count==1 && feature_status().heater_fuse_fired);
            step(n+1);assert(fuse_count==1);
        }
    }else{g_bms_report.u16Ichg=0;step(10);assert(!heater && !fuse_count);}
    /* 真正经过 service 发起诊断，完成帧仍不得用于 SOC。 */
    reset();g_bms_report.u16Ichg=0;step(101);assert(ow_started && feature_status().openwire_active);
    assert(bms_features_outputs_blocked());
    ow_result.valid=1;ow_result.determinate=1;ow_result.open_cell_mask=1;ow_state=BMS_AFE_DIAG_READY;
    step(1);assert(!feature_status().openwire_active && feature_status().openwire_sample_active);
    assert(bms_features_outputs_blocked());
    printf("PASS cells=%u heater=%u fuse=%u balance=%u：边界/迟滞/资格/故障/断线隔离\n",
           (unsigned)BMS_PRODUCT_CELL_COUNT,bms_board_heater_supported(),bms_board_heater_fuse_supported(),bms_board_balance_supported());
    return 0;
}
