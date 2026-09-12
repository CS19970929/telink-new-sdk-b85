#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"

#include "app_config.h"
#include "app.h"
#include "app_att.h"
#include "battery_check.h"
#include "ble_ota.h"
#include "bms_event_log.h"
#include "bms_state.h"
#include "btname_modbus.h"
#include "bus_mux.h"
#include "modbus_rtu.h"
#include "modbus_uart.h"
#include "param.h"
#include "runtime.h"
#include "sh367309_datadeal.h"
#include "sif_send.h"
#include "SocEnhance.h"
#include "soc_kv_store.h"
#include <string.h>

struct stCell_Info g_stCellInfoReport;
volatile struct SYSTEM_ERROR System_ErrFlag;
bool deepsleep_en = false;

#define APP_PM_TICKS_PER_SEC       32000u
#define APP_CONN_LATENCY_NORMAL    99
#define APP_CONN_LATENCY_OTA       0
#define MY_DIRECT_ADV_TIME         2000000u
#define MY_APP_ADV_CHANNEL         BLT_ENABLE_ADV_ALL
#define MY_ADV_INTERVAL_MIN        ADV_INTERVAL_800MS
#define MY_ADV_INTERVAL_MAX        ADV_INTERVAL_800MS
#define MY_RF_POWER_INDEX          RF_POWER_P3dBm
#define BLE_DEVICE_ADDRESS_TYPE    BLE_DEVICE_ADDRESS_PUBLIC

#define RX_FIFO_SIZE 64
#define RX_FIFO_NUM  8
#define TX_FIFO_SIZE 40
#define TX_FIFO_NUM  16

#define MCU_NTC_TABLE_LEN ((UINT16)60u)

extern void WriteProID_Default(void);

_attribute_data_retention_ u8 ota_is_working;
_attribute_data_retention_ u8 blt_rxfifo_b[RX_FIFO_SIZE * RX_FIFO_NUM];
_attribute_data_retention_ my_fifo_t blt_rxfifo = {
    RX_FIFO_SIZE, RX_FIFO_NUM, 0, 0, blt_rxfifo_b,
};
_attribute_data_retention_ u8 blt_txfifo_b[TX_FIFO_SIZE * TX_FIFO_NUM];
_attribute_data_retention_ my_fifo_t blt_txfifo = {
    TX_FIFO_SIZE, TX_FIFO_NUM, 0, 0, blt_txfifo_b,
};

_attribute_data_retention_ static int s_ble_connected;
_attribute_data_retention_ static own_addr_type_t s_own_address_type = OWN_ADDRESS_PUBLIC;
_attribute_data_retention_ static u32 s_bms_sample_tick;
_attribute_data_retention_ static u32 s_bms_1s_tick;

static u8 s_adv_data[31];
static u8 s_scan_rsp[31];

static const UINT16 s_ntc_10k_table[MCU_NTC_TABLE_LEN] = {
    2037, 0,
    1526, 50,
    1161, 100,
    893, 150,
    694, 200,
    544, 250,
    430, 300,
    342, 350,
    275, 400,
    221, 450,
    180, 500,
    147, 550,
    121, 600,
    100, 650,
    83, 700,
    69, 750,
    58, 800,
    49, 850,
    41, 900,
    35, 950,
    30, 1000,
    26, 1050,
    22, 1100,
    19, 1150,
    16, 1200,
    14, 1250,
    12, 1300,
    11, 1350,
    9, 1400,
    8, 1450,
};

typedef struct
{
    u32 last_tick_32k;
    u32 pending_tick_32k;
    u8 ready;
} app_pm_elapsed_ctx_t;

static UINT8 app_charger_active(void)
{
    return (UINT8)!gpio_read(CHG_IN_PIN);
}

static UINT8 app_key_active(void)
{
#ifdef _DI_SWITCH_SYS_ONOFF
    return (UINT8)!gpio_read(SW_PIN);
#else
    return 1u;
#endif
}

static u32 app_pm_take_elapsed_seconds(app_pm_elapsed_ctx_t *ctx)
{
    u32 now_tick_32k;
    u32 elapsed_tick_32k;
    u32 total_tick_32k;
    u32 elapsed_sec;

    if (ctx == NULL)
    {
        return 0u;
    }

    now_tick_32k = pm_get_32k_tick();
    if (!ctx->ready)
    {
        ctx->last_tick_32k = now_tick_32k;
        ctx->ready = 1u;
        return 0u;
    }

    elapsed_tick_32k = now_tick_32k - ctx->last_tick_32k;
    ctx->last_tick_32k = now_tick_32k;
    total_tick_32k = ctx->pending_tick_32k + elapsed_tick_32k;
    elapsed_sec = total_tick_32k / APP_PM_TICKS_PER_SEC;
    ctx->pending_tick_32k = total_tick_32k % APP_PM_TICKS_PER_SEC;
    return elapsed_sec;
}

static void app_event_log_1s_task(void)
{
    bms_event_log_sample_t sample;

    memset(&sample, 0, sizeof(sample));
    sample.sleep = sys_time.low_power_mode ? 1u : 0u;
    sample.balance = ((g_stCellInfoReport.u16BalanceFlag1 != 0u) ||
                      (g_stCellInfoReport.u16BalanceFlag2 != 0u)) ? 1u : 0u;
    sample.heat = SystemStatus.bits.b1Status_Heat ? 1u : 0u;
    sample.cool = SystemStatus.bits.b1Status_Cool ? 1u : 0u;
    sample.vcell_ovp = g_stCellInfoReport.unMdlFault_Third.bits.b1CellOvp ? 1u : 0u;
    sample.vbus_ovp = g_stCellInfoReport.unMdlFault_Third.bits.b1BatOvp ? 1u : 0u;
    sample.chg_ocp = g_stCellInfoReport.unMdlFault_Third.bits.b1IchgOcp ? 1u : 0u;
    sample.vcell_uvp = g_stCellInfoReport.unMdlFault_Third.bits.b1CellUvp ? 1u : 0u;
    sample.vbus_uvp = g_stCellInfoReport.unMdlFault_Third.bits.b1BatUvp ? 1u : 0u;
    sample.dsg_ocp = g_stCellInfoReport.unMdlFault_Third.bits.b1IdischgOcp ? 1u : 0u;
    sample.chg_utp = g_stCellInfoReport.unMdlFault_Third.bits.b1CellChgUtp ? 1u : 0u;
    sample.dsg_utp = g_stCellInfoReport.unMdlFault_Third.bits.b1CellDischgUtp ? 1u : 0u;
    sample.chg_otp = g_stCellInfoReport.unMdlFault_Third.bits.b1CellChgOtp ? 1u : 0u;
    sample.dsg_otp = g_stCellInfoReport.unMdlFault_Third.bits.b1CellDischgOtp ? 1u : 0u;
    sample.vdelta_op = g_stCellInfoReport.unMdlFault_Third.bits.b1VcellDeltaBig ? 1u : 0u;
    sample.afe2_err = System_ERROR_UserCallback(ERROR_STATUS_AFE1) ? 1u : 0u;
    sample.eeprom_err = (System_ERROR_UserCallback(ERROR_STATUS_EEPROM_STORE) ||
                         System_ERROR_UserCallback(ERROR_STATUS_EEPROM_COM)) ? 1u : 0u;
    sample.cbc_err = System_ERROR_UserCallback(ERROR_STATUS_CBC_DSG) ? 1u : 0u;

    bms_event_log_poll_1s(&sample);
}

static int app_enter_deepsleep(u8 need_afe_sleep)
{
    int sleep_status;

    if (app_charger_active())
    {
        return 0;
    }

    bms_event_log_note_sleep();
    if (need_afe_sleep)
    {
        AFE_Sleep();
    }
    Runtime_PrepareForDeepSleep();
    sleep_status = cpu_sleep_wakeup(DEEPSLEEP_MODE, PM_WAKEUP_PAD, 0);
    Runtime_CancelPendingDeepSleep();
    return ((sleep_status & STATUS_GPIO_ERR_NO_ENTER_PM) == 0);
}

static void app_open_ctlc(void)
{
    gpio_write(AFE_CTL_PIN, 1);
}

static void app_close_ctlc(void)
{
    gpio_write(AFE_CTL_PIN, 0);
    gpio_write(MCC_C_PIN, 0);
}

static void app_write_mos_state(UINT8 charge_on, UINT8 discharge_on)
{
    SH367309_Reg_Store.REG_MTP_CONF.bits.CADCON = 1;
    SH367309_Reg_Store.REG_MTP_CONF.bits.CHGMOS = charge_on;
    SH367309_Reg_Store.REG_MTP_CONF.bits.DSGMOS = discharge_on;
    (void)MTPWrite(MTP_CONF, 1, &SH367309_Reg_Store.REG_MTP_CONF.all);

    gpio_write(MCC_C_PIN, charge_on ? 1 : 0);
}

static void app_update_mos_state(void)
{
    uint8_t chg_target = 0u;
    uint8_t dsg_target = 0u;

    if (app_charger_active())
    {
        chg_target = 1u;
        SystemStatus.bits.b1Status_Cool = 1;
    }
    else if (app_key_active())
    {
        SystemStatus.bits.b1Status_Cool = 0;
        dsg_target = 1u;
        if (Runtime_GetMode() == MODE_FACTORY)
        {
            chg_target = 1u;
        }
    }
    else
    {
        SystemStatus.bits.b1Status_Cool = 0;
    }

    if ((chg_target != SystemStatus.bits.b1Status_MOS_CHG) ||
        (dsg_target != SystemStatus.bits.b1Status_MOS_DSG))
    {
        app_write_mos_state(chg_target, dsg_target);
    }
}

static void app_adc_init(void)
{
    adc_init();
    adc_power_on_sar_adc(1);
}

static unsigned int app_adc_read_mv(GPIO_PinTypeDef pin)
{
    adc_base_init(pin);
    return adc_sample_and_get_result();
}

static void app_adc_multi_sample(void)
{
    static u8 mos_state;
    static u32 fuse_hold_count;
#ifdef _UL_RENZHENG_ENABLE_
    static u8 fuse_state;
    static u32 afe_error_fuse_count;
    static u16 ov_delay_count;
#endif
    unsigned int bat_temp_mv;
    unsigned int mos_temp_mv;
    unsigned int vbat_mv;
    u32 bat_temp_r;
    u32 mos_temp_r;

    if (sys_time.low_power_mode)
    {
        mos_state = 0u;
#ifdef _UL_RENZHENG_ENABLE_
        fuse_state = 0u;
        afe_error_fuse_count = 0u;
#endif
        return;
    }

    bat_temp_mv = app_adc_read_mv(ADC_NTC_PIN);
    mos_temp_mv = app_adc_read_mv(ADC_NMOS_PIN);
    vbat_mv = app_adc_read_mv(ADC_VBUS_PIN);

    if (bat_temp_mv >= 3299u) bat_temp_mv = 3299u;
    if (mos_temp_mv >= 3299u) mos_temp_mv = 3299u;

    bat_temp_r = 100u * bat_temp_mv / (3300u - bat_temp_mv);
    mos_temp_r = 100u * mos_temp_mv / (3300u - mos_temp_mv);
    g_stCellInfoReport.u16Temperature[8] =
        GetEndValue(s_ntc_10k_table, MCU_NTC_TABLE_LEN, bat_temp_r);
    g_stCellInfoReport.u16Temperature[9] =
        GetEndValue(s_ntc_10k_table, MCU_NTC_TABLE_LEN, mos_temp_r);

    vbat_mv = vbat_mv * 485u / 15u;
#ifdef DISP_VBAT_AND_TEMP_
    g_stCellInfoReport.u16VCell[29] = (u16)bat_temp_mv;
    g_stCellInfoReport.u16VCell[30] = (u16)mos_temp_mv;
    g_stCellInfoReport.u16VCell[31] = (u16)vbat_mv;
#endif

    switch (mos_state)
    {
    case 0:
        if (g_stCellInfoReport.u16Temperature[9] >= (95 + 40) * 10)
        {
            app_close_ctlc();
            FaultWarnRecord2(MosOTp_Third);
            mos_state = 1u;
        }
        break;
    case 1:
        if (g_stCellInfoReport.u16Temperature[9] <= (75 + 40) * 10)
        {
            app_open_ctlc();
            mos_state = 0u;
        }
        break;
    default:
        mos_state = 0u;
        break;
    }

#ifdef _UL_RENZHENG_ENABLE_
    if (System_ErrFlag.u8ErrFlag_Com_AFE1 == 1u)
    {
        fuse_hold_count = 0u;
        fuse_state = 0u;
        app_close_ctlc();

        if ((vbat_mv >= 4280u * SeriesNum) ||
            (g_stCellInfoReport.u16Temperature[8] >= (85 + 40) * 10))
        {
            if (++afe_error_fuse_count >= 10u)
            {
                afe_error_fuse_count = 0u;
                gpio_write(RF_EN_PIN, 1);
            }
        }
    }
    else
    {
        switch (fuse_state)
        {
        case 0:
            if (g_stCellInfoReport.u16Temperature[8] >= (80 + 40) * 10)
            {
                fuse_state = 1u;
                app_close_ctlc();
                FaultWarnRecord2(CellChgOTp_Third);
                FaultWarnRecord2(CellDsgOTp_Third);
            }

            if ((g_stCellInfoReport.u16VCellMax >= 4270u) &&
                (g_stCellInfoReport.u16VCellMin >= 1000u))
            {
                if (++ov_delay_count >= 15u)
                {
                    ov_delay_count = 0u;
                    fuse_state = 1u;
                    app_close_ctlc();
                    FaultWarnRecord2(CellOvp_Third);
                    FaultWarnRecord2(BatOvp_Third);
                }
            }
            else
            {
                ov_delay_count = 0u;
            }
            break;

        case 1:
            if ((g_stCellInfoReport.u16Temperature[8] < (75 + 40) * 10) &&
                (g_stCellInfoReport.u16VCellMax <= 4150u))
            {
                fuse_state = 0u;
                app_open_ctlc();
            }

            if (((g_stCellInfoReport.u16VCellMax >= 4280u) ||
                 (vbat_mv >= 4280u * SeriesNum) ||
                 (g_stCellInfoReport.u16Temperature[8] >= (85 + 40) * 10)) &&
                g_stCellInfoReport.u16Ichg)
            {
                if (++fuse_hold_count >= 15u)
                {
                    fuse_hold_count = 0u;
                    gpio_write(RF_EN_PIN, 1);
                }
            }
            else
            {
                fuse_hold_count = 0u;
            }
            break;

        default:
            fuse_state = 0u;
            break;
        }
    }
#endif
}

static void app_init_bms_io(void)
{
    gpio_set_func(AFE1_PRO_EN_PIN, AS_GPIO);
    gpio_set_input_en(AFE1_PRO_EN_PIN, 0);
    gpio_set_output_en(AFE1_PRO_EN_PIN, 1);

    gpio_set_func(AFE_CTL_PIN, AS_GPIO);
    gpio_set_input_en(AFE_CTL_PIN, 0);
    gpio_set_output_en(AFE_CTL_PIN, 1);
    app_close_ctlc();

#ifdef _UL_RENZHENG_ENABLE_
    gpio_set_func(RF_EN_PIN, AS_GPIO);
    gpio_set_input_en(RF_EN_PIN, 0);
    gpio_set_output_en(RF_EN_PIN, 1);
    gpio_write(RF_EN_PIN, 0);
#endif

    gpio_set_func(SW_PIN, AS_GPIO);
    gpio_set_input_en(SW_PIN, 1);
    gpio_set_output_en(SW_PIN, 0);

    gpio_set_func(MCC_C_PIN, AS_GPIO);
    gpio_set_input_en(MCC_C_PIN, 0);
    gpio_set_output_en(MCC_C_PIN, 1);
    gpio_write(MCC_C_PIN, 0);

    gpio_set_func(CHG_IN_PIN, AS_GPIO);
    gpio_setup_up_down_resistor(CHG_IN_PIN, PM_PIN_PULLUP_1M);
    gpio_set_input_en(CHG_IN_PIN, 1);
    gpio_set_output_en(CHG_IN_PIN, 0);

    gpio_set_func(CHG_WK_PIN, AS_GPIO);
    gpio_set_input_en(CHG_WK_PIN, 1);
    gpio_set_output_en(CHG_WK_PIN, 0);

    gpio_set_func(ADC_BUSEN_PIN, AS_GPIO);
    gpio_set_input_en(ADC_BUSEN_PIN, 0);
    gpio_set_output_en(ADC_BUSEN_PIN, 1);
    gpio_write(ADC_BUSEN_PIN, 1);

    gpio_set_func(ADC_EN_PIN, AS_GPIO);
    gpio_set_input_en(ADC_EN_PIN, 0);
    gpio_set_output_en(ADC_EN_PIN, 1);
    gpio_write(ADC_EN_PIN, 1);
}

static void app_afe_i2c_init(void)
{
    i2c_gpio_set(I2C_GPIO_GROUP_C0C1);
    i2c_master_init(AFE_ID, (unsigned char)(CLOCK_SYS_CLOCK_HZ / (4u * 100000u)));
}

static void app_build_adv_scan_rsp(void)
{
    u8 index = 0u;

    memset(s_adv_data, 0, sizeof(s_adv_data));
    memset(s_scan_rsp, 0, sizeof(s_scan_rsp));

    s_adv_data[index++] = 0x02;
    s_adv_data[index++] = 0x01;
    s_adv_data[index++] = 0x05;
    s_adv_data[index++] = 0x03;
    s_adv_data[index++] = 0x19;
    s_adv_data[index++] = 0x80;
    s_adv_data[index++] = 0x01;

    /* Preserve deployed advertising UUID bytes, including the historical HID UUID. */
    s_adv_data[index++] = 0x05;
    s_adv_data[index++] = 0x02;
    s_adv_data[index++] = 0x12;
    s_adv_data[index++] = 0x18;
    s_adv_data[index++] = 0x0F;
    s_adv_data[index++] = 0x18;

    index = 0u;
    s_scan_rsp[index++] = (u8)(DEV_NAME_LEN + 1u);
    s_scan_rsp[index++] = 0x09;
    memcpy(&s_scan_rsp[index], DEV_NAME_STR, DEV_NAME_LEN);
}

void app_ble_request_normal_conn_param(void)
{
    if (s_ble_connected)
    {
        bls_l2cap_requestConnParamUpdate(CONN_INTERVAL_10MS,
                                         CONN_INTERVAL_10MS,
                                         APP_CONN_LATENCY_NORMAL,
                                         CONN_TIMEOUT_4S);
    }
}

void app_ble_request_ota_conn_param(void)
{
    if (s_ble_connected)
    {
        bls_l2cap_requestConnParamUpdate(CONN_INTERVAL_10MS,
                                         CONN_INTERVAL_10MS,
                                         APP_CONN_LATENCY_OTA,
                                         CONN_TIMEOUT_4S);
    }
}

void app_ble_restore_normal_power(void)
{
#if (BLE_APP_PM_ENABLE)
    bls_pm_setManualLatency(bls_ll_getConnectionLatency());
#endif
    app_ble_request_normal_conn_param();
}

static void app_task_sleep_enter(u8 event, u8 *data, int len)
{
    (void)event;
    (void)data;
    (void)len;

    if ((blc_ll_getCurrentState() == BLS_LINK_STATE_CONN) &&
        ((u32)(bls_pm_getSystemWakeupTick() - clock_time()) > 80u * SYSTEM_TIMER_TICK_1MS))
    {
        bls_pm_setWakeupSource(PM_WAKEUP_PAD);
    }
}

#if (BLE_APP_SECURITY_ENABLE)
static void app_switch_to_undirected_adv(u8 event, u8 *data, int len)
{
    (void)event;
    (void)data;
    (void)len;

    bls_ll_setAdvParam(MY_ADV_INTERVAL_MIN, MY_ADV_INTERVAL_MAX,
                       ADV_TYPE_CONNECTABLE_UNDIRECTED, s_own_address_type,
                       0, NULL, MY_APP_ADV_CHANNEL, ADV_FP_NONE);
    blc_ll_clearResolvingList();
    bls_ll_setAdvEnable(BLC_ADV_ENABLE);
}
#endif

static void app_task_connect(u8 event, u8 *data, int len)
{
    tlk_contr_evt_connect_t *connect_event = (tlk_contr_evt_connect_t *)data;

    (void)event;
    (void)len;
    s_ble_connected = 1;
    tlkapi_send_string_data(APP_CONTR_EVENT_LOG_EN,
                            "[APP][EVT] connect, intA & advA:",
                            connect_event->initA, 12);
    app_ble_request_normal_conn_param();
}

static void app_task_terminate(u8 event, u8 *data, int len)
{
    tlk_contr_evt_terminate_t *terminate_event = (tlk_contr_evt_terminate_t *)data;

    (void)event;
    (void)len;
    s_ble_connected = 0;
    tlkapi_printf(APP_CONTR_EVENT_LOG_EN,
                  "[APP][EVT] disconnect, reason 0x%x\n",
                  terminate_event->terminate_reason);
}

static void app_task_suspend_exit(u8 event, u8 *data, int len)
{
    (void)event;
    (void)data;
    (void)len;
    rf_set_power_level_index(MY_RF_POWER_INDEX);
}

static void app_task_dle_exchange(u8 event, u8 *data, int len)
{
    tlk_contr_evt_dataLenExg_t *dle = (tlk_contr_evt_dataLenExg_t *)data;

    (void)event;
    (void)len;
    tlkapi_send_string_data(APP_CONTR_EVENT_LOG_EN,
                            "[APP][EVT] DLE exchange",
                            &dle->connEffectiveMaxRxOctets, 4);
}

static int app_host_event_callback(u32 host_event, u8 *data, int len)
{
    u8 event = (u8)(host_event & 0xFFu);

    (void)len;
    switch (event)
    {
    case GAP_EVT_SMP_PAIRING_BEGIN:
        tlkapi_send_string_data(APP_SMP_LOG_EN, "[APP][SMP] pairing begin:",
                                data, sizeof(gap_smp_pairingBeginEvt_t));
        break;
    case GAP_EVT_SMP_PAIRING_SUCCESS:
        tlkapi_send_string_data(APP_SMP_LOG_EN, "[APP][SMP] pairing success:",
                                data, sizeof(gap_smp_pairingSuccessEvt_t));
        break;
    case GAP_EVT_SMP_PAIRING_FAIL:
        tlkapi_send_string_data(APP_SMP_LOG_EN, "[APP][SMP] pairing fail:",
                                data, sizeof(gap_smp_pairingFailEvt_t));
        break;
    case GAP_EVT_ATT_EXCHANGE_MTU:
        tlkapi_send_string_data(APP_HOST_EVENT_LOG_EN, "[APP][MTU] mtu exchange",
                                data, sizeof(gap_gatt_mtuSizeExchangeEvt_t));
        break;
    default:
        break;
    }

    return 0;
}

static void app_power_process(void)
{
    static u16 key_off_sleep_s;
    static u32 very_low_sleep_s;
    static u32 low_sleep_s;
    static u32 normal_low_sleep_s;
    static u32 afe_error_sleep_s;
    static app_pm_elapsed_ctx_t elapsed_ctx;
    u32 elapsed_s = app_pm_take_elapsed_seconds(&elapsed_ctx);

    if (elapsed_s != 0u)
    {
#ifdef _DI_SWITCH_SYS_ONOFF
        if (!app_charger_active() && !app_key_active())
        {
            key_off_sleep_s = (u16)(key_off_sleep_s + elapsed_s);
            if (key_off_sleep_s >= 3u)
            {
                key_off_sleep_s = 0u;
                cpu_set_gpio_wakeup(SW_PIN, Level_Low, 1);
                (void)app_enter_deepsleep(1u);
            }
        }
        else
        {
            key_off_sleep_s = 0u;
        }
#endif

        if (g_stCellInfoReport.u16VCellMin < 2550u)
        {
            low_sleep_s = 0u;
            normal_low_sleep_s = 0u;
            afe_error_sleep_s = 0u;
            very_low_sleep_s += elapsed_s;
            if (very_low_sleep_s >= 60u * 60u)
            {
                very_low_sleep_s = 0u;
                (void)app_enter_deepsleep(1u);
            }
        }
        else if (g_stCellInfoReport.u16VCellMin < __SLEEP_VLOW__)
        {
            very_low_sleep_s = 0u;
            normal_low_sleep_s = 0u;
            afe_error_sleep_s = 0u;
            low_sleep_s += elapsed_s;
            if (low_sleep_s >= __SLEEP_TIMEVLOW__)
            {
                low_sleep_s = 0u;
                (void)app_enter_deepsleep(1u);
            }
        }
        else if ((g_stCellInfoReport.u16VCellMin < __SLEEP_VNORMAL__) &&
                 !g_stCellInfoReport.u16Ichg)
        {
            very_low_sleep_s = 0u;
            low_sleep_s = 0u;
            afe_error_sleep_s = 0u;
            normal_low_sleep_s += elapsed_s;
            if (normal_low_sleep_s >= __SLEEP_TIMENORMAL__)
            {
                normal_low_sleep_s = 0u;
                (void)app_enter_deepsleep(1u);
            }
        }
        else if (System_ErrFlag.u8ErrFlag_Com_AFE1 == 1u)
        {
            very_low_sleep_s = 0u;
            low_sleep_s = 0u;
            normal_low_sleep_s = 0u;
            afe_error_sleep_s += elapsed_s;
            if (afe_error_sleep_s >= 60u * 30u)
            {
                afe_error_sleep_s = 0u;
                cpu_set_gpio_wakeup(SW_PIN, Level_Low, 1);
                (void)app_enter_deepsleep(1u);
            }
        }
        else
        {
            very_low_sleep_s = 0u;
            low_sleep_s = 0u;
            normal_low_sleep_s = 0u;
            afe_error_sleep_s = 0u;
        }
    }

    bls_pm_setSuspendMask(SUSPEND_ADV | SUSPEND_CONN);
    sys_time.low_power_mode = true;

#if (BLE_OTA_SERVER_ENABLE)
    if (ota_is_working)
    {
        bls_pm_setManualLatency(0);
    }
#endif

    if (app_charger_active() ||
        (bus_mux_get_state() != BUS_STATE_OWC_IDLE) ||
        g_stCellInfoReport.u16IDischg ||
        ota_is_working)
    {
        sys_time.low_power_mode = false;
        bls_pm_setSuspendMask(SUSPEND_DISABLE);
    }
    else if (s_ble_connected)
    {
        sys_time.low_power_mode = false;
    }
}

static void app_ble_init(void)
{
    u8 mac_public[6];
    u8 mac_random_static[6];
    u8 adv_status = BLE_SUCCESS;
    u8 i;

    blc_initMacAddress(flash_sector_mac_address, mac_public, mac_random_static);
    tlkapi_send_string_data(APP_LOG_EN, "[APP][INI]Public Address", mac_public, 6);

#if (BLE_DEVICE_ADDRESS_TYPE == BLE_DEVICE_ADDRESS_PUBLIC)
    s_own_address_type = OWN_ADDRESS_PUBLIC;
#elif (BLE_DEVICE_ADDRESS_TYPE == BLE_DEVICE_ADDRESS_RANDOM_STATIC)
    s_own_address_type = OWN_ADDRESS_RANDOM;
    blc_ll_setRandomAddr(mac_random_static);
#endif

    for (i = 0u; i < 6u; ++i)
    {
        g_stCellInfoReport.mac_public[i] = mac_public[5u - i];
    }

    blc_ll_initBasicMCU();
    blc_ll_initStandby_module(mac_public);
    blc_ll_initAdvertising_module(mac_public);
    blc_ll_initConnection_module();
    blc_ll_initSlaveRole_module();

    blc_gap_peripheral_init();
    blc_l2cap_register_handler(blc_l2cap_packet_receive);
    my_att_init();
    blc_att_setRxMtuSize(MTU_SIZE_SETTING);

#if (BLE_APP_SECURITY_ENABLE)
    bls_smp_configPairingSecurityInfoStorageAddr(flash_sector_smp_storage);
    blc_smp_peripheral_init();
    bls_smp_configSecurityRequestSending(SecReq_IMM_SEND, SecReq_PEND_SEND, 1000);
#else
    blc_smp_setSecurityLevel(No_Security);
#endif

    blc_gap_registerHostEventHandler(app_host_event_callback);
    blc_gap_setEventMask(GAP_EVT_MASK_SMP_PAIRING_BEGIN |
                         GAP_EVT_MASK_SMP_PAIRING_SUCCESS |
                         GAP_EVT_MASK_SMP_PAIRING_FAIL |
                         GAP_EVT_MASK_ATT_EXCHANGE_MTU);

#if (BLE_OTA_SERVER_ENABLE)
#if (UART_PRINT_DEBUG_ENABLE)
    blc_debug_addStackLog(STK_LOG_OTA_FLOW);
#endif
    blc_ota_initOtaServer_module();
    blc_ota_setOtaProcessTimeout(APP_OTA_PROCESS_TIMEOUT_S);
    blc_ota_setOtaDataPacketTimeout(APP_OTA_DATA_PACKET_TIMEOUT_S);
    blc_ota_registerOtaStartCmdCb(app_enter_ota_mode);
    blc_ota_registerOtaResultIndicationCb(app_ota_end_result);
#endif

#if (BLE_APP_SECURITY_ENABLE)
    {
        u8 bond_number = blc_smp_param_getCurrentBondingDeviceNumber();
        smp_param_save_t bond_info;

        if (bond_number)
        {
            bls_smp_param_loadByIndex((u8)(bond_number - 1u), &bond_info);
            adv_status = bls_ll_setAdvParam(MY_ADV_INTERVAL_MIN, MY_ADV_INTERVAL_MAX,
                                            ADV_TYPE_CONNECTABLE_DIRECTED_LOW_DUTY,
                                            s_own_address_type,
                                            bond_info.peer_addr_type,
                                            bond_info.peer_addr,
                                            MY_APP_ADV_CHANNEL,
                                            ADV_FP_NONE);
            if (blc_app_isIrkValid(bond_info.peer_irk))
            {
                blc_ll_addDeviceToResolvingList(bond_info.peer_id_adrType,
                                                bond_info.peer_id_addr,
                                                bond_info.peer_irk, NULL);
                blc_ll_setAddressResolutionEnable(1);
            }
            bls_ll_setAdvDuration(MY_DIRECT_ADV_TIME, 1);
            bls_app_registerEventCallback(BLT_EV_FLAG_ADV_DURATION_TIMEOUT,
                                          &app_switch_to_undirected_adv);
        }
        else
        {
            adv_status = bls_ll_setAdvParam(MY_ADV_INTERVAL_MIN, MY_ADV_INTERVAL_MAX,
                                            ADV_TYPE_CONNECTABLE_UNDIRECTED,
                                            s_own_address_type,
                                            0, NULL,
                                            MY_APP_ADV_CHANNEL,
                                            ADV_FP_NONE);
        }
    }
#else
    adv_status = bls_ll_setAdvParam(MY_ADV_INTERVAL_MIN, MY_ADV_INTERVAL_MAX,
                                    ADV_TYPE_CONNECTABLE_UNDIRECTED,
                                    s_own_address_type,
                                    0, NULL,
                                    MY_APP_ADV_CHANNEL,
                                    ADV_FP_NONE);
#endif

    if (adv_status != BLE_SUCCESS)
    {
        tlkapi_printf(APP_LOG_EN, "[APP][INI] ADV parameters error 0x%x!!!\n", adv_status);
    }

    app_build_adv_scan_rsp();
    bls_ll_setAdvData(s_adv_data, sizeof(s_adv_data));
    bls_ll_setScanRspData(s_scan_rsp, sizeof(s_scan_rsp));
    bls_ll_setAdvEnable(BLC_ADV_ENABLE);

    rf_set_power_level_index(MY_RF_POWER_INDEX);
    bls_app_registerEventCallback(BLT_EV_FLAG_CONNECT, &app_task_connect);
    bls_app_registerEventCallback(BLT_EV_FLAG_TERMINATE, &app_task_terminate);
    bls_app_registerEventCallback(BLT_EV_FLAG_SUSPEND_EXIT, &app_task_suspend_exit);
    bls_app_registerEventCallback(BLT_EV_FLAG_DATA_LENGTH_EXCHANGE, &app_task_dle_exchange);

#if (BLE_APP_PM_ENABLE)
    blc_ll_initPowerManagement_module();
#if (PM_DEEPSLEEP_RETENTION_ENABLE)
    blc_app_setDeepsleepRetentionSramSize();
    bls_pm_setSuspendMask(SUSPEND_ADV | DEEPSLEEP_RETENTION_ADV |
                          SUSPEND_CONN | DEEPSLEEP_RETENTION_CONN);
    blc_pm_setDeepsleepRetentionThreshold(95, 95);
#if (MCU_CORE_TYPE == MCU_CORE_825x || MCU_CORE_TYPE == MCU_CORE_827x)
    blc_pm_setDeepsleepRetentionEarlyWakeupTiming(270);
#else
    blc_pm_setDeepsleepRetentionEarlyWakeupTiming(340);
#endif
#else
    bls_pm_setSuspendMask(SUSPEND_ADV | SUSPEND_CONN);
#endif
    bls_app_registerEventCallback(BLT_EV_FLAG_SUSPEND_ENTER, &app_task_sleep_enter);
#else
    bls_pm_setSuspendMask(SUSPEND_DISABLE);
#endif

    blc_app_checkControllerHostInitialization();
}

static void app_bms_init(void)
{
    soc_kv_data_t soc_data;

    app_init_bms_io();
    LoadParam();
    Param_UpgradeReset_Apply();
    (void)bms_event_log_init();

    app_afe_i2c_init();
    WaitMs(100);

    AFE_Reset();
    (void)AFE_IsReady();
    SH367309_UpdataAfeConfig();

    app_adc_init();
    cpu_set_gpio_wakeup(CHG_IN_PIN, Level_Low, 1);

    App_AFEGet();
    (void)soc_kv_store_init();
    soc_data = soc_kv_store_get();
    soc_param_lib_init(&soc_data);

    sif_timer_init();
    bus_mux_init();
    btname_init();
    bms_event_log_note_startup();
    Runtime_Init();
    app_update_mos_state();
    WriteProID_Default();
    app_open_ctlc();
}

_attribute_no_inline_ void user_init_normal(void)
{
#if (MCU_CORE_TYPE == MCU_CORE_825x || MCU_CORE_TYPE == MCU_CORE_827x)
    random_generator_init();
#endif

#if (UART_PRINT_DEBUG_ENABLE)
    tlkapi_debug_init();
    blc_debug_enableStackLog(STK_LOG_DISABLE);
#endif

    blc_readFlashSize_autoConfigCustomFlashSector();
    blc_app_loadCustomizedParameters_normal();

#if (APP_BATT_CHECK_ENABLE)
    user_battery_power_check(VBAT_DEEP_THRES_MV);
#endif

#if (APP_FLASH_PROTECTION_ENABLE)
    app_flash_protection_operation(FLASH_OP_EVT_APP_INITIALIZATION, 0, 0);
    blc_appRegisterStackFlashOperationCallback(app_flash_protection_operation);
#endif

    app_ble_init();
    app_bms_init();
    tlkapi_printf(APP_LOG_EN, "[APP][INI] BMS init\n");
}

_attribute_ram_code_ void user_init_deepRetn(void)
{
#if (PM_DEEPSLEEP_RETENTION_ENABLE)
    blc_app_loadCustomizedParameters_deepRetn();
    blc_ll_initBasicMCU();
    rf_set_power_level_index(MY_RF_POWER_INDEX);
    blc_ll_recoverDeepRetention();
#if (APP_BATT_CHECK_ENABLE)
    battery_clear_adc_setting_flag();
#endif
    DBG_CHN0_HIGH;
    irq_enable();
#endif
}

#if (APP_FLASH_PROTECTION_ENABLE)
_attribute_data_retention_ u16 flash_lockBlock_cmd;
_attribute_data_retention_ static u8 s_flash_stack_session_active;

int app_flash_lock_restore_enabled(void)
{
    return (s_flash_stack_session_active == 0u);
}

void app_flash_protection_operation(u8 flash_op_evt, u32 op_addr_begin, u32 op_addr_end)
{
    (void)op_addr_begin;
    (void)op_addr_end;

    if (flash_op_evt == FLASH_OP_EVT_APP_INITIALIZATION)
    {
        u32 app_lock_block = 0u;

        s_flash_stack_session_active = 0u;
        flash_protection_init();

#if (BLE_OTA_SERVER_ENABLE)
        {
            u32 multi_boot_address = blc_ota_getCurrentUsedMultipleBootAddress();
            if (multi_boot_address == MULTI_BOOT_ADDR_0x20000)
            {
                app_lock_block = FLASH_LOCK_FW_LOW_256K;
            }
            else if (multi_boot_address == MULTI_BOOT_ADDR_0x40000)
            {
                app_lock_block = FLASH_LOCK_FW_LOW_512K;
            }
#if (MCU_CORE_TYPE == MCU_CORE_827x)
            else if (multi_boot_address == MULTI_BOOT_ADDR_0x80000)
            {
                if (blc_flash_capacity < FLASH_SIZE_1M)
                {
                    blc_flashProt.init_err = 1;
                }
                else
                {
                    app_lock_block = FLASH_LOCK_FW_LOW_1M;
                }
            }
#endif
        }
#else
        app_lock_block = FLASH_LOCK_FW_LOW_256K;
#endif

        flash_lockBlock_cmd = flash_change_app_lock_block_to_flash_lock_block(app_lock_block);
        if (blc_flashProt.init_err)
        {
            tlkapi_printf(APP_FLASH_PROT_LOG_EN,
                          "[FLASH][PROT] flash protection initialization error!!!\n");
        }
        flash_lock(flash_lockBlock_cmd);
        return;
    }

#if (BLE_OTA_SERVER_ENABLE)
    if ((flash_op_evt == FLASH_OP_EVT_STACK_OTA_CLEAR_OLD_FW_BEGIN) ||
        (flash_op_evt == FLASH_OP_EVT_STACK_OTA_WRITE_NEW_FW_BEGIN))
    {
        s_flash_stack_session_active = 1u;
        flash_unlock();
    }
    else if ((flash_op_evt == FLASH_OP_EVT_STACK_OTA_CLEAR_OLD_FW_END) ||
             (flash_op_evt == FLASH_OP_EVT_STACK_OTA_WRITE_NEW_FW_END))
    {
        s_flash_stack_session_active = 0u;
        flash_lock(flash_lockBlock_cmd);
    }
#endif
}
#endif

_attribute_no_inline_ void main_loop(void)
{
    blt_sdk_main_loop();
    Runtime_Poll();

#if (APP_BATT_CHECK_ENABLE)
    if (battery_get_detect_enable() && clock_time_exceed(lowBattDet_tick, 500000u))
    {
        lowBattDet_tick = clock_time();
        user_battery_power_check(VBAT_DEEP_THRES_MV);
    }
#endif

    if (clock_time_exceed(s_bms_sample_tick, 200000u))
    {
        s_bms_sample_tick = clock_time();
        App_AFEGet();
        APP_SOC_IntEnhance_Ctrl();
        app_update_mos_state();
    }

    if (clock_time_exceed(s_bms_1s_tick, 1000000u))
    {
        s_bms_1s_tick = clock_time();
        app_adc_multi_sample();
        app_event_log_1s_task();
    }

    bus_mux_task();
#ifdef _FUNC_UART_
    main_loop_modbus();
#endif

    soc_kv_store_update_and_log_if_changed(SOC_Calculate_Element.u8SOC_Now,
                                           SOC_Calculate_Element.u8DSG_SOC_Int,
                                           SOC_Calculate_Element.u32Cycle_times);
    app_power_process();
}
