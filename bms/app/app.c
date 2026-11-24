/*
 * 文件功能：BMS 启动、采样、SOC/MOS 与通信调度；低功耗策略见 app_power.c。
 * bms/app/app.c；实际编译归属见各产品 sources.txt。
 */
/********************************************************************************************************
 * @file    app.c
 *
 * @brief   BMS 业务启动与协作主循环。
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
#include "bms_product.h"
#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"
#include "app_config.h"
#include "app.h"
#include "app_power.h"
#include "ble_ota.h"
#include "app_att.h"
#include "modbus_uart.h"
#include "modbus_rtu.h"
#include "bms_afe.h"
#include "bms_features.h"
#include "bms_afe_hw_access.h"
#include "bms_error.h"
#include "bms_state.h"
#include "bms_soc_app.h"
#include "bms_time.h"
#include "bms_event_log.h"
#include "bms_sleep_config.h"
#include "bms_state_store.h"
#include "bms_storage_platform.h"
#include "btname_modbus.h"
#include "bms_parameters.h"
#include "bms_parameter_access.h"
#include <string.h>

#include "sif_send.h"
#include "bus_mux.h"

/* 采集并处理一次 AFE 样本，推进保护、SOC 和 MOS 仲裁。 */
static void app_sample_task(void);
/* 采样唤醒回调消费 SDK 期限并交付标志，AFE/SOC 留在主循环。 */
static void app_sample_wakeup(int type);
static u32 s_sample_tick;

/* 唤醒回调交付期限；主循环消费后执行采样，不在回调中跑保护/SOC。 */
static volatile u8 s_sample_due;

/* SDK 临睡回调也检查期限，覆盖主循环许可后才到期的窗口。 */
uint8_t app_sample_is_due(void)
{
    return s_sample_due || clock_time_exceed(s_sample_tick, APP_SAMPLE_PERIOD_US);
}


/* 四产品始终在下一次采样期限前唤醒，不从处理结束时再睡满一个周期。 */
static void app_schedule_sample_wakeup(void)
{
    bls_pm_setAppWakeupLowPower(
        s_sample_tick + APP_SAMPLE_PERIOD_US * SYSTEM_TIMER_TICK_1US, 1u);
}

/* 编程和擦除窗口分别估算；实装 Flash 最长时序仍需板上核定。 */
#define APP_FLASH_PROGRAM_WINDOW_US 6000u
#define APP_FLASH_ERASE_WINDOW_US 60000u

static uint32_t app_flash_window_us(void)
{
    const uint32_t ticks_per_us = CLOCK_SYS_CLOCK_HZ / 1000000u;
    uint32_t now = clock_time();
    uint32_t age = (now - s_sample_tick) / ticks_per_us;
    uint32_t window;

    if (ota_is_working || !app_flash_lock_restore_enabled() || s_sample_due ||
        modbus_uart_tx_active() || age >= APP_SAMPLE_PERIOD_US) return 0u;
    window = APP_SAMPLE_PERIOD_US - age;
    if (device_in_connection_state) {
        int32_t ticks = (int32_t)(bls_pm_getNexteventWakeupTick() - now);
        if (ticks <= 0) return 0u;
        if ((uint32_t)ticks / ticks_per_us < window) window = (uint32_t)ticks / ticks_per_us;
    }
    return window;
}

static void app_storage_task(void)
{
    static uint32_t last_state_poll_ms;
    uint32_t window = app_flash_window_us();
    uint32_t now;
    uint8_t allow_erase;

    if (window < APP_FLASH_PROGRAM_WINDOW_US) return;
    allow_erase = window >= APP_FLASH_ERASE_WINDOW_US && bus_mux_get_state() != BUS_STATE_UART_MODBUS;
    now = bms_time_ms();
    if (now - last_state_poll_ms >= 1000u) {
        bms_soc_checkpoint_t state = bms_soc_app_checkpoint();
        last_state_poll_ms = now;
        if (bms_state_store_poll_snapshot(&state, allow_erase)) return;
    }
    (void)bms_event_log_service(allow_erase);
}

void app_storage_before_ota(void)
{
    bms_soc_checkpoint_t state;

    /* 升级入口只使用现成空槽，不在协议处理路径擦扇区。 */
    if (app_flash_window_us() < APP_FLASH_PROGRAM_WINDOW_US) return;
    state = bms_soc_app_checkpoint();
    (void)bms_state_store_save_snapshot(&state, 0u);
    if (app_flash_window_us() >= APP_FLASH_PROGRAM_WINDOW_US)
        (void)bms_event_log_service(0u);
}

/* 按选定产品初始化 AFE 接口、供电和板级 GPIO。 */
static void board_init(void)
{
	bms_afe_set_output_enabled(0u);

	/* PD4 是加热保险丝驱动，不是 BLE 射频供电；启动保持安全无效电平。 */
	gpio_set_func(BMS_BOARD_HEATER_FUSE_PIN, AS_GPIO);
	gpio_set_input_en(BMS_BOARD_HEATER_FUSE_PIN, 0);
	gpio_set_output_en(BMS_BOARD_HEATER_FUSE_PIN, 1);
	gpio_write(BMS_BOARD_HEATER_FUSE_PIN, 0);

	gpio_set_func(BMS_BOARD_ACC_PIN, AS_GPIO);
	gpio_set_input_en(BMS_BOARD_ACC_PIN, 1);
	gpio_set_output_en(BMS_BOARD_ACC_PIN, 0);

	/* PB1 用于保护后的负载移除检测；保持原输入电气配置，不启用 PAD 唤醒。 */
	gpio_set_func(BMS_BOARD_LOAD_DETECT_PIN, AS_GPIO);
	gpio_setup_up_down_resistor(BMS_BOARD_LOAD_DETECT_PIN, PM_PIN_PULLUP_1M);
	gpio_set_input_en(BMS_BOARD_LOAD_DETECT_PIN, 1);
	gpio_set_output_en(BMS_BOARD_LOAD_DETECT_PIN, 0);

	gpio_set_func(LED_BLUE_PIN, AS_GPIO);
	gpio_set_input_en(LED_BLUE_PIN, 0);
	gpio_set_output_en(LED_BLUE_PIN, 1);
	gpio_write(LED_BLUE_PIN, 1);
}

/* BLE → 通信接收恢复 → 到期采样 → 状态检查点 → 协议通信 → 低功耗。 */
_attribute_no_inline_ void main_loop(void)
{
    if (app_power_prepare_loop()) return;
    /* 到期业务必须先撤下 SDK 旧期限；不能等 SDK 返回后才处理过期定时器。 */
    if (app_sample_is_due()) {
        bls_pm_setAppWakeupLowPower(0u, 0u);
        bls_pm_setSuspendMask(SUSPEND_DISABLE);
    }
    /*
     * SDK唤醒后先恢复UART接收/通信引脚所有权，避免等AFE和Flash任务后
     * 才开始接收主机重发。采样/保护仍优先于协议命令解析。
     */
	blt_sdk_main_loop();
    bus_mux_task();

    app_sample_task();

	/* 处理采样/存储期间到来的接管边沿，避免再多等一轮SDK。 */
	bus_mux_task();
    sif_prepare_task(s_sample_tick);
#if BMS_PRODUCT_UART_ENABLE
	main_loop_modbus();
#endif
    app_storage_task();
	app_power_process(&s_sample_due);
}


/* 采集并处理一次 AFE 样本，推进保护、SOC 和 MOS 仲裁。 */
static void app_sample_task(void)
{
    bms_afe_aux_measurements_t m;
    bms_afe_sample_status_t result;
    u8 valid;

    if (!app_sample_is_due()) return;

    s_sample_due = 0u;
    s_sample_tick = clock_time();
    result = bms_afe_sample();
    if (result == BMS_AFE_SAMPLE_OK) {
        bms_features_service();
        bms_afe_apply_outputs();
    }
    valid = bms_afe_get_aux_measurements(&m);
    bms_soc_app_sample(valid ? &m : NULL);
    bms_event_log_observe();


    /* 采样期限独立于 BLE 广播间隔；超期只标记下一轮，不补跑历史样本。 */
    if (clock_time_exceed(s_sample_tick, APP_SAMPLE_PERIOD_US)) s_sample_due = 1u;
    app_schedule_sample_wakeup();
    gpio_toggle(LED_BLUE_PIN);
}

/* 四产品共用 BLE 回调；后端差异留在采样和电源流程。 */
/* 交付一次应用唤醒；回调不执行业务或外设访问。 */
static void app_sample_wakeup(int type)
{
    (void)type;
    s_sample_due = 1u;
    /* 一次性唤醒已交付：先消费 SDK 期限并交还运行权，主循环采样后重新布防。
     * 此处只改 SDK/RAM 状态，不执行 AFE、保护、SOC、Flash 或串口收发。 */
    bls_pm_setAppWakeupLowPower(0u, 0u);
    bls_pm_setSuspendMask(SUSPEND_DISABLE);
}



/* 参数、AFE 与业务状态启动；由平台完成 BLE 初始化后调用一次。 */
void app_init(void)
{
	{
		board_init();
		bms_parameters_init();

		bms_afe_init();
        cpu_set_gpio_wakeup(BMS_BOARD_LOAD_DETECT_PIN, Level_Low, 0);
        cpu_set_gpio_wakeup(BMS_BOARD_ACC_PIN, Level_Low, 0);

		/* 启动电压、电流和温度状态使用同一 AFE 快照。 */
		bms_afe_sample();
		bms_soc_app_init();
	}

    sif_timer_init();
    bus_mux_init();
	btname_init();
	bms_event_log_note_startup();

    s_sample_tick = clock_time();
    s_sample_due = 0u;
    bls_pm_registerAppWakeupLowPowerCb(app_sample_wakeup);
    app_schedule_sample_wakeup();
    /* 固定同口产品请求只在启动提交；guard 每次采样后按资格和保护重新仲裁。
     * AFE watchdog 恢复只重初始化后端，保留此请求；完整 app_init 则重新提交。 */
    g_bms_system_status.bits.cooler_status = 0u;
    (void)bms_afe_set_fets(1u, 1u);

	bms_product_info_refresh();
	bms_afe_set_output_enabled(1u);
}
