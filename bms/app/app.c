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
#include "bms_product.h"
#include "bms_afe_backend.h"
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
#include "sh3673510_project_config.h"
#endif
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
#include "bms_parameters.h"
#include "bms_parameter_access.h"
#include <string.h>

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
#include "sif_send.h"
#include "bus_mux.h"
#endif

/* 采集并处理一次 AFE 样本，推进保护、SOC 和 MOS 仲裁。 */
static void app_sample_task(void);
/* 把合格 AFE 样本及时间戳提交给 SOC 算法。 */
static void app_update_soc_from_sample(uint8_t valid, int32_t current_ma,
                             uint32_t sample_tick_32k);
/* 采样唤醒回调只置位，由主循环执行 AFE 和 SOC 处理。 */
static void app_sample_wakeup(int type);
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
#define SOC_SAMPLE_PACK_FAULT_MASK         0x000Cu
#define SOC_SAMPLE_CURRENT_FAULT_MASK      0x0030u
#define SOC_SAMPLE_TEMP_FAULT_MASK         0x2BC0u

static bool s_low_power_mode;
bool deepsleep_en = false;
// nvm_cfg_t nvm_cfg;

/* 四产品同口请求均为 CHG/DSG 开启；guard 授权、方向保护和后端反馈决定实际输出。
 * D008 ACC/PB1 和 SH PA0 均不能代替已有安全门禁。 */
void mos_update(void)
{
    g_bms_system_status.bits.b1Status_Cool = 0u;
    (void)bms_afe_set_fets(1u, 1u);
}

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
static uint8_t board_switch_is_on(void)
{
#if BMS_PRODUCT_SWITCH_ENABLE
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

    _attribute_data_retention_ static u32 event_log_tick = 0;

    if (!clock_time_exceed(event_log_tick, 1000 * 1000)) return;
    event_log_tick = clock_time();

	memset(&sample, 0, sizeof(sample));

	sample.balance = ((g_bms_report.u16BalanceFlag1 != 0u) || (g_bms_report.u16BalanceFlag2 != 0u)) ? 1u : 0u;

	sample.vcell_ovp = g_bms_report.unMdlFault_Third.bits.b1CellOvp ? 1u : 0u;
	sample.vbus_ovp = g_bms_report.unMdlFault_Third.bits.b1BatOvp ? 1u : 0u;
	sample.chg_ocp = g_bms_report.unMdlFault_Third.bits.b1IchgOcp ? 1u : 0u;
	sample.vcell_uvp = g_bms_report.unMdlFault_Third.bits.b1CellUvp ? 1u : 0u;
	sample.vbus_uvp = g_bms_report.unMdlFault_Third.bits.b1BatUvp ? 1u : 0u;
	sample.dsg_ocp = g_bms_report.unMdlFault_Third.bits.b1IdischgOcp ? 1u : 0u;
	sample.chg_utp = g_bms_report.unMdlFault_Third.bits.b1CellChgUtp ? 1u : 0u;
	sample.dsg_utp = g_bms_report.unMdlFault_Third.bits.b1CellDischgUtp ? 1u : 0u;
	sample.chg_otp = g_bms_report.unMdlFault_Third.bits.b1CellChgOtp ? 1u : 0u;
	sample.dsg_otp = g_bms_report.unMdlFault_Third.bits.b1CellDischgOtp ? 1u : 0u;
	sample.vdelta_op = g_bms_report.unMdlFault_Third.bits.b1VcellDeltaBig ? 1u : 0u;

	sample.afe1_err = bms_error_get(BMS_ERROR_AFE1) ? 1u : 0u;
	sample.cbc_err = bms_error_get(BMS_ERROR_CBC_DSG) ? 1u : 0u;

	bms_event_log_poll_1s(&sample);
#if BMS_DEBUG_LOG_ENABLE
    {
        static u32 previous_exits;
        u32 exits = app_ble_suspend_exit_count();
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
    if (!bms_state_store_write_all(g_bms_soc.u8SOC_Now,
                               g_bms_soc.u8DSG_SOC_Int,
                               g_bms_soc.u32Cycle_times) ||
        !bms_event_log_note_sleep()) return 0;
    if (!bms_afe_enter_shutdown()) {
        bms_event_log_cancel_sleep();
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
    if (!bms_state_store_write_all(g_bms_soc.u8SOC_Now,
                               g_bms_soc.u8DSG_SOC_Int,
                               g_bms_soc.u32Cycle_times) ||
        !bms_event_log_note_sleep()) return 0;
    if (!gpio_read(ACC_MCU_PIN)) { bms_event_log_cancel_sleep(); return 0; }
    if (bls_ll_setAdvEnable(BLC_ADV_DISABLE) != BLE_SUCCESS) {
        bms_event_log_cancel_sleep(); return 0;
    }
    if (!bms_afe_enter_shutdown()) {
        bms_event_log_cancel_sleep();
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

    /* SH 保持低功耗优先级：保存失败报告诊断，不改变原来的入睡门禁。 */
    if (!bms_state_store_write_all(g_bms_soc.u8SOC_Now,
                                  g_bms_soc.u8DSG_SOC_Int,
                                  g_bms_soc.u32Cycle_times))
        BMS_LOG(BMS_LOG_WARN, BMS_LOG_POWER, BMS_LOG_SLEEP_ATTEMPT, need_afe_sleep, 1u);
    if (!bms_event_log_note_sleep())
        BMS_LOG(BMS_LOG_WARN, BMS_LOG_POWER, BMS_LOG_SLEEP_ATTEMPT, need_afe_sleep, 2u);
    sleep_status = cpu_sleep_wakeup(DEEPSLEEP_MODE, PM_WAKEUP_PAD, 0);
    bms_event_log_cancel_sleep();

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


#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
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
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
/* 处理 SDK 休眠进入事件并保存必要状态。 */
void task_sleep_enter(u8 e, u8 *p, int n)
{
    (void)e;
    (void)p;
    (void)n;
    /* 无 ACC/负载 PAD 唤醒策略，采样由 SDK 应用定时器限定。 */
}

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
        if (g_bms_report.u16VCellMin < 2550u)
        {
            region = 1u;
            limit_seconds = 3600u;
        }
        else if (g_bms_report.u16VCellMin < BMS_SLEEP_LOW_CELL_MV)
        {
            region = 2u;
            limit_seconds = BMS_SLEEP_LOW_SECONDS;
        }
        else if (g_bms_report.u16VCellMin < BMS_SLEEP_NORMAL_CELL_MV && m.current_ma >= 0)
        {
            region = 3u;
            limit_seconds = BMS_SLEEP_NORMAL_SECONDS;
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

#else
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
#if BMS_PRODUCT_SWITCH_ENABLE
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

		if (g_bms_report.u16VCellMin < 2550)
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
		// else if ((g_bms_report.u16VCellMin <= 2750 && !g_bms_report.u16Ichg) || deepsleep_en)
		else if ((g_bms_report.u16VCellMin < BMS_SLEEP_LOW_CELL_MV))
		{
			sleep_veryvlow_cnt = 0;
			sleep_vnormal_cnt = 0;
			afe_comm_err_sleepcnt = 0;
			// if(deepsleep_en) {
			// 	deepsleep_en = false;
			// 	sleep_vlow_cnt = (60 * 60 * 1);
			// }
			sleep_vlow_cnt = app_pm_elapsed_limit(sleep_vlow_cnt, sleep_elapsed_sec, BMS_SLEEP_LOW_SECONDS);
			if (sleep_vlow_cnt >= BMS_SLEEP_LOW_SECONDS)
			{
				if (app_note_sleep_and_enter_deepsleep(1u)) sleep_vlow_cnt = 0;
			}
		}
		else if ((g_bms_report.u16VCellMin < BMS_SLEEP_NORMAL_CELL_MV && !g_bms_report.u16Ichg))
		{
			sleep_veryvlow_cnt = 0;
			sleep_vlow_cnt = 0;
			afe_comm_err_sleepcnt = 0;

			sleep_vnormal_cnt = app_pm_elapsed_limit(sleep_vnormal_cnt, sleep_elapsed_sec, BMS_SLEEP_NORMAL_SECONDS);
			if (sleep_vnormal_cnt >= BMS_SLEEP_NORMAL_SECONDS)
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

	// if(!gpio_read(BMS_BOARD_SWITCH_PIN) || g_bms_report.u16IDischg || )
	if (!gpio_read(BMS_BOARD_SWITCH_PIN) ||
		SH3673510_FIXED_UART_BLOCKS_PM ||
		uart_tx_is_busy() || modbus_uart_tx_active() ||
		g_bms_report.u16IDischg ||

		ota_is_working)
	// if(
	// 	g_bms_report.u16IDischg
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

#endif

/* 一条主循环：BLE → 到期采样 → 通信 → 状态检查点 → 板级低功耗。 */
_attribute_no_inline_ void main_loop(void)
{
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    if (s_acc_sleep_committed) {
        app_acc_sleep_hold();
        return;
    }
    bms_parameters_diag_poll();
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
#endif
    /*
     * 协作顺序：先服务 BLE，到期只采样一次，再处理通信和持久化；
     * 最后根据新状态评估 suspend。
     */
	blt_sdk_main_loop();


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

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
	bus_mux_task();
    sif_prepare_task(s_sample_tick);
#endif
#if BMS_PRODUCT_UART_ENABLE
	main_loop_modbus();
#endif
	bms_state_store_update_and_log_if_changed(g_bms_soc.u8SOC_Now, g_bms_soc.u8DSG_SOC_Int, g_bms_soc.u32Cycle_times);
	blt_pm_proc();
}

/* 采集并处理一次 AFE 样本，推进保护、SOC 和 MOS 仲裁。 */
static void app_sample_task(void)
{
    bms_afe_aux_measurements_t m;
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

    bms_diag_poll_runtime(valid, valid ? m.raw_current_ma : 0,
                          valid ? m.current_ma : 0,
                          valid ? m.sample_tick_32k : pm_get_32k_tick());

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



/* 把合格 AFE 样本及时间戳提交给 SOC 算法。 */
static void app_update_soc_from_sample(uint8_t valid, int32_t current_ma,
                             uint32_t sample_tick_32k)
{
    bms_afe_feature_snapshot_t feature;
    bms_features_status_t status;
    bms_soc_sample_t sample;
    uint16_t third_faults = g_bms_report.unMdlFault_Third.all;
    memset(&sample, 0, sizeof(sample));
    memset(&feature, 0, sizeof(feature));

    sample.timestamp_32k = sample_tick_32k;
    sample.current_ma = current_ma;
    sample.pack_voltage_mv = (uint32_t)g_bms_report.u16VCellTotle * 10u;
    sample.cell_min_mv = g_bms_report.u16VCellMin;
    sample.cell_max_mv = g_bms_report.u16VCellMax;
    sample.cell_delta_mv = g_bms_report.u16VCellDelta;
    sample.sample_valid = valid ? 1u : 0u;
    sample.voltage_valid = (valid && sample.cell_min_mv != 0u &&
                            sample.cell_max_mv >= sample.cell_min_mv) ? 1u : 0u;
    if (valid && bms_afe_get_feature_snapshot(&feature) &&
        feature.valid && feature.battery_temp_valid) {
        sample.temperature_valid = 1u;
        sample.temperature_min_x10 = feature.battery_temp_min_x10;
        sample.temperature_max_x10 = feature.battery_temp_max_x10;
    }
    bms_features_get_status(&status);
    sample.balancing_active = status.balance_active;
    sample.heating_active = status.heater_on;
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    sample.open_wire_active = status.openwire_sample_active;
#else
    sample.open_wire_active = status.openwire_active;
#endif
    sample.open_wire_suspected = status.openwire_suspected;
    sample.afe_fault = bms_error_get(BMS_ERROR_AFE1);
    sample.temperature_fault =
        ((third_faults & SOC_SAMPLE_TEMP_FAULT_MASK) != 0u) ? 1u : 0u;
    sample.current_fault =
        ((third_faults & SOC_SAMPLE_CURRENT_FAULT_MASK) != 0u) ? 1u : 0u;
    sample.pack_fault =
        ((third_faults & SOC_SAMPLE_PACK_FAULT_MASK) != 0u) ? 1u : 0u;
    sample.third_cell_ovp =
        g_bms_report.unMdlFault_Third.bits.b1CellOvp;
    sample.third_cell_uvp =
        g_bms_report.unMdlFault_Third.bits.b1CellUvp;
    sample.charger_state_known = 1u;
    sample.charger_present = status.charge_session_active;
    bms_soc_process_sample(&sample);
}

/* 参数、AFE 与业务状态启动；由平台完成 BLE 初始化后调用一次。 */
void app_init(void)
{
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
		bms_parameters_init();

		bms_afe_init();
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
        cpu_set_gpio_wakeup(CHG_IN_PIN, Level_Low, 0);
        cpu_set_gpio_wakeup(ACC_MCU_PIN, Level_Low, 0);
#else
        cpu_set_gpio_wakeup(BMS_BOARD_SWITCH_PIN, Level_Low, 1);
#endif

		/* 启动电压、电流和温度状态使用同一 AFE 快照。 */
		bms_afe_sample();
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

    s_sample_tick = clock_time();
    s_sample_due = 0u;
    bls_pm_registerAppWakeupLowPowerCb(app_sample_wakeup);
    app_schedule_sample_wakeup();
	mos_update();

	bms_product_info_refresh();
	bms_afe_set_output_enabled(1u);
    bms_diag_set_boot_result(
        g_bms_system_status.bits.b1Status_AFE1 ? DIAG_OK : DIAG_INVALID,
        bms_protection_params_valid() ? DIAG_OK : DIAG_INVALID);
    bms_parameters_diag_poll();
    bms_storage_platform_diag_poll();
    bms_afe_diag_poll();
    bms_diag_freeze_boot();
}
