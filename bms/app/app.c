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
#include "ble_ota.h"
#include "app_att.h"
#include "modbus_uart.h"
#include "modbus_rtu.h"
#include "bms_afe.h"
#include "bms_features.h"
#include "bms_afe_hw_access.h"
#include "bms_diag.h"
#include "bms_error.h"
#include "bms_state.h"
#include "bms_soc.h"
#include "bms_time.h"
#include "bms_soc_profile.h"
#include "bms_event_log.h"
#include "bms_sleep_config.h"
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
static void app_update_soc_from_sample(uint8_t valid, int32_t current_ma, uint8_t calibration_valid, uint8_t check_active);
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

#define SOC_SAMPLE_PACK_FAULT_MASK         0x000Cu
#define SOC_SAMPLE_CURRENT_FAULT_MASK      0x0030u
#define SOC_SAMPLE_TEMP_FAULT_MASK         0x2BC0u

/* 四产品始终在下一次采样期限前唤醒，不从处理结束时再睡满一个周期。 */
static void app_schedule_sample_wakeup(void)
{
    bls_pm_setAppWakeupLowPower(
        s_sample_tick + APP_SAMPLE_PERIOD_US * SYSTEM_TIMER_TICK_1US, 1u);
}

/* 按秒推进历史事件与运行计时任务。 */
static void app_storage_1s_task(void)
{
	bms_event_log_sample_t sample;

    _attribute_data_retention_ static u32 event_log_ms = 0;

    if ((u32)(bms_time_ms() - event_log_ms) <= 1000u) return;
    event_log_ms = bms_time_ms();

	memset(&sample, 0, sizeof(sample));

	sample.vcell_ovp = g_bms_report.fault_third.bits.cell_ovp ? 1u : 0u;
	sample.vbus_ovp = g_bms_report.fault_third.bits.pack_ovp ? 1u : 0u;
	sample.chg_ocp = g_bms_report.fault_third.bits.charge_ocp ? 1u : 0u;
	sample.vcell_uvp = g_bms_report.fault_third.bits.cell_uvp ? 1u : 0u;
	sample.vbus_uvp = g_bms_report.fault_third.bits.pack_uvp ? 1u : 0u;
	sample.dsg_ocp = g_bms_report.fault_third.bits.discharge_ocp ? 1u : 0u;
	sample.chg_utp = g_bms_report.fault_third.bits.charge_utp ? 1u : 0u;
	sample.dsg_utp = g_bms_report.fault_third.bits.discharge_utp ? 1u : 0u;
	sample.chg_otp = g_bms_report.fault_third.bits.charge_otp ? 1u : 0u;
	sample.dsg_otp = g_bms_report.fault_third.bits.discharge_otp ? 1u : 0u;
	sample.vdelta_op = g_bms_report.fault_third.bits.cell_delta_high ? 1u : 0u;

	sample.afe1_err = bms_error_get(BMS_ERROR_AFE1) ? 1u : 0u;
	sample.cbc_err = bms_error_get(BMS_ERROR_CBC_DSG) ? 1u : 0u;

	bms_event_log_poll_1s(&sample);
#if BMS_SOC_BOARD_TEST_ENABLE
    /* 假电流结果不能进入正常 State checkpoint。 */
    if (!bms_soc_board_test_active())
#endif
	{
        bms_state_store_data_t state = bms_soc_state_snapshot();
        bms_state_store_poll_snapshot(&state);
    }
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

#else

/* 按选定产品初始化 AFE 接口、供电和板级 GPIO。 */
static void board_init(void)
{
	bms_afe_set_output_enabled(0u);

	/* 加热 GPIO 归功能后端与产品能力管理。 */

	gpio_set_func(BMS_BOARD_SWITCH_PIN, AS_GPIO);
	gpio_set_input_en(BMS_BOARD_SWITCH_PIN, 1);
	gpio_set_output_en(BMS_BOARD_SWITCH_PIN, 0);

	/* D014由UART所有者按独立唤醒供电；其余板保持原有通信供电。 */
	gpio_set_func(BMS_BOARD_CMNT_EN_PIN, AS_GPIO);
#if BMS_PRODUCT_RS485_POWER_GATING_ENABLE && !BMS_RS485_TX_DIAG_ENABLE
	gpio_write(BMS_BOARD_CMNT_EN_PIN, 0);
#else
	gpio_write(BMS_BOARD_CMNT_EN_PIN, 1);
#endif
	gpio_set_input_en(BMS_BOARD_CMNT_EN_PIN, 0);
	gpio_set_output_en(BMS_BOARD_CMNT_EN_PIN, 1);

	/*
	 * D014的PD3光耦高电平唤醒由UART所有者配置；不推定其余板的极性。
	 */
	gpio_set_func(BMS_BOARD_CMNT_WK_PIN, AS_GPIO);
	gpio_set_output_en(BMS_BOARD_CMNT_WK_PIN, 0);
	gpio_set_input_en(BMS_BOARD_CMNT_WK_PIN, 1);
}

#endif
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
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    bus_mux_task();
#elif BMS_PRODUCT_UART_ENABLE
    (void)modbus_uart_service_wakeup();
#endif

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
    app_storage_1s_task();

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
	/* 处理采样/存储期间到来的接管边沿，避免再多等一轮SDK。 */
	bus_mux_task();
    sif_prepare_task(s_sample_tick);
#endif
#if BMS_PRODUCT_UART_ENABLE
	main_loop_modbus();
#endif
	app_power_process(&s_sample_due);
#if BMS_SOC_BOARD_TEST_ENABLE
    if (bms_soc_board_test_keep_awake()) bls_pm_setSuspendMask(SUSPEND_DISABLE);
#endif
}

#if BMS_SERVICE_DIAG_ENABLE
/* 从应用与后端缓存汇总运行诊断，不额外采样 AFE。 */
static void app_diag_poll_runtime(uint8_t valid, int32_t raw_current_ma,
                                  int32_t current_ma, uint32_t tick_32k)
{
    bms_soc_diag_t soc;
    bms_diag_runtime_sample(valid,raw_current_ma,current_ma,tick_32k,bms_afe_current_recovery_pending());
    BMS_LOG(BMS_LOG_DEBUG, BMS_LOG_AFE, BMS_LOG_CELL_RANGE,
            ((uint32_t)g_bms_report.cell_min_mv << 16) | g_bms_report.cell_max_mv,
            ((uint32_t)g_bms_report.cell_min_index << 16) | g_bms_report.cell_max_index);
    bms_soc_get_diag(&soc);
    bms_diag_runtime_soc(&soc);
    bms_diag_runtime_faults(g_bms_report.fault_first.all,
        g_bms_report.fault_second.all,g_bms_report.fault_third.all);
#if BMS_AFE_BACKEND != BMS_AFE_BACKEND_DVC1124
    /* DVC 在主循环停机分支之前刷新，保留原诊断时点与总线静默资格。 */
    bms_storage_platform_diag_poll();
    bms_parameters_diag_poll();
    bms_afe_diag_poll();
#endif
}
#endif

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
    app_update_soc_from_sample(valid, valid ? m.current_ma : 0, valid ? m.voltage_calibration_valid : 0u, valid ? m.voltage_check_active : 0u);

#if BMS_SERVICE_DIAG_ENABLE
    app_diag_poll_runtime(valid, valid ? m.raw_current_ma : 0,
                          valid ? m.current_ma : 0,
                          valid ? m.sample_tick_32k : pm_get_32k_tick());
#endif

    /* 采样期限独立于 BLE 广播间隔；超期只标记下一轮，不补跑历史样本。 */
    if (clock_time_exceed(s_sample_tick, APP_SAMPLE_PERIOD_US)) s_sample_due = 1u;
    app_schedule_sample_wakeup();
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    gpio_toggle(LED_BLUE_PIN);
#elif BMS_BOARD_DEBUG_LED_ENABLE
    /* 调试灯跟随真实采样尝试翻转，避免独立200ms计时在Suspend后相位错开。
     * AFE失败也翻转：它表示调度仍在运行，不表示采样结果有效。 */
    gpio_toggle(BMS_BOARD_DEBUG_LED_PIN);
#endif
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



/* 把合格 AFE 样本及时间戳提交给 SOC 算法。 */
static void app_update_soc_from_sample(uint8_t valid, int32_t current_ma, uint8_t calibration_valid, uint8_t check_active)
{
    bms_afe_feature_snapshot_t feature;
    bms_soc_sample_t sample;
    uint16_t third_faults = g_bms_report.fault_third.all;
    memset(&sample, 0, sizeof(sample));

    /* 积分使用应用时间；旧电流可用于新时间段，但不能冒充新的确认样本。 */
    sample.timestamp_ms = bms_time_ms();
    sample.timestamp_32k = pm_get_32k_tick();
    sample.current_ma = current_ma;
    sample.cell_min_mv = g_bms_report.cell_min_mv;
    sample.cell_max_mv = g_bms_report.cell_max_mv;
    sample.cell_delta_mv = g_bms_report.cell_delta_mv;
    sample.pack_voltage_10mv = g_bms_report.pack_voltage_10mv;
    sample.cell_count = BMS_PRODUCT_CELL_COUNT;
    memcpy(sample.cell_mv, g_bms_report.cell_voltage_mv, sample.cell_count * sizeof(sample.cell_mv[0]));
    sample.voltage_calibration_valid = calibration_valid;
    sample.voltage_check_active = check_active;
    sample.balance_active = (g_bms_report.balance_bits_low || g_bms_report.balance_bits_high) ? 1u : 0u;
    sample.sample_valid = valid ? 1u : 0u;
    sample.voltage_valid = (valid && sample.cell_min_mv != 0u &&
                            sample.cell_max_mv >= sample.cell_min_mv) ? 1u : 0u;
    if (valid && bms_afe_get_feature_snapshot(&feature) &&
        feature.battery_temp_valid) {
        sample.temperature_valid = 1u;
        sample.temperature_min_x10 = feature.battery_temp_min_x10;
        sample.temperature_max_x10 = feature.battery_temp_max_x10;
    }
    sample.afe_fault = bms_error_get(BMS_ERROR_AFE1);
    sample.temperature_fault =
        ((third_faults & SOC_SAMPLE_TEMP_FAULT_MASK) != 0u) ? 1u : 0u;
    sample.current_fault =
        ((third_faults & SOC_SAMPLE_CURRENT_FAULT_MASK) != 0u) ? 1u : 0u;
    sample.pack_fault =
        ((third_faults & SOC_SAMPLE_PACK_FAULT_MASK) != 0u) ? 1u : 0u;
    sample.third_cell_ovp =
        g_bms_report.fault_third.bits.cell_ovp;
    sample.third_cell_uvp =
        g_bms_report.fault_third.bits.cell_uvp;
    sample.third_pack_uvp = g_bms_report.fault_third.bits.pack_uvp;
#if BMS_SOC_BOARD_TEST_ENABLE
    bms_soc_board_test_suspend_exits(app_ble_suspend_exit_count());
    bms_soc_board_test_prepare(&sample, pm_get_32k_tick());
#endif
    bms_soc_process_sample(&sample);
    /* 板测伪电压只用于算法观察，不得触发真实 AFE 激励。 */
#if BMS_SOC_BOARD_TEST_ENABLE
    if (!bms_soc_board_test_active())
#endif
        if (bms_soc_voltage_check_requested()) bms_afe_request_voltage_check();
#if BMS_SOC_BOARD_TEST_ENABLE
    bms_soc_board_test_note();
#endif
}

/* 参数、AFE 与业务状态启动；由平台完成 BLE 初始化后调用一次。 */
void app_init(void)
{
	{
#if BMS_SERVICE_DIAG_ENABLE
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
#endif
		board_init();
		bms_parameters_init();

		bms_afe_init();
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
        cpu_set_gpio_wakeup(BMS_BOARD_LOAD_DETECT_PIN, Level_Low, 0);
        cpu_set_gpio_wakeup(BMS_BOARD_ACC_PIN, Level_Low, 0);
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
    /* 固定同口产品请求只在启动提交；guard 每次采样后按资格和保护重新仲裁。
     * AFE watchdog 恢复只重初始化后端，保留此请求；完整 app_init 则重新提交。 */
    g_bms_system_status.bits.cooler_status = 0u;
    (void)bms_afe_set_fets(1u, 1u);

	bms_product_info_refresh();
	bms_afe_set_output_enabled(1u);
#if BMS_SERVICE_DIAG_ENABLE
    bms_diag_set_boot_result(
        g_bms_system_status.bits.afe1_status ? DIAG_OK : DIAG_INVALID,
        bms_protection_params_valid() ? DIAG_OK : DIAG_INVALID);
    bms_parameters_diag_poll();
    bms_storage_platform_diag_poll();
    bms_afe_diag_poll();
    bms_diag_freeze_boot();
#endif
}
