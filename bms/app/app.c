/*
 * 文件功能：应用调度与 BLE 电源策略；主循环按 200 ms 执行 AFE/SOC/MOS，
 * 协调通信、OTA 和休眠入口。
 * bms/app/app.c；实际编译归属见各产品 sources.txt。
 */
/********************************************************************************************************
 * @file    app.c
 *
 * @brief   BLE SDK 应用源文件。
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
#include "bms_debug_log.h"
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
#include "bms_afe_hw_access.h"
#include "bms_features.h"
#include "bms_diag.h"
#include "bms_error.h"
#include "bms_state.h"
#include "bms_soc.h"
#include "bms_event_log.h"
#include "bms_state_store.h"
#include "bms_storage_platform.h"
#include "btname_modbus.h"
#include "bms_factory_mode.h"
#include "param.h"
#include <string.h>

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
#include "sif_send.h"
#include "bus_mux.h"
#endif

/* 处理 BLE 断开并恢复广播和电源策略。 */
void task_terminate(u8 e, u8 *p, int n);
/* 采集并处理一次 AFE 样本，推进保护、SOC 和 MOS 仲裁。 */
static void app_sample_task(void);
/* 把合格 AFE 样本及时间戳提交给 SOC 算法。 */
static void app_update_soc_from_sample(uint8_t valid, int32_t current_ma,
                             uint32_t sample_tick_32k);
/* 低功耗采样唤醒回调只置位，由主循环执行采样。 */
/* 采样唤醒回调只置位，由主循环执行 AFE 和 SOC 处理。 */
static void app_sample_wakeup(int type);
/* 构造广播与扫描响应中的产品名称和数据。 */
void ble_build_adv_scanrsp(void);
/* 切换为非定向 BLE 广播。 */
void app_switch_to_undirected_adv(u8 e, u8 *p, int n);
/* 处理 BLE 连接建立并更新连接与低功耗状态。 */
void task_connect(u8 e, u8 *p, int n);
/* 恢复 suspend 后的外设与应用时序状态。 */
void task_suspend_exit(u8 e, u8 *p, int n);
/* 处理 BLE 数据长度交换事件。 */
void task_dle_exchange(u8 e, u8 *p, int n);
/* 分派 BLE 主机事件并更新应用连接状态。 */
int app_host_event_callback(u32 h, u8 *para, int n);

#if BMS_DEBUG_LOG_ENABLE
static volatile u32 s_debug_suspend_exits; /* 只有唤醒回调写入，主循环读取。 */
#endif
#define APP_PM_TICKS_PER_SEC 32000u

#define APP_SAMPLE_PERIOD_US 200000u

static u32 s_sample_tick;

/* 低功耗唤醒回调只置位；主循环消费后执行采样，不在回调中跑保护/SOC。 */
static volatile u8 s_sample_due;

typedef struct
{
	u32 last_tick_32k;
	u32 pending_tick_32k;
	u8 ready;
} app_pm_elapsed_ctx_t;
#define SOC_LEARNING_PACK_FAULT_MASK         0x000Cu
#define SOC_LEARNING_CURRENT_FAULT_MASK      0x0030u
#define SOC_LEARNING_TEMP_FAULT_MASK         0x2BC0u

static bool s_low_power_mode;
bool deepsleep_en = false;
// nvm_cfg_t nvm_cfg;

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
#define APP_SUSPEND_EXIT_CURRENT_MA 500
#define APP_POWER_OFF_RETRY_SECONDS 5u
#define APP_ACC_HIGH_STABLE_TICKS (APP_PM_TICKS_PER_SEC / 5u)
extern int device_in_connection_state;
static u8 s_power_off_committed;
static u8 s_power_off_retry_ready;
static u32 s_power_off_retry_tick;
static u8 s_acc_high_seen, s_acc_sleep_committed, s_acc_retry_ready, s_acc_disconnect_sent;
static u32 s_acc_high_tick, s_acc_retry_tick;

/* 检查并取得同一采样周期的有效测量快照。 */
static uint8_t app_get_fresh_measurements(bms_afe_aux_measurements_t *m)
{
    if (!bms_afe_get_aux_measurements(m)) return 0u;
    return ((u32)(pm_get_32k_tick() - m->sample_tick_32k) <=
            BMS_SOC_MAX_SAMPLE_GAP_32K) ? 1u : 0u;
}

/*
 * SDK 低功耗回调只安排任务；I2C、SOC、Flash 仍在协作主循环执行，
 * 包括从 suspend 回调发起的任务。
 */

/* 按配置安排下次周期采样唤醒；关闭周期唤醒时为空入口。 */
static void app_schedule_sample_wakeup(void)
{
    /*
     * 功耗测试期间故障恢复也需连续样本；查询状态所有者的 RAM，
     * 不从驱动反馈反推目标请求。
     */
    if (BMS_APP_SAMPLE_WAKEUP_ENABLE || bms_afe_current_recovery_pending())
        bls_pm_setAppWakeupLowPower(s_sample_tick + APP_SAMPLE_PERIOD_US * SYSTEM_TIMER_TICK_1US, 1u);
    else
        bls_pm_setAppWakeupLowPower(0u, 0u);
}

#else

/* 设置下一次周期采样的低功耗唤醒期限。 */
/* 按配置安排下次周期采样唤醒；关闭周期唤醒时为空入口。 */
static void app_schedule_sample_wakeup(void)
{
    bls_pm_setAppWakeupLowPower(
        s_sample_tick + APP_SAMPLE_PERIOD_US * SYSTEM_TIMER_TICK_1US, 1u);
}

/* 按产品配置读取开关输入状态。 */
static UINT8 board_switch_is_on(void)
{
#ifdef _DI_SWITCH_SYS_ONOFF
	return gpio_read(BMS_BOARD_SWITCH_PIN) ? 0u : 1u;
#else
	return 1u;
#endif
}

#endif
/* 从低功耗时间差中消费完整秒数并保留余量。 */
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

/* 按秒推进历史事件与运行计时任务。 */
static void app_event_log_1s_task(void)
{
	bms_event_log_sample_t sample;

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    _attribute_data_retention_ static u32 event_log_tick = 0;

    if (!clock_time_exceed(event_log_tick, 1000 * 1000)) return;
    event_log_tick = clock_time();
#endif

	memset(&sample, 0, sizeof(sample));

	sample.balance = ((g_stCellInfoReport.u16BalanceFlag1 != 0u) || (g_stCellInfoReport.u16BalanceFlag2 != 0u)) ? 1u : 0u;

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
	sample.cbc_err = bms_error_get(BMS_ERROR_CBC_DSG) ? 1u : 0u;

	bms_event_log_poll_1s(&sample);
#if BMS_DEBUG_LOG_ENABLE
    {
        static u32 previous_exits;
        u32 exits = s_debug_suspend_exits;
        if (exits != previous_exits)
            BMS_LOG(BMS_LOG_DEBUG, BMS_LOG_POWER, BMS_LOG_PM_CYCLE, exits - previous_exits, exits);
        previous_exits = exits;
    }
#endif
}

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124

/* 完成关断准备后按板级流程关闭电源。 */
static int app_enter_power_off(void)
{
    bms_afe_aux_measurements_t m;
    u32 now = pm_get_32k_tick();

    if (s_power_off_committed || ota_is_working ||
        !app_flash_lock_restore_enabled() ||
        BUS_STATE_OWC_IDLE != bus_mux_get_state()) return 0;
    /*
     * 显式休眠命令不要求低电压、BLE 断连或有效电流样本。断电前必须发完命令应答；
     * 自动低压关机保留原资格条件。
     */
    if (deepsleep_en)
    {
        if (device_in_connection_state && blc_ll_getTxFifoNumber() != 0u) return 0;
    }
    else if (device_in_connection_state || !app_get_fresh_measurements(&m)) return 0;
    if (s_power_off_retry_ready &&
        (u32)(now - s_power_off_retry_tick) <
            APP_POWER_OFF_RETRY_SECONDS * APP_PM_TICKS_PER_SEC) return 0;
    BMS_LOG(BMS_LOG_INFO, BMS_LOG_POWER, BMS_LOG_SLEEP_ATTEMPT, 1u, 1u);
    s_power_off_retry_ready = 1u;
    s_power_off_retry_tick = now;

    /*
     * PC4 拉低后不能再延后执行 Flash 操作；休眠事件记录尝试，
     * shutdown 失败绝不切断供电。
     */
    if (!bms_state_store_write_all(SOC_Calculate_Element.u8SOC_Now,
                               SOC_Calculate_Element.u8DSG_SOC_Int,
                               SOC_Calculate_Element.u32Cycle_times) ||
        !bms_event_log_note_sleep()) return 0;
    if (!bms_afe_enter_shutdown()) {
        BMS_LOG(BMS_LOG_WARN, BMS_LOG_POWER, BMS_LOG_SLEEP_ABORT, 1u, 1u);
        return 0;
    }

    s_power_off_committed = 1u;
    bls_pm_setAppWakeupLowPower(0u, 0u);
    s_low_power_mode = true;
    gpio_write(MCU_LDO_PIN, 0u); /* 最后硬件动作使整个 MCU 掉电。 */
    return 1;
}

/*
 * ACC 休眠保持 PC4 高；深睡唤醒走完整正常启动，
 * 不能对已主动 shutdown 的 AFE 直接恢复采样。
 */
static void app_acc_sleep_hold(void)
{
    if (!gpio_read(ACC_MCU_PIN)) {
        start_reboot();
        return;
    }
    cpu_set_gpio_wakeup(ACC_MCU_PIN, Level_Low, 1);
    cpu_sleep_wakeup(DEEPSLEEP_MODE, PM_WAKEUP_PAD, 0u);
    /* PAD 可能在检查后、入睡前变为有效。 */
    if (!gpio_read(ACC_MCU_PIN)) start_reboot();
}

/* 判断 ACC 条件是否请求进入休眠。 */
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

/* 满足通信和硬件门禁后进入 ACC 休眠。 */
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
    if (!bms_state_store_write_all(SOC_Calculate_Element.u8SOC_Now,
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
    s_low_power_mode = true;
    gpio_write(MCU_LDO_PIN, 1u);
    cpu_set_gpio_wakeup(CHG_IN_PIN, Level_Low, 0);
    app_acc_sleep_hold();
    return 1;
}

#else

/* 检查当前深睡唤醒引脚是否已处于有效电平。 */
static int app_deepsleep_pad_wakeup_active(void)
{
	/* 使用所选产品唤醒网络，实际有效电平仍需实板验证。 */
	if (board_switch_is_on()) return 1;
	if (gpio_read(BMS_BOARD_INT_WK_MCU_PIN)) return 1;      /* 高电平有效。 */
	if (!gpio_read(BMS_BOARD_AFE_ALARM_PIN)) return 1;     /* 低电平有效。 */
	if (!gpio_read(BMS_BOARD_AFE_RESET_OUT_PIN)) return 1; /* 低电平有效。 */
	return 0;
}

/*
 * 仅在 OTA、Flash、UART、总线及唤醒脚门禁满足后尝试深睡；
 * 失败保留请求并按 32K 时间退避。
 */
static int app_note_sleep_and_enter_deepsleep(u8 need_afe_sleep)
{
    static u32 last_attempt_tick_32k;
    static u8 attempt_ready;
    u32 now_tick_32k = pm_get_32k_tick();
    int sleep_status;

    /*
     * 每个显式深睡入口前都需这些门禁，不仅是下方 BLE suspend 策略；
     * 不能打断 OTA、未锁 Flash 或 UART。
     */
    if (ota_is_working || !app_flash_lock_restore_enabled() ||
        SH3673510_FIXED_UART_BLOCKS_PM || uart_tx_is_busy() ||
        modbus_uart_tx_active() ||
        app_deepsleep_pad_wakeup_active()) return 0;

    /* 已到期休眠请求保持待处理，但 SPI/PM 失败时不能忙循环。 */
    if (attempt_ready && (u32)(now_tick_32k - last_attempt_tick_32k) <
        3u * APP_PM_TICKS_PER_SEC) return 0;
    last_attempt_tick_32k = now_tick_32k;
    attempt_ready = 1u;

    BMS_LOG(BMS_LOG_INFO, BMS_LOG_POWER, BMS_LOG_SLEEP_ATTEMPT, need_afe_sleep, 0u);
    if (need_afe_sleep && !bms_afe_sleep()) {
        BMS_LOG(BMS_LOG_WARN, BMS_LOG_POWER, BMS_LOG_SLEEP_ABORT, 1u, 0u);
        return 0;
    }
    /* AFE 事务期间 GPIO 可变化；MCU 转换中止后由下个正常采样唤醒/恢复 AFE。 */
    if (app_deepsleep_pad_wakeup_active()) {
        BMS_LOG(BMS_LOG_WARN, BMS_LOG_POWER, BMS_LOG_SLEEP_ABORT, 2u, 0u);
        return 0;
    }

    bms_event_log_note_sleep();
    Runtime_PrepareForDeepSleep();
    sleep_status = cpu_sleep_wakeup(DEEPSLEEP_MODE, PM_WAKEUP_PAD, 0);
    Runtime_CancelPendingDeepSleep();
    BMS_LOG(BMS_LOG_INFO, BMS_LOG_POWER, BMS_LOG_SLEEP_RETURN, sleep_status, 0u);
    return ((sleep_status & STATUS_GPIO_ERR_NO_ENTER_PM) == 0);
}

/* 限制休眠时间累计值，避免异常跨度影响策略。 */
static u32 app_pm_elapsed_limit(u32 elapsed, u32 increment, u32 limit)
{
    if (elapsed >= limit || increment >= limit - elapsed) return limit;
    return elapsed + increment;
}
#define ADV_IDLE_ENTER_DEEP_TIME 60	 // 60 s
#define CONN_IDLE_ENTER_DEEP_TIME 60 // 60 s

#endif
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

/* @brief 配置链路层接收与发送 FIFO。 */
/* CAL_LL_ACL_RX_BUF_SIZE(maxRxOct) 为 maxRxOct+22，再按 16 字节对齐。 */
#define RX_FIFO_SIZE 64
/* 必须为 2 的幂且至少 4，推荐 4、8、16。 */
#define RX_FIFO_NUM 8

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
/* CAL_LL_ACL_TX_BUF_SIZE(maxTxOct) 为 maxTxOct+10，再按 4 字节对齐。 */
#define TX_FIFO_SIZE 40
/* 必须为 2 的幂且至少 8，推荐 8、16、32，不允许其它值。 */
#define TX_FIFO_NUM 16

#else
/* CAL_LL_ACL_TX_BUF_SIZE(maxTxOct) 为 maxTxOct+10，再按 4 字节对齐。 */
#define TX_FIFO_SIZE 40
/* 必须为 2 的幂且至少 8，推荐 8、16、32，不允许其它值。 */
#define TX_FIFO_NUM 16

#endif
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

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124

/* 合并保护与功能阻断条件，更新充放电 MOS 请求。 */
void mos_update(void)
{
    /*
     * D008 无独立按键，ACC/PB1 尚无业务策略。现有输出授权、保护和门禁负责最终资格，
     * 不能将产品请求与驱动反馈直接比较。
     */
    g_bms_system_status.bits.b1Status_Cool = 0u;
    (void)bms_afe_set_fets(1u, 1u);
}

/* 按选定产品初始化 AFE 接口、供电和板级 GPIO。 */
static void board_init(void)
{
	bms_afe_set_output_enabled(0u);

	/* PD4 是加热保险丝驱动，不是 BLE 射频供电；启动保持安全无效电平。 */
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

#else

/* 合并保护与功能阻断条件，更新充放电 MOS 请求。 */
void mos_update(void)
{
	/*
	 * SH 产品采用同口策略；健康正常状态请求两个背靠背 FET 开启，
	 * AFE 后端按方向保护与故障安全阻断；PA0/SW1 不是 DSG 门禁。
	 */
	uint8_t chg_target = 1u;
	uint8_t dsg_target = 1u;

	g_bms_system_status.bits.b1Status_Cool = 0u;
	(void)bms_afe_set_fets(chg_target, dsg_target);
}

/* 按选定产品初始化 AFE 接口、供电和板级 GPIO。 */
static void board_init(void)
{
	bms_afe_set_output_enabled(0u);

	/* 加热 GPIO 归功能后端与产品能力管理。 */

	gpio_set_func(BMS_BOARD_SWITCH_PIN, AS_GPIO);
	gpio_set_input_en(BMS_BOARD_SWITCH_PIN, 1);
	gpio_set_output_en(BMS_BOARD_SWITCH_PIN, 0);

	/* 正常运行保持所选通信电源开启；D013 沿用映射，等待原理图验证。 */
	gpio_set_func(BMS_BOARD_CMNT_EN_PIN, AS_GPIO);
	gpio_write(BMS_BOARD_CMNT_EN_PIN, 1);
	gpio_set_input_en(BMS_BOARD_CMNT_EN_PIN, 0);
	gpio_set_output_en(BMS_BOARD_CMNT_EN_PIN, 1);

	/*
	 * PD3 是原理图 CMNT-WK 输入；有效极性尚未实板确认，只配置输入，
	 * 不擅自定义唤醒极性。
	 */
	gpio_set_func(BMS_BOARD_CMNT_WK_PIN, AS_GPIO);
	gpio_set_output_en(BMS_BOARD_CMNT_WK_PIN, 0);
	gpio_set_input_en(BMS_BOARD_CMNT_WK_PIN, 1);
}

#endif
_attribute_data_retention_ int device_in_connection_state;

_attribute_data_retention_ u32 advertise_begin_tick;

_attribute_data_retention_ u8 sendTerminate_before_enterDeep = 0;

/* 请求常规 BLE 连接参数。 */
void app_ble_request_normal_conn_param(void)
{
	if (device_in_connection_state)
	{
		bls_l2cap_requestConnParamUpdate(CONN_INTERVAL_10MS, CONN_INTERVAL_10MS, APP_CONN_LATENCY_NORMAL, CONN_TIMEOUT_4S);
	}
}

/* 请求 OTA 所需的 BLE 连接参数。 */
void app_ble_request_ota_conn_param(void)
{
	if (device_in_connection_state)
	{
		bls_l2cap_requestConnParamUpdate(CONN_INTERVAL_10MS, CONN_INTERVAL_10MS, APP_CONN_LATENCY_OTA, CONN_TIMEOUT_4S);
	}
}

/* 恢复常规 BLE 发射功率及连接设置。 */
void app_ble_restore_normal_power(void)
{
#if (BLE_APP_PM_ENABLE)
	bls_pm_setManualLatency(bls_ll_getConnectionLatency());
#endif
	app_ble_request_normal_conn_param();
}

_attribute_data_retention_ u32 latest_user_event_tick;

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
/*
 * @brief      链路层事件回调： "BLT_EV_FLAG_SUSPEND_ENTER"
 * @param[in]  e - 链路层事件类型
 * @param[in]  p - 事件数据指针
 * @param[in]  n - 事件数据长度
 * @return     无
 */
/* 处理 SDK 休眠进入事件并保存必要状态。 */
void task_sleep_enter(u8 e, u8 *p, int n)
{
    (void)e;
    (void)p;
    (void)n;
    /* 无 ACC/负载 PAD 唤醒策略，采样由 SDK 应用定时器限定。 */
}

/* @brief 链路层 BLT_EV_FLAG_ADV_DURATION_TIMEOUT 事件回调。 */

/* @brief 链路层 BLT_EV_FLAG_CONNECT 事件回调。 */

/* @brief 链路层 BLT_EV_FLAG_TERMINATE 事件回调。 */

/* @brief 链路层 BLT_EV_FLAG_SUSPEND_EXIT 事件回调。 */

/* @brief 链路层 BLT_EV_FLAG_DATA_LENGTH_EXCHANGE 事件回调。 */

/* @brief Host 事件回调。 */

/* @brief 应用电源管理。 */
/* 根据运行状态和通信互锁选择 SDK suspend/深睡；调试日志积压本身不增加休眠阻断条件。 */
void blt_pm_proc(void)
{
    static u32 low_voltage_seconds;
    static u8 low_voltage_region;
    static app_pm_elapsed_ctx_t elapsed_ctx;
    bms_afe_aux_measurements_t m;
    u32 elapsed_sec = app_pm_take_elapsed_seconds(&elapsed_ctx);
    u32 limit_seconds = 0u;
    u32 pm_block = 0u;
    u8 region = 0u;
    u8 valid = app_get_fresh_measurements(&m);
    u8 ota_busy = ota_is_working ? 1u : 0u;
    u8 flash_busy = app_flash_lock_restore_enabled() ? 0u : 1u;
    u8 bus_busy = (BUS_STATE_OWC_IDLE != bus_mux_get_state()) ? 1u : 0u;
    u8 busy = (uint8_t)(ota_busy || flash_busy || bus_busy);

    if (!valid) pm_block |= DIAG_PM_BLOCK_SAMPLE_INVALID;
    if (ota_busy) pm_block |= DIAG_PM_BLOCK_OTA;
    if (flash_busy) pm_block |= DIAG_PM_BLOCK_FLASH;
    if (bus_busy) pm_block |= DIAG_PM_BLOCK_BUS;
    if (valid && (m.current_ma >= APP_SUSPEND_EXIT_CURRENT_MA ||
                  m.current_ma <= -APP_SUSPEND_EXIT_CURRENT_MA))
        pm_block |= DIAG_PM_BLOCK_CURRENT;
    if (s_sample_due) pm_block |= DIAG_PM_BLOCK_SAMPLE_PENDING;

    /*
     * 0x1102=0x000A 是锁存关机请求，不是空闲 suspend 提示。
     * OTA/总线/持久化/AFE 失败时保留请求；此处直接返回，
     * 防止自动低压计时重置五秒重试门禁。
     */
    if (deepsleep_en)
    {
        pm_block |= DIAG_PM_BLOCK_POWER_OFF;
        bms_diag_runtime_pm(0u, pm_block, low_voltage_region, low_voltage_seconds,
                            (uint8_t)(device_in_connection_state != 0),
                            s_sample_due, APP_SUSPEND_EXIT_CURRENT_MA);
        if (app_enter_power_off()) return;
        s_low_power_mode = false;
        bls_pm_setSuspendMask(SUSPEND_DISABLE);
        if (ota_is_working) bls_pm_setManualLatency(0);
        return;
    }

    if (app_acc_sleep_requested())
    {
        low_voltage_seconds = 0u;
        low_voltage_region = 0u;
        pm_block |= DIAG_PM_BLOCK_ACC_SLEEP;
        bms_diag_runtime_pm(0u, pm_block, low_voltage_region, low_voltage_seconds,
                            (uint8_t)(device_in_connection_state != 0),
                            s_sample_due, APP_SUSPEND_EXIT_CURRENT_MA);
        if (app_enter_acc_sleep()) return;
        s_low_power_mode = false;
        bls_pm_setSuspendMask(SUSPEND_DISABLE);
        if (ota_is_working) bls_pm_setManualLatency(0);
        return;
    }

    /* 保留电压阈值/超时，但仅合格样本可累计；不增加按键、负载检测或通信错误关机。 */
    /* BLE 连接允许事件间 suspend，但仍禁止自动低压断电；SDK 调度连接事件唤醒。 */
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

    /*
     * 使用精确有符号 mA，避免旧 0.1 A 截断并检查正负两侧；无效数据必须主动恢复，
     * 不能假装空闲。
     */
    if (pm_block != 0u)
    {
        s_low_power_mode = false;
        bls_pm_setSuspendMask(SUSPEND_DISABLE);
        if (ota_is_working) bls_pm_setManualLatency(0);
        bms_diag_runtime_pm(0u, pm_block, low_voltage_region, low_voltage_seconds,
                            (uint8_t)(device_in_connection_state != 0),
                            s_sample_due, APP_SUSPEND_EXIT_CURRENT_MA);
    }
    else
    {
        s_low_power_mode = true;
        bls_pm_setSuspendMask(SUSPEND_ADV | SUSPEND_CONN);
        bms_diag_runtime_pm(1u, 0u, low_voltage_region, low_voltage_seconds,
                            (uint8_t)(device_in_connection_state != 0),
                            s_sample_due, APP_SUSPEND_EXIT_CURRENT_MA);
    }
}

/* @brief MCU 上电或 deepSleep 唤醒时的用户初始化。 */

/* @brief MCU 从 deepSleep_retention 唤醒时的用户初始化。 */

/*
 * 一次采样统一执行保护/SOC/输出；保持顺序，合并逾期任务。
 * 重复补采会扭曲样本计数滤波并阻塞 BLE/UART；唤醒回调只标记到期。
 */

/* 200 ms 采样调度的唯一主循环入口；无效 AFE 样本不得推进 SOC，超时只合并一次补采。 */

/* @brief 应用主循环。 */
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
        /*
         * 调试器等外部供电保持 3V3 时仍保持静止；
         * 成功 shutdown 后不忙循环、不重试I2C、不写 Flash。
         */
        cpu_sleep_wakeup(SUSPEND_MODE, PM_WAKEUP_TIMER,
                         clock_time() + APP_SAMPLE_PERIOD_US * SYSTEM_TIMER_TICK_1US);
        return;
    }
    /*
     * 协作顺序：先服务 BLE，到期只采样一次，再处理通信和持久化；
     * 最后根据新状态评估 suspend。
     */
	blt_sdk_main_loop();
	Runtime_Poll();
    bms_diag_runtime_mode((Runtime_GetMode() == MODE_FACTORY) ? 1u : 0u);

#if BMS_DEBUG_LOG_ENABLE
    /* 在此观察 SDK 回调负责的标志，ISR 不格式化或生产日志。 */
    {
        static u8 last_link = 0xFFu, last_ota = 0xFFu;
        if (last_link != (u8)device_in_connection_state) {
            last_link = (u8)device_in_connection_state;
            BMS_LOG(BMS_LOG_INFO, BMS_LOG_COMM, BMS_LOG_LINK_STATE, last_link, 0u);
        }
        if (last_ota != ota_is_working) {
            last_ota = ota_is_working;
            BMS_LOG(BMS_LOG_INFO, BMS_LOG_OTA, BMS_LOG_OTA_STATE, last_ota, 0u);
        }
    }
#endif
    app_sample_task();
    app_event_log_1s_task();

	bus_mux_task();
    sif_prepare_task(s_sample_tick);
#ifdef _FUNC_UART_
	main_loop_modbus();
#endif
	bms_state_store_update_and_log_if_changed(SOC_Calculate_Element.u8SOC_Now, SOC_Calculate_Element.u8DSG_SOC_Int, SOC_Calculate_Element.u32Cycle_times);
	blt_pm_proc();
}

#else
/*
 * @brief      链路层事件回调： "BLT_EV_FLAG_SUSPEND_ENTER"
 * @param[in]  e - 链路层事件类型
 * @param[in]  p - 事件数据指针
 * @param[in]  n - 事件数据长度
 * @return     无
 */
/* 处理 SDK 休眠进入事件并保存必要状态。 */
void task_sleep_enter(u8 e, u8 *p, int n)
{
	(void)e;
	(void)p;
	(void)n;
	if (blc_ll_getCurrentState() == BLS_LINK_STATE_CONN && ((u32)(bls_pm_getSystemWakeupTick() - clock_time())) > 80 * SYSTEM_TIMER_TICK_1MS)
	{										   // suspend 时间超过 30 ms 时增加 GPIO 唤
	// 醒。
		bls_pm_setWakeupSource(PM_WAKEUP_PAD); // GPIO PAD 唤醒用于 suspend/deepsleep。
	}
}

/*
 * @brief      链路层事件回调： "BLT_EV_FLAG_ADV_DURATION_TIMEOUT"
 * @param[in]  e - 链路层事件类型
 * @param[in]  p - 事件数据指针
 * @param[in]  n - 事件数据长度
 * @return     无
 */

/*
 * @brief      链路层事件回调： "BLT_EV_FLAG_CONNECT"
 * @param[in]  e - 链路层事件类型
 * @param[in]  p - 事件数据指针
 * @param[in]  n - 事件数据长度
 * @return     无
 */

/*
 * @brief      链路层事件回调： "BLT_EV_FLAG_TERMINATE"
 * @param[in]  e - 链路层事件类型
 * @param[in]  p - 事件数据指针
 * @param[in]  n - 事件数据长度
 * @return     无
 */

/*
 * @brief      链路层事件回调： "BLT_EV_FLAG_SUSPEND_EXIT"
 * @param[in]  e - 链路层事件类型
 * @param[in]  p - 事件数据指针
 * @param[in]  n - 事件数据长度
 * @return     无
 */

/*
 * @brief      链路层事件回调： "BLT_EV_FLAG_DATA_LENGTH_EXCHANGE"
 * @param[in]  e - 链路层事件类型
 * @param[in]  p - 事件数据指针
 * @param[in]  n - 事件数据长度
 * @return     无
 */

/*
 * @brief      主机事件回调。
 * @param[in]  h - 主机事件类型
 * @param[in]  para - 事件数据指针
 * @param[in]  n - 事件数据长度
 * @return     0
 */

/*
 * @brief      应用低功耗管理。
 * @param	   无
 * @return     无
 */
/* 根据运行状态和通信互锁选择 SDK suspend/深睡；调试日志积压本身不增加休眠阻断条件。 */
void blt_pm_proc(void)
{
	static u16 sleep_cnt = 0;
	static u32 sleep_veryvlow_cnt = 0;
	static u32 sleep_vlow_cnt = 0;
	static u32 sleep_vnormal_cnt = 0;
	static u32 afe_comm_err_sleepcnt = 0;
	static app_pm_elapsed_ctx_t sleep_elapsed_ctx = {0};
	u32 sleep_elapsed_sec = app_pm_take_elapsed_seconds(&sleep_elapsed_ctx);

	if (sleep_elapsed_sec != 0u)
	{
#ifdef _DI_SWITCH_SYS_ONOFF
		if (!board_switch_is_on() && !gpio_read(BMS_BOARD_INT_WK_MCU_PIN))
		{
			sleep_cnt = app_pm_elapsed_limit(sleep_cnt, sleep_elapsed_sec, 3u);
			if (sleep_cnt >= 3u)
			{
				cpu_set_gpio_wakeup(BMS_BOARD_SWITCH_PIN, Level_Low, 1);
				if (app_note_sleep_and_enter_deepsleep(1u)) sleep_cnt = 0;
			}
		}
		else
		{
			sleep_cnt = 0;
		}
#endif

		if (g_stCellInfoReport.u16VCellMin < 2550)
		{
			sleep_vlow_cnt = 0;
			sleep_vnormal_cnt = 0;
			afe_comm_err_sleepcnt = 0;

			sleep_veryvlow_cnt = app_pm_elapsed_limit(sleep_veryvlow_cnt, sleep_elapsed_sec, (60 * 60 * 1));
			if (sleep_veryvlow_cnt >= (60 * 60 * 1))
			{
				if (app_note_sleep_and_enter_deepsleep(1u)) sleep_veryvlow_cnt = 0;
			}
		}
		// else if ((g_stCellInfoReport.u16VCellMin <= 2750 && !g_stCellInfoReport.u16Ichg) || deepsleep_en)
		else if ((g_stCellInfoReport.u16VCellMin < __SLEEP_VLOW__))
		{
			sleep_veryvlow_cnt = 0;
			sleep_vnormal_cnt = 0;
			afe_comm_err_sleepcnt = 0;
			// if(deepsleep_en) {
			// 	deepsleep_en = false;
			// 	sleep_vlow_cnt = (60 * 60 * 1);
			// }
			sleep_vlow_cnt = app_pm_elapsed_limit(sleep_vlow_cnt, sleep_elapsed_sec, __SLEEP_TIMEVLOW__);
			if (sleep_vlow_cnt >= __SLEEP_TIMEVLOW__)
			{
				if (app_note_sleep_and_enter_deepsleep(1u)) sleep_vlow_cnt = 0;
			}
		}
		else if ((g_stCellInfoReport.u16VCellMin < __SLEEP_VNORMAL__ && !g_stCellInfoReport.u16Ichg))
		{
			sleep_veryvlow_cnt = 0;
			sleep_vlow_cnt = 0;
			afe_comm_err_sleepcnt = 0;

			sleep_vnormal_cnt = app_pm_elapsed_limit(sleep_vnormal_cnt, sleep_elapsed_sec, __SLEEP_TIMENORMAL__);
			if (sleep_vnormal_cnt >= __SLEEP_TIMENORMAL__)
			// if (sleep_vnormal_cnt >= (60 * 30))
			{
				if (app_note_sleep_and_enter_deepsleep(1u)) sleep_vnormal_cnt = 0;
			}
		}
		else if (bms_error_get(BMS_ERROR_AFE1) != 0u)
		{
			sleep_veryvlow_cnt = 0;
			sleep_vlow_cnt = 0;
			sleep_vnormal_cnt = 0;

			afe_comm_err_sleepcnt = app_pm_elapsed_limit(afe_comm_err_sleepcnt, sleep_elapsed_sec, (60 * 30));
			if (afe_comm_err_sleepcnt >= (60 * 30))
			{
				cpu_set_gpio_wakeup(BMS_BOARD_SWITCH_PIN, Level_Low, 1);
				if (app_note_sleep_and_enter_deepsleep(1u)) afe_comm_err_sleepcnt = 0;
			}
		}
		else
		{
			sleep_veryvlow_cnt = 0;
			sleep_vlow_cnt = 0;
			sleep_vnormal_cnt = 0;
			afe_comm_err_sleepcnt = 0;
		}
	}

	bls_pm_setSuspendMask(SUSPEND_ADV | SUSPEND_CONN);
	s_low_power_mode = true;
	// 此处不处理 keyScan/button_detect 功耗；需要时参考 ble_remote 示例。
	if (0)
	{
	}
#if (UI_KEYBOARD_ENABLE)
	else if (scan_pin_need || key_not_released)
	{
		bls_pm_setSuspendMask(SUSPEND_DISABLE);
	}
#elif (UI_BUTTON_ENABLE)
	else if (button_not_released)
	{
		bls_pm_setSuspendMask(SUSPEND_DISABLE);
	}
#endif
#if (BLE_OTA_SERVER_ENABLE)
	else if (ota_is_working)
	{
		s_low_power_mode = false;
		bls_pm_setManualLatency(0);
		bls_pm_setSuspendMask(SUSPEND_DISABLE);
	}
#endif

	// if(!gpio_read(BMS_BOARD_SWITCH_PIN) || g_stCellInfoReport.u16IDischg || )
	if (!gpio_read(BMS_BOARD_SWITCH_PIN) ||
		SH3673510_FIXED_UART_BLOCKS_PM ||
		uart_tx_is_busy() || modbus_uart_tx_active() ||
		g_stCellInfoReport.u16IDischg ||
		// MODE_FACTORY == Runtime_GetMode() ||
		ota_is_working)
	// if(
	// 	g_stCellInfoReport.u16IDischg
	// 	)
	{
		s_low_power_mode = false;
		bls_pm_setSuspendMask(SUSPEND_DISABLE);
	}
	else if (device_in_connection_state)
	{
		s_low_power_mode = false;
	}
}

/*
 * @brief		MCU 上电或普通深睡唤醒时初始化应用。
 * @param[in]	无
 * @return      无
 */

/*
 * @brief		MCU 深睡保留唤醒时恢复应用。
 * @param[in]	无
 * @return      无
 */

/////////////////////////////////////////////////////////////////////s
// 主循环流程。
/////////////////////////////////////////////////////////////////////

/* 200 ms 采样调度的唯一主循环入口；无效 AFE 样本不得推进 SOC，超时只合并一次补采。 */

/*
 * @brief		应用主循环入口。
 * @param[in]	无
 * @return      无
 */
/* 推进 SDK 与应用主循环任务，业务处理留在主循环。 */
_attribute_no_inline_ void main_loop(void)
{
	// BLE 处理入口。
	blt_sdk_main_loop();
	Runtime_Poll();
	// UI 处理入口。
	// 电池检查。

#if BMS_DEBUG_LOG_ENABLE
    /* 在此观察 SDK 回调负责的标志，ISR 不格式化或生产日志。 */
    {
        static u8 last_link = 0xFFu, last_ota = 0xFFu;
        if (last_link != (u8)device_in_connection_state) {
            last_link = (u8)device_in_connection_state;
            BMS_LOG(BMS_LOG_INFO, BMS_LOG_COMM, BMS_LOG_LINK_STATE, last_link, 0u);
        }
        if (last_ota != ota_is_working) {
            last_ota = ota_is_working;
            BMS_LOG(BMS_LOG_INFO, BMS_LOG_OTA, BMS_LOG_OTA_STATE, last_ota, 0u);
        }
    }
#endif
    app_sample_task();
	_attribute_data_retention_ static u32 update_bms_info_tick = 0;
	if (clock_time_exceed(update_bms_info_tick, 1000 * 1000))
	{
		// 待办：低功耗，时基偏移
		update_bms_info_tick = clock_time();
		app_event_log_1s_task();
	}
#ifdef _FUNC_UART_
	main_loop_modbus();
#endif
	bms_state_store_update_and_log_if_changed(SOC_Calculate_Element.u8SOC_Now, SOC_Calculate_Element.u8DSG_SOC_Int, SOC_Calculate_Element.u32Cycle_times);
	// bms_state_store_update_and_log_if_changed(g_stCellInfoReport.SocElement.u16Soc, SOC_Calculate_Element.u8DSG_SOC_Int, SOC_Calculate_Element.u32Cycle_times);
	// nvm_process();
	// 电源管理处理。
	blt_pm_proc();
}

#endif

/* 采集并处理一次 AFE 样本，推进保护、SOC 和 MOS 仲裁。 */
static void app_sample_task(void)
{
    bms_afe_aux_measurements_t m;
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    bms_soc_diag_t soc_diag;
#endif
    u8 valid;

    if (!s_sample_due && !clock_time_exceed(s_sample_tick, APP_SAMPLE_PERIOD_US)) return;

    s_sample_due = 0u;
    s_sample_tick = clock_time();
    bms_afe_sample();
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    valid = app_get_fresh_measurements(&m);
#else
    valid = bms_afe_get_aux_measurements(&m);
#endif
    app_update_soc_from_sample(valid, valid ? m.current_ma : 0,
                           valid ? m.sample_tick_32k : pm_get_32k_tick());
    mos_update();

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    bms_diag_runtime_sample(valid,
                            valid ? m.raw_current_ma : 0,
                            valid ? m.current_ma : 0,
                            valid ? m.sample_tick_32k : pm_get_32k_tick(),
                            bms_afe_current_recovery_pending());
    bms_soc_get_diag(&soc_diag);
    bms_diag_runtime_soc(soc_diag.soc_estimate, soc_diag.soc_display,
                         soc_diag.ocv_state, soc_diag.ocv_center,
                         soc_diag.ocv_low, soc_diag.ocv_high,
                         soc_diag.ocv_confidence, soc_diag.rest_seconds,
                         soc_diag.learning_state, soc_diag.capacity_learned,
                         soc_diag.learned_capacity_0p1ah,
                         soc_diag.current_deadband_ma);
    bms_diag_runtime_soc_extended(&soc_diag);
    bms_diag_runtime_faults(g_stCellInfoReport.unMdlFault_First.all,
                            g_stCellInfoReport.unMdlFault_Second.all,
                            g_stCellInfoReport.unMdlFault_Third.all);
#else
    bms_diag_poll_runtime(valid, valid ? m.current_ma : 0,
                         valid ? m.sample_tick_32k : pm_get_32k_tick(),
                         (Runtime_GetMode() == MODE_FACTORY) ? 1u : 0u);
#endif

    /* 即使 BLE 广播间隔为 800 ms，也保持固定采样节拍。 */
    if (clock_time_exceed(s_sample_tick, APP_SAMPLE_PERIOD_US)) s_sample_due = 1u;
    app_schedule_sample_wakeup();
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    gpio_toggle(LED_BLUE_PIN);
#endif
}

/* 四产品共用 BLE 回调；后端差异留在采样和电源流程。 */
/* 采样唤醒回调只置位，由主循环执行 AFE 和 SOC 处理。 */
static void app_sample_wakeup(int type)
{
    (void)type;
    s_sample_due = 1u;
}

/* 构造广播与扫描响应中的产品名称和数据。 */
void ble_build_adv_scanrsp(void)
{
	u8 i = 0;

	// 广播包含 Flags、Appearance 和 UUID 列表，名称放在 scanRsp 中。
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

/* 切换为非定向 BLE 广播。 */
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

/* 处理 BLE 连接建立并更新连接与低功耗状态。 */
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

/* 恢复 suspend 后的外设与应用时序状态。 */
void task_suspend_exit(u8 e, u8 *p, int n)
{
#if BMS_DEBUG_LOG_ENABLE
    ++s_debug_suspend_exits; /* 此处不写环形缓存、不读 tick、不格式化、不执行 I/O。 */
#endif
	(void)e;
	(void)p;
	(void)n;
	rf_set_power_level_index(MY_RF_POWER_INDEX);
}

/* 处理 BLE 数据长度交换事件。 */
void task_dle_exchange(u8 e, u8 *p, int n)
{
	tlk_contr_evt_dataLenExg_t *pEvt = (tlk_contr_evt_dataLenExg_t *)p;
	tlkapi_send_string_data(APP_CONTR_EVENT_LOG_EN, "[APP][EVT] DLE exchange", &pEvt->connEffectiveMaxRxOctets, 4);
}

/* 分派 BLE 主机事件并更新应用连接状态。 */
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

#if (APP_FLASH_PROTECTION_ENABLE)

_attribute_data_retention_ u16 flash_lockBlock_cmd = 0;
_attribute_data_retention_ static u8 g_app_flash_stack_session_active = 0;

/* 判断 Flash 操作后是否需要恢复保护锁。 */
int app_flash_lock_restore_enabled(void)
{
	return (g_app_flash_stack_session_active == 0u);
}

/* 按 SDK Flash 操作阶段解锁或恢复保护范围。 */
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
/* 判断 Flash 操作后是否需要恢复保护锁。 */
int app_flash_lock_restore_enabled(void)
{
    return 1;
}
#endif

/* 正常启动时初始化参数、硬件、BLE 与应用状态。 */
_attribute_no_inline_ void user_init_normal(void)
{

	// 基础硬件初始化开始。

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

	// 基础硬件初始化结束。

	// BLE 协议栈初始化开始。
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
	#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    bls_smp_configSecurityRequestSending(SecReq_IMM_SEND, SecReq_PEND_SEND, 1000);
#else
    blc_smp_configSecurityRequestSending(SecReq_IMM_SEND, SecReq_PEND_SEND, 1000);
#endif
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
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
        bms_diag_set_build_flags((DVC1124_SW_PROTECT_ENABLE ? 1u : 0u) |
                                 (DVC1124_HW_PROTECT_ENABLE ? 2u : 0u) |
                                 (BMS_PRODUCTION_BUILD ? 4u : 0u) |
                                 (BMS_DIAG_BUILD_DIRTY ? 8u : 0u));
#else
        bms_diag_set_build_flags((SH3673510_SW_PROTECT_ENABLE ? 1u : 0u) |
                                 (SH3673510_HW_PROTECT_ENABLE ? 2u : 0u) |
                                 (BMS_PRODUCTION_BUILD ? 4u : 0u) |
                                 (BMS_DIAG_BUILD_DIRTY ? 8u : 0u));
#endif
		board_init();
		bms_parameters_startup();
		LoadParam();
		bms_event_log_init();

		bms_afe_init();
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
        cpu_set_gpio_wakeup(CHG_IN_PIN, Level_Low, 0);
        cpu_set_gpio_wakeup(ACC_MCU_PIN, Level_Low, 0);
#else
        cpu_set_gpio_wakeup(BMS_BOARD_SWITCH_PIN, Level_Low, 1);
#endif

		/* 启动电压、电流和温度状态使用同一 AFE 快照。 */
		bms_afe_sample();
		bms_state_store_init();
		bms_state_store_data_t d = bms_state_store_get();
		soc_param_lib_init(&d);
	}

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    sif_timer_init();
    bus_mux_init();
#else
    modbus_uart_init();
#endif
	btname_init();
	bms_event_log_note_startup();
	Runtime_Init();
    s_sample_tick = clock_time();
    s_sample_due = 0u;
    bls_pm_registerAppWakeupLowPowerCb(app_sample_wakeup);
    app_schedule_sample_wakeup();
	mos_update();

	extern void WriteProID_Default(void);
	WriteProID_Default();
	bms_afe_set_output_enabled(1u);
    bms_diag_set_boot_result(
        g_bms_system_status.bits.b1Status_AFE1 ? DIAG_OK : DIAG_INVALID,
        bms_protection_params_valid() ? DIAG_OK : DIAG_INVALID);
    bms_param_diag_poll();
    bms_storage_platform_diag_poll();
    bms_afe_diag_poll();
    bms_diag_freeze_boot();
}

/* 深睡保留唤醒时恢复 SDK 与应用必要状态。 */
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

/* 处理 BLE 断开并恢复广播和电源策略。 */
void task_terminate(u8 e, u8 *p, int n)
{
	(void)e;
	(void)n;

	device_in_connection_state = 0;

	tlk_contr_evt_terminate_t *pEvt = (tlk_contr_evt_terminate_t *)p;
    /* 每次 BLE 断连撤销设备范围的授权；UART 也必须在该边界后取得新 token。 */
    bms_afe_hw_access_close();

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

/* 把合格 AFE 样本及时间戳提交给 SOC 算法。 */
static void app_update_soc_from_sample(uint8_t valid, int32_t current_ma,
                             uint32_t sample_tick_32k)
{
    bms_afe_feature_snapshot_t feature;
    bms_soc_sample_t sample;
    uint16_t third_faults = g_stCellInfoReport.unMdlFault_Third.all;
    memset(&sample, 0, sizeof(sample));
    memset(&feature, 0, sizeof(feature));

    sample.timestamp_32k = sample_tick_32k;
    sample.current_ma = current_ma;
    sample.pack_voltage_mv = (uint32_t)g_stCellInfoReport.u16VCellTotle * 10u;
    sample.cell_min_mv = g_stCellInfoReport.u16VCellMin;
    sample.cell_max_mv = g_stCellInfoReport.u16VCellMax;
    sample.cell_delta_mv = g_stCellInfoReport.u16VCellDelta;
    sample.sample_valid = valid ? 1u : 0u;
    sample.voltage_valid = (valid && sample.cell_min_mv != 0u &&
                            sample.cell_max_mv >= sample.cell_min_mv) ? 1u : 0u;
    if (valid && bms_afe_get_feature_snapshot(&feature) &&
        feature.valid && feature.battery_temp_valid) {
        sample.temperature_valid = 1u;
        sample.temperature_min_x10 = feature.battery_temp_min_x10;
        sample.temperature_max_x10 = feature.battery_temp_max_x10;
    }
    sample.balancing_active = bms_features_balance_active();
    sample.heating_active = bms_features_heater_on();
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    sample.open_wire_active = bms_features_openwire_sample_active();
#else
    sample.open_wire_active = bms_features_openwire_active();
#endif
    sample.open_wire_suspected = bms_features_openwire_suspected();
    sample.afe_fault = bms_error_get(BMS_ERROR_AFE1);
    sample.temperature_fault =
        ((third_faults & SOC_LEARNING_TEMP_FAULT_MASK) != 0u) ? 1u : 0u;
    sample.current_fault =
        ((third_faults & SOC_LEARNING_CURRENT_FAULT_MASK) != 0u) ? 1u : 0u;
    sample.pack_fault =
        ((third_faults & SOC_LEARNING_PACK_FAULT_MASK) != 0u) ? 1u : 0u;
    sample.third_cell_ovp =
        g_stCellInfoReport.unMdlFault_Third.bits.b1CellOvp;
    sample.third_cell_uvp =
        g_stCellInfoReport.unMdlFault_Third.bits.b1CellUvp;
    sample.charger_state_known = 1u;
    sample.charger_present = bms_features_charge_session_active();
    bms_soc_process_sample(&sample);
}
