#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
typedef uint8_t u8;typedef uint32_t u32;
#define APP_PM_TICKS_PER_SEC 32000u
#define APP_SUSPEND_EXIT_CURRENT_MA 500
#define APP_POWER_OFF_RETRY_SECONDS 5u
#define BMS_SOC_MAX_SAMPLE_GAP_32K 12800u
#define MCU_LDO_PIN 4
#define BUS_STATE_OWC_IDLE 0
#define SUSPEND_ADV 1
#define SUSPEND_CONN 2
#define SUSPEND_DISABLE 0
#define __SLEEP_VLOW__ 2800
#define __SLEEP_VNORMAL__ 3000
#define __SLEEP_TIMEVLOW__ 10000u
#define __SLEEP_TIMENORMAL__ 10000u
typedef struct{uint32_t sample_tick_32k;int32_t current_ma;}bms_afe_aux_measurements_t;
typedef struct{int unused;}app_pm_elapsed_ctx_t;
static u8 s_power_off_committed,s_power_off_retry_ready,s_sample_due;
static u32 s_power_off_retry_tick,now,elapsed;
static int valid=1,flash_ready=1,ota_is_working,device_in_connection_state,bus_busy,mask;
static int storage_ok=1,event_ok=1,shutdown_ok=1,cut_calls,seq[8],seq_len;
static bool deepsleep_en;
static u8 ble_tx_pending;
static u8 blc_ll_getTxFifoNumber(void){return ble_tx_pending;}
static bms_afe_aux_measurements_t measurement;
static struct{bool low_power_mode;}sys_time;
static struct{uint16_t u16VCellMin;}g_stCellInfoReport={3300};
static struct{uint8_t u8SOC_Now,u8DSG_SOC_Int;uint32_t u32Cycle_times;}SOC_Calculate_Element;
static u32 pm_get_32k_tick(void){return now;}
static int bms_afe_get_aux_measurements(bms_afe_aux_measurements_t*m){*m=measurement;return valid;}
static int app_flash_lock_restore_enabled(void){return flash_ready;}
static int bus_mux_get_state(void){return bus_busy;}
static int soc_kv_store_write_all(int s,int d,uint32_t c){seq[seq_len++]=1;return storage_ok;}
static int bms_event_log_note_sleep(void){seq[seq_len++]=2;return event_ok;}
static int bms_afe_enter_shutdown(void){seq[seq_len++]=3;return shutdown_ok;}
static void bls_pm_setAppWakeupLowPower(u32 t,int en){assert(en==0);seq[seq_len++]=4;}
static void gpio_write(int pin,int level){assert(pin==MCU_LDO_PIN&&level==0);cut_calls++;seq[seq_len++]=5;}
static u32 app_pm_take_elapsed_seconds(app_pm_elapsed_ctx_t*c){return elapsed;}
static void bls_pm_setSuspendMask(int m){mask=m;}
static void bls_pm_setManualLatency(int n){assert(n==0);}
/* PRODUCTION_SOURCE */
static void reset(void){
 deepsleep_en=false;ble_tx_pending=0;
 s_power_off_committed=s_power_off_retry_ready=s_sample_due=0;
 storage_ok=event_ok=shutdown_ok=valid=flash_ready=1;
 ota_is_working=device_in_connection_state=bus_busy=seq_len=cut_calls=0;
 now=measurement.sample_tick_32k=100;measurement.current_ma=0;elapsed=0;
 g_stCellInfoReport.u16VCellMin=3300;blt_pm_proc();
}
int main(void){
 reset();int currents[]={-501,-500,-499,0,499,500,501};
 for(unsigned i=0;i<sizeof(currents)/sizeof(currents[0]);i++){
  measurement.current_ma=currents[i];blt_pm_proc();
  int active=currents[i]>=500||currents[i]<=-500;
  assert(mask==(active?SUSPEND_DISABLE:SUSPEND_ADV|SUSPEND_CONN));
  assert(sys_time.low_power_mode==!active);
 }
 reset();valid=0;blt_pm_proc();assert(mask==SUSPEND_DISABLE);assert(!app_enter_power_off());
 reset();now+=12801;blt_pm_proc();assert(mask==SUSPEND_DISABLE);assert(!app_enter_power_off());
 reset();ota_is_working=1;elapsed=3600;g_stCellInfoReport.u16VCellMin=2400;blt_pm_proc();assert(!cut_calls);assert(mask==0);
 reset();flash_ready=0;assert(!app_enter_power_off());assert(seq_len==0);
 reset();device_in_connection_state=1;assert(!app_enter_power_off());assert(seq_len==0);
 reset();bus_busy=1;assert(!app_enter_power_off());assert(seq_len==0);
 reset();storage_ok=0;assert(!app_enter_power_off());assert(seq_len==1&&!cut_calls);
 reset();event_ok=0;assert(!app_enter_power_off());assert(seq_len==2&&!cut_calls);
 reset();shutdown_ok=0;assert(!app_enter_power_off());assert(seq_len==3&&!cut_calls);
 assert(!app_enter_power_off());assert(seq_len==3); /* retry must not wear Flash every loop */
 now+=5*32000;measurement.sample_tick_32k=now;seq_len=0;shutdown_ok=1;
 assert(app_enter_power_off());assert(cut_calls==1&&seq_len==5);
 for(int i=0;i<5;i++)assert(seq[i]==i+1);
 assert(!app_enter_power_off());assert(cut_calls==1);
 reset();s_power_off_retry_ready=1;s_power_off_retry_tick=UINT32_MAX-32000;
 now=s_power_off_retry_tick+160000u;measurement.sample_tick_32k=now;
 assert(app_enter_power_off());assert(cut_calls==1);
 /* Explicit command works at normal voltage while connected and with stale
  * sampling. Its acknowledgement must drain; busy/failed work stays pending. */
 reset();deepsleep_en=true;device_in_connection_state=1;valid=0;ble_tx_pending=1;
 blt_pm_proc();assert(!cut_calls && seq_len==0 && deepsleep_en);
 ble_tx_pending=0;ota_is_working=1;blt_pm_proc();assert(!cut_calls);
 ota_is_working=0;bus_busy=1;blt_pm_proc();assert(!cut_calls);
 bus_busy=0;flash_ready=0;blt_pm_proc();assert(!cut_calls);
 flash_ready=1;storage_ok=0;blt_pm_proc();assert(seq_len==1 && !cut_calls);
 for(int i=0;i<10;i++)blt_pm_proc();assert(seq_len==1);
 now+=160000u;seq_len=0;storage_ok=1;shutdown_ok=0;
 blt_pm_proc();assert(seq_len==3 && !cut_calls && deepsleep_en);
 now+=160000u;seq_len=0;shutdown_ok=1;
 blt_pm_proc();assert(cut_calls==1 && seq_len==5 && s_power_off_committed);
 for(int i=0;i<5;i++)assert(seq[i]==i+1);
 puts("PASS commanded shutdown: BLE response drain, connected/stale samples, OTA/bus/Flash wait, retry, final PC4 cut");
 puts("PASS PM: +/-500 mA, invalid/stale, OTA/bus/Flash gates, persistence/AFE failures, ordered cut-off, retry/wrap");
}
