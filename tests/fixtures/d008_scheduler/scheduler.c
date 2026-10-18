#define BMS_SERVICE_DIAG_ENABLE 1
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef enum { BMS_AFE_SAMPLE_OK, BMS_AFE_SAMPLE_NOT_READY, BMS_AFE_SAMPLE_ERROR } bms_afe_sample_status_t;
typedef uint32_t u32;
typedef uint8_t u8;
#define _attribute_data_retention_
#define _attribute_no_inline_
#define BMS_PRODUCT_UART_ENABLE
#define APP_SAMPLE_PERIOD_US 200000u
#define SYSTEM_TIMER_TICK_1US 16u
#define SUSPEND_MODE 0
#define PM_WAKEUP_TIMER 0
#define LED_BLUE_PIN 0
static u32 now, now_ms, s_sample_tick, sample_cost;
static u32 bms_time_ms(void){return now_ms;}
static volatile u8 s_sample_due;
static u8 s_acc_sleep_committed, valid = 1, callback_during_sample;
static u8 s_protective_sleep_committed;
static void app_protective_sleep_hold(void) {}
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
static bms_afe_sample_status_t bms_afe_sample(void){bms_afe_sample_status_t r=BMS_AFE_SAMPLE_OK;note('A');now+=sample_cost;if(callback_during_sample)s_sample_due=1;return r;}
static void bms_features_service(void){note('T');}
static void bms_afe_apply_outputs(void){note('M');}
static u8 bms_afe_get_aux_measurements(bms_afe_aux_measurements_t *m){note('V');m->raw_current_ma=-710;m->current_ma=-700;m->sample_tick_32k=99;return valid;}
static void app_diag_poll_runtime(u8 v,int raw,int current,u32 tick){assert(v==valid);assert(raw==(v?-710:0));assert(current==(v?-700:0));assert(tick==(v?99u:77u));}
static void app_update_soc_from_sample(u8 v,int ma){note('S');assert(v==valid);assert(ma==(v?-700:0));}
static void app_schedule_sample_wakeup(void){note('W');}
static void gpio_toggle(int pin){(void)pin;note('L');}
static void app_acc_sleep_hold(void){note('H');}
static void bms_parameters_diag_poll(void){note('1');}
static void bms_storage_platform_diag_poll(void){note('2');}
static void bms_afe_diag_poll(void){note('3');}
static void blt_sdk_main_loop(void){note('B');}
static void bus_mux_task(void){note('X');}
static void sif_prepare_task(u32 tick){(void)tick;}
static void main_loop_modbus(void){note('U');}
static void app_power_process(const volatile uint8_t *pending){assert(pending==&s_sample_due);note('P');}
static void bms_state_store_poll(int a,int b,int c){(void)a;(void)b;(void)c;note('F');}
static struct {int soc_estimate_percent,discharge_fraction_percent,cycle_count;} g_bms_soc;
/* PRODUCTION_SOURCE */
static void expect(const char *s){assert(!strcmp(calls,s));n=0;calls[0]=0;}
int main(void){
 main_loop();expect("123BXUP");
 now=3200000;main_loop();expect("123BXUP"); /* SDK uses strict >. */
 now++;main_loop();expect("123BATMVSWLXUP");assert(s_sample_tick==now&&!s_sample_due);
 s_sample_due=1;main_loop();expect("123BATMVSWLXUP");
 valid=0;s_sample_due=1;main_loop();expect("123BATMVSWLXUP");valid=1;
 now=16000001;now_ms=1001u;main_loop();expect("123BATMVSWLEFXUP");
 main_loop();expect("123BXUP"); /* no repeated event or sample */
 now=0x100; s_sample_tick=0xffc00000u;app_sample_task();expect("ATMVSWL");
 sample_cost=3200001;s_sample_due=1;app_sample_task();expect("ATMVSWL");assert(s_sample_due);
 sample_cost=0;s_sample_due=1;callback_during_sample=1;app_sample_task();expect("ATMVSWL");assert(s_sample_due);
 callback_during_sample=0;
 s_acc_sleep_committed=1;main_loop();expect("H");
 s_acc_sleep_committed=0;s_protective_sleep_committed=1;main_loop();expect("");
 return 0;
}
