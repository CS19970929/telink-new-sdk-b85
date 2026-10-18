/* 只模拟 SDK/AFE/存储边界；生产计时与已提交保持动作从 app_power.c 提取。 */
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "bms_diag.h"
typedef uint8_t u8;
typedef uint32_t u32;
typedef int GPIO_PinTypeDef;
#define APP_PM_MS_PER_SEC 1000u
#define BMS_SOC_MAX_SAMPLE_GAP_MS 400u
#define BMS_SAMPLE_MAX_POLL_GAP_MS 400u
#define BMS_CURRENT_UNRELIABLE_MAX_MA 200u
#define BMS_SLEEP_LOW_CELL_MV 2800u
#define BMS_SLEEP_LOW_SECONDS 3600u
#define BMS_SLEEP_NORMAL_CELL_MV 3000u
#define BMS_SLEEP_NORMAL_SECONDS 86400u
#define BMS_ERROR_AFE1 0
#define BMS_BOARD_SWITCH_PIN 0
#define BMS_BOARD_INT_WK_MCU_PIN 1
#define BMS_BOARD_AFE_ALARM_PIN 2
#define BMS_BOARD_AFE_RESET_OUT_PIN 3
#define BMS_BOARD_CMNT_EN_PIN 4
#define BMS_BOARD_SCI1_RX_PIN 5
#define BMS_PRODUCT_SWITCH_ENABLE 1
#define DEEP_ANA_REG6 0x35u
#define WAKEUP_STATUS_PAD 8u
#define AS_GPIO 1
#define FLD_IRQ_GPIO_RISC0_EN 8
#define Level_Low 0
#define Level_High 1
#define DEEPSLEEP_MODE 0x80
#define PM_WAKEUP_PAD 16
#define HCI_ERR_REMOTE_USER_TERM_CONN 19
#define BLC_ADV_DISABLE 0
#define SUSPEND_DISABLE 0
typedef struct { u32 epoch_ms; u8 ready; } app_pm_elapsed_ctx_t;
typedef struct { u32 sample_time_ms; int32_t current_ma; } bms_afe_aux_measurements_t;
static struct { uint16_t cell_min_mv, pack_voltage_10mv; struct { struct { uint8_t cell_uvp, pack_uvp; } bits; } fault_third; } g_bms_report;
static struct { u8 soc_estimate_percent, discharge_fraction_percent; u32 cycle_count; } g_bms_soc;
static bms_afe_aux_measurements_t measurement;
static int valid, afe_error, ota_is_working, device_in_connection_state, flash_ready;
static bool s_low_power_mode, deepsleep_en;
static int tx_busy, ble_pending, uart_ready, uart_entered;
static u32 reg_irq_src;
static u8 retained_analog, wakeup_status;
static u8 analog_read(u8 address) { assert(address==DEEP_ANA_REG6); return retained_analog; }
static void analog_write(u8 address,u8 value) { assert(address==DEEP_ANA_REG6); retained_analog=value; }
static u8 pm_get_wakeup_src(void) { return wakeup_status; }
static void gpio_set_func(int pin,int func) { (void)pin; assert(func==AS_GPIO); }
static void gpio_set_input_en(int pin,int en) { (void)pin;(void)en; }
static void gpio_set_output_en(int pin,int en) { (void)pin;(void)en; }
static u32 now, saves, events, afe_calls, output_calls, sleep_calls, pad_calls;
static u32 comm_sleep_calls;
static u8 reason;
static int pins[6], levels[6], enabled[6];
static int board_switch_is_on(void) { return !pins[0]; }
static int uart_tx_is_busy(void) { return tx_busy; }
static int modbus_uart_tx_active(void) { return tx_busy; }
static int blc_ll_getTxFifoNumber(void) { return ble_pending; }
static int modbus_uart_suspend_ready(void) { return uart_ready; }
static void modbus_uart_suspend_enter(void) { ++uart_entered; }
static void modbus_uart_prepare_deep_sleep(void) { ++comm_sleep_calls; }
static void gpio_en_interrupt_risc0(int pin,int en) { assert(pin==5&&!en); }
static u32 bms_time_ms(void) { return now; }
static u8 bms_afe_get_aux_measurements(bms_afe_aux_measurements_t *m) { *m=measurement; return valid; }
static u8 bms_error_get(int error) { assert(error==BMS_ERROR_AFE1); return afe_error; }
static void bms_afe_set_output_enabled(u8 en) { assert(!en); ++output_calls; }
static u8 bms_afe_sleep(void) { ++afe_calls; return 0u; }
static int app_flash_lock_restore_enabled(void) { return flash_ready; }
static u8 bms_state_store_save(u8 s, u8 d, u32 c) { (void)s;(void)d;(void)c; ++saves; return 0u; }
static u8 bms_event_log_note_sleep(void) { ++events; return 0u; }
static int bls_ll_terminateConnection(int r) { assert(r==HCI_ERR_REMOTE_USER_TERM_CONN); return -1; }
static int bls_ll_setAdvEnable(int en) { assert(!en); return -1; }
static void bls_pm_setSuspendMask(int mask) { assert(!mask); }
static void bls_pm_setAppWakeupLowPower(u32 t, u8 en) { assert(!t&&!en); }
static int gpio_read(int pin) { assert(pin>=0&&pin<4); return pins[pin]; }
static void gpio_write(int pin, int en) { assert(pin==BMS_BOARD_CMNT_EN_PIN&&!en); }
static void cpu_set_gpio_wakeup(int pin, int level, int en) {
    assert(pin>=0&&pin<6&&pin!=4); if(pin==5) assert(!en); levels[pin]=level; enabled[pin]=en; ++pad_calls;
}
static int cpu_sleep_wakeup(int mode, int source, u32 tick) {
    assert(mode==DEEPSLEEP_MODE&&source==PM_WAKEUP_PAD&&!tick);
    for (int pin=0;pin<4;++pin) if(enabled[pin]) assert(levels[pin]!=pins[pin]);
    assert(!enabled[5]);
    ++sleep_calls; return 0x100; /* SDK PAD 竞争拒睡 */
}
void bms_diag_sleep(u8 r,u32 b,u32 e,u32 d,u32 retry,u8 allowed) {
    (void)e;(void)d;(void)allowed; assert(!b&&!retry); reason=r;
}
void bms_diag_sleep_committed(void) {}
#include "bms_sleep_config.h"
static bms_sleep_config_t sleep_config;
static struct { uint16_t cell_uvp_recover_mv, pack_uvp_recover_10mv; } g_bms_protection_params={2900u,2900u};
void bms_config_get_sleep(bms_sleep_config_t *config){*config=sleep_config;}

/* PRODUCTION */
/* 回归场景仍按生产调用方读取一次有效快照，然后推进保护性休眠计时。 */
static u8 fixture_sleep_poll(u32 seconds, app_sleep_status_t *status)
{
    bms_afe_aux_measurements_t snapshot;
    u8 sample_valid = bms_afe_get_aux_measurements(&snapshot);
    return app_protective_sleep_poll(seconds, &snapshot, sample_valid, status);
}
static app_sleep_status_t sleep_status;
static void reset(void) {
    memset(&s_protective_sleep,0,sizeof(s_protective_sleep));s_protective_sleep_committed=0u;
 sleep_config=(bms_sleep_config_t){2800,60,3000,1440,30};
 g_bms_report.fault_third.bits.cell_uvp=g_bms_report.fault_third.bits.pack_uvp=0;
 g_bms_report.pack_voltage_10mv=3300;
    s_command_seen=0; retained_analog=wakeup_status=0;
    deepsleep_en=0; tx_busy=ble_pending=uart_ready=uart_entered=0;
    now=measurement.sample_time_ms=100u; measurement.current_ma=0;
    valid=flash_ready=1; afe_error=ota_is_working=device_in_connection_state=0;
    saves=events=afe_calls=output_calls=sleep_calls=pad_calls=comm_sleep_calls=0;
    g_bms_report.cell_min_mv=3300u; reason=0u;
    pins[0]=0; pins[1]=1; pins[2]=pins[3]=0; /* 全部原有效电平，包括 ON/ALARM/RESET */
    memset(enabled,0,sizeof(enabled));
}
int main(void) {
    reset();sleep_config.afe_delay_minutes=1u;valid=0;afe_error=1;
    assert(!fixture_sleep_poll(1799u,&sleep_status));assert(fixture_sleep_poll(1u,&sleep_status));
    assert(reason==DIAG_SLEEP_REASON_AFE); /* 旧配置槽不能改变固定30分钟。 */

    reset();sleep_config.low_delay_minutes=10u;g_bms_report.cell_min_mv=2600u;
    g_bms_report.fault_third.bits.cell_uvp=1u;
    assert(!fixture_sleep_poll(179u,&sleep_status));
    /* AFE 重配清了故障位，但有效电压尚未恢复；固定计时必须保持。 */
    g_bms_report.fault_third.bits.cell_uvp=0u;
    assert(fixture_sleep_poll(1u,&sleep_status));assert(reason==DIAG_SLEEP_REASON_OVERDISCHARGE);
    reset();g_bms_report.fault_third.bits.pack_uvp=1u;
    assert(!fixture_sleep_poll(179u,&sleep_status));
    valid=0;g_bms_report.fault_third.bits.pack_uvp=0u;
    assert(fixture_sleep_poll(1u,&sleep_status));assert(reason==DIAG_SLEEP_REASON_OVERDISCHARGE);
    reset();g_bms_report.fault_third.bits.cell_uvp=1u;
    assert(!fixture_sleep_poll(179u,&sleep_status));
    g_bms_report.fault_third.bits.cell_uvp=0u;
    assert(!fixture_sleep_poll(1u,&sleep_status));assert(!s_protective_sleep.overdischarge_seconds);
    reset();sleep_config.low_delay_minutes=1u;g_bms_report.cell_min_mv=2500u;
    assert(!fixture_sleep_poll(59u,&sleep_status));assert(fixture_sleep_poll(1u,&sleep_status));
    assert(reason==DIAG_SLEEP_REASON_LOW); /* 删除2550分段，2500也使用配置的一分钟。 */

    reset(); g_bms_report.cell_min_mv=2600; device_in_connection_state=1;
    assert(!fixture_sleep_poll(1800, &sleep_status));
    g_bms_report.cell_min_mv=2500;
    assert(fixture_sleep_poll(1800, &sleep_status)); /* 2500 mV 与 2600 mV 同属下档，保留累计时间 */
    assert(sleep_calls==1&&saves==1&&events==1&&afe_calls==1&&output_calls==1&&comm_sleep_calls==1);
    assert(s_protective_sleep_committed&&s_low_power_mode);
    pins[1]=0; assert(app_power_prepare_loop());
    assert(sleep_calls==2&&saves==1&&events==1&&afe_calls==1&&output_calls==1&&comm_sleep_calls==1);
    reset(); ota_is_working=1; flash_ready=0; valid=0; afe_error=1;
    assert(!fixture_sleep_poll(1799, &sleep_status)); assert(fixture_sleep_poll(1, &sleep_status));
    assert(reason==DIAG_SLEEP_REASON_AFE&&sleep_calls==1&&!saves&&!events);
    reset(); g_bms_report.cell_min_mv=2400; afe_error=1;
    assert(!fixture_sleep_poll(1799, &sleep_status)); assert(fixture_sleep_poll(1, &sleep_status));
    assert(reason==DIAG_SLEEP_REASON_AFE); /* 低压不能遮住 AFE 异常 */
    reset(); valid=0; assert(!fixture_sleep_poll(900, &sleep_status));
    valid=1; assert(!fixture_sleep_poll(1, &sleep_status)); assert(!s_protective_sleep.afe_error_seconds);
    now+=BMS_SAMPLE_MAX_POLL_GAP_MS+1; assert(!fixture_sleep_poll(1800, &sleep_status));
    afe_error=1; assert(fixture_sleep_poll(1800, &sleep_status));
    reset(); g_bms_report.cell_min_mv=2900; measurement.current_ma=-201;
    assert(!fixture_sleep_poll(86400, &sleep_status));
    measurement.current_ma=0; assert(!fixture_sleep_poll(86399, &sleep_status));
    assert(fixture_sleep_poll(1, &sleep_status));
    reset(); g_bms_report.cell_min_mv=2700;
    assert(!fixture_sleep_poll(3599, &sleep_status));
    now+=401u; assert(!fixture_sleep_poll(0, &sleep_status));
    assert(s_protective_sleep.low_voltage_seconds==3599u);
    assert(fixture_sleep_poll(1, &sleep_status)); /* 采样等待不能推迟已确认低压的一小时到期。 */
    reset(); g_bms_report.cell_min_mv=2900; measurement.current_ma=-90;
    assert(fixture_sleep_poll(86400, &sleep_status)); /* 可靠电流区外的偏移不冒充充电。 */
    reset(); g_bms_report.cell_min_mv=2700; now=measurement.sample_time_ms=0;
    app_pm_elapsed_ctx_t wall_clock={0};
    assert(!fixture_sleep_poll(app_pm_take_elapsed_seconds(&wall_clock), &sleep_status));
    for(u32 seconds=1;seconds<=3600u;++seconds) {
        now=seconds*1000u; measurement.sample_time_ms=now;
        assert(fixture_sleep_poll(app_pm_take_elapsed_seconds(&wall_clock), &sleep_status)==(seconds==3600u));
        assert(s_protective_sleep.low_voltage_seconds==seconds);
        if(seconds==3600u) break;
        now+=401u;
        assert(!fixture_sleep_poll(app_pm_take_elapsed_seconds(&wall_clock), &sleep_status));
        assert(s_protective_sleep.low_voltage_seconds==seconds);
    }
    /* 始终低于3000mV，跨2800mV不得丢掉24小时资格；低压一小时仍需连续。 */
    reset();
    for (unsigned step=1;step<=48u;++step) {
        g_bms_report.cell_min_mv=(step&1u)?2799u:2801u;
        assert(fixture_sleep_poll(1800u, &sleep_status)==(step==48u));
    }
    assert(reason==DIAG_SLEEP_REASON_NORMAL);
    reset(); app_pm_elapsed_ctx_t ctx={UINT32_MAX-999u,1u}; now=0u;
    assert(app_pm_take_elapsed_seconds(&ctx)==1u&&ctx.epoch_ms==0u);
    ctx.epoch_ms=1u;now=0u;
    assert(app_pm_take_elapsed_seconds(&ctx)==4294967u&&(u32)(now-ctx.epoch_ms)==295u);
    /* 宏启用：OFF本次调用立即提交；关闭：仅移除开关触发。 */
    reset(); pins[0]=1; tx_busy=ble_pending=ota_is_working=device_in_connection_state=1;
    flash_ready=0;
#if BMS_PRODUCT_SWITCH_SLEEP_ENABLE
    assert(app_sh_explicit_sleep_poll()&&sleep_calls==1&&!saves&&afe_calls==1&&comm_sleep_calls==1);
    assert(reason==DIAG_SLEEP_REASON_SWITCH);
    assert(app_power_before_suspend());
    assert(app_power_prepare_loop()&&sleep_calls==2&&afe_calls==1);
#else
    assert(!app_sh_explicit_sleep_poll()&&!sleep_calls&&!afe_calls);
    /* 禁用开关休眠不影响命令，OFF时命令仍立即提交。 */
    deepsleep_en=1; tx_busy=device_in_connection_state=0;
    assert(app_sh_explicit_sleep_poll()&&reason==DIAG_SLEEP_REASON_COMMAND);
#endif
    reset(); assert(!app_sh_explicit_sleep_poll()); /* ON不触发开关休眠。 */
    /* 命令空闲立即睡；通信卡死时最多等500ms应答窗口。 */
    reset(); deepsleep_en=1; assert(app_sh_explicit_sleep_poll());
    assert(reason==DIAG_SLEEP_REASON_COMMAND);
    reset(); deepsleep_en=1; tx_busy=ble_pending=device_in_connection_state=1;
    ota_is_working=1; flash_ready=0;
    assert(!app_sh_explicit_sleep_poll()); now+=499;
    assert(!app_sh_explicit_sleep_poll()); now++;
    assert(app_sh_explicit_sleep_poll()&&sleep_calls==1&&!saves&&comm_sleep_calls==1);
    /* 命令ON入睡，OFF只在业务初始化前重新深睡，ON才放行正常启动。 */
    reset(); deepsleep_en=1; assert(app_sh_explicit_sleep_poll());
    assert((retained_analog&APP_SH_SWITCH_WAKE_MASK)==APP_SH_SWITCH_WAKE_TAG);
    u32 saved_saves=saves, saved_afe=afe_calls;
    wakeup_status=WAKEUP_STATUS_PAD; pins[0]=1;
    assert(app_power_boot_sleep_hold()&&sleep_calls==2&&levels[0]==Level_Low);
    assert(saves==saved_saves&&afe_calls==saved_afe);
    assert(app_power_boot_sleep_hold()&&sleep_calls==3); /* SDK拒睡仅重试。 */
    pins[0]=0; assert(!app_power_boot_sleep_hold()&&!retained_analog);
    /* 独立AFE/INT变化不被OFF过滤；上电/其他复位不采用旧标记。 */
    reset(); deepsleep_en=1; assert(app_sh_explicit_sleep_poll());
    wakeup_status=WAKEUP_STATUS_PAD; pins[0]=1; pins[1]=0;
    assert(!app_power_boot_sleep_hold()&&!retained_analog);
    reset(); deepsleep_en=1; assert(app_sh_explicit_sleep_poll());
    pins[0]=1; wakeup_status=0;
    assert(!app_power_boot_sleep_hold()&&!retained_analog);
    reset(); pins[0]=1; app_enter_protective_sleep();
    assert(!retained_analog&&enabled[0]&&levels[0]==Level_Low);
    /* SDK临睡检查再次检查UART资格；普通开关ON不阻止Suspend。 */
    reset(); s_low_power_mode=1; assert(!app_power_before_suspend());
    uart_ready=1; assert(app_power_before_suspend()&&uart_entered==1);
    assert(!comm_sleep_calls); /* 普通 Suspend 保留 UART 唤醒，不释放通信域 GPIO。 */
    for(int pin=0;pin<4;pin++) assert(enabled[pin]&&levels[pin]!=pins[pin]);
    s_low_power_mode=0; assert(!app_power_before_suspend());
    puts("PASS protective sleep: failure overrides, independent timers, PAD retry hold and tick wrap");
    return 0;
}
