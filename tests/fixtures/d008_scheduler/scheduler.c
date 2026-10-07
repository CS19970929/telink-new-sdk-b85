#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uint32_t u32;
typedef uint8_t u8;
#define _attribute_data_retention_
#define _attribute_no_inline_
#define _FUNC_UART_
#define APP_SAMPLE_PERIOD_US 200000u
#define SYSTEM_TIMER_TICK_1US 16u
#define SUSPEND_MODE 0
#define PM_WAKEUP_TIMER 0
#define LED_BLUE_PIN 0
static u32 now, s_sample_tick, sample_cost;
static volatile u8 s_sample_due;
static u8 s_acc_sleep_committed, s_power_off_committed, valid = 1, callback_during_sample;
static char calls[100];
static unsigned n;
static void note(char c){calls[n++]=c;calls[n]=0;}
static u32 clock_time(void){return now;}
static int clock_time_exceed(u32 start,u32 us){return (u32)(now-start)>us*16u;}
static u32 pm_get_32k_tick(void){return 77;}
typedef struct {int raw_current_ma;int current_ma;u32 sample_tick_32k;} bms_afe_aux_measurements_t;
typedef struct {
    uint8_t chemistry;
    uint8_t profile_id;
    uint16_t profile_version;
    uint8_t soc_estimate;
    uint8_t soc_display;
    uint16_t current_deadband_ma;
    uint8_t ocv_state;
    uint8_t ocv_center;
    uint8_t ocv_low;
    uint8_t ocv_high;
    uint8_t ocv_confidence;
    uint16_t ocv_cell_mv;
    uint16_t rest_seconds;
    uint16_t nominal_capacity_0p1ah;
    uint16_t effective_capacity_0p1ah;
    uint16_t remaining_capacity_0p1ah;
    uint8_t endpoint_state;
    uint8_t endpoint_event_flags;
    int32_t filtered_current_ma;
    uint16_t current_variation_ma;
    uint16_t time_to_empty_min;
    uint16_t time_to_full_min;
    uint8_t eta_state;
    uint8_t eta_direction;
    uint8_t eta_confidence;
    uint8_t eta_valid;
    uint8_t soh;
    uint8_t soh_source;
    uint8_t soh_confidence;
    uint8_t last_sample_state;
    uint8_t last_integral_direction;
    uint8_t last_soc_action;
    uint8_t last_soc_before;
    uint8_t last_soc_after;
    uint8_t last_soc_target;
    uint8_t last_decision_detail;
    uint32_t last_sample_elapsed_32k;
    uint32_t last_integral_delta_as10;
} bms_soc_diag_t;
typedef union {uint16_t all;} diag_fault_t;
static struct {diag_fault_t unMdlFault_First,unMdlFault_Second,unMdlFault_Third;} g_bms_report;
static void bms_afe_sample(void){note('A');now+=sample_cost;if(callback_during_sample)s_sample_due=1;}
static u8 app_get_fresh_measurements(bms_afe_aux_measurements_t *m){note('V');m->raw_current_ma=-710;m->current_ma=-700;m->sample_tick_32k=99;return valid;}
static u8 bms_afe_current_recovery_pending(void){return 0;}
static void bms_soc_get_diag(bms_soc_diag_t *d){memset(d,0,sizeof(*d));d->soc_estimate=50;d->soc_display=50;d->current_deadband_ma=200;}
static void bms_diag_runtime_sample(u8 v,int raw,int current,u32 tick,u8 recovery){(void)v;(void)raw;(void)current;(void)tick;(void)recovery;}
static void bms_diag_runtime_soc(const bms_soc_diag_t *d){(void)d;}
static void bms_diag_runtime_faults(uint16_t a,uint16_t b,uint16_t c){(void)a;(void)b;(void)c;}
static void app_update_soc_from_sample(u8 v,int ma,u32 tick){note('S');assert(v==valid);assert(ma==(v?-700:0));assert(tick==(v?99u:77u));}
static void mos_update(void){note('M');}
static void app_schedule_sample_wakeup(void){note('W');}
static void gpio_toggle(int pin){(void)pin;note('L');}
static void app_acc_sleep_hold(void){note('H');}
static void bms_parameters_diag_poll(void){note('1');}
static void bms_storage_platform_diag_poll(void){note('2');}
static void bms_afe_diag_poll(void){note('3');}
static void cpu_sleep_wakeup(int m,int w,u32 t){(void)m;(void)w;assert(t==now+3200000u);note('Q');}
static void blt_sdk_main_loop(void){note('B');}
static void bms_diag_runtime_mode(u8 factory){(void)factory;}
static void bus_mux_task(void){note('X');}
static void sif_prepare_task(u32 tick){(void)tick;}
static void main_loop_modbus(void){note('U');}
static void bms_state_store_update_and_log_if_changed(int a,int b,int c){(void)a;(void)b;(void)c;note('F');}
static void blt_pm_proc(void){note('P');}
static struct {int u8SOC_Now,u8DSG_SOC_Int,u32Cycle_times;} g_bms_soc;
/* PRODUCTION_SOURCE */
static void expect(const char *s){assert(!strcmp(calls,s));n=0;calls[0]=0;}
int main(void){
 main_loop();expect("123BXUFP");
 now=3200000;main_loop();expect("123BXUFP"); /* SDK uses strict >. */
 now++;main_loop();expect("123BAVSMWLXUFP");assert(s_sample_tick==now&&!s_sample_due);
 s_sample_due=1;main_loop();expect("123BAVSMWLXUFP");
 valid=0;s_sample_due=1;main_loop();expect("123BAVSMWLXUFP");valid=1;
 now=16000001;main_loop();expect("123BAVSMWLEXUFP");
 main_loop();expect("123BXUFP"); /* no repeated event or sample */
 now=0x100; s_sample_tick=0xffc00000u;app_sample_task();expect("AVSMWL");
 sample_cost=3200001;s_sample_due=1;app_sample_task();expect("AVSMWL");assert(s_sample_due);
 sample_cost=0;s_sample_due=1;callback_during_sample=1;app_sample_task();expect("AVSMWL");assert(s_sample_due);
 callback_during_sample=0;
 s_acc_sleep_committed=1;main_loop();expect("H");
 s_acc_sleep_committed=0;s_power_off_committed=1;main_loop();expect("123Q");
 return 0;
}
