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
#define APP_PM_TICKS_PER_SEC 32000u
#define BMS_SOC_MAX_SAMPLE_GAP_32K 12800u
#define BMS_SAMPLE_MAX_POLL_GAP_32K (400u * 32u)
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
#define Level_Low 0
#define Level_High 1
#define DEEPSLEEP_MODE 0x80
#define PM_WAKEUP_PAD 16
#define HCI_ERR_REMOTE_USER_TERM_CONN 19
#define BLC_ADV_DISABLE 0
#define SUSPEND_DISABLE 0
typedef struct { u32 last_tick_32k, pending_tick_32k; u8 ready; } app_pm_elapsed_ctx_t;
typedef struct { u32 sample_tick_32k; int32_t current_ma; } bms_afe_aux_measurements_t;
static struct { uint16_t cell_min_mv; } g_bms_report;
static struct { u8 soc_estimate_percent, discharge_fraction_percent; u32 cycle_count; } g_bms_soc;
static bms_afe_aux_measurements_t measurement;
static int valid, afe_error, ota_is_working, device_in_connection_state, flash_ready;
static bool s_low_power_mode;
static u32 now, saves, events, afe_calls, output_calls, sleep_calls, pad_calls;
static u8 reason;
static int pins[5], levels[5], enabled[5];
static u32 pm_get_32k_tick(void) { return now; }
static u8 bms_afe_get_aux_measurements(bms_afe_aux_measurements_t *m) { *m=measurement; return valid; }
static u8 bms_error_get(int error) { assert(error==BMS_ERROR_AFE1); return afe_error; }
static void bms_afe_set_output_enabled(u8 en) { assert(!en); ++output_calls; }
static u8 bms_afe_sleep(void) { ++afe_calls; return 0u; }
static int app_flash_lock_restore_enabled(void) { return flash_ready; }
static u8 bms_state_store_write_all(u8 s, u8 d, u32 c) { (void)s;(void)d;(void)c; ++saves; return 0u; }
static u8 bms_event_log_note_sleep(void) { ++events; return 0u; }
static int bls_ll_terminateConnection(int r) { assert(r==HCI_ERR_REMOTE_USER_TERM_CONN); return -1; }
static int bls_ll_setAdvEnable(int en) { assert(!en); return -1; }
static void bls_pm_setSuspendMask(int mask) { assert(!mask); }
static void bls_pm_setAppWakeupLowPower(u32 t, u8 en) { assert(!t&&!en); }
static int gpio_read(int pin) { assert(pin>=0&&pin<4); return pins[pin]; }
static void gpio_write(int pin, int en) { assert(pin==BMS_BOARD_CMNT_EN_PIN&&!en); }
static void cpu_set_gpio_wakeup(int pin, int level, int en) {
    assert(pin>=0&&pin<4); levels[pin]=level; enabled[pin]=en; ++pad_calls;
}
static int cpu_sleep_wakeup(int mode, int source, u32 tick) {
    assert(mode==DEEPSLEEP_MODE&&source==PM_WAKEUP_PAD&&!tick);
    for (int pin=0;pin<4;++pin) if(enabled[pin]) assert(levels[pin]!=pins[pin]);
    ++sleep_calls; return 0x100; /* SDK PAD 竞争拒睡 */
}
void bms_diag_sleep(u8 r,u32 b,u32 e,u32 d,u32 retry,u8 allowed) {
    (void)e;(void)d;(void)allowed; assert(!b&&!retry); reason=r;
}
void bms_diag_sleep_committed(void) {}
/* PRODUCTION */
static void reset(void) {
    memset(&s_protective_sleep,0,sizeof(s_protective_sleep));
    now=measurement.sample_tick_32k=100u; measurement.current_ma=0;
    valid=flash_ready=1; afe_error=ota_is_working=device_in_connection_state=0;
    saves=events=afe_calls=output_calls=sleep_calls=pad_calls=0;
    g_bms_report.cell_min_mv=3300u; reason=0u;
    pins[0]=0; pins[1]=1; pins[2]=pins[3]=0; /* 全部原有效电平，包括 ON/ALARM/RESET */
    memset(enabled,0,sizeof(enabled));
}
int main(void) {
    reset(); g_bms_report.cell_min_mv=2600; device_in_connection_state=1;
    assert(!app_protective_sleep_poll(1800));
    g_bms_report.cell_min_mv=2500;
    assert(app_protective_sleep_poll(1800)); /* 跨极低压分界仍累计一小时 */
    assert(sleep_calls==1&&saves==1&&events==1&&afe_calls==1&&output_calls==1);
    assert(s_protective_sleep.committed&&s_low_power_mode);
    pins[1]=0; assert(app_power_prepare_loop());
    assert(sleep_calls==2&&saves==1&&events==1&&afe_calls==1&&output_calls==1);
    reset(); ota_is_working=1; flash_ready=0; valid=0;
    assert(!app_protective_sleep_poll(1799)); assert(app_protective_sleep_poll(1));
    assert(reason==DIAG_SLEEP_REASON_AFE&&sleep_calls==1&&!saves&&!events);
    reset(); g_bms_report.cell_min_mv=2400; afe_error=1;
    assert(!app_protective_sleep_poll(1799)); assert(app_protective_sleep_poll(1));
    assert(reason==DIAG_SLEEP_REASON_AFE); /* 低压不能遮住 AFE 异常 */
    reset(); valid=0; assert(!app_protective_sleep_poll(900));
    valid=1; assert(!app_protective_sleep_poll(1)); assert(!s_protective_sleep.afe_error_seconds);
    now+=BMS_SOC_MAX_SAMPLE_GAP_32K+1; assert(app_protective_sleep_poll(1800));
    reset(); g_bms_report.cell_min_mv=2900; measurement.current_ma=-201;
    assert(!app_protective_sleep_poll(86400));
    measurement.current_ma=0; assert(!app_protective_sleep_poll(86399));
    assert(app_protective_sleep_poll(1));
    reset(); g_bms_report.cell_min_mv=2700;
    assert(!app_protective_sleep_poll(3599));
    now+=12801u; assert(!app_protective_sleep_poll(0));
    assert(s_protective_sleep.low_voltage_seconds==3599u);
    assert(app_protective_sleep_poll(1)); /* 采样等待不能推迟已确认低压的一小时到期。 */
    reset(); g_bms_report.cell_min_mv=2900; measurement.current_ma=-90;
    assert(app_protective_sleep_poll(86400)); /* 可靠电流区外的偏移不冒充充电。 */
    reset(); g_bms_report.cell_min_mv=2700; now=measurement.sample_tick_32k=0;
    app_pm_elapsed_ctx_t wall_clock={0};
    assert(!app_protective_sleep_poll(app_pm_take_elapsed_seconds(&wall_clock)));
    for(u32 seconds=1;seconds<=3600u;++seconds) {
        now=seconds*32000u; measurement.sample_tick_32k=now;
        assert(app_protective_sleep_poll(app_pm_take_elapsed_seconds(&wall_clock))==(seconds==3600u));
        assert(s_protective_sleep.low_voltage_seconds==seconds);
        if(seconds==3600u) break;
        now+=12801u;
        assert(!app_protective_sleep_poll(app_pm_take_elapsed_seconds(&wall_clock)));
        assert(s_protective_sleep.low_voltage_seconds==seconds);
    }
    reset(); app_pm_elapsed_ctx_t ctx={UINT32_MAX-15999u,16000u,1u}; now=0u;
    assert(app_pm_take_elapsed_seconds(&ctx)==1u&&!ctx.pending_tick_32k);
    ctx.last_tick_32k=1u;ctx.pending_tick_32k=31999u;now=0u;
    assert(app_pm_take_elapsed_seconds(&ctx)==134218u&&ctx.pending_tick_32k==23294u);
    puts("PASS protective sleep: failure overrides, independent timers, PAD retry hold and tick wrap");
    return 0;
}
