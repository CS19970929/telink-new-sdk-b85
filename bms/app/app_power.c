/* 低功耗状态由本模块持有；采样标志由 app.c 持有，PM 仅按 volatile 语义读取。 */
/********************************************************************************************************
 * @file    app_power.c
 *
 * @brief   产品低功耗与 Telink PM 策略。
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
#include "app_power.h"
#include "modbus_uart.h"
#include "bms_afe.h"
#include "bms_diag.h"
#include "bms_error.h"
#include "bms_state.h"
#include "bms_soc.h"
#include "bms_event_log.h"
#include "bms_state_store.h"
#include "bms_storage_platform.h"
#include "bms_parameters.h"

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
#include "bus_mux.h"
#endif

#define APP_PM_TICKS_PER_SEC 32000u

typedef struct
{
	u32 last_tick_32k;
	u32 pending_tick_32k;
	u8 ready;
} app_pm_elapsed_ctx_t;
static bool s_low_power_mode;
bool deepsleep_en = false;
/* 仅用于诊断最后一次入睡失败，不参与原有门禁或计时。 */
static u32 s_sleep_failure_mask;
static u8 s_sleep_report_reason;

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
#define APP_SUSPEND_EXIT_CURRENT_MA 500
#define APP_POWER_OFF_RETRY_SECONDS 5u
#define APP_ACC_HIGH_STABLE_TICKS (APP_PM_TICKS_PER_SEC / 5u)
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

#else
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

/* 退避剩余值使用原 32K 时间戳，按毫秒向上取整。 */
static u32 app_sleep_retry_ms(u8 ready, u32 started, u32 seconds)
{
    u32 elapsed = (u32)(pm_get_32k_tick() - started);
    u32 limit = seconds * APP_PM_TICKS_PER_SEC;
    return ready && elapsed < limit ? (limit - elapsed + 31u) / 32u : 0u;
}

static void app_publish_sleep(u8 reason, u32 block, u32 elapsed_ms,
                              u32 delay_ms, u32 retry_ms, u8 suspend_allowed)
{
    if (reason != s_sleep_report_reason) s_sleep_failure_mask = 0u;
    s_sleep_report_reason = reason;
    bms_diag_sleep(reason, block | s_sleep_failure_mask, elapsed_ms,
                    delay_ms, retry_ms, suspend_allowed);
}

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124

/* 深睡/关机与普通 suspend 的门禁不同，不能复用 pm_block 推断倒计时。 */
static void app_dvc_publish_sleep(u8 reason, u32 elapsed_ms, u32 delay_ms,
                                  u8 suspend_allowed)
{
    bms_afe_aux_measurements_t m;
    u32 block = 0u;
    u32 retry_ms;
    if (ota_is_working) block |= DIAG_SLEEP_BLOCK_OTA;
    if (!app_flash_lock_restore_enabled()) block |= DIAG_SLEEP_BLOCK_FLASH;
    if (BUS_STATE_OWC_IDLE != bus_mux_get_state()) block |= DIAG_SLEEP_BLOCK_BUS;
    if (device_in_connection_state &&
        (reason != DIAG_SLEEP_REASON_COMMAND || blc_ll_getTxFifoNumber() != 0u))
        block |= DIAG_SLEEP_BLOCK_BLE;
    if (reason >= DIAG_SLEEP_REASON_VERY_LOW && !app_get_fresh_measurements(&m))
        block |= DIAG_SLEEP_BLOCK_SAMPLE;
    retry_ms = reason == DIAG_SLEEP_REASON_ACC ?
        app_sleep_retry_ms(s_acc_retry_ready, s_acc_retry_tick, APP_POWER_OFF_RETRY_SECONDS) :
        app_sleep_retry_ms(s_power_off_retry_ready, s_power_off_retry_tick, APP_POWER_OFF_RETRY_SECONDS);
    if (reason == DIAG_SLEEP_REASON_NONE) {
        retry_ms = 0u;
        if (!app_get_fresh_measurements(&m)) block |= DIAG_SLEEP_BLOCK_SAMPLE;
    }
    app_publish_sleep(reason, block, elapsed_ms, delay_ms, retry_ms, suspend_allowed);
}

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
    s_sleep_failure_mask = 0u;

    /*
     * PC4 拉低后不能再延后执行 Flash 操作；休眠事件记录尝试，
     * shutdown 失败绝不切断供电。
     */
    if (!bms_state_store_write_all(g_bms_soc.soc_estimate_percent,
                               g_bms_soc.discharge_fraction_percent,
                               g_bms_soc.cycle_count) ||
        !bms_event_log_note_sleep()) {
        s_sleep_failure_mask = DIAG_SLEEP_BLOCK_STORAGE;
        return 0;
    }
    if (!bms_afe_enter_shutdown()) {
        bms_event_log_cancel_sleep();
        s_sleep_failure_mask = DIAG_SLEEP_BLOCK_AFE;
        BMS_LOG(BMS_LOG_WARN, BMS_LOG_POWER, BMS_LOG_SLEEP_ABORT, 1u, 1u);
        return 0;
    }

    s_power_off_committed = 1u;
    bls_pm_setAppWakeupLowPower(0u, 0u);
    s_low_power_mode = true;
    bms_diag_sleep_committed();
    gpio_write(BMS_BOARD_MCU_LDO_PIN, 0u); /* 最后硬件动作使整个 MCU 掉电。 */
    return 1;
}

/*
 * ACC 休眠保持 PC4 高；深睡唤醒走完整正常启动，
 * 不能对已主动 shutdown 的 AFE 直接恢复采样。
 */
static void app_acc_sleep_hold(void)
{
    if (!gpio_read(BMS_BOARD_ACC_PIN)) {
        start_reboot();
        return;
    }
    cpu_set_gpio_wakeup(BMS_BOARD_ACC_PIN, Level_Low, 1);
    cpu_sleep_wakeup(DEEPSLEEP_MODE, PM_WAKEUP_PAD, 0u);
    /* PAD 可能在检查后、入睡前变为有效。 */
    if (!gpio_read(BMS_BOARD_ACC_PIN)) start_reboot();
}

/* 判断 ACC 条件是否请求进入休眠。 */
static int app_acc_sleep_requested(void)
{
    u32 now = pm_get_32k_tick();
    if (!gpio_read(BMS_BOARD_ACC_PIN)) {
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
    if (!gpio_read(BMS_BOARD_ACC_PIN) || ota_is_working ||
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
    s_sleep_failure_mask = 0u;
    if (!bms_state_store_write_all(g_bms_soc.soc_estimate_percent,
                               g_bms_soc.discharge_fraction_percent,
                               g_bms_soc.cycle_count) ||
        !bms_event_log_note_sleep()) {
        s_sleep_failure_mask = DIAG_SLEEP_BLOCK_STORAGE;
        return 0;
    }
    if (!gpio_read(BMS_BOARD_ACC_PIN)) { bms_event_log_cancel_sleep(); return 0; }
    if (bls_ll_setAdvEnable(BLC_ADV_DISABLE) != BLE_SUCCESS) {
        s_sleep_failure_mask = DIAG_SLEEP_BLOCK_BLE_CONTROL;
        bms_event_log_cancel_sleep(); return 0;
    }
    if (!bms_afe_enter_shutdown()) {
        bms_event_log_cancel_sleep();
        s_sleep_failure_mask = DIAG_SLEEP_BLOCK_AFE;
        bls_ll_setAdvEnable(BLC_ADV_ENABLE);
        return 0;
    }
    s_acc_sleep_committed = 1u;
    bls_pm_setSuspendMask(SUSPEND_DISABLE);
    bls_pm_setAppWakeupLowPower(0u, 0u);
    s_low_power_mode = true;
    gpio_write(BMS_BOARD_MCU_LDO_PIN, 1u);
    cpu_set_gpio_wakeup(BMS_BOARD_LOAD_DETECT_PIN, Level_Low, 0);
    bms_diag_sleep_committed();
    app_acc_sleep_hold();
    return 1;
}

#else

static u32 s_sleep_last_attempt_tick_32k;
static u8 s_sleep_attempt_ready;

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

static u32 app_sh_sleep_blocks(void)
{
    u32 block = 0u;
    if (ota_is_working) block |= DIAG_SLEEP_BLOCK_OTA;
    if (!app_flash_lock_restore_enabled()) block |= DIAG_SLEEP_BLOCK_FLASH;
    if (SH3673510_FIXED_UART_BLOCKS_PM) block |= DIAG_SLEEP_BLOCK_FIXED_UART;
    if (uart_tx_is_busy() || modbus_uart_tx_active()) block |= DIAG_SLEEP_BLOCK_UART_TX;
    if (app_deepsleep_pad_wakeup_active()) block |= DIAG_SLEEP_BLOCK_WAKE_PAD;
    return block;
}

/* 多个原有计时并行时，展示最早到期的一条；不修改其他计数。 */
static void app_sh_sleep_candidate(u8 next_reason, u32 next_elapsed, u32 next_limit,
                                   u8 *reason, u32 *elapsed, u32 *limit)
{
    if (*reason == DIAG_SLEEP_REASON_NONE || next_limit - next_elapsed < *limit - *elapsed) {
        *reason = next_reason;
        *elapsed = next_elapsed;
        *limit = next_limit;
    }
}

static void app_sh_publish_sleep(u32 switch_seconds, u32 very_low_seconds,
                                 u32 low_seconds, u32 normal_seconds,
                                 u32 afe_error_seconds, u8 suspend_allowed)
{
    u8 reason = DIAG_SLEEP_REASON_NONE;
    u32 elapsed = 0u, limit = 0u;
#if BMS_PRODUCT_SWITCH_ENABLE
    if (!board_switch_is_on() && !gpio_read(BMS_BOARD_INT_WK_MCU_PIN))
        app_sh_sleep_candidate(DIAG_SLEEP_REASON_SWITCH, switch_seconds, 3u,
                                &reason, &elapsed, &limit);
#else
    (void)switch_seconds;
#endif
    if (g_bms_report.cell_min_mv < 2550u)
        app_sh_sleep_candidate(DIAG_SLEEP_REASON_VERY_LOW, very_low_seconds, 3600u,
                                &reason, &elapsed, &limit);
    else if (g_bms_report.cell_min_mv < BMS_SLEEP_LOW_CELL_MV)
        app_sh_sleep_candidate(DIAG_SLEEP_REASON_LOW, low_seconds, BMS_SLEEP_LOW_SECONDS,
                                &reason, &elapsed, &limit);
    else if (g_bms_report.cell_min_mv < BMS_SLEEP_NORMAL_CELL_MV && !g_bms_report.charge_current_a10)
        app_sh_sleep_candidate(DIAG_SLEEP_REASON_NORMAL, normal_seconds, BMS_SLEEP_NORMAL_SECONDS,
                                &reason, &elapsed, &limit);
    else if (bms_error_get(BMS_ERROR_AFE1))
        app_sh_sleep_candidate(DIAG_SLEEP_REASON_AFE, afe_error_seconds, 1800u,
                                &reason, &elapsed, &limit);
    app_publish_sleep(reason, app_sh_sleep_blocks(), elapsed * 1000u, limit * 1000u,
        reason == DIAG_SLEEP_REASON_NONE ? 0u :
        app_sleep_retry_ms(s_sleep_attempt_ready, s_sleep_last_attempt_tick_32k, 3u), suspend_allowed);
}

/*
 * 仅在 OTA、Flash、UART、总线及唤醒脚门禁满足后尝试深睡；
 * 失败保留请求并按 32K 时间退避。
 */
static int app_note_sleep_and_enter_deepsleep(u8 need_afe_sleep)
{
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
    if (s_sleep_attempt_ready && (u32)(now_tick_32k - s_sleep_last_attempt_tick_32k) <
        3u * APP_PM_TICKS_PER_SEC) return 0;
    s_sleep_last_attempt_tick_32k = now_tick_32k;
    s_sleep_attempt_ready = 1u;
    s_sleep_failure_mask = 0u;

    BMS_LOG(BMS_LOG_INFO, BMS_LOG_POWER, BMS_LOG_SLEEP_ATTEMPT, need_afe_sleep, 0u);
    if (need_afe_sleep && !bms_afe_sleep()) {
        s_sleep_failure_mask = DIAG_SLEEP_BLOCK_AFE;
        BMS_LOG(BMS_LOG_WARN, BMS_LOG_POWER, BMS_LOG_SLEEP_ABORT, 1u, 0u);
        return 0;
    }
    /* AFE 事务期间 GPIO 可变化；MCU 转换中止后由下个正常采样唤醒/恢复 AFE。 */
    if (app_deepsleep_pad_wakeup_active()) {
        s_sleep_failure_mask = DIAG_SLEEP_BLOCK_WAKE_PAD;
        BMS_LOG(BMS_LOG_WARN, BMS_LOG_POWER, BMS_LOG_SLEEP_ABORT, 2u, 0u);
        return 0;
    }

    /* SH 保持低功耗优先级：保存失败报告诊断，不改变原来的入睡门禁。 */
    if (!bms_state_store_write_all(g_bms_soc.soc_estimate_percent,
                                  g_bms_soc.discharge_fraction_percent,
                                  g_bms_soc.cycle_count))
        BMS_LOG(BMS_LOG_WARN, BMS_LOG_POWER, BMS_LOG_SLEEP_ATTEMPT, need_afe_sleep, 1u);
    if (!bms_event_log_note_sleep())
        BMS_LOG(BMS_LOG_WARN, BMS_LOG_POWER, BMS_LOG_SLEEP_ATTEMPT, need_afe_sleep, 2u);
    bms_diag_sleep_committed();
    sleep_status = cpu_sleep_wakeup(DEEPSLEEP_MODE, PM_WAKEUP_PAD, 0);
    if (sleep_status & STATUS_GPIO_ERR_NO_ENTER_PM)
        s_sleep_failure_mask = DIAG_SLEEP_BLOCK_WAKE_PAD;
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
void app_power_process(const volatile uint8_t *sample_due)
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
    if ((*sample_due)) pm_block |= DIAG_PM_BLOCK_SAMPLE_PENDING;

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
                            (*sample_due), APP_SUSPEND_EXIT_CURRENT_MA);
        app_dvc_publish_sleep(DIAG_SLEEP_REASON_COMMAND, 0u, 0u, 0u);
        if (app_enter_power_off()) return;
        app_dvc_publish_sleep(DIAG_SLEEP_REASON_COMMAND, 0u, 0u, 0u);
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
                            (*sample_due), APP_SUSPEND_EXIT_CURRENT_MA);
        app_dvc_publish_sleep(DIAG_SLEEP_REASON_ACC, 200u, 200u, 0u);
        if (app_enter_acc_sleep()) return;
        app_dvc_publish_sleep(DIAG_SLEEP_REASON_ACC, 200u, 200u, 0u);
        s_low_power_mode = false;
        bls_pm_setSuspendMask(SUSPEND_DISABLE);
        if (ota_is_working) bls_pm_setManualLatency(0);
        return;
    }

    /* 保留电压阈值/超时，但仅合格样本可累计；不增加按键、负载检测或通信错误关机。 */
    /* BLE 连接允许事件间 suspend，但仍禁止自动低压断电；SDK 调度连接事件唤醒。 */
    if (valid && !busy && !device_in_connection_state)
    {
        if (g_bms_report.cell_min_mv < 2550u)
        {
            region = 1u;
            limit_seconds = 3600u;
        }
        else if (g_bms_report.cell_min_mv < BMS_SLEEP_LOW_CELL_MV)
        {
            region = 2u;
            limit_seconds = BMS_SLEEP_LOW_SECONDS;
        }
        else if (g_bms_report.cell_min_mv < BMS_SLEEP_NORMAL_CELL_MV && m.current_ma >= 0)
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
        if (low_voltage_seconds >= limit_seconds) {
            app_dvc_publish_sleep((u8)(DIAG_SLEEP_REASON_VERY_LOW + region - 1u),
                low_voltage_seconds * 1000u, limit_seconds * 1000u, 0u);
            if (app_enter_power_off()) return;
        }
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
                            (*sample_due), APP_SUSPEND_EXIT_CURRENT_MA);
    }
    else
    {
        s_low_power_mode = true;
        bls_pm_setSuspendMask(SUSPEND_ADV | SUSPEND_CONN);
        bms_diag_runtime_pm(1u, 0u, low_voltage_region, low_voltage_seconds,
                            (uint8_t)(device_in_connection_state != 0),
                            (*sample_due), APP_SUSPEND_EXIT_CURRENT_MA);
    }
    /* ACC 确认窗口也要可见；低压资格被 BLE/互锁撤销时展示原因和已重置的计时。 */
    if (s_acc_high_seen) {
        u32 acc_ms = (u32)(pm_get_32k_tick() - s_acc_high_tick) / 32u;
        if (acc_ms > 200u) acc_ms = 200u;
        app_dvc_publish_sleep(DIAG_SLEEP_REASON_ACC, acc_ms, 200u, s_low_power_mode);
    } else {
        u8 reason = DIAG_SLEEP_REASON_NONE;
        u32 delay_ms = 0u;
        if (valid) {
            if (g_bms_report.cell_min_mv < 2550u) {
                reason = DIAG_SLEEP_REASON_VERY_LOW; delay_ms = 3600000u;
            } else if (g_bms_report.cell_min_mv < BMS_SLEEP_LOW_CELL_MV) {
                reason = DIAG_SLEEP_REASON_LOW; delay_ms = BMS_SLEEP_LOW_SECONDS * 1000u;
            } else if (g_bms_report.cell_min_mv < BMS_SLEEP_NORMAL_CELL_MV && m.current_ma >= 0) {
                reason = DIAG_SLEEP_REASON_NORMAL; delay_ms = BMS_SLEEP_NORMAL_SECONDS * 1000u;
            }
        }
        app_dvc_publish_sleep(reason, low_voltage_seconds * 1000u, delay_ms, s_low_power_mode);
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
void app_power_process(const volatile uint8_t *sample_due)
{
	static u16 sleep_cnt = 0;
	static u32 sleep_veryvlow_cnt = 0;
	static u32 sleep_vlow_cnt = 0;
	static u32 sleep_vnormal_cnt = 0;
	static u32 afe_comm_err_sleepcnt = 0;
	static app_pm_elapsed_ctx_t sleep_elapsed_ctx = {0};
	u32 sleep_elapsed_sec = app_pm_take_elapsed_seconds(&sleep_elapsed_ctx);
	(void)sample_due;
	app_sh_publish_sleep(sleep_cnt, sleep_veryvlow_cnt, sleep_vlow_cnt,
	                      sleep_vnormal_cnt, afe_comm_err_sleepcnt, s_low_power_mode);

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

		if (g_bms_report.cell_min_mv < 2550)
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
		// else if ((g_bms_report.cell_min_mv <= 2750 && !g_bms_report.charge_current_a10) || deepsleep_en)
		else if ((g_bms_report.cell_min_mv < BMS_SLEEP_LOW_CELL_MV))
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
		else if ((g_bms_report.cell_min_mv < BMS_SLEEP_NORMAL_CELL_MV && !g_bms_report.charge_current_a10))
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

	// if(!gpio_read(BMS_BOARD_SWITCH_PIN) || g_bms_report.discharge_current_a10 || )
	if (!gpio_read(BMS_BOARD_SWITCH_PIN) ||
		SH3673510_FIXED_UART_BLOCKS_PM ||
		uart_tx_is_busy() || modbus_uart_tx_active() ||
		g_bms_report.discharge_current_a10 ||

		ota_is_working)
	// if(
	// 	g_bms_report.discharge_current_a10
	// 	)
	{
		s_low_power_mode = false;
		bls_pm_setSuspendMask(SUSPEND_DISABLE);
	}
	else if (device_in_connection_state)
	{
		s_low_power_mode = false;
	}
    /* 此字段记录 SDK suspend mask 是否许可，不把 BLE 连接等同于禁止 suspend。 */
    app_sh_publish_sleep(sleep_cnt, sleep_veryvlow_cnt, sleep_vlow_cnt,
        sleep_vnormal_cnt, afe_comm_err_sleepcnt,
        (u8)(gpio_read(BMS_BOARD_SWITCH_PIN) && !SH3673510_FIXED_UART_BLOCKS_PM &&
             !uart_tx_is_busy() && !modbus_uart_tx_active() &&
             !g_bms_report.discharge_current_a10 && !ota_is_working
#if (UI_KEYBOARD_ENABLE)
             && !scan_pin_need && !key_not_released
#elif (UI_BUTTON_ENABLE)
             && !button_not_released
#endif
             ));
}

#endif

/* 保留已提交 ACC/关机的保持动作和 DVC 外围诊断时点，阻止再次进入业务主循环。 */
uint8_t app_power_prepare_loop(void)
{
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    if (s_acc_sleep_committed) {
        app_acc_sleep_hold();
        return 1u;
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
        return 1u;
    }
#endif
    return 0u;
}
