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
#define ACC_MCU_PIN 0
#define CHG_IN_PIN 1
#define Level_Low 0
#define DEEPSLEEP_MODE 0x80
#define PM_WAKEUP_PAD 16
#define BLE_SUCCESS 0
#define BLC_ADV_ENABLE 1
#define BLC_ADV_DISABLE 0
#define HCI_ERR_REMOTE_USER_TERM_CONN 19
#define APP_ACC_HIGH_STABLE_TICKS (APP_PM_TICKS_PER_SEC / 5u)
static u8 s_acc_high_seen,s_acc_sleep_committed,s_acc_retry_ready,s_acc_disconnect_sent;
static u32 s_acc_high_tick,s_acc_retry_tick;
static int acc_high,ldo_high,acc_wake,deep_calls,reboot_calls,disconnect_calls,adv_enabled=1;
static int acc_low_during_shutdown,acc_low_during_sleep;
static int gpio_read(int pin){assert(pin==ACC_MCU_PIN);return acc_high;}
static void start_reboot(void){reboot_calls++;}
static void cpu_set_gpio_wakeup(int pin,int level,int en){assert(level==Level_Low);if(pin==ACC_MCU_PIN)acc_wake=en;else assert(pin==CHG_IN_PIN&&!en);}
static int cpu_sleep_wakeup(int mode,int src,u32 tick){assert(mode==DEEPSLEEP_MODE&&src==PM_WAKEUP_PAD&&tick==0&&acc_wake&&ldo_high);deep_calls++;if(acc_low_during_sleep)acc_high=0;return 0;}
static int bls_ll_terminateConnection(int reason){assert(reason==HCI_ERR_REMOTE_USER_TERM_CONN);disconnect_calls++;return BLE_SUCCESS;}
static int bls_ll_setAdvEnable(int en){adv_enabled=en;return BLE_SUCCESS;}
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
static int storage_ok=1,event_ok=1,shutdown_ok=1,cut_calls,seq[64],seq_len;
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
static int bms_afe_enter_shutdown(void){seq[seq_len++]=3;if(acc_low_during_shutdown)acc_high=0;return shutdown_ok;}
static void bls_pm_setAppWakeupLowPower(u32 t,int en){assert(en==0);seq[seq_len++]=4;}
static void gpio_write(int pin,int level){assert(pin==MCU_LDO_PIN);if(level)ldo_high=1;else cut_calls++;seq[seq_len++]=5;}
static u32 app_pm_take_elapsed_seconds(app_pm_elapsed_ctx_t*c){return elapsed;}
static void bls_pm_setSuspendMask(int m){mask=m;}
static void bls_pm_setManualLatency(int n){assert(n==0);}
/* PRODUCTION_SOURCE */
static void reset(void){
 acc_high=acc_wake=deep_calls=reboot_calls=disconnect_calls=0;ldo_high=adv_enabled=1;
 acc_low_during_shutdown=acc_low_during_sleep=0;
 s_acc_high_seen=s_acc_sleep_committed=s_acc_retry_ready=s_acc_disconnect_sent=0;
 deepsleep_en=false;ble_tx_pending=0;
 s_power_off_committed=s_power_off_retry_ready=s_sample_due=0;
 storage_ok=event_ok=shutdown_ok=valid=flash_ready=1;
 ota_is_working=device_in_connection_state=bus_busy=seq_len=cut_calls=0;
 now=measurement.sample_tick_32k=100;measurement.current_ma=0;elapsed=0;
 g_stCellInfoReport.u16VCellMin=3300;blt_pm_proc();
}
static void test_acc_sleep(void){
 reset();acc_high=1;blt_pm_proc();assert(!deep_calls);
 now+=APP_ACC_HIGH_STABLE_TICKS-1;blt_pm_proc();assert(!deep_calls);
 acc_high=0;blt_pm_proc();acc_high=1;blt_pm_proc();assert(!deep_calls);
 now+=APP_ACC_HIGH_STABLE_TICKS;blt_pm_proc();
 assert(s_acc_sleep_committed&&deep_calls==1&&!cut_calls&&ldo_high&&!adv_enabled);
 assert(seq[0]==1&&seq[1]==2&&seq[2]==3&&seq[3]==4);
 int saved=seq_len;app_acc_sleep_hold();assert(seq_len==saved&&deep_calls==2);
 acc_high=0;app_acc_sleep_hold();assert(reboot_calls==1&&seq_len==saved);
 reset();acc_high=1;ota_is_working=1;assert(!app_enter_acc_sleep()&&!seq_len);
 ota_is_working=0;flash_ready=0;assert(!app_enter_acc_sleep()&&!seq_len);
 flash_ready=1;bus_busy=1;assert(!app_enter_acc_sleep()&&!seq_len);
 bus_busy=0;device_in_connection_state=1;ble_tx_pending=1;
 assert(!app_enter_acc_sleep()&&!disconnect_calls);
 ble_tx_pending=0;assert(!app_enter_acc_sleep()&&disconnect_calls==1);
 assert(!app_enter_acc_sleep()&&disconnect_calls==1&&!seq_len);
 device_in_connection_state=0;assert(app_enter_acc_sleep()&&deep_calls==1&&!cut_calls);
 reset();acc_high=1;storage_ok=0;assert(!app_enter_acc_sleep()&&!deep_calls);
 saved=seq_len;assert(!app_enter_acc_sleep()&&seq_len==saved);
 now+=APP_POWER_OFF_RETRY_SECONDS*APP_PM_TICKS_PER_SEC;storage_ok=1;shutdown_ok=0;
 assert(!app_enter_acc_sleep()&&!s_acc_sleep_committed&&adv_enabled&&!cut_calls);
 now+=APP_POWER_OFF_RETRY_SECONDS*APP_PM_TICKS_PER_SEC;shutdown_ok=1;
 assert(app_enter_acc_sleep()&&deep_calls==1);
 reset();acc_high=1;event_ok=0;assert(!app_enter_acc_sleep()&&!deep_calls&&seq_len==2);
 reset();acc_high=1;acc_low_during_shutdown=1;
 assert(app_enter_acc_sleep()&&reboot_calls==1&&!deep_calls&&!cut_calls);
 reset();acc_high=1;acc_low_during_sleep=1;
 assert(app_enter_acc_sleep()&&reboot_calls==1&&deep_calls==1&&!cut_calls);
 reset();now=UINT32_MAX-100;acc_high=1;assert(!app_acc_sleep_requested());
 now+=APP_ACC_HIGH_STABLE_TICKS;assert(app_acc_sleep_requested());
 reset();acc_high=1;deepsleep_en=true;blt_pm_proc();assert(cut_calls==1&&!deep_calls);
 puts("PASS ACC: debounce/cancel/wrap, keep LDO high, PAD low wake, persistence/OTA/bus/BLE deferral, failures/retry, low race reboot, command priority");
}
int main(void){
 test_acc_sleep();
 reset();int currents[]={-501,-500,-499,0,499,500,501};
 for(unsigned i=0;i<sizeof(currents)/sizeof(currents[0]);i++){
  measurement.current_ma=currents[i];blt_pm_proc();
  int active=currents[i]>=500||currents[i]<=-500;
  assert(mask==(active?SUSPEND_DISABLE:SUSPEND_ADV|SUSPEND_CONN));
  assert(sys_time.low_power_mode==!active);
 }
 /* A live BLE connection is not itself an active-mode request. */
 reset();device_in_connection_state=1;blt_pm_proc();
 assert(mask==(SUSPEND_ADV|SUSPEND_CONN)&&sys_time.low_power_mode);
 elapsed=7200;g_stCellInfoReport.u16VCellMin=2400;blt_pm_proc();assert(!cut_calls);
 ota_is_working=1;blt_pm_proc();assert(mask==SUSPEND_DISABLE);
 ota_is_working=0;bus_busy=1;blt_pm_proc();assert(mask==SUSPEND_DISABLE);
 bus_busy=0;flash_ready=0;blt_pm_proc();assert(mask==SUSPEND_DISABLE);
 flash_ready=1;s_sample_due=1;blt_pm_proc();assert(mask==SUSPEND_DISABLE);
 s_sample_due=0;measurement.current_ma=500;blt_pm_proc();assert(mask==SUSPEND_DISABLE);
 measurement.current_ma=-500;blt_pm_proc();assert(mask==SUSPEND_DISABLE);
 measurement.current_ma=0;valid=0;blt_pm_proc();assert(mask==SUSPEND_DISABLE);
 puts("PASS connected suspend: link retained; OTA/bus/Flash/sample/current/invalid gates and no automatic power-off");
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
