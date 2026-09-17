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
typedef struct {int current_ma;u32 sample_tick_32k;} bms_afe_aux_measurements_t;
static void bms_afe_sample(void){note('A');now+=sample_cost;if(callback_during_sample)s_sample_due=1;}
static u8 app_get_fresh_measurements(bms_afe_aux_measurements_t *m){note('V');m->current_ma=-700;m->sample_tick_32k=99;return valid;}
static void APP_SOC_IntEnhance_Ctrl(u8 v,int ma,u32 tick){note('S');assert(v==valid);assert(ma==(v?-700:0));assert(tick==(v?99u:77u));}
static void mos_update(void){note('M');}
static void app_schedule_sample_wakeup(void){note('W');}
static void gpio_toggle(int pin){(void)pin;note('L');}
static void app_acc_sleep_hold(void){note('H');}
static void bms_param_diag_poll(void){note('1');}
static void bms_storage_platform_diag_poll(void){note('2');}
static void bms_afe_diag_poll(void){note('3');}
static void cpu_sleep_wakeup(int m,int w,u32 t){(void)m;(void)w;assert(t==now+3200000u);note('Q');}
static void blt_sdk_main_loop(void){note('B');}
static void Runtime_Poll(void){note('R');}
static void bus_mux_task(void){note('X');}
static void main_loop_modbus(void){note('U');}
static void soc_kv_store_update_and_log_if_changed(int a,int b,int c){(void)a;(void)b;(void)c;note('F');}
static void blt_pm_proc(void){note('P');}
static struct {int u8SOC_Now,u8DSG_SOC_Int,u32Cycle_times;} SOC_Calculate_Element;
/* PRODUCTION_SOURCE */
static void expect(const char *s){assert(!strcmp(calls,s));n=0;calls[0]=0;}
int main(void){
 main_loop();expect("123BRXUFP");
 now=3200000;main_loop();expect("123BRXUFP"); /* SDK uses strict >. */
 now++;main_loop();expect("123BRAVSMWLXUFP");assert(s_sample_tick==now&&!s_sample_due);
 s_sample_due=1;main_loop();expect("123BRAVSMWLXUFP");
 valid=0;s_sample_due=1;main_loop();expect("123BRAVSMWLXUFP");valid=1;
 now=16000001;main_loop();expect("123BRAVSMWLEXUFP");
 main_loop();expect("123BRXUFP"); /* no repeated event or sample */
 now=0x100; s_sample_tick=0xffc00000u;app_sample_task();expect("AVSMWL");
 sample_cost=3200001;s_sample_due=1;app_sample_task();expect("AVSMWL");assert(s_sample_due);
 sample_cost=0;s_sample_due=1;callback_during_sample=1;app_sample_task();expect("AVSMWL");assert(s_sample_due);
 callback_during_sample=0;
 s_acc_sleep_committed=1;main_loop();expect("H");
 s_acc_sleep_committed=0;s_power_off_committed=1;main_loop();expect("123Q");
 return 0;
}
