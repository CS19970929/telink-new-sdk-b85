#include "bms_afe_hw_access.h"
#include "bms_diag.h"
#include "bms_storage_platform.h"
/********************************************************************************************************
 * @file    app.c
 *
 * @brief   This is the source file for BLE SDK
 *
 * @author  BLE GROUP
 * @date    06,2022
 *
 * @par     Copyright (c) 2022, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
 *
 *          Licensed under the Apache License, Version 2.0 (the "License");
 *          you may not use this file except in compliance with the License.
 *          You may obtain a copy of the License at
 *
 *              http://www.apache.org/licenses/LICENSE-2.0
 *
 *          Unless required by applicable law or agreed to in writing, software
 *          distributed under the License is distributed on an "AS IS" BASIS,
 *          WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *          See the License for the specific language governing permissions and
 *          limitations under the License.
 *
 *******************************************************************************************************/
#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"

#include "app_config.h"
#include "app.h"
#include "ble_ota.h"
#include "app_att.h"

#include "modbus_uart.h"
#include "modbus_rtu.h"

#include "bms_afe.h"
#include "bms_error.h"
#include "bms_state.h"

#include "SocEnhance.h"
#include "bms_event_log.h"
#include "soc_kv_store.h"
#include "sif_send.h"
// #include "nvm_flash.h"
#include "bus_mux.h"
#include "btname_modbus.h"
#include "runtime.h"
#include <string.h>

extern void LoadParam(void);
extern void Param_UpgradeReset_Apply(void);

Time_T sys_time;
bool deepsleep_en = false;
// nvm_cfg_t nvm_cfg;

#define APP_PM_TICKS_PER_SEC 32000u
#define APP_SAMPLE_PERIOD_US 200000u
#define APP_SUSPEND_EXIT_CURRENT_MA 500
#define APP_POWER_OFF_RETRY_SECONDS 5u
#define APP_ACC_HIGH_STABLE_TICKS (APP_PM_TICKS_PER_SEC / 5u)
extern int device_in_connection_state;
static u8 s_power_off_committed;
static u8 s_power_off_retry_ready;
static u32 s_power_off_retry_tick;
static u8 s_acc_high_seen, s_acc_sleep_committed, s_acc_retry_ready, s_acc_disconnect_sent;
static u32 s_acc_high_tick, s_acc_retry_tick;
static u32 s_sample_tick;
static volatile u8 s_sample_due;

static uint8_t app_get_fresh_measurements(bms_afe_aux_measurements_t *m)
{
    if (!bms_afe_get_aux_measurements(m)) return 0u;
    return ((u32)(pm_get_32k_tick() - m->sample_tick_32k) <=
            BMS_SOC_MAX_SAMPLE_GAP_32K) ? 1u : 0u;
}

/* SDK low-power callback only schedules work. I2C, SOC and Flash stay in the
 * cooperative main loop, including when invoked from a suspend callback. */
static void app_sample_wakeup(int type)
{
    (void)type;
    s_sample_due = 1u;
}

static void app_schedule_sample_wakeup(void)
{
    /* Fault recovery needs consecutive samples even during the power test.
     * Query owned RAM state, never infer a target from AFE driver feedback. */
    if (BMS_APP_SAMPLE_WAKEUP_ENABLE || bms_afe_current_recovery_pending())
        bls_pm_setAppWakeupLowPower(s_sample_tick + APP_SAMPLE_PERIOD_US * SYSTEM_TIMER_TICK_1US, 1u);
    else
        bls_pm_setAppWakeupLowPower(0u, 0u);
}

typedef struct
{
	u32 last_tick_32k;
	u32 pending_tick_32k;
	u8 ready;
} app_pm_elapsed_ctx_t;

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
	u8 eeprom_err;
    _attribute_data_retention_ static u32 event_log_tick = 0;

    if (!clock_time_exceed(event_log_tick, 1000 * 1000)) return;
    event_log_tick = clock_time();

	memset(&sample, 0, sizeof(sample));
	sample.sleep = sys_time.low_power_mode ? 1u : 0u;
	sample.balance = ((g_stCellInfoReport.u16BalanceFlag1 != 0u) || (g_stCellInfoReport.u16BalanceFlag2 != 0u)) ? 1u : 0u;
	sample.heat = g_bms_system_status.bits.b1Status_Heat ? 1u : 0u;
	sample.cool = g_bms_system_status.bits.b1Status_Cool ? 1u : 0u;

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

	sample.afe2_err = bms_error_get(BMS_ERROR_AFE1) ? 1u : 0u;
	eeprom_err = (bms_error_get(BMS_ERROR_EEPROM_STORE) ||
				  bms_error_get(BMS_ERROR_EEPROM_COM))
					 ? 1u
					 : 0u;
	sample.eeprom_err = eeprom_err;
	sample.cbc_err = bms_error_get(BMS_ERROR_CBC_DSG) ? 1u : 0u;

	bms_event_log_poll_1s(&sample);
}

static int app_enter_power_off(void)
{
    bms_afe_aux_measurements_t m;
    u32 now = pm_get_32k_tick();

    if (s_power_off_committed || ota_is_working ||
        !app_flash_lock_restore_enabled() ||
        BUS_STATE_OWC_IDLE != bus_mux_get_state()) return 0;
    /* An explicit sleep command does not require low voltage, a disconnected
     * BLE link or valid current sampling. Drain its response before cutting
     * power; automatic low-voltage shutdown retains its original qualifiers. */
    if (deepsleep_en)
    {
        if (device_in_connection_state && blc_ll_getTxFifoNumber() != 0u) return 0;
    }
    else if (device_in_connection_state || !app_get_fresh_measurements(&m)) return 0;
    if (s_power_off_retry_ready &&
        (u32)(now - s_power_off_retry_tick) <
            APP_POWER_OFF_RETRY_SECONDS * APP_PM_TICKS_PER_SEC) return 0;
    s_power_off_retry_ready = 1u;
    s_power_off_retry_tick = now;

    /* No Flash operation can be deferred until after PC4 drops. The sleep
     * event records the attempt; a failed shutdown never cuts the supply. */
    if (!soc_kv_store_write_all(SOC_Calculate_Element.u8SOC_Now,
                               SOC_Calculate_Element.u8DSG_SOC_Int,
                               SOC_Calculate_Element.u32Cycle_times) ||
        !bms_event_log_note_sleep()) return 0;
    if (!bms_afe_enter_shutdown()) return 0;

    s_power_off_committed = 1u;
    bls_pm_setAppWakeupLowPower(0u, 0u);
    sys_time.low_power_mode = true;
    gpio_write(MCU_LDO_PIN, 0u); /* final hardware action: whole MCU loses power */
    return 1;
}

/* ACC sleep keeps PC4 high. Deep sleep wakes through a full normal boot;
 * never resume sampling against the intentionally shutdown AFE. */
static void app_acc_sleep_hold(void)
{
    if (!gpio_read(ACC_MCU_PIN)) {
        start_reboot();
        return;
    }
    cpu_set_gpio_wakeup(ACC_MCU_PIN, Level_Low, 1);
    cpu_sleep_wakeup(DEEPSLEEP_MODE, PM_WAKEUP_PAD, 0u);
    /* PAD may become active between the check and sleep entry. */
    if (!gpio_read(ACC_MCU_PIN)) start_reboot();
}

static int app_acc_sleep_requested(void)
{
    u32 now = pm_get_32k_tick();
    if (!gpio_read(ACC_MCU_PIN)) {
        s_acc_high_seen = s_acc_retry_ready = s_acc_disconnect_sent = 0u;
        return 0;
    }
    if (!s_acc_high_seen) {
        s_acc_high_seen = 1u; s_acc_high_tick = now;
    }
    return (u32)(now - s_acc_high_tick) >= APP_ACC_HIGH_STABLE_TICKS;
}

static int app_enter_acc_sleep(void)
{
    u32 now = pm_get_32k_tick();
    if (!gpio_read(ACC_MCU_PIN) || ota_is_working ||
        !app_flash_lock_restore_enabled() || BUS_STATE_OWC_IDLE != bus_mux_get_state()) return 0;
    if (device_in_connection_state) {
        if (!s_acc_disconnect_sent && blc_ll_getTxFifoNumber() == 0u &&
            bls_ll_terminateConnection(HCI_ERR_REMOTE_USER_TERM_CONN) == BLE_SUCCESS)
            s_acc_disconnect_sent = 1u;
        return 0;
    }
    if (s_acc_retry_ready && (u32)(now - s_acc_retry_tick) <
        APP_POWER_OFF_RETRY_SECONDS * APP_PM_TICKS_PER_SEC) return 0;
    s_acc_retry_ready = 1u; s_acc_retry_tick = now;
    if (!soc_kv_store_write_all(SOC_Calculate_Element.u8SOC_Now,
                               SOC_Calculate_Element.u8DSG_SOC_Int,
                               SOC_Calculate_Element.u32Cycle_times) ||
        !bms_event_log_note_sleep()) return 0;
    if (!gpio_read(ACC_MCU_PIN)) return 0;
    if (bls_ll_setAdvEnable(BLC_ADV_DISABLE) != BLE_SUCCESS) return 0;
    if (!bms_afe_enter_shutdown()) {
        bls_ll_setAdvEnable(BLC_ADV_ENABLE);
        return 0;
    }
    s_acc_sleep_committed = 1u;
    bls_pm_setSuspendMask(SUSPEND_DISABLE);
    bls_pm_setAppWakeupLowPower(0u, 0u);
    sys_time.low_power_mode = true;
    gpio_write(MCU_LDO_PIN, 1u);
    cpu_set_gpio_wakeup(CHG_IN_PIN, Level_Low, 0);
    app_acc_sleep_hold();
    return 1;
}

#define ADV_IDLE_ENTER_DEEP_TIME 60	 // 60 s
#define CONN_IDLE_ENTER_DEEP_TIME 60 // 60 s

#define APP_CONN_LATENCY_NORMAL 99
#define APP_CONN_LATENCY_OTA 0

#define MY_DIRECT_ADV_TIME 2000000

#define MY_APP_ADV_CHANNEL BLT_ENABLE_ADV_ALL
#define MY_ADV_INTERVAL_MIN ADV_INTERVAL_800MS
#define MY_ADV_INTERVAL_MAX ADV_INTERVAL_800MS
// #define 	MY_ADV_INTERVAL_MIN					ADV_INTERVAL_500MS
// #define 	MY_ADV_INTERVAL_MAX					ADV_INTERVAL_500MS
// #define 	MY_ADV_INTERVAL_MIN					ADV_INTERVAL_30MS
// #define 	MY_ADV_INTERVAL_MAX					ADV_INTERVAL_30MS

#define MY_RF_POWER_INDEX RF_POWER_P3dBm

#define BLE_DEVICE_ADDRESS_TYPE BLE_DEVICE_ADDRESS_PUBLIC

_attribute_data_retention_ u8 ota_is_working = 0;
_attribute_data_retention_ own_addr_type_t app_own_address_type = OWN_ADDRESS_PUBLIC;

/**
 * @brief      LinkLayer RX & TX FIFO configuration
 */
/* CAL_LL_ACL_RX_BUF_SIZE(maxRxOct): maxRxOct + 22, then 16 byte align */
#define RX_FIFO_SIZE 64
/* must be: 2^n, (power of 2);at least 4; recommended value: 4, 8, 16 */
#define RX_FIFO_NUM 8

/* CAL_LL_ACL_TX_BUF_SIZE(maxTxOct): maxTxOct + 10, then 4 byte align */
#define TX_FIFO_SIZE 40
/* must be: (2^n), (power of 2); at least 8; recommended value: 8, 16, 32, other value not allowed. */
#define TX_FIFO_NUM 16

_attribute_data_retention_ u8 blt_rxfifo_b[RX_FIFO_SIZE * RX_FIFO_NUM] = {0};
_attribute_data_retention_ my_fifo_t blt_rxfifo = {
	RX_FIFO_SIZE,
	RX_FIFO_NUM,
	0,
	0,
	blt_rxfifo_b,
};

_attribute_data_retention_ u8 blt_txfifo_b[TX_FIFO_SIZE * TX_FIFO_NUM] = {0};
_attribute_data_retention_ my_fifo_t blt_txfifo = {
	TX_FIFO_SIZE,
	TX_FIFO_NUM,
	0,
	0,
	blt_txfifo_b,
};

u8 tbl_advData[31];
u8 tbl_advDataLen;

u8 tbl_scanRsp[31];
u8 tbl_scanRspLen;

void ble_build_adv_scanrsp(void)
{
	u8 i = 0;

	// --- ADV: Flags + Appearance + UUID list; name is placed in scanRsp. ---
	i = 0;
	tbl_advData[i++] = 0x02;
	tbl_advData[i++] = 0x01;
	tbl_advData[i++] = 0x05;

	tbl_advData[i++] = 0x03;
	tbl_advData[i++] = 0x19;
	tbl_advData[i++] = 0x80;
	tbl_advData[i++] = 0x01;

	tbl_advData[i++] = 0x05;
	tbl_advData[i++] = 0x02;
	tbl_advData[i++] = 0x12;
	tbl_advData[i++] = 0x18;
	tbl_advData[i++] = 0x0F;
	tbl_advData[i++] = 0x18;

	tbl_advDataLen = i;

	i = 0;
	tbl_scanRsp[i++] = (u8)(DEV_NAME_LEN + 1);
	tbl_scanRsp[i++] = 0x09;
	memcpy(&tbl_scanRsp[i], DEV_NAME_STR, DEV_NAME_LEN);
	i += DEV_NAME_LEN;

	tbl_scanRspLen = i;
}

void mos_update(void)
{
    /* D008 has no discrete key. ACC/PB1 have no business policy yet. The
     * existing output-enable, protection and guard own final authorization;
     * never compare a product request with driver feedback. */
    g_bms_system_status.bits.b1Status_Cool = 0u;
    (void)bms_afe_set_fets(1u, 1u);
}

static void board_init(void)
{
	bms_afe_set_output_enabled(0u);

	/* PD4 is the heater fuse drive, not BLE RF power. Safe inactive boot. */
	gpio_set_func(RF_EN_PIN, AS_GPIO);
	gpio_set_input_en(RF_EN_PIN, 0);
	gpio_set_output_en(RF_EN_PIN, 1);
	gpio_write(RF_EN_PIN, 0);

	gpio_set_func(ACC_MCU_PIN, AS_GPIO);
	gpio_set_input_en(ACC_MCU_PIN, 1);
	gpio_set_output_en(ACC_MCU_PIN, 0);

	gpio_set_func(CHG_IN_PIN, AS_GPIO);
	gpio_setup_up_down_resistor(CHG_IN_PIN, PM_PIN_PULLUP_1M);
	gpio_set_input_en(CHG_IN_PIN, 1);
	gpio_set_output_en(CHG_IN_PIN, 0);

	gpio_set_func(LED_BLUE_PIN, AS_GPIO);
	gpio_set_input_en(LED_BLUE_PIN, 0);
	gpio_set_output_en(LED_BLUE_PIN, 1);
	gpio_write(LED_BLUE_PIN, 1);
}

_attribute_data_retention_ int device_in_connection_state;

_attribute_data_retention_ u32 advertise_begin_tick;

_attribute_data_retention_ u8 sendTerminate_before_enterDeep = 0;

void app_ble_request_normal_conn_param(void)
{
	if (device_in_connection_state)
	{
		bls_l2cap_requestConnParamUpdate(CONN_INTERVAL_10MS, CONN_INTERVAL_10MS, APP_CONN_LATENCY_NORMAL, CONN_TIMEOUT_4S);
	}
}

void app_ble_request_ota_conn_param(void)
{
	if (device_in_connection_state)
	{
		bls_l2cap_requestConnParamUpdate(CONN_INTERVAL_10MS, CONN_INTERVAL_10MS, APP_CONN_LATENCY_OTA, CONN_TIMEOUT_4S);
	}
}

void app_ble_restore_normal_power(void)
{
#if (BLE_APP_PM_ENABLE)
	bls_pm_setManualLatency(bls_ll_getConnectionLatency());
#endif
	app_ble_request_normal_conn_param();
}

_attribute_data_retention_ u32 latest_user_event_tick;

/**
 * @brief      callback function of LinkLayer Event "BLT_EV_FLAG_SUSPEND_ENTER"
 * @param[in]  e - LinkLayer Event type
 * @param[in]  p - data pointer of event
 * @param[in]  n - data length of event
 * @return     none
 */
void task_sleep_enter(u8 e, u8 *p, int n)
{
    (void)e;
    (void)p;
    (void)n;
    /* No ACC/load PAD wake policy. SDK application timer bounds sampling. */
}

/**
 * @brief      callback function of LinkLayer Event "BLT_EV_FLAG_ADV_DURATION_TIMEOUT"
 */
void app_switch_to_undirected_adv(u8 e, u8 *p, int n)
{
	(void)e;
	(void)p;
	(void)n;
	bls_ll_setAdvParam(MY_ADV_INTERVAL_MIN, MY_ADV_INTERVAL_MAX,
					   ADV_TYPE_CONNECTABLE_UNDIRECTED, app_own_address_type,
					   0, NULL,
					   MY_APP_ADV_CHANNEL,
					   ADV_FP_NONE);

	blc_ll_clearResolvingList();
	bls_ll_setAdvEnable(BLC_ADV_ENABLE);
}

/**
 * @brief      callback function of LinkLayer Event "BLT_EV_FLAG_CONNECT"
 */
void task_connect(u8 e, u8 *p, int n)
{
	(void)e;
	(void)p;
	(void)n;
	tlk_contr_evt_connect_t *pConnEvt = (tlk_contr_evt_connect_t *)p;
	tlkapi_send_string_data(APP_CONTR_EVENT_LOG_EN, "[APP][EVT] connect, intA & advA:", pConnEvt->initA, 12);
	device_in_connection_state = 1;
	app_ble_request_normal_conn_param();
	latest_user_event_tick = clock_time();

#if (UI_LED_ENABLE && !TEST_CONN_CURRENT_ENABLE)
	gpio_write(GPIO_LED_RED, LED_ON_LEVEL);
#endif
}

/**
 * @brief      callback function of LinkLayer Event "BLT_EV_FLAG_TERMINATE"
 */
void task_terminate(u8 e, u8 *p, int n)
{
	(void)e;
	(void)n;

	device_in_connection_state = 0;

	tlk_contr_evt_terminate_t *pEvt = (tlk_contr_evt_terminate_t *)p;
	if (pEvt->terminate_reason == HCI_ERR_CONN_TIMEOUT)
	{
	}
	else if (pEvt->terminate_reason == HCI_ERR_REMOTE_USER_TERM_CONN)
	{
	}
	else if (pEvt->terminate_reason == HCI_ERR_CONN_TERM_MIC_FAILURE)
	{
	}
	else
	{
	}

    bms_afe_hw_access_close(); /* Drop incomplete fragments and authorization on disconnect. */
	tlkapi_printf(APP_CONTR_EVENT_LOG_EN, "[APP][EVT] disconnect, reason 0x%x\n", pEvt->terminate_reason);

#if (BLE_APP_PM_ENABLE)
	if (sendTerminate_before_enterDeep == 1 && !TEST_CONN_CURRENT_ENABLE)
	{
		sendTerminate_before_enterDeep = 2;
		bls_ll_setAdvEnable(BLC_ADV_DISABLE);
	}
#endif

#if (UI_LED_ENABLE && !TEST_CONN_CURRENT_ENABLE)
	gpio_write(GPIO_LED_RED, !LED_ON_LEVEL);
#endif

	advertise_begin_tick = clock_time();
}

/**
 * @brief      callback function of LinkLayer Event "BLT_EV_FLAG_SUSPEND_EXIT"
 */
void task_suspend_exit(u8 e, u8 *p, int n)
{
	(void)e;
	(void)p;
	(void)n;
	rf_set_power_level_index(MY_RF_POWER_INDEX);
}

/**
 * @brief      callback function of LinkLayer Event "BLT_EV_FLAG_DATA_LENGTH_EXCHANGE"
 */
void task_dle_exchange(u8 e, u8 *p, int n)
{
	tlk_contr_evt_dataLenExg_t *pEvt = (tlk_contr_evt_dataLenExg_t *)p;
	tlkapi_send_string_data(APP_CONTR_EVENT_LOG_EN, "[APP][EVT] DLE exchange", &pEvt->connEffectiveMaxRxOctets, 4);
}

/**
 * @brief      callback function of Host Event
 */
int app_host_event_callback(u32 h, u8 *para, int n)
{

	u8 event = h & 0xFF;

	switch (event)
	{
	case GAP_EVT_SMP_PAIRING_BEGIN:
	{
		gap_smp_pairingBeginEvt_t *pEvt = (gap_smp_pairingBeginEvt_t *)para;
		tlkapi_send_string_data(APP_SMP_LOG_EN, "[APP][SMP] paring begin:", pEvt, sizeof(gap_smp_pairingBeginEvt_t));
	}
	break;

	case GAP_EVT_SMP_PAIRING_SUCCESS:
	{
		gap_smp_pairingSuccessEvt_t *pEvt = (gap_smp_pairingSuccessEvt_t *)para;
		tlkapi_send_string_data(APP_SMP_LOG_EN, "[APP][SMP] paring success:", pEvt, sizeof(gap_smp_pairingSuccessEvt_t));
	}
	break;

	case GAP_EVT_SMP_PAIRING_FAIL:
	{
		gap_smp_pairingFailEvt_t *pEvt = (gap_smp_pairingFailEvt_t *)para;
		tlkapi_send_string_data(APP_SMP_LOG_EN, "[APP][SMP] paring fail:", pEvt, sizeof(gap_smp_pairingFailEvt_t));
	}
	break;

	case GAP_EVT_SMP_CONN_ENCRYPTION_DONE:
	{
	}
	break;

	case GAP_EVT_SMP_SECURITY_PROCESS_DONE:
	{
	}
	break;

	case GAP_EVT_SMP_TK_DISPLAY:
	{
	}
	break;

	case GAP_EVT_SMP_TK_REQUEST_PASSKEY:
	{
	}
	break;

	case GAP_EVT_SMP_TK_REQUEST_OOB:
	{
	}
	break;

	case GAP_EVT_SMP_TK_NUMERIC_COMPARE:
	{
	}
	break;

	case GAP_EVT_ATT_EXCHANGE_MTU:
	{
		gap_gatt_mtuSizeExchangeEvt_t *pEvt = (gap_gatt_mtuSizeExchangeEvt_t *)para;
		tlkapi_send_string_data(APP_HOST_EVENT_LOG_EN, "[APP][MTU] mtu exchange", pEvt, sizeof(gap_gatt_mtuSizeExchangeEvt_t));
	}
	break;

	case GAP_EVT_GATT_HANDLE_VALUE_CONFIRM:
	{
	}
	break;

	default:
		break;
	}

	return 0;
}

/**
 * @brief      power management code for application
 */
void blt_pm_proc(void)
{
    static u32 low_voltage_seconds;
    static u8 low_voltage_region;
    static app_pm_elapsed_ctx_t elapsed_ctx;
    bms_afe_aux_measurements_t m;
    u32 elapsed_sec = app_pm_take_elapsed_seconds(&elapsed_ctx);
    u32 limit_seconds = 0u;
    u8 region = 0u;
    u8 valid = app_get_fresh_measurements(&m);
    u8 busy = ota_is_working || !app_flash_lock_restore_enabled() ||
              BUS_STATE_OWC_IDLE != bus_mux_get_state();

    /* 0x1102=0x000A is a latched power-off request, not an idle-suspend hint.
     * Keep it pending across OTA/bus/persistence/AFE failures. Return here so
     * the automatic low-voltage timer cannot reset the five-second retry gate. */
    if (deepsleep_en)
    {
        if (app_enter_power_off()) return;
        sys_time.low_power_mode = false;
        bls_pm_setSuspendMask(SUSPEND_DISABLE);
        if (ota_is_working) bls_pm_setManualLatency(0);
        return;
    }

    if (app_acc_sleep_requested())
    {
        low_voltage_seconds = 0u;
        low_voltage_region = 0u;
        if (app_enter_acc_sleep()) return;
        sys_time.low_power_mode = false;
        bls_pm_setSuspendMask(SUSPEND_DISABLE);
        if (ota_is_working) bls_pm_setManualLatency(0);
        return;
    }

    /* Preserve voltage thresholds/timeouts, but only qualified samples may
     * accumulate them. No key, load-detect or communication-error shutdown. */
    /* A BLE link permits between-event suspend, but still prevents automatic
     * low-voltage power-off. The SDK schedules connection-event wakeups. */
    if (valid && !busy && !device_in_connection_state)
    {
        if (g_stCellInfoReport.u16VCellMin < 2550u)
        {
            region = 1u;
            limit_seconds = 3600u;
        }
        else if (g_stCellInfoReport.u16VCellMin < __SLEEP_VLOW__)
        {
            region = 2u;
            limit_seconds = __SLEEP_TIMEVLOW__;
        }
        else if (g_stCellInfoReport.u16VCellMin < __SLEEP_VNORMAL__ && m.current_ma >= 0)
        {
            region = 3u;
            limit_seconds = __SLEEP_TIMENORMAL__;
        }
    }
    if (!region || region != low_voltage_region)
    {
        low_voltage_seconds = 0u;
        s_power_off_retry_ready = 0u;
    }
    low_voltage_region = region;
    if (region && elapsed_sec != 0u)
    {
        if (elapsed_sec >= limit_seconds - low_voltage_seconds)
            low_voltage_seconds = limit_seconds;
        else
            low_voltage_seconds += elapsed_sec;
        if (low_voltage_seconds >= limit_seconds && app_enter_power_off()) return;
    }

    /* Exact signed mA avoids the old 0.1 A truncation and checks both sides.
     * Invalid data forces active recovery instead of pretending to be idle. */
    if (!valid || busy || m.current_ma >= APP_SUSPEND_EXIT_CURRENT_MA ||
        m.current_ma <= -APP_SUSPEND_EXIT_CURRENT_MA || s_sample_due)
    {
        sys_time.low_power_mode = false;
        bls_pm_setSuspendMask(SUSPEND_DISABLE);
        if (ota_is_working) bls_pm_setManualLatency(0);
    }
    else
    {
        sys_time.low_power_mode = true;
        bls_pm_setSuspendMask(SUSPEND_ADV | SUSPEND_CONN);
    }
}

/**
 * @brief		user initialization when MCU power on or wake_up from deepSleep mode
 */
_attribute_no_inline_ void user_init_normal(void)
{

	//////////////////////////// basic hardware Initialization  Begin //////////////////////////////////

#if (MCU_CORE_TYPE == MCU_CORE_825x || MCU_CORE_TYPE == MCU_CORE_827x)
	random_generator_init();
#endif

#if (UART_PRINT_DEBUG_ENABLE)
	tlkapi_debug_init();
	blc_debug_enableStackLog(STK_LOG_DISABLE);
#endif

	blc_readFlashSize_autoConfigCustomFlashSector();
	blc_app_loadCustomizedParameters_normal();

#if (APP_FLASH_PROTECTION_ENABLE)
	app_flash_protection_operation(FLASH_OP_EVT_APP_INITIALIZATION, 0, 0);
	blc_appRegisterStackFlashOperationCallback(app_flash_protection_operation);
#endif

	//////////////////////////// basic hardware Initialization  End //////////////////////////////////

	//////////////////////////// BLE stack Initialization  Begin //////////////////////////////////
	u8 mac_public[6];
	u8 mac_random_static[6];
	blc_initMacAddress(flash_sector_mac_address, mac_public, mac_random_static);
	tlkapi_send_string_data(APP_LOG_EN, "[APP][INI]Public Address", mac_public, 6);

#if (BLE_DEVICE_ADDRESS_TYPE == BLE_DEVICE_ADDRESS_PUBLIC)
	app_own_address_type = OWN_ADDRESS_PUBLIC;
#elif (BLE_DEVICE_ADDRESS_TYPE == BLE_DEVICE_ADDRESS_RANDOM_STATIC)
	app_own_address_type = OWN_ADDRESS_RANDOM;
	blc_ll_setRandomAddr(mac_random_static);
#endif

	for (size_t i = 0; i < 6; i++)
	{
		g_stCellInfoReport.mac_public[i] = mac_public[5 - i];
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

	u8 adv_param_status = BLE_SUCCESS;
#if (BLE_APP_SECURITY_ENABLE)
	u8 bond_number = blc_smp_param_getCurrentBondingDeviceNumber();
	smp_param_save_t bondInfo;
	if (bond_number)
	{
		bls_smp_param_loadByIndex(bond_number - 1, &bondInfo);
	}

	if (bond_number)
	{
		adv_param_status = bls_ll_setAdvParam(MY_ADV_INTERVAL_MIN, MY_ADV_INTERVAL_MAX,
											  ADV_TYPE_CONNECTABLE_DIRECTED_LOW_DUTY, app_own_address_type,
											  bondInfo.peer_addr_type, bondInfo.peer_addr,
											  MY_APP_ADV_CHANNEL,
											  ADV_FP_NONE);

		if (blc_app_isIrkValid(bondInfo.peer_irk))
		{
			blc_ll_addDeviceToResolvingList(bondInfo.peer_id_adrType, bondInfo.peer_id_addr, bondInfo.peer_irk, NULL);
			blc_ll_setAddressResolutionEnable(1);
		}

		bls_ll_setAdvDuration(MY_DIRECT_ADV_TIME, 1);
		bls_app_registerEventCallback(BLT_EV_FLAG_ADV_DURATION_TIMEOUT, &app_switch_to_undirected_adv);
	}
	else
#endif
	{
		adv_param_status = bls_ll_setAdvParam(MY_ADV_INTERVAL_MIN, MY_ADV_INTERVAL_MAX,
											  ADV_TYPE_CONNECTABLE_UNDIRECTED, app_own_address_type,
											  0, NULL,
											  MY_APP_ADV_CHANNEL,
											  ADV_FP_NONE);
	}

	if (adv_param_status != BLE_SUCCESS)
	{
		tlkapi_printf(APP_LOG_EN, "[APP][INI] ADV parameters error 0x%x!!!\n", adv_param_status);
	}

	ble_build_adv_scanrsp();
	bls_ll_setAdvData((u8 *)tbl_advData, sizeof(tbl_advData));
	bls_ll_setScanRspData((u8 *)tbl_scanRsp, sizeof(tbl_scanRsp));
	bls_ll_setAdvEnable(BLC_ADV_ENABLE);

	rf_set_power_level_index(MY_RF_POWER_INDEX);

	bls_app_registerEventCallback(BLT_EV_FLAG_CONNECT, &task_connect);
	bls_app_registerEventCallback(BLT_EV_FLAG_TERMINATE, &task_terminate);
	bls_app_registerEventCallback(BLT_EV_FLAG_SUSPEND_EXIT, &task_suspend_exit);
	bls_app_registerEventCallback(BLT_EV_FLAG_DATA_LENGTH_EXCHANGE, &task_dle_exchange);

#if (BLE_APP_PM_ENABLE)
	blc_ll_initPowerManagement_module();

#if (PM_DEEPSLEEP_RETENTION_ENABLE)
	blc_app_setDeepsleepRetentionSramSize();
	bls_pm_setSuspendMask(SUSPEND_ADV | DEEPSLEEP_RETENTION_ADV | SUSPEND_CONN | DEEPSLEEP_RETENTION_CONN);
	blc_pm_setDeepsleepRetentionThreshold(95, 95);

#if (MCU_CORE_TYPE == MCU_CORE_825x || MCU_CORE_TYPE == MCU_CORE_827x)
	blc_pm_setDeepsleepRetentionEarlyWakeupTiming(270);
#else
	blc_pm_setDeepsleepRetentionEarlyWakeupTiming(340);
#endif

#else
	bls_pm_setSuspendMask(SUSPEND_ADV | SUSPEND_CONN);
#endif

	bls_app_registerEventCallback(BLT_EV_FLAG_SUSPEND_ENTER, &task_sleep_enter);
#else
	bls_pm_setSuspendMask(SUSPEND_DISABLE);
#endif

	blc_app_checkControllerHostInitialization();

	advertise_begin_tick = clock_time();
	tlkapi_printf(APP_LOG_EN, "[APP][INI] BLE sample init \n");

	{
		bms_diag_init();
		bms_storage_platform_diag_boot();
        bms_diag_boot_word(13u, (DVC1124_SW_PROTECT_ENABLE ? 1u : 0u) | (DVC1124_HW_PROTECT_ENABLE ? 2u : 0u));
		board_init();
		Param_UpgradeReset_Apply();
		LoadParam();
		bms_event_log_init();

		bms_afe_init();
        cpu_set_gpio_wakeup(CHG_IN_PIN, Level_Low, 0);
        cpu_set_gpio_wakeup(ACC_MCU_PIN, Level_Low, 0);

		/* One AFE snapshot supplies startup voltage/current/temperature state. */
		bms_afe_sample();
		soc_kv_store_init();
		soc_kv_data_t d = soc_kv_store_get();
		soc_param_lib_init(&d);
	}

	sif_timer_init();
	bus_mux_init();
	btname_init();
	bms_event_log_note_startup();
	Runtime_Init();
    s_sample_tick = clock_time();
    bls_pm_registerAppWakeupLowPowerCb(app_sample_wakeup);
    app_schedule_sample_wakeup();
	mos_update();

	extern void WriteProID_Default(void);
	WriteProID_Default();
	bms_afe_set_output_enabled(1u);
    bms_param_diag_poll();
    bms_storage_platform_diag_poll();
    bms_afe_diag_poll();
    bms_diag_freeze_boot();
}

/**
 * @brief		user initialization when MCU wake_up from deepSleep_retention mode
 */
_attribute_ram_code_ void user_init_deepRetn(void)
{
#if (PM_DEEPSLEEP_RETENTION_ENABLE)

	blc_app_loadCustomizedParameters_deepRetn();
	blc_ll_initBasicMCU();
	rf_set_power_level_index(MY_RF_POWER_INDEX);
	blc_ll_recoverDeepRetention();
	DBG_CHN0_HIGH;
	irq_enable();

#if (UI_KEYBOARD_ENABLE)
	u32 pin[] = KB_DRIVE_PINS;
	for (int i = 0; i < (sizeof(pin) / sizeof(*pin)); i++)
	{
		cpu_set_gpio_wakeup(pin[i], Level_High, 1);
	}
#elif (UI_BUTTON_ENABLE)
	cpu_set_gpio_wakeup(SW1_GPIO, Level_Low, 1);
	cpu_set_gpio_wakeup(SW2_GPIO, Level_Low, 1);
#endif
#endif
}

#if (APP_FLASH_PROTECTION_ENABLE)

_attribute_data_retention_ u16 flash_lockBlock_cmd = 0;
_attribute_data_retention_ static u8 g_app_flash_stack_session_active = 0;

int app_flash_lock_restore_enabled(void)
{
	return (g_app_flash_stack_session_active == 0u);
}

void app_flash_protection_operation(u8 flash_op_evt, u32 op_addr_begin, u32 op_addr_end)
{
	if (flash_op_evt == FLASH_OP_EVT_APP_INITIALIZATION)
	{
		g_app_flash_stack_session_active = 0u;
		flash_protection_init();
		u32 app_lockBlock = 0;
#if (BLE_OTA_SERVER_ENABLE)
		u32 multiBootAddress = blc_ota_getCurrentUsedMultipleBootAddress();
		if (multiBootAddress == MULTI_BOOT_ADDR_0x20000)
		{
			app_lockBlock = FLASH_LOCK_FW_LOW_256K;
		}
		else if (multiBootAddress == MULTI_BOOT_ADDR_0x40000)
		{
			app_lockBlock = FLASH_LOCK_FW_LOW_512K;
		}
#if (MCU_CORE_TYPE == MCU_CORE_827x)
		else if (multiBootAddress == MULTI_BOOT_ADDR_0x80000)
		{
			if (blc_flash_capacity < FLASH_SIZE_1M)
			{
				blc_flashProt.init_err = 1;
			}
			else
			{
				app_lockBlock = FLASH_LOCK_FW_LOW_1M;
			}
		}
#endif
#else
		app_lockBlock = FLASH_LOCK_FW_LOW_256K;
#endif

		flash_lockBlock_cmd = flash_change_app_lock_block_to_flash_lock_block(app_lockBlock);

		if (blc_flashProt.init_err)
		{
			tlkapi_printf(APP_FLASH_PROT_LOG_EN, "[FLASH][PROT] flash protection initialization error!!!\n");
		}

		tlkapi_printf(APP_FLASH_PROT_LOG_EN, "[FLASH][PROT] initialization, lock flash\n");
		flash_lock(flash_lockBlock_cmd);
	}
#if (BLE_OTA_SERVER_ENABLE)
	else if (flash_op_evt == FLASH_OP_EVT_STACK_OTA_CLEAR_OLD_FW_BEGIN)
	{
		g_app_flash_stack_session_active = 1u;
		tlkapi_printf(APP_FLASH_PROT_LOG_EN, "[FLASH][PROT] OTA clear old FW begin, unlock flash\n");
		flash_unlock();
	}
	else if (flash_op_evt == FLASH_OP_EVT_STACK_OTA_CLEAR_OLD_FW_END)
	{
		g_app_flash_stack_session_active = 0u;
		tlkapi_printf(APP_FLASH_PROT_LOG_EN, "[FLASH][PROT] OTA clear old FW end, restore flash locking\n");
		flash_lock(flash_lockBlock_cmd);
	}
	else if (flash_op_evt == FLASH_OP_EVT_STACK_OTA_WRITE_NEW_FW_BEGIN)
	{
		g_app_flash_stack_session_active = 1u;
		tlkapi_printf(APP_FLASH_PROT_LOG_EN, "[FLASH][PROT] OTA write new FW begin, unlock flash\n");
		flash_unlock();
	}
	else if (flash_op_evt == FLASH_OP_EVT_STACK_OTA_WRITE_NEW_FW_END)
	{
		g_app_flash_stack_session_active = 0u;
		tlkapi_printf(APP_FLASH_PROT_LOG_EN, "[FLASH][PROT] OTA write new FW end, restore flash locking\n");
		flash_lock(flash_lockBlock_cmd);
	}
#endif
	(void)op_addr_begin;
	(void)op_addr_end;
}

#else
int app_flash_lock_restore_enabled(void)
{
    return 1;
}
#endif

/* One acquisition owns the complete protection/SOC/output sequence. Keep this
 * order and coalesce overdue work: repeated catch-up samples would distort
 * sample-count filters and starve BLE/UART. The wake callback only sets due. */
static void app_sample_task(void)
{
    bms_afe_aux_measurements_t m;
    u8 valid;

    if (!s_sample_due && !clock_time_exceed(s_sample_tick, APP_SAMPLE_PERIOD_US)) return;

    s_sample_due = 0u;
    s_sample_tick = clock_time();
    bms_afe_sample();
    valid = app_get_fresh_measurements(&m);
    APP_SOC_IntEnhance_Ctrl(valid, valid ? m.current_ma : 0,
                           valid ? m.sample_tick_32k : pm_get_32k_tick());
    mos_update();
    /* Keep a fixed acquisition cadence even if BLE advertises at 800 ms. */
    if (clock_time_exceed(s_sample_tick, APP_SAMPLE_PERIOD_US)) s_sample_due = 1u;
    app_schedule_sample_wakeup();
    gpio_toggle(LED_BLUE_PIN);
}

/**
 * @brief		This is main_loop function
 */
_attribute_no_inline_ void main_loop(void)
{
    if (s_acc_sleep_committed) {
        app_acc_sleep_hold();
        return;
    }
    bms_param_diag_poll();
    bms_storage_platform_diag_poll();
    bms_afe_diag_poll();
    if (s_power_off_committed)
    {
        /* If external power holds 3V3 up (e.g. a debugger), remain quiescent.
         * Do not spin, retry I2C, or write Flash after a successful shutdown. */
        cpu_sleep_wakeup(SUSPEND_MODE, PM_WAKEUP_TIMER,
                         clock_time() + APP_SAMPLE_PERIOD_US * SYSTEM_TIMER_TICK_1US);
        return;
    }
    /* Cooperative order: service BLE, acquire once if due, then communication
     * and persistence. Evaluate suspend last using the resulting state. */
	blt_sdk_main_loop();
	Runtime_Poll();

    app_sample_task();
    app_event_log_1s_task();

	bus_mux_task();
#ifdef _FUNC_UART_
	main_loop_modbus();
#endif
	soc_kv_store_update_and_log_if_changed(SOC_Calculate_Element.u8SOC_Now, SOC_Calculate_Element.u8DSG_SOC_Int, SOC_Calculate_Element.u32Cycle_times);
	blt_pm_proc();
}
