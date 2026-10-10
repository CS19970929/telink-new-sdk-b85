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

#if (BMS_PRODUCT_SWITCH_SLEEP_ENABLE != 0) && (BMS_PRODUCT_SWITCH_SLEEP_ENABLE != 1)
#error "BMS_PRODUCT_SWITCH_SLEEP_ENABLE must be 0 or 1"
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
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
static u32 s_sleep_failure_mask;
static u8 s_sleep_report_reason;
#endif

/* 四产品保护性休眠共用计时；到期后锁存，不再回到通信/采样业务。 */
#define APP_AFE_ERROR_SLEEP_SECONDS (30u * 60u)
typedef struct {
    u32 low_voltage_seconds;
    u32 normal_voltage_seconds;
    u8 normal_voltage_active;
    u32 afe_error_seconds;
    u32 elapsed_ms;
    u32 delay_ms;
    u8 region;
    u8 reason;
    u8 committed;
} app_protective_sleep_t;
static app_protective_sleep_t s_protective_sleep;

/* 普通 suspend 只接受新鲜缓存；低压计时不能用此门禁失败推断电压已恢复。 */
static uint8_t app_get_fresh_measurements(bms_afe_aux_measurements_t *m)
{
    if (!bms_afe_get_aux_measurements(m)) return 0u;
    return ((u32)(pm_get_32k_tick() - m->sample_tick_32k) <=
            BMS_SAMPLE_MAX_POLL_GAP_32K) ? 1u : 0u;
}

/* 与 SOC 可靠电流范围一致；充、放电两侧均阻止普通 suspend。 */
#define APP_SUSPEND_EXIT_CURRENT_MA ((int32_t)BMS_CURRENT_UNRELIABLE_MAX_MA)

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
#define APP_POWER_OFF_RETRY_SECONDS 5u
static u8 s_power_off_committed;
static u8 s_power_off_retry_ready;
static u32 s_power_off_retry_tick;
static u8 s_acc_sleep_committed, s_acc_retry_ready, s_acc_disconnect_sent;
static u32 s_acc_retry_tick;

#else
#if BMS_PRODUCT_SWITCH_ENABLE
/* 按产品配置读取开关输入状态。 */
static uint8_t board_switch_is_on(void)
{
	return gpio_read(BMS_BOARD_SWITCH_PIN) ? 0u : 1u;
}
#endif
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
	/* 先取余再合并，避免一次接近完整 u32 周期的跨度与余量相加溢出。 */
	total_tick_32k = ctx->pending_tick_32k + elapsed_tick_32k % APP_PM_TICKS_PER_SEC;
	elapsed_sec = elapsed_tick_32k / APP_PM_TICKS_PER_SEC + total_tick_32k / APP_PM_TICKS_PER_SEC;
	ctx->pending_tick_32k = total_tick_32k % APP_PM_TICKS_PER_SEC;
	return elapsed_sec;
}

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
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

#endif

/* 限制休眠时间累计值，避免异常跨度影响策略。 */
static u32 app_pm_elapsed_limit(u32 elapsed, u32 increment, u32 limit)
{
    if (elapsed >= limit || increment >= limit - elapsed) return limit;
    return elapsed + increment;
}

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
/* B85 pm.h：REG6在深睡保留，watchdog/芯片复位/断电清零；本字节仅由PM持有。
 * 低3位保存其他唤醒输入；命令/低压在开关ON时入睡，OFF只允许重新布防ON。 */
#define APP_SH_SWITCH_WAKE_TAG 0xa8u
#define APP_SH_SWITCH_WAKE_MASK 0xf8u
static u8 app_sh_other_wakeup_levels(void)
{
    return (gpio_read(BMS_BOARD_INT_WK_MCU_PIN) ? 1u : 0u) |
        (gpio_read(BMS_BOARD_AFE_ALARM_PIN) ? 2u : 0u) |
        (gpio_read(BMS_BOARD_AFE_RESET_OUT_PIN) ? 4u : 0u);
}

static void app_sh_set_other_wakeup(u8 levels)
{
    cpu_set_gpio_wakeup(BMS_BOARD_INT_WK_MCU_PIN, (levels & 1u) ? Level_Low : Level_High, 1);
    cpu_set_gpio_wakeup(BMS_BOARD_AFE_ALARM_PIN, (levels & 2u) ? Level_Low : Level_High, 1);
    cpu_set_gpio_wakeup(BMS_BOARD_AFE_RESET_OUT_PIN, (levels & 4u) ? Level_Low : Level_High, 1);
}

/* GPIO/clock初始化之后、watchdog/BLE/AFE/业务初始化之前调用。
 * 返回1仅重复本保持动作；不读写Flash、不访问AFE、不恢复通信供电。 */
u8 app_power_boot_sleep_hold(void)
{
    u8 retained = analog_read(DEEP_ANA_REG6);
#if BMS_PRODUCT_SWITCH_ENABLE
    if ((pm_get_wakeup_src() & WAKEUP_STATUS_PAD) &&
        (retained & APP_SH_SWITCH_WAKE_MASK) == APP_SH_SWITCH_WAKE_TAG) {
        static const GPIO_PinTypeDef inputs[] = { BMS_BOARD_SWITCH_PIN,
            BMS_BOARD_INT_WK_MCU_PIN, BMS_BOARD_AFE_ALARM_PIN, BMS_BOARD_AFE_RESET_OUT_PIN };
        u8 i;
        for (i = 0u; i < sizeof(inputs) / sizeof(inputs[0]); ++i) {
            gpio_set_func(inputs[i], AS_GPIO);
            gpio_set_input_en(inputs[i], 1);
            gpio_set_output_en(inputs[i], 0);
        }
        /* 其他PAD同时改变时，保留其正常唤醒；仅过滤单独的开关OFF。 */
        if (!board_switch_is_on() && app_sh_other_wakeup_levels() == (retained & 7u)) {
            gpio_set_func(BMS_BOARD_CMNT_EN_PIN, AS_GPIO);
            gpio_write(BMS_BOARD_CMNT_EN_PIN, 0);
            gpio_set_input_en(BMS_BOARD_CMNT_EN_PIN, 0);
            gpio_set_output_en(BMS_BOARD_CMNT_EN_PIN, 1);
            cpu_set_gpio_wakeup(BMS_BOARD_SCI1_RX_PIN, Level_Low, 0);
            cpu_set_gpio_wakeup(BMS_BOARD_SWITCH_PIN, Level_Low, 1);
            app_sh_set_other_wakeup(retained & 7u);
            (void)cpu_sleep_wakeup(DEEPSLEEP_MODE, PM_WAKEUP_PAD, 0u);
            return 1u; /* PAD竞争返回后重新检查；ON或其他输入改变才放行。 */
        }
    }
#else
    (void)retained;
#endif
    analog_write(DEEP_ANA_REG6, 0u);
    return 0u;
}

/* 等待原有输入改变，当前静态有效电平不作为拒睡条件。 */
static void app_protective_wakeup_pin(GPIO_PinTypeDef pin)
{
    cpu_set_gpio_wakeup(pin, gpio_read(pin) ? Level_Low : Level_High, 1);
}
#endif

/* D008 已请求断电，不再执行低功耗调用；SH 仅重试深睡，不重复写 Flash/AFE。 */
static void app_protective_sleep_hold(void)
{
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    return; /* PC4 已拉低，禁止继续采样、通信或重复保存。 */
#else
    u8 levels = app_sh_other_wakeup_levels();
    /* UART 只唤醒普通 Suspend，持续串口活动不能打断已提交的深睡。 */
    cpu_set_gpio_wakeup(BMS_BOARD_SCI1_RX_PIN, Level_Low, 0);
    gpio_en_interrupt_risc0(BMS_BOARD_SCI1_RX_PIN, 0);
    reg_irq_src = FLD_IRQ_GPIO_RISC0_EN;
#if BMS_PRODUCT_SWITCH_ENABLE
    u8 switch_on = board_switch_is_on();
    analog_write(DEEP_ANA_REG6, switch_on ? APP_SH_SWITCH_WAKE_TAG | levels : 0u);
    cpu_set_gpio_wakeup(BMS_BOARD_SWITCH_PIN, switch_on ? Level_High : Level_Low, 1);
#else
    analog_write(DEEP_ANA_REG6, 0u);
    cpu_set_gpio_wakeup(BMS_BOARD_SWITCH_PIN, Level_Low, 0);
#endif
    app_sh_set_other_wakeup(levels);
    bms_diag_sleep_committed();
    (void)cpu_sleep_wakeup(DEEPSLEEP_MODE, PM_WAKEUP_PAD, 0u);
#endif
}

/* 用户要求：保护性休眠不受通信、OTA、保存或 AFE 命令成功与否阻断。 */
static void app_enter_protective_sleep(void)
{
    s_protective_sleep.committed = 1u;
    bms_afe_set_output_enabled(0u);
    /* guard 选择 DVC Shutdown / SH Sleep 并保持总线静默资格；
     * 无法通知 AFE 时也必须执行 D008 断电 / SH 深睡。 */
    (void)bms_afe_sleep();
    /* 不在 OTA/Flash 会话内追加写入；其余情况各保存一次，失败不重试、不拒睡。 */
    if (!ota_is_working && app_flash_lock_restore_enabled()) {
        (void)bms_state_store_write_all(g_bms_soc.soc_estimate_percent,
                                        g_bms_soc.discharge_fraction_percent,
                                        g_bms_soc.cycle_count);
        (void)bms_event_log_note_sleep();
    }
    if (device_in_connection_state)
        (void)bls_ll_terminateConnection(HCI_ERR_REMOTE_USER_TERM_CONN);
    (void)bls_ll_setAdvEnable(BLC_ADV_DISABLE);
    bls_pm_setSuspendMask(SUSPEND_DISABLE);
    bls_pm_setAppWakeupLowPower(0u, 0u);
    s_low_power_mode = true;
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    bms_diag_sleep_committed();
    /* 低压/AFE 异常均切断 MCU 供电；PC4 拉低后禁止再保存或访问 AFE。 */
    gpio_write(BMS_BOARD_MCU_LDO_PIN, 0u);
#else
    gpio_write(BMS_BOARD_CMNT_EN_PIN, 0u);
#endif
    app_protective_sleep_hold();
}

/* 两类计时独立；BLE/串口/OTA 不清零，AFE 异常不被旧低压报告遮住。 */
static u8 app_protective_sleep_poll(u32 elapsed_sec)
{
    bms_afe_aux_measurements_t m;
    u8 valid;
    u8 region = s_protective_sleep.region;
    u32 limit = region == 1u ? 3600u :
                region == 2u ? BMS_SLEEP_LOW_SECONDS :
                region == 3u ? BMS_SLEEP_NORMAL_SECONDS : 0u;
    if (s_protective_sleep.committed) {
        app_protective_sleep_hold();
        return 1u;
    }
    valid = app_get_fresh_measurements(&m);
    if (valid) {
        region = 0u;
        limit = 0u;
        s_protective_sleep.normal_voltage_active =
            g_bms_report.cell_min_mv < BMS_SLEEP_NORMAL_CELL_MV &&
            m.current_ma >= -(int32_t)BMS_CURRENT_UNRELIABLE_MAX_MA;
        if (g_bms_report.cell_min_mv < 2550u) {
            region = 1u; limit = 3600u;
        } else if (g_bms_report.cell_min_mv < BMS_SLEEP_LOW_CELL_MV) {
            region = 2u; limit = BMS_SLEEP_LOW_SECONDS;
        } else if (g_bms_report.cell_min_mv < BMS_SLEEP_NORMAL_CELL_MV &&
                   m.current_ma >= -(int32_t)BMS_CURRENT_UNRELIABLE_MAX_MA) {
            region = 3u; limit = BMS_SLEEP_NORMAL_SECONDS;
        }
    }
    /* 两个电压条件分别累计；跨2800mV只结束一小时条件，不丢24小时资格。
     * 无新鲜数据保持最后确认的条件；有效恢复或可靠充电才解除相应计时。 */
    s_protective_sleep.region = region;
    s_protective_sleep.low_voltage_seconds = (region == 1u || region == 2u) ?
        app_pm_elapsed_limit(s_protective_sleep.low_voltage_seconds, elapsed_sec, limit) : 0u;
    s_protective_sleep.normal_voltage_seconds = s_protective_sleep.normal_voltage_active ?
        app_pm_elapsed_limit(s_protective_sleep.normal_voltage_seconds, elapsed_sec,
                             BMS_SLEEP_NORMAL_SECONDS) : 0u;
    s_protective_sleep.afe_error_seconds = (!valid || bms_error_get(BMS_ERROR_AFE1)) ?
        app_pm_elapsed_limit(s_protective_sleep.afe_error_seconds, elapsed_sec,
                             APP_AFE_ERROR_SLEEP_SECONDS) : 0u;
    s_protective_sleep.reason = region ? (u8)(DIAG_SLEEP_REASON_VERY_LOW + region - 1u) :
                                         DIAG_SLEEP_REASON_NONE;
    s_protective_sleep.elapsed_ms = s_protective_sleep.low_voltage_seconds * 1000u;
    s_protective_sleep.delay_ms = limit * 1000u;
    if (s_protective_sleep.normal_voltage_active &&
        (region == 3u || BMS_SLEEP_NORMAL_SECONDS - s_protective_sleep.normal_voltage_seconds <=
                         limit - s_protective_sleep.low_voltage_seconds)) {
        s_protective_sleep.reason = (u8)(DIAG_SLEEP_REASON_VERY_LOW + 2u);
        s_protective_sleep.elapsed_ms = s_protective_sleep.normal_voltage_seconds * 1000u;
        s_protective_sleep.delay_ms = BMS_SLEEP_NORMAL_SECONDS * 1000u;
    }
    if (s_protective_sleep.afe_error_seconds &&
        (!region || (APP_AFE_ERROR_SLEEP_SECONDS - s_protective_sleep.afe_error_seconds) * 1000u <=
                    s_protective_sleep.delay_ms - s_protective_sleep.elapsed_ms)) {
        s_protective_sleep.reason = DIAG_SLEEP_REASON_AFE;
        s_protective_sleep.elapsed_ms = s_protective_sleep.afe_error_seconds * 1000u;
        s_protective_sleep.delay_ms = APP_AFE_ERROR_SLEEP_SECONDS * 1000u;
    }
    if (s_protective_sleep.reason != DIAG_SLEEP_REASON_NONE)
        bms_diag_sleep(s_protective_sleep.reason, 0u, s_protective_sleep.elapsed_ms,
                        s_protective_sleep.delay_ms, 0u, 0u);
    if (s_protective_sleep.delay_ms &&
        s_protective_sleep.elapsed_ms >= s_protective_sleep.delay_ms) {
        app_enter_protective_sleep();
        return 1u;
    }
    return 0u;
}

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124

/* 深睡/关机与普通 suspend 的门禁不同，不能复用 pm_block 推断倒计时。 */
static void app_dvc_publish_sleep(u8 reason, u32 elapsed_ms, u32 delay_ms,
                                  u8 suspend_allowed)
{
    bms_afe_aux_measurements_t m;
    u32 block = 0u;
    u32 retry_ms;
    if (s_protective_sleep.reason != DIAG_SLEEP_REASON_NONE) {
        bms_diag_sleep(s_protective_sleep.reason, 0u, s_protective_sleep.elapsed_ms,
                        s_protective_sleep.delay_ms, 0u, suspend_allowed);
        return;
    }
    if (ota_is_working) block |= DIAG_SLEEP_BLOCK_OTA;
    if (!app_flash_lock_restore_enabled()) block |= DIAG_SLEEP_BLOCK_FLASH;
    if (BUS_STATE_OWC_IDLE != bus_mux_get_state()) block |= DIAG_SLEEP_BLOCK_BUS;
    if (device_in_connection_state &&
        (reason != DIAG_SLEEP_REASON_COMMAND || blc_ll_getTxFifoNumber() != 0u))
        block |= DIAG_SLEEP_BLOCK_BLE;
    retry_ms = reason == DIAG_SLEEP_REASON_ACC ?
        app_sleep_retry_ms(s_acc_retry_ready, s_acc_retry_tick, APP_POWER_OFF_RETRY_SECONDS) :
        app_sleep_retry_ms(s_power_off_retry_ready, s_power_off_retry_tick, APP_POWER_OFF_RETRY_SECONDS);
    if (reason == DIAG_SLEEP_REASON_NONE) {
        retry_ms = 0u;
        if (!app_get_fresh_measurements(&m)) block |= DIAG_SLEEP_BLOCK_SAMPLE;
    }
    app_publish_sleep(reason, block, elapsed_ms, delay_ms, retry_ms, suspend_allowed);
}

/* 仅由显式命令分支调用；保护性低压休眠不经过此关机流程。 */
static int app_enter_command_power_off(void)
{
    u32 now = pm_get_32k_tick();

    if (s_power_off_committed || ota_is_working ||
        !app_flash_lock_restore_enabled() ||
        BUS_STATE_OWC_IDLE != bus_mux_get_state()) return 0;
    /*
     * 显式休眠命令不要求低电压、BLE 断连或有效电流样本。断电前必须发完命令应答；
     * 保护性深睡由独立入口执行，不使用本函数的门禁。
     */
    if (device_in_connection_state && blc_ll_getTxFifoNumber() != 0u) return 0;
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
    gpio_write(BMS_BOARD_MCU_LDO_PIN, 0u); /* 请求切断 MCU 电源。 */
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
#if BMS_PRODUCT_SWITCH_SLEEP_ENABLE
    if (!gpio_read(BMS_BOARD_ACC_PIN)) {
        s_acc_retry_ready = s_acc_disconnect_sent = 0u;
        return 0;
    }
    return 1; /* 主循环检测到OFF立即请求，不增加确认延时。 */
#else
    return 0;
#endif
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

/* 开关/命令与低压复用同一个已提交深睡入口；失败后不恢复业务。 */
static u8 s_command_seen;
static u32 s_command_tick;
#define APP_SH_COMMAND_REPLY_32K (APP_PM_TICKS_PER_SEC / 2u)

static u8 app_sh_explicit_sleep_poll(void)
{
    u32 now = pm_get_32k_tick();
    u8 reason = DIAG_SLEEP_REASON_NONE;
    u32 elapsed = 0u, delay = 0u;
#if BMS_PRODUCT_SWITCH_ENABLE && BMS_PRODUCT_SWITCH_SLEEP_ENABLE
    /* 开关OFF优先于命令应答，主循环本次直接提交深睡。 */
    if (!board_switch_is_on()) {
        bms_diag_sleep(DIAG_SLEEP_REASON_SWITCH, 0u, 0u, 0u, 0u, 0u);
        app_enter_protective_sleep();
        return 1u;
    }
#endif
    if (deepsleep_en) {
        if (!s_command_seen) { s_command_seen = 1u; s_command_tick = now; }
        reason = DIAG_SLEEP_REASON_COMMAND;
        elapsed = (u32)(now - s_command_tick);
        delay = APP_SH_COMMAND_REPLY_32K;
        /* 尽量发完既有应答，最长500ms；坏UART/BLE不能无限推迟命令。 */
        if ((!uart_tx_is_busy() && !modbus_uart_tx_active() &&
             (!device_in_connection_state || blc_ll_getTxFifoNumber() == 0u)) || elapsed >= delay) {
            bms_diag_sleep(reason, 0u, elapsed / 32u, delay / 32u, 0u, 0u);
            app_enter_protective_sleep();
            return 1u;
        }
    }
    if (reason != DIAG_SLEEP_REASON_NONE)
        bms_diag_sleep(reason, 0u, elapsed / 32u, delay / 32u, 0u, 0u);
    return 0u;
}

/* SDK 最后一道普通 Suspend 检查；直接深睡已提交时绝不被此回调否决。 */
int app_power_before_suspend(void)
{
    if (s_protective_sleep.committed) return 1;
    if (!s_low_power_mode || !modbus_uart_suspend_ready()) return 0;
    /* 当前稳定电平不拒绝普通 Suspend；变化仍唤醒，200ms采样定时器保留。 */
#if BMS_PRODUCT_SWITCH_ENABLE
    app_protective_wakeup_pin(BMS_BOARD_SWITCH_PIN);
#else
    cpu_set_gpio_wakeup(BMS_BOARD_SWITCH_PIN, Level_Low, 0);
#endif
    app_protective_wakeup_pin(BMS_BOARD_INT_WK_MCU_PIN);
    app_protective_wakeup_pin(BMS_BOARD_AFE_ALARM_PIN);
    app_protective_wakeup_pin(BMS_BOARD_AFE_RESET_OUT_PIN);
    modbus_uart_suspend_enter();
    return 1;
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
    static app_pm_elapsed_ctx_t elapsed_ctx;
    bms_afe_aux_measurements_t m;
    u32 elapsed_sec = app_pm_take_elapsed_seconds(&elapsed_ctx);
    u32 pm_block = 0u;
    u32 voltage_seconds;
    u8 valid = app_get_fresh_measurements(&m);
    u8 ota_busy = ota_is_working ? 1u : 0u;
    u8 flash_busy = app_flash_lock_restore_enabled() ? 0u : 1u;
    u8 bus_busy = (BUS_STATE_OWC_IDLE != bus_mux_get_state()) ? 1u : 0u;

    if (app_protective_sleep_poll(elapsed_sec)) return;
    voltage_seconds = (s_protective_sleep.region == 3u) ?
        s_protective_sleep.normal_voltage_seconds : s_protective_sleep.low_voltage_seconds;

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
     * OTA/总线/持久化/AFE 失败时保留请求；五秒重试门禁由显式关机路径持有，
     * 保护性计时已在本入口之前推进。
     */
    if (deepsleep_en)
    {
        pm_block |= DIAG_PM_BLOCK_POWER_OFF;
        bms_diag_runtime_pm(0u, pm_block, s_protective_sleep.region, voltage_seconds,
                            (uint8_t)(device_in_connection_state != 0),
                            (*sample_due), APP_SUSPEND_EXIT_CURRENT_MA);
        app_dvc_publish_sleep(DIAG_SLEEP_REASON_COMMAND, 0u, 0u, 0u);
        if (app_enter_command_power_off()) return;
        app_dvc_publish_sleep(DIAG_SLEEP_REASON_COMMAND, 0u, 0u, 0u);
        s_low_power_mode = false;
        bls_pm_setSuspendMask(SUSPEND_DISABLE);
        if (ota_is_working) bls_pm_setManualLatency(0);
        return;
    }

    if (app_acc_sleep_requested())
    {
        pm_block |= DIAG_PM_BLOCK_ACC_SLEEP;
        bms_diag_runtime_pm(0u, pm_block, s_protective_sleep.region, voltage_seconds,
                            (uint8_t)(device_in_connection_state != 0),
                            (*sample_due), APP_SUSPEND_EXIT_CURRENT_MA);
        app_dvc_publish_sleep(DIAG_SLEEP_REASON_ACC, 0u, 0u, 0u);
        if (app_enter_acc_sleep()) return;
        app_dvc_publish_sleep(DIAG_SLEEP_REASON_ACC, 0u, 0u, 0u);
        s_low_power_mode = false;
        bls_pm_setSuspendMask(SUSPEND_DISABLE);
        if (ota_is_working) bls_pm_setManualLatency(0);
        return;
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
        bms_diag_runtime_pm(0u, pm_block, s_protective_sleep.region, voltage_seconds,
                            (uint8_t)(device_in_connection_state != 0),
                            (*sample_due), APP_SUSPEND_EXIT_CURRENT_MA);
    }
    else
    {
        s_low_power_mode = true;
        bls_pm_setSuspendMask(SUSPEND_ADV | SUSPEND_CONN);
        bms_diag_runtime_pm(1u, 0u, s_protective_sleep.region, voltage_seconds,
                            (uint8_t)(device_in_connection_state != 0),
                            (*sample_due), APP_SUSPEND_EXIT_CURRENT_MA);
    }
    if (s_protective_sleep.reason != DIAG_SLEEP_REASON_NONE) {
        app_dvc_publish_sleep(s_protective_sleep.reason, s_protective_sleep.elapsed_ms,
                                s_protective_sleep.delay_ms, s_low_power_mode);
        return;
    }
    app_dvc_publish_sleep(DIAG_SLEEP_REASON_NONE, 0u, 0u, s_low_power_mode);
}

#else
/* 所有普通 Suspend（广播/连接、长/短窗口）均保留 UART PAD 唤醒。 */
void task_sleep_enter(u8 e, u8 *p, int n)
{
    (void)e; (void)p; (void)n;
    bls_pm_setWakeupSource(PM_WAKEUP_PAD);
}

void app_power_process(const volatile uint8_t *sample_due)
{
    static app_pm_elapsed_ctx_t sleep_elapsed_ctx = {0};
    u32 elapsed_sec = app_pm_take_elapsed_seconds(&sleep_elapsed_ctx);
    bms_afe_aux_measurements_t m;
    u8 ui_busy = 0u;
    u8 sample_block;
    if (app_protective_sleep_poll(elapsed_sec)) return;
    if (app_sh_explicit_sleep_poll()) return;
    sample_block = (u8)(!app_get_fresh_measurements(&m) || (*sample_due) ||
        m.current_ma >= APP_SUSPEND_EXIT_CURRENT_MA ||
        m.current_ma <= -APP_SUSPEND_EXIT_CURRENT_MA);
#if (UI_KEYBOARD_ENABLE)
    ui_busy = (scan_pin_need || key_not_released) ? 1u : 0u;
#elif (UI_BUTTON_ENABLE)
    ui_busy = button_not_released ? 1u : 0u;
#endif
#if (BLE_OTA_SERVER_ENABLE)
    if (ota_is_working && !ui_busy) bls_pm_setManualLatency(0);
#endif
    /* 开关开着也可普通Suspend；串口30秒静默由UART所有者确认。 */
    s_low_power_mode = !ui_busy && !sample_block && modbus_uart_suspend_ready() &&
        app_flash_lock_restore_enabled() && !ota_is_working;
    bls_pm_setSuspendMask(s_low_power_mode ? SUSPEND_ADV | SUSPEND_CONN : SUSPEND_DISABLE);
    /* 最终快照使用本轮Suspend决定，不沿用计时入口发布的保守值。 */
    if (deepsleep_en)
        bms_diag_sleep(DIAG_SLEEP_REASON_COMMAND, 0u,
            (u32)(pm_get_32k_tick() - s_command_tick) / 32u, 500u, 0u, s_low_power_mode);
    else if (s_protective_sleep.reason != DIAG_SLEEP_REASON_NONE)
        bms_diag_sleep(s_protective_sleep.reason, 0u, s_protective_sleep.elapsed_ms,
            s_protective_sleep.delay_ms, 0u, s_low_power_mode);
    else
        bms_diag_sleep(DIAG_SLEEP_REASON_NONE, 0u, 0u, 0u, 0u, s_low_power_mode);
}

#endif

/* 保护性深睡保持动作优先；保留 ACC/关机保持与正常 DVC 外围诊断时点。 */
uint8_t app_power_prepare_loop(void)
{
    if (s_protective_sleep.committed) {
        app_protective_sleep_hold();
        return 1u;
    }
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    if (s_acc_sleep_committed) {
        app_acc_sleep_hold();
        return 1u;
    }
    if (s_power_off_committed)
    {
        /* PC4 已拉低；只阻止正常业务继续执行，不调用 Suspend/DeepSleep。 */
        return 1u;
    }
    bms_parameters_diag_poll();
    bms_storage_platform_diag_poll();
    bms_afe_diag_poll();
#endif
    return 0u;
}
