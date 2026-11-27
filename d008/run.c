#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"
#include "device.h"
#include "link.h"

MYFIFO_INIT(blt_rxfifo,64,8);
MYFIFO_INIT(blt_txfifo,40,16);
static volatile bool wake_due;

_attribute_ram_code_ void irq_handler(void)
{
    irq_blt_sdk_handler();
    wired_irq();
}
static void app_wake(int source) {(void)source; wake_due=true;}
static void sleep_enter(u8 evt,u8 *data,int n)
{
    (void)evt; (void)data; (void)n; wired_prepare_suspend();
}
static void sleep_exit(u8 evt,u8 *data,int n)
{
    (void)evt; (void)data; (void)n; rf_set_power_level_index(RF_POWER_P0dBm);
}

_attribute_ram_code_ int main(void)
{
    /* SDK 要求在 cpu_wakeup_init 前设定 OTA 槽。 */
    blc_ota_setFirmwareSizeAndBootAddress(124,MULTI_BOOT_ADDR_0x20000);
    blc_pm_select_internal_32k_crystal(); cpu_wakeup_init();
    gpio_write(GPIO_PC4,1); gpio_init(1); clock_init(SYS_CLK_16M_Crystal);
    rf_drv_ble_init(); product_radio_init();
    blc_ll_initPowerManagement_module(); bls_pm_setSuspendMask(SUSPEND_DISABLE);
    bls_pm_registerAppWakeupLowPowerCb(app_wake);
    bls_app_registerEventCallback(BLT_EV_FLAG_SUSPEND_ENTER,sleep_enter);
    bls_app_registerEventCallback(BLT_EV_FLAG_SUSPEND_EXIT,sleep_exit);
    device_init(); wired_init();
    uint32_t phase=clock_time(), last=phase, ms=0, fraction=0, scheduled=0;
    wd_set_interval_ms(2000,TICKS_PER_MS); wd_clear(); wd_start();
    irq_enable(); wake_due=true;
    for (;;) {
        wd_clear(); blt_sdk_main_loop();
        uint32_t now=clock_time(), elapsed=now-last;
        last=now; ms+=elapsed/TICKS_PER_MS; fraction+=elapsed%TICKS_PER_MS;
        if (fraction>=TICKS_PER_MS) {fraction-=TICKS_PER_MS; ++ms;}
        uint32_t due=sample_periods(now,&phase);
        if (due) device_measure(now,ms);
        if (!device_service(ms)) continue;
        wired_poll(ms); product_radio_poll(ms);
        bool sleep=!wired_busy() && !device_busy() && !update_active();
        bls_pm_setSuspendMask(sleep?(SUSPEND_ADV|SUSPEND_CONN):SUSPEND_DISABLE);
        uint32_t next=wired_wakeup(phase+SAMPLE_TICKS);
        if (next!=scheduled || wake_due) {
            scheduled=next; wake_due=false; bls_pm_setAppWakeupLowPower(next,1);
        }
    }
}
