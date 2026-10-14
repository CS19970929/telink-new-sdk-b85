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
#include "bms_sleep_config.h"
#include <string.h>

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

/* 四产品保护性休眠共用计时；到期后锁存，不再回到通信/采样业务。 */
#define APP_OVERDISCHARGE_SLEEP_SECONDS 180u
/* 命令应答只作有界等待，不能成为强制休眠的永久门禁。 */
#define APP_COMMAND_REPLY_32K (APP_PM_TICKS_PER_SEC / 2u)
static u8 s_command_seen;
static u32 s_command_tick;
/* 普通 Suspend 与低压可靠充电分别拥有阈值，不随 SOC 电流死区调整。 */
#define APP_SUSPEND_EXIT_CURRENT_MA ((int32_t)200)
#define APP_SLEEP_CHARGE_CURRENT_MA ((int32_t)200)
/* 本轮推导结果仅用于决策/诊断，不作为跨轮状态保留。 */
typedef struct {
    u32 elapsed_ms;
    u32 delay_ms;
    u8 reason;
} app_sleep_status_t;
typedef struct {
    u32 low_voltage_seconds;
    u32 normal_voltage_seconds;
    u8 normal_voltage_active;
    u32 afe_error_seconds;
    u32 overdischarge_seconds;
    u8 overdischarge_active;
    u8 region;
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

/* 普通 Suspend 只看电流是否达到活动阈值；与保护性深睡资格独立。 */
static uint8_t app_current_prevents_suspend(const bms_afe_aux_measurements_t *m, u8 valid)
{
    return (valid && (m->current_ma >= APP_SUSPEND_EXIT_CURRENT_MA ||
                      m->current_ma <= -APP_SUSPEND_EXIT_CURRENT_MA)) ? 1u : 0u;
}

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
static u8 s_acc_sleep_committed;

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

/* 已锁存的深睡/关机只收尾一次；保存和外设失败不能恢复业务或无限重试。 */
static void app_prepare_deep_sleep(void)
{
    bms_afe_set_output_enabled(0u);
    /* guard 选择 DVC Shutdown / SH Sleep 并保持总线静默资格；
     * 无法通知 AFE 时也必须执行对应的 MCU 断电或深睡。 */
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
}

/* 命令及到期保护性休眠不受通信、OTA、保存或 AFE 命令成功与否阻断。 */
static void app_enter_protective_sleep(void)
{
    s_protective_sleep.committed = 1u;
    app_prepare_deep_sleep();
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    bms_diag_sleep_committed();
    /* 命令/保护性休眠均切断 MCU 供电；PC4 拉低后禁止再保存或访问 AFE。 */
    gpio_write(BMS_BOARD_MCU_LDO_PIN, 0u);
#else
    gpio_write(BMS_BOARD_CMNT_EN_PIN, 0u);
#endif
    app_protective_sleep_hold();
}

/* 四产品命令均复用强制入口；一线通接入、OTA及失败不能撤销已锁存请求。 */
static u8 app_command_sleep_poll(void)
{
    u32 now = pm_get_32k_tick();
    u32 elapsed;
    if (!deepsleep_en) return 0u;
    if (s_protective_sleep.committed) return 1u;
    if (!s_command_seen) { s_command_seen = 1u; s_command_tick = now; }
    elapsed = (u32)(now - s_command_tick);
    bms_diag_sleep(DIAG_SLEEP_REASON_COMMAND, 0u, elapsed / 32u,
                    APP_COMMAND_REPLY_32K / 32u, 0u, 0u);
    /* 应答空闲即执行，最长等待500ms；使用无符号时间差处理tick回绕。 */
    if (elapsed < APP_COMMAND_REPLY_32K &&
        (uart_tx_is_busy() || modbus_uart_tx_active() ||
         (device_in_connection_state && blc_ll_getTxFifoNumber() != 0u))) return 0u;
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    BMS_LOG(BMS_LOG_INFO, BMS_LOG_POWER, BMS_LOG_SLEEP_ATTEMPT, 1u, 1u);
#endif
    app_enter_protective_sleep();
    return 1u;
}

/* 选最先到期的条件；所有乘法都在配置上限 65535 分钟以内。 */
static void app_select_protective_sleep(app_sleep_status_t *status, u8 reason,
                                        u32 elapsed_seconds, u32 delay_seconds)
{
    u32 elapsed_ms = elapsed_seconds * 1000u;
    u32 delay_ms = delay_seconds * 1000u;
    if (status->reason == DIAG_SLEEP_REASON_NONE ||
        delay_ms - elapsed_ms <= status->delay_ms - status->elapsed_ms) {
        status->reason = reason;
        status->elapsed_ms = elapsed_ms;
        status->delay_ms = delay_ms;
    }
}

/* 两档低压、AFE 异常和过放分别计时；无新鲜数据不能证明已恢复。 */
static u8 app_protective_sleep_poll(u32 elapsed_sec,
                                    const bms_afe_aux_measurements_t *m, u8 valid,
                                    app_sleep_status_t *status)
{
    bms_sleep_config_t config;
    u8 region = s_protective_sleep.region;
    u8 afe_fault = !valid || bms_error_get(BMS_ERROR_AFE1);
    u32 low_delay, normal_delay, afe_delay;
    if (s_protective_sleep.committed) {
        app_protective_sleep_hold();
        return 1u;
    }
    bms_sleep_config_get(&config);
    low_delay = (u32)config.low_delay_minutes * 60u;
    normal_delay = (u32)config.normal_delay_minutes * 60u;
    afe_delay = BMS_SLEEP_AFE_FIXED_MINUTES * 60u;
    memset(status, 0, sizeof(*status));
    if (valid) {
        region = 0u;
        s_protective_sleep.normal_voltage_active =
            g_bms_report.cell_min_mv < config.normal_cell_mv &&
            m->current_ma >= -APP_SLEEP_CHARGE_CURRENT_MA;
        if (g_bms_report.cell_min_mv < config.low_cell_mv) region = 2u;
        else if (s_protective_sleep.normal_voltage_active) region = 3u;
    }
    /* 下档不排除充电，上档只在无可靠充电时累计；保持原有两条件资格。 */
    s_protective_sleep.region = region;
    s_protective_sleep.low_voltage_seconds = region == 2u ?
        app_pm_elapsed_limit(s_protective_sleep.low_voltage_seconds, elapsed_sec, low_delay) : 0u;
    s_protective_sleep.normal_voltage_seconds = s_protective_sleep.normal_voltage_active ?
        app_pm_elapsed_limit(s_protective_sleep.normal_voltage_seconds, elapsed_sec,
                             normal_delay) : 0u;
    s_protective_sleep.afe_error_seconds = afe_fault ?
        app_pm_elapsed_limit(s_protective_sleep.afe_error_seconds, elapsed_sec,
                             afe_delay) : 0u;
    /* fault_third 已合并软件/AFE 欠压。通信恢复重初始化不能冒充电池恢复。 */
    if (g_bms_report.fault_third.bits.cell_uvp || g_bms_report.fault_third.bits.pack_uvp)
        s_protective_sleep.overdischarge_active = 1u;
    else if (valid && !afe_fault &&
             g_bms_report.cell_min_mv >= g_bms_protection_params.cell_uvp_recover_mv &&
             g_bms_report.pack_voltage_10mv >= g_bms_protection_params.pack_uvp_recover_10mv)
        s_protective_sleep.overdischarge_active = 0u;
    s_protective_sleep.overdischarge_seconds = s_protective_sleep.overdischarge_active ?
        app_pm_elapsed_limit(s_protective_sleep.overdischarge_seconds, elapsed_sec,
                             APP_OVERDISCHARGE_SLEEP_SECONDS) : 0u;

    if (region == 2u)
        app_select_protective_sleep(status, DIAG_SLEEP_REASON_LOW,
            s_protective_sleep.low_voltage_seconds, low_delay);
    if (s_protective_sleep.normal_voltage_active)
        app_select_protective_sleep(status, DIAG_SLEEP_REASON_NORMAL,
            s_protective_sleep.normal_voltage_seconds, normal_delay);
    if (afe_fault)
        app_select_protective_sleep(status, DIAG_SLEEP_REASON_AFE,
            s_protective_sleep.afe_error_seconds, afe_delay);
    if (s_protective_sleep.overdischarge_active)
        app_select_protective_sleep(status, DIAG_SLEEP_REASON_OVERDISCHARGE,
            s_protective_sleep.overdischarge_seconds, APP_OVERDISCHARGE_SLEEP_SECONDS);
    if (status->reason != DIAG_SLEEP_REASON_NONE)
        bms_diag_sleep(status->reason, 0u, status->elapsed_ms,
                        status->delay_ms, 0u, 0u);
    if (status->delay_ms && status->elapsed_ms >= status->delay_ms) {
        app_enter_protective_sleep();
        return 1u;
    }
    return 0u;
}

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124

/* 深睡/关机与普通 suspend 的门禁不同，不能复用 pm_block 推断倒计时。 */
static void app_dvc_publish_sleep(const app_sleep_status_t *protective,
                                  u8 suspend_allowed, u8 sample_valid)
{
    u32 block = 0u;
    if (protective->reason != DIAG_SLEEP_REASON_NONE) {
        bms_diag_sleep(protective->reason, 0u, protective->elapsed_ms,
                        protective->delay_ms, 0u, suspend_allowed);
        return;
    }
    if (ota_is_working) block |= DIAG_SLEEP_BLOCK_OTA;
    if (!app_flash_lock_restore_enabled()) block |= DIAG_SLEEP_BLOCK_FLASH;
    if (BUS_STATE_OWC_IDLE != bus_mux_get_state()) block |= DIAG_SLEEP_BLOCK_BUS;
    if (device_in_connection_state) block |= DIAG_SLEEP_BLOCK_BLE;
    if (!sample_valid) block |= DIAG_SLEEP_BLOCK_SAMPLE;
    bms_diag_sleep(DIAG_SLEEP_REASON_NONE, block, 0u, 0u, 0u, suspend_allowed);
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
    return gpio_read(BMS_BOARD_ACC_PIN) ? 1 : 0; /* OFF立即请求，无确认延时。 */
#else
    return 0;
#endif
}

/* ACC OFF 直接锁存深睡；保留 PC4 供电，不等待通信退出或失败重试。 */
static int app_enter_acc_sleep(void)
{
    if (!gpio_read(BMS_BOARD_ACC_PIN)) return 0;
    s_acc_sleep_committed = 1u;
    bms_diag_sleep(DIAG_SLEEP_REASON_ACC, 0u, 0u, 0u, 0u, 0u);
    app_prepare_deep_sleep();
    gpio_write(BMS_BOARD_MCU_LDO_PIN, 1u);
    cpu_set_gpio_wakeup(BMS_BOARD_LOAD_DETECT_PIN, Level_Low, 0);
    bms_diag_sleep_committed();
    app_acc_sleep_hold();
    return 1;
}

#else

/* 开关/命令与低压复用同一个已提交深睡入口；失败后不恢复业务。 */
static u8 app_sh_explicit_sleep_poll(void)
{
#if BMS_PRODUCT_SWITCH_ENABLE && BMS_PRODUCT_SWITCH_SLEEP_ENABLE
    /* 开关OFF优先于命令应答，主循环本次直接提交深睡。 */
    if (!board_switch_is_on()) {
        bms_diag_sleep(DIAG_SLEEP_REASON_SWITCH, 0u, 0u, 0u, 0u, 0u);
        app_enter_protective_sleep();
        return 1u;
    }
#endif
    return app_command_sleep_poll();
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
    app_sleep_status_t protective;
    u32 elapsed_sec = app_pm_take_elapsed_seconds(&elapsed_ctx);
    u32 pm_block = 0u;
    u32 voltage_seconds;
    u8 valid = app_get_fresh_measurements(&m);
    u8 ota_busy = ota_is_working ? 1u : 0u;
    u8 flash_busy = app_flash_lock_restore_enabled() ? 0u : 1u;
    u8 bus_busy = (BUS_STATE_OWC_IDLE != bus_mux_get_state()) ? 1u : 0u;

    if (app_protective_sleep_poll(elapsed_sec, &m, valid, &protective)) return;
    voltage_seconds = (s_protective_sleep.region == 3u) ?
        s_protective_sleep.normal_voltage_seconds : s_protective_sleep.low_voltage_seconds;

    if (!valid) pm_block |= DIAG_PM_BLOCK_SAMPLE_INVALID;
    if (ota_busy) pm_block |= DIAG_PM_BLOCK_OTA;
    if (flash_busy) pm_block |= DIAG_PM_BLOCK_FLASH;
    if (bus_busy) pm_block |= DIAG_PM_BLOCK_BUS;
    if (app_current_prevents_suspend(&m, valid)) pm_block |= DIAG_PM_BLOCK_CURRENT;
    if ((*sample_due)) pm_block |= DIAG_PM_BLOCK_SAMPLE_PENDING;

    /*
     * 0x1102=0x000A 是锁存关机请求，不是空闲 suspend 提示。
     * 应答等待最多500ms，之后直接提交强制入口；保护性计时已在本入口之前推进。
     */
    if (deepsleep_en)
    {
        pm_block |= DIAG_PM_BLOCK_POWER_OFF;
        bms_diag_runtime_pm(0u, pm_block, s_protective_sleep.region, voltage_seconds,
                            (uint8_t)(device_in_connection_state != 0),
                            (*sample_due), APP_SUSPEND_EXIT_CURRENT_MA);
        if (app_command_sleep_poll()) return;
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
        if (app_enter_acc_sleep()) return;
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
    app_dvc_publish_sleep(&protective, s_low_power_mode, valid);
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
    app_sleep_status_t protective;
    u8 sample_valid = app_get_fresh_measurements(&m);
    u8 ui_busy = 0u;
    u8 sample_block;
    if (app_protective_sleep_poll(elapsed_sec, &m, sample_valid, &protective)) return;
    if (app_sh_explicit_sleep_poll()) return;
    sample_block = (u8)(!sample_valid || (*sample_due) ||
        app_current_prevents_suspend(&m, sample_valid));
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
    else if (protective.reason != DIAG_SLEEP_REASON_NONE)
        bms_diag_sleep(protective.reason, 0u, protective.elapsed_ms,
            protective.delay_ms, 0u, s_low_power_mode);
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
    bms_parameters_diag_poll();
    bms_storage_platform_diag_poll();
    bms_afe_diag_poll();
#endif
    return 0u;
}
