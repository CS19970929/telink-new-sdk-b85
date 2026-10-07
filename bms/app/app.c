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
static u32 s_sample_tick;

/* 低功耗唤醒回调只置位；主循环消费后执行采样，不在回调中跑保护/SOC。 */
static volatile u8 s_sample_due;

#define SOC_SAMPLE_PACK_FAULT_MASK         0x000Cu
#define SOC_SAMPLE_CURRENT_FAULT_MASK      0x0030u
#define SOC_SAMPLE_TEMP_FAULT_MASK         0x2BC0u

/* 四产品同口请求均为 CHG/DSG 开启；guard 授权、方向保护和后端反馈决定实际输出。
 * D008 ACC/PB1 和 SH PA0 均不能代替已有安全门禁。 */
void mos_update(void)
{
    g_bms_system_status.bits.cooler_status = 0u;
    (void)bms_afe_set_fets(1u, 1u);
}

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
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

#endif
/* 按秒推进历史事件与运行计时任务。 */
static void app_event_log_1s_task(void)
{
	bms_event_log_sample_t sample;

    _attribute_data_retention_ static u32 event_log_tick = 0;

    if (!clock_time_exceed(event_log_tick, 1000 * 1000)) return;
    event_log_tick = clock_time();

	memset(&sample, 0, sizeof(sample));

	sample.balance = ((g_bms_report.balance_bits_low != 0u) || (g_bms_report.balance_bits_high != 0u)) ? 1u : 0u;

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
/* 一条主循环：BLE → 到期采样 → 通信 → 状态检查点 → 板级低功耗。 */
_attribute_no_inline_ void main_loop(void)
{
    if (app_power_prepare_loop()) return;
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
	bms_state_store_update_and_log_if_changed(g_bms_soc.soc_estimate_percent, g_bms_soc.discharge_fraction_percent, g_bms_soc.cycle_count);
	app_power_process(&s_sample_due);
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
    uint16_t third_faults = g_bms_report.fault_third.all;
    memset(&sample, 0, sizeof(sample));
    memset(&feature, 0, sizeof(feature));

    sample.timestamp_32k = sample_tick_32k;
    sample.current_ma = current_ma;
    sample.pack_voltage_mv = (uint32_t)g_bms_report.pack_voltage_10mv * 10u;
    sample.cell_min_mv = g_bms_report.cell_min_mv;
    sample.cell_max_mv = g_bms_report.cell_max_mv;
    sample.cell_delta_mv = g_bms_report.cell_delta_mv;
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
        g_bms_report.fault_third.bits.cell_ovp;
    sample.third_cell_uvp =
        g_bms_report.fault_third.bits.cell_uvp;
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
	mos_update();

	bms_product_info_refresh();
	bms_afe_set_output_enabled(1u);
    bms_diag_set_boot_result(
        g_bms_system_status.bits.afe1_status ? DIAG_OK : DIAG_INVALID,
        bms_protection_params_valid() ? DIAG_OK : DIAG_INVALID);
    bms_parameters_diag_poll();
    bms_storage_platform_diag_poll();
    bms_afe_diag_poll();
    bms_diag_freeze_boot();
}
