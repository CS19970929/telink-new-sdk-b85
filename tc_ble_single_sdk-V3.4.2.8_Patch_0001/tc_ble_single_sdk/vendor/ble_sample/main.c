/********************************************************************************************************
 * @file    main.c
 * @brief   TLSR825x BMS firmware entry and IRQ dispatch.
 *******************************************************************************************************/
#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"

#include "app.h"
#include "bus_mux.h"
#include "modbus_uart.h"
#include "sif_send.h"

_attribute_ram_code_ void irq_handler(void)
{
    irq_blt_sdk_handler();
    modbus_uart_irq_proc();
    sif_timer_irq_proc();
    bus_mux_irq_handler();
}

_attribute_ram_code_ int main(void)
{
    void (*const irq_entry)(void) = irq_handler;
    int deep_retention_wakeup;

    (void)irq_entry;
    DBG_CHN0_LOW;

    blc_pm_select_internal_32k_crystal();

#if (MCU_CORE_TYPE == MCU_CORE_825x)
    cpu_wakeup_init();
#else
    cpu_wakeup_init(LDO_MODE, INTERNAL_CAP_XTAL24M);
#endif

    deep_retention_wakeup = pm_is_MCU_deepRetentionWakeup();

    rf_drv_ble_init();
    gpio_init(!deep_retention_wakeup);
    clock_init(SYS_CLK_TYPE);

#if (MODULE_WATCHDOG_ENABLE)
    wd_set_interval_ms(WATCHDOG_INIT_TIMEOUT, CLOCK_SYS_CLOCK_1MS);
    wd_start();
#endif

    if (deep_retention_wakeup)
    {
        user_init_deepRetn();
    }
    else
    {
        user_init_normal();
    }

    irq_enable();
    while (1)
    {
#if (MODULE_WATCHDOG_ENABLE)
#if (MCU_CORE_TYPE == MCU_CORE_TC321X)
        if (g_chip_version != CHIP_VERSION_A0)
#endif
        {
            wd_clear();
        }
#endif
        main_loop();
    }
}
