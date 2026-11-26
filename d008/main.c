#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"
#include "vendor/common/ble_flash.h"
#include "meter.h"

MYFIFO_INIT(blt_rxfifo, 64, 8);
MYFIFO_INIT(blt_txfifo, 40, 16);

static const u16 service_uuid = GATT_UUID_PRIMARY_SERVICE;
static const u16 characteristic_uuid = GATT_UUID_CHARACTER;
static const u16 gap_uuid = SERVICE_UUID_GENERIC_ACCESS;
static const u16 name_uuid = GATT_UUID_DEVICE_NAME;
static u8 name[] = "D008-Time";
static u8 name_decl[] = {CHAR_PROP_READ, 3, 0, 0x00, 0x2a};
static const attribute_t attributes[] = {
    {3},
    {3, ATT_PERMISSIONS_READ, 2, 2, (u8 *)&service_uuid, (u8 *)&gap_uuid},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(name_decl), (u8 *)&characteristic_uuid, name_decl},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(name)-1, (u8 *)&name_uuid, name}
};

/* 仅台架目标使用，在 SWS 读取两个时点的差值，与独立时钟和电量计对照。 */
static volatile struct {
    uint32_t samples, missed, max_interval, pm_exits;
    uint64_t elapsed_ticks, missing_ticks;
    int64_t discharge_uas;
    uint8_t radio_status;
} bench;
static uint32_t phase;
static Meter meter;
static volatile u8 app_woke;

_attribute_ram_code_ void irq_handler(void)
{
    irq_blt_sdk_handler();
}

static void app_wakeup(int source)
{
    (void)source;
    app_woke = 1;
}

static void suspend_exit(u8 event, u8 *data, int length)
{
    (void)event; (void)data; (void)length;
    rf_set_power_level_index(RF_POWER_P0dBm);
    ++bench.pm_exits;
}

static u8 radio_init(void)
{
    u8 addr[6];
    u8 advert[] = {2, DT_FLAGS, 6, sizeof(name), DT_COMPLETE_LOCAL_NAME,
                   'D','0','0','8','-','T','i','m','e'};
    random_generator_init();
    blc_readFlashSize_autoConfigCustomFlashSector();
    blc_app_loadCustomizedParameters_normal();
    generateRandomNum(sizeof(addr), addr);
    addr[5] |= 0xc0; /* 台架使用随机静态地址，避免写入出厂 MAC 页。 */
    blc_ll_initBasicMCU();
    blc_ll_initStandby_module(addr);
    blc_ll_initAdvertising_module(addr);
    blc_ll_initConnection_module();
    blc_ll_initSlaveRole_module();
    blc_gap_peripheral_init();
    blc_l2cap_register_handler(blc_l2cap_packet_receive);
    bls_att_setAttributeTable((u8 *)attributes);
    blc_smp_setSecurityLevel(No_Security);
    blc_ll_initPowerManagement_module();
    bls_pm_registerAppWakeupLowPowerCb(app_wakeup);
    bls_app_registerEventCallback(BLT_EV_FLAG_SUSPEND_EXIT, suspend_exit);
    bls_pm_setSuspendMask(BENCH_AWAKE ? SUSPEND_DISABLE : (SUSPEND_ADV | SUSPEND_CONN));
    rf_set_power_level_index(RF_POWER_P0dBm);
    u8 status = blc_ll_setRandomAddr(addr);
    if (!status) status = bls_ll_setAdvData(advert, sizeof(advert));
    if (!status) status = bls_ll_setAdvParam(ADV_INTERVAL_500MS, ADV_INTERVAL_500MS,
        ADV_TYPE_CONNECTABLE_UNDIRECTED, OWN_ADDRESS_RANDOM, 0, NULL, BLT_ENABLE_ADV_ALL, ADV_FP_NONE);
    if (!status) status = bls_ll_setAdvEnable(BLC_ADV_ENABLE);
    return status;
}

_attribute_ram_code_ int main(void)
{
    blc_pm_select_internal_32k_crystal();
    cpu_wakeup_init();
    gpio_write(GPIO_PC4, 1); /* 先预装保持电平，再由 gpio_init 打开输出。 */
    gpio_init(1);
    clock_init(SYS_CLK_16M_Crystal);
    rf_drv_ble_init();
    bench.radio_status = radio_init();
    phase = clock_time();
    uint32_t missing;
    meter_sample(&meter, phase, 1000, true, &missing);
    if (!BENCH_AWAKE) bls_pm_setAppWakeupLowPower(phase + SAMPLE_TICKS, 1);
    irq_enable();
    for (;;) {
        blt_sdk_main_loop();
        uint32_t now = clock_time();
        uint32_t periods = sample_periods(now, &phase);
        if (periods) {
            uint32_t interval = now - meter.last_tick;
            bench.elapsed_ticks += interval;
            if (interval > bench.max_interval) bench.max_interval = interval;
            bench.discharge_uas += meter_sample(&meter, now, 1000, true, &missing);
            bench.missing_ticks += missing;
            bench.missed += periods - 1u;
            ++bench.samples;
        }
        if (!BENCH_AWAKE && (periods || app_woke)) {
            app_woke = 0;
            bls_pm_setAppWakeupLowPower(phase + SAMPLE_TICKS, 1);
        }
    }
}
