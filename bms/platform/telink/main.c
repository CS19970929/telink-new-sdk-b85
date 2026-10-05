/*
 * 文件功能：TLSR8251 启动与 IRQ 入口；初始化 SDK/应用并分派硬件中断，
 * 业务处理保持在主循环。
 * bms/platform/telink/main.c；实际编译归属见各产品 sources.txt。
 */
#include "bms_afe_backend.h"

/********************************************************************************************************
 * @file    main.c
 *
 * @brief   BLE SDK 应用源文件。
 *
 * @author  BLE GROUP
 * @date    06,2020
 *
 * @par     Copyright (c) 2020, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
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
#include "app.h"
#include "bms_stack_monitor.h"
#include "modbus_uart.h"
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
#include "bus_mux.h"
#include "sif_send.h"
#else
#include "sh3673510_project_config.h"
#endif

/* 分派 SDK、UART DMA、单线与总线中断，业务留在主循环。 */
_attribute_ram_code_ void irq_handler(void)
{
    irq_blt_sdk_handler();
    modbus_uart_irq_proc();
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    sif_timer_irq_handler();
    bus_mux_irq_handler();
#endif
}

/* 初始化 MCU 和 SDK，按启动类型进入应用并持续运行主循环。 */
_attribute_ram_code_ int main (void)    // 必须在 ramcode 中执行。
{
	/* irq_handler 由启动汇编/中断向量调用；局部引用显式记录该外部入口。 */
	void (*const irq_entry)(void) = irq_handler;
#if BMS_BOARD_DEBUG_LED_ENABLE
	u32 debug_led_tick;
	u8 debug_led_level = 0u;
#endif
	(void)irq_entry;

	DBG_CHN0_LOW;   // 调试

	blc_pm_select_internal_32k_crystal();

	#if(MCU_CORE_TYPE == MCU_CORE_825x)
		cpu_wakeup_init();
	#else
		cpu_wakeup_init(LDO_MODE,INTERNAL_CAP_XTAL24M);
	#endif

	int deepRetWakeUp = pm_is_MCU_deepRetentionWakeup();  // MCU 深睡保留唤醒。

	rf_drv_ble_init();

	gpio_init(!deepRetWakeUp);  // 模拟电阻配置在 deepSleep 中保留，无需重新初始化。

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    /* 先设置供电锁存电平再启用输出，低脉冲会切断 MCU 电源。 */
    gpio_set_func(MCU_LDO_PIN, AS_GPIO);
    gpio_write(MCU_LDO_PIN, 1u);
    gpio_set_input_en(MCU_LDO_PIN, 0u);
    gpio_set_output_en(MCU_LDO_PIN, 1u);
#endif

	clock_init(SYS_CLK_TYPE);

	#if (MODULE_WATCHDOG_ENABLE)
		wd_set_interval_ms(WATCHDOG_INIT_TIMEOUT,CLOCK_SYS_CLOCK_1MS);
		wd_start();
	#endif

	if( deepRetWakeUp ){
		user_init_deepRetn();
	}
	else{
		user_init_normal();
	}

#if BMS_BOARD_DEBUG_LED_ENABLE
	gpio_set_func(BMS_BOARD_DEBUG_LED_PIN, AS_GPIO);
	gpio_set_input_en(BMS_BOARD_DEBUG_LED_PIN, 0);
	gpio_write(BMS_BOARD_DEBUG_LED_PIN, debug_led_level);
	gpio_set_output_en(BMS_BOARD_DEBUG_LED_PIN, 1);
	debug_led_tick = clock_time();
#endif

    irq_enable();
	while (1) {
	#if (MODULE_WATCHDOG_ENABLE)
		#if (MCU_CORE_TYPE == MCU_CORE_TC321X)
			if (g_chip_version != CHIP_VERSION_A0)
		#endif
			{
				wd_clear(); // 清除看门狗。
			}
	#endif
#if BMS_BOARD_DEBUG_LED_ENABLE
		if (clock_time_exceed(debug_led_tick, 200 * 1000)) {
			debug_led_tick = clock_time();
			debug_led_level ^= 1u;
			gpio_write(BMS_BOARD_DEBUG_LED_PIN, debug_led_level);
		}
#endif
		main_loop();
		bms_stack_monitor_poll();
	}
}
