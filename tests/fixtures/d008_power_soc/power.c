#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
typedef uint8_t u8;typedef uint32_t u32;
typedef int GPIO_PinTypeDef;
#define BMS_AFE_BACKEND 1
#define BMS_AFE_BACKEND_DVC1124 1
#define APP_PM_TICKS_PER_SEC 32000u
/* CURRENT_FLOOR */
/* SUSPEND_CURRENT_LIMIT */
#define APP_POWER_OFF_RETRY_SECONDS 5u
#define BMS_SOC_MAX_SAMPLE_GAP_32K 12800u
#define BMS_SAMPLE_MAX_POLL_GAP_32K (400u * 32u)
#define BMS_BOARD_MCU_LDO_PIN 4
#define BMS_BOARD_ACC_PIN 0
#define BMS_BOARD_LOAD_DETECT_PIN 1
#define Level_Low 0
#define Level_High 1
#define DEEPSLEEP_MODE 0x80
#define SUSPEND_MODE 0
#define PM_WAKEUP_TIMER 2
#define PM_WAKEUP_PAD 16
#define APP_SAMPLE_PERIOD_US 200000u
#define SYSTEM_TIMER_TICK_1US 16u
#define BLE_SUCCESS 0
#define BLC_ADV_ENABLE 1
#define BLC_ADV_DISABLE 0
#define HCI_ERR_REMOTE_USER_TERM_CONN 19
static u8 s_acc_sleep_committed,s_acc_retry_ready,s_acc_disconnect_sent;
static u32 s_acc_retry_tick;
static int acc_high,ldo_high,acc_wake,load_wake,deep_calls,timer_hold_calls,reboot_calls,disconnect_calls,adv_enabled=1;
static int acc_low_during_shutdown,acc_low_during_sleep;
static int gpio_read(int pin){assert(pin==BMS_BOARD_ACC_PIN||pin==BMS_BOARD_LOAD_DETECT_PIN);return pin==BMS_BOARD_ACC_PIN?acc_high:0;}
static void start_reboot(void){reboot_calls++;}
static void cpu_set_gpio_wakeup(int pin,int level,int en){assert(level==Level_Low||level==Level_High);if(pin==BMS_BOARD_ACC_PIN)acc_wake=en;else{assert(pin==BMS_BOARD_LOAD_DETECT_PIN);load_wake=en;}}
static int cpu_sleep_wakeup(int mode,int src,u32 tick);
static int bls_ll_terminateConnection(int reason){assert(reason==HCI_ERR_REMOTE_USER_TERM_CONN);disconnect_calls++;return BLE_SUCCESS;}
static int bls_ll_setAdvEnable(int en){adv_enabled=en;return BLE_SUCCESS;}
#define BUS_STATE_OWC_IDLE 0
#define SUSPEND_ADV 1
#define SUSPEND_CONN 2
#define SUSPEND_DISABLE 0
#define BMS_SLEEP_LOW_CELL_MV 2800
#define BMS_SLEEP_NORMAL_CELL_MV 3000
#define BMS_SLEEP_LOW_SECONDS 3600u
#define BMS_SLEEP_NORMAL_SECONDS 86400u
#define DIAG_PM_BLOCK_SAMPLE_INVALID 1u
#define DIAG_PM_BLOCK_OTA 2u
#define DIAG_PM_BLOCK_FLASH 4u
#define DIAG_PM_BLOCK_BUS 8u
#define DIAG_PM_BLOCK_CURRENT 16u
#define DIAG_PM_BLOCK_SAMPLE_PENDING 32u
#define DIAG_PM_BLOCK_POWER_OFF 64u
#define DIAG_PM_BLOCK_ACC_SLEEP 128u
typedef struct{uint32_t sample_tick_32k;int32_t current_ma;}bms_afe_aux_measurements_t;
typedef struct{int unused;}app_pm_elapsed_ctx_t;
static u8 s_power_off_committed,s_power_off_retry_ready;
static volatile u8 s_sample_due;
static u32 s_power_off_retry_tick,now,elapsed;
static int valid=1,flash_ready=1,ota_is_working,device_in_connection_state,bus_busy,mask;
static int storage_ok=1,event_ok=1,shutdown_ok=1,cut_calls,seq[64],seq_len;
static int afe_error, output_enabled=1,afe_shutdown_calls;
#define BMS_ERROR_AFE1 0
static int bms_error_get(int error){assert(error==BMS_ERROR_AFE1);return afe_error;}
static void bms_afe_set_output_enabled(u8 en){output_enabled=en;}
static int bms_afe_enter_shutdown(void);
static int bms_afe_sleep(void){return bms_afe_enter_shutdown();}
static bool deepsleep_en;
static u8 ble_tx_pending;
static u8 blc_ll_getTxFifoNumber(void){return ble_tx_pending;}
static bms_afe_aux_measurements_t measurement;
static bool s_low_power_mode;
static u32 s_sleep_failure_mask;
static u8 s_sleep_report_reason;
static u8 observed_sleep_reason;
static u32 observed_sleep_block, observed_sleep_elapsed, observed_sleep_retry;
void bms_diag_sleep(u8 reason,u32 block,u32 elapsed_ms,u32 delay_ms,u32 retry_ms,u8 allowed){
 (void)delay_ms;(void)allowed;
 observed_sleep_reason=reason;observed_sleep_block=block;
 observed_sleep_elapsed=elapsed_ms;observed_sleep_retry=retry_ms;
}
void bms_diag_sleep_committed(void){}
static struct{uint16_t cell_min_mv;}g_bms_report={3300};
static struct{uint8_t soc_estimate_percent,discharge_fraction_percent;uint32_t cycle_count;}g_bms_soc;
static u32 pm_get_32k_tick(void){return now;}
static int bms_afe_get_aux_measurements(bms_afe_aux_measurements_t*m){*m=measurement;return valid;}
static int app_flash_lock_restore_enabled(void){return flash_ready;}
static int bus_mux_get_state(void){return bus_busy;}
static int bms_state_store_write_all(int s,int d,uint32_t c){seq[seq_len++]=1;return storage_ok;}
static int bms_event_log_note_sleep(void){seq[seq_len++]=2;return event_ok;}
static void bms_event_log_cancel_sleep(void){}
static int bms_afe_enter_shutdown(void){afe_shutdown_calls++;seq[seq_len++]=3;if(acc_low_during_shutdown)acc_high=0;return shutdown_ok;}
static void bls_pm_setAppWakeupLowPower(u32 t,int en){assert(en==0);seq[seq_len++]=4;}
static void gpio_write(int pin,int level){assert(pin==BMS_BOARD_MCU_LDO_PIN);ldo_high=level;if(!level)cut_calls++;seq[seq_len++]=5;}
static u32 clock_time(void){return now;}
/* 模拟外部供电：PC4 已低而 MCU 仍执行，必须只进入保持路径。 */
static int cpu_sleep_wakeup(int mode,int src,u32 tick){
 if(mode==DEEPSLEEP_MODE){
  assert(src==PM_WAKEUP_PAD&&tick==0&&acc_wake&&!load_wake&&ldo_high);
  deep_calls++;
  if(acc_low_during_sleep)acc_high=0;
 }else{
  assert(mode==SUSPEND_MODE&&src==PM_WAKEUP_TIMER&&tick==now+APP_SAMPLE_PERIOD_US*SYSTEM_TIMER_TICK_1US);
  assert(cut_calls==1&&!ldo_high&&!acc_wake&&!load_wake&&seq[seq_len-1]==5);
  timer_hold_calls++;
 }
 return 0;
}
static u32 app_pm_take_elapsed_seconds(app_pm_elapsed_ctx_t*c){return elapsed;}
static void bls_pm_setSuspendMask(int m){mask=m;}
static void bls_pm_setManualLatency(int n){assert(n==0);}
static u32 observed_voltage_seconds;
void bms_diag_runtime_pm(u8 allowed,u32 reason,u8 region,u32 seconds,u8 connected,u8 pending,uint16_t threshold){
 (void)allowed;(void)reason;(void)region;observed_voltage_seconds=seconds;(void)connected;(void)pending;assert(threshold==APP_SUSPEND_EXIT_CURRENT_MA);
}
/* PRODUCTION_SOURCE */
static void reset(void){
 acc_high=acc_wake=load_wake=deep_calls=timer_hold_calls=reboot_calls=disconnect_calls=0;ldo_high=adv_enabled=1;
 acc_low_during_shutdown=acc_low_during_sleep=0;
 s_acc_sleep_committed=s_acc_retry_ready=s_acc_disconnect_sent=0;
 deepsleep_en=false;ble_tx_pending=0;
 s_sleep_failure_mask=s_sleep_report_reason=0;
 memset(&s_protective_sleep,0,sizeof(s_protective_sleep));afe_error=0;output_enabled=1;
 afe_shutdown_calls=0;
 s_power_off_committed=s_power_off_retry_ready=s_sample_due=0;
 storage_ok=event_ok=shutdown_ok=valid=flash_ready=1;
 ota_is_working=device_in_connection_state=bus_busy=seq_len=cut_calls=0;
 now=measurement.sample_tick_32k=100;measurement.current_ma=0;elapsed=0;
 g_bms_report.cell_min_mv=3300;app_power_process(&s_sample_due);
}
static void test_acc_sleep(void){
 reset();acc_high=1;app_power_process(&s_sample_due);
#if !BMS_PRODUCT_SWITCH_SLEEP_ENABLE
 assert(!s_acc_sleep_committed&&!deep_calls&&!cut_calls);
 /* 关掉ACC触发后，命令断电及低压入口继续工作。 */
 deepsleep_en=true;app_power_process(&s_sample_due);assert(cut_calls==1&&!deep_calls);
 return;
#endif
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
 reset();now=UINT32_MAX-100;acc_high=1;assert(app_acc_sleep_requested());
 now+=200u;assert(app_acc_sleep_requested());
 reset();acc_high=1;deepsleep_en=true;app_power_process(&s_sample_due);assert(cut_calls==1&&!deep_calls);
 puts("PASS ACC: immediate request/configuration, keep LDO high, PAD low wake, persistence/OTA/bus/BLE deferral, failures/retry, low race reboot, command priority");
}
static void test_low_voltage_sample_wait(void){
 reset();g_bms_report.cell_min_mv=2700;
 /* 实板 PM 轨迹：正常低压采样间反复出现 >400 ms 的旧电流缓存。
  * 每次等待都不能撤销已经确认的低压，一小时墙钟到期仍必须断电。 */
 for(unsigned seconds=1;seconds<=3600;seconds++){
  now=seconds*32000u;measurement.sample_tick_32k=now;elapsed=1;
  app_power_process(&s_sample_due);
  assert(observed_sleep_elapsed==seconds*1000u);
  if(seconds==3600){assert(cut_calls==1&&timer_hold_calls==1&&!deep_calls&&!output_enabled);break;}
  now+=12801u;elapsed=0;app_power_process(&s_sample_due);
  assert(s_protective_sleep.region==2&&observed_sleep_elapsed==seconds*1000u);
  assert(mask==SUSPEND_DISABLE&&!deep_calls);
 }
 /* 无效缓存的“恢复电压”不能清零；有效恢复和不同延时区才可以。 */
 reset();g_bms_report.cell_min_mv=2700;elapsed=100;app_power_process(&s_sample_due);
 valid=0;g_bms_report.cell_min_mv=3300;elapsed=1;app_power_process(&s_sample_due);
 assert(s_protective_sleep.low_voltage_seconds==101u);
 valid=1;elapsed=0;app_power_process(&s_sample_due);
 assert(!s_protective_sleep.low_voltage_seconds&&!s_protective_sleep.region);
 g_bms_report.cell_min_mv=2700;elapsed=100;app_power_process(&s_sample_due);
 g_bms_report.cell_min_mv=2900;elapsed=0;app_power_process(&s_sample_due);
 assert(s_protective_sleep.region==3&&!s_protective_sleep.low_voltage_seconds);
 /* 小电流偏移处于 SOC 不可靠区，不能冒充可靠充电取消 24 小时计时。 */
 measurement.current_ma=-90;elapsed=5;app_power_process(&s_sample_due);
 assert(s_protective_sleep.region==3&&s_protective_sleep.normal_voltage_seconds==105u);
 measurement.current_ma=-200;app_power_process(&s_sample_due);
 assert(s_protective_sleep.normal_voltage_seconds==110u);
 assert(observed_voltage_seconds==110u);
 measurement.current_ma=-201;elapsed=0;app_power_process(&s_sample_due);
 assert(!s_protective_sleep.region&&!s_protective_sleep.low_voltage_seconds);
 puts("PASS low-voltage timer: repeated ADC waits cannot postpone one-hour expiry; only valid recovery cancels; reliable charge threshold");
}
static void test_protective_low_voltage_shutdown(void){
 const struct {uint16_t cell_mv;u32 seconds;u8 reason;} cases[]={
  {2400u,3600u,DIAG_SLEEP_REASON_VERY_LOW},
  {2700u,BMS_SLEEP_LOW_SECONDS,DIAG_SLEEP_REASON_LOW},
  {2900u,BMS_SLEEP_NORMAL_SECONDS,DIAG_SLEEP_REASON_NORMAL}
 };
 for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++){
  reset();g_bms_report.cell_min_mv=cases[i].cell_mv;elapsed=cases[i].seconds;
  app_power_process(&s_sample_due);
  assert(observed_sleep_reason==cases[i].reason);
  assert(afe_shutdown_calls==1&&cut_calls==1&&!ldo_high&&!deep_calls&&timer_hold_calls==1);
  assert(seq_len==5&&seq[0]==3&&seq[1]==1&&seq[2]==2&&seq[3]==4&&seq[4]==5);
  int saved=seq_len;app_power_process(&s_sample_due);
  assert(timer_hold_calls==2&&cut_calls==1&&afe_shutdown_calls==1&&seq_len==saved);
  acc_high=1;app_power_process(&s_sample_due);
  assert(timer_hold_calls==3&&!reboot_calls&&!deep_calls&&cut_calls==1&&seq_len==saved);
 }
 /* 允许保存时，State、Event 或 Shutdown 各自失败都不能撤销保护性断电。 */
 for(unsigned failure=0;failure<3;failure++){
  reset();storage_ok=failure!=0;event_ok=failure!=1;shutdown_ok=failure!=2;
  g_bms_report.cell_min_mv=2700;elapsed=BMS_SLEEP_LOW_SECONDS;
  app_power_process(&s_sample_due);
  assert(cut_calls==1&&!ldo_high&&!deep_calls&&timer_hold_calls==1&&s_protective_sleep.committed);
  assert(afe_shutdown_calls==1&&seq_len==5&&seq[seq_len-1]==5);
 }
 puts("PASS D008 low-voltage power-off: all voltage regions, save/AFE failures still cut PC4, external supply hold never repeats AFE/Flash/cut or enables PAD wake");
}
int main(void){
 test_acc_sleep();
 test_low_voltage_sample_wait();
 test_protective_low_voltage_shutdown();
 reset();g_bms_report.cell_min_mv=2770;elapsed=2;app_power_process(&s_sample_due);
 assert(observed_sleep_reason==DIAG_SLEEP_REASON_LOW && observed_sleep_elapsed==2000u);
 device_in_connection_state=1;app_power_process(&s_sample_due);
 assert(observed_sleep_elapsed==4000u && observed_sleep_block==0u);
 reset();int currents[]={INT32_MIN,-501,-500,-201,-200,-199,0,199,200,201,500,501,INT32_MAX};
 for(unsigned i=0;i<sizeof(currents)/sizeof(currents[0]);i++){
  measurement.current_ma=currents[i];app_power_process(&s_sample_due);
  int active=currents[i]>=200||currents[i]<=-200;
  assert(mask==(active?SUSPEND_DISABLE:SUSPEND_ADV|SUSPEND_CONN));
  assert(s_low_power_mode==!active);
 }
 /* A live BLE connection is not itself an active-mode request. */
 reset();device_in_connection_state=1;app_power_process(&s_sample_due);
 assert(mask==(SUSPEND_ADV|SUSPEND_CONN)&&s_low_power_mode);
 elapsed=0;g_bms_report.cell_min_mv=3300;
 ota_is_working=1;app_power_process(&s_sample_due);assert(mask==SUSPEND_DISABLE);
 ota_is_working=0;bus_busy=1;app_power_process(&s_sample_due);assert(mask==SUSPEND_DISABLE);
 bus_busy=0;flash_ready=0;app_power_process(&s_sample_due);assert(mask==SUSPEND_DISABLE);
 flash_ready=1;s_sample_due=1;app_power_process(&s_sample_due);assert(mask==SUSPEND_DISABLE);
 s_sample_due=0;measurement.current_ma=500;app_power_process(&s_sample_due);assert(mask==SUSPEND_DISABLE);
 measurement.current_ma=-500;app_power_process(&s_sample_due);assert(mask==SUSPEND_DISABLE);
 measurement.current_ma=0;valid=0;app_power_process(&s_sample_due);assert(mask==SUSPEND_DISABLE);
 puts("PASS connected suspend: link retained; ordinary OTA/bus/Flash/sample/current/invalid gates");
 reset();valid=0;app_power_process(&s_sample_due);assert(mask==SUSPEND_DISABLE && !cut_calls && seq_len==0);
 reset();now+=12801;app_power_process(&s_sample_due);assert(mask==SUSPEND_DISABLE && !cut_calls && seq_len==0);
 reset();ota_is_working=device_in_connection_state=bus_busy=1;flash_ready=storage_ok=event_ok=shutdown_ok=0;
 elapsed=3600;g_bms_report.cell_min_mv=2400;app_power_process(&s_sample_due);
 assert(cut_calls==1&&!deep_calls&&timer_hold_calls==1&&s_protective_sleep.committed&&!output_enabled);
 assert(afe_shutdown_calls==1); /* shutdown 失败不能阻止低压断电。 */
 assert(seq_len==3&&seq[0]==3&&seq[1]==4&&seq[2]==5); /* OTA/Flash skip saves; AFE failure cannot block */
 int saved=seq_len;app_power_process(&s_sample_due);assert(timer_hold_calls==2&&cut_calls==1&&seq_len==saved);
 reset();valid=0;elapsed=900;g_bms_report.cell_min_mv=2400;app_power_process(&s_sample_due);
 assert(!cut_calls&&!timer_hold_calls&&observed_sleep_reason==DIAG_SLEEP_REASON_AFE);
 app_power_process(&s_sample_due);assert(cut_calls==1&&timer_hold_calls==1&&!deep_calls&&s_protective_sleep.committed);
 assert(afe_shutdown_calls==1); /* AFE 异常入口也请求 Shutdown。 */
 reset();g_bms_report.cell_min_mv=2400;afe_error=1;elapsed=900;app_power_process(&s_sample_due);
 g_bms_report.cell_min_mv=2770;app_power_process(&s_sample_due);
 assert(cut_calls==1&&timer_hold_calls==1&&!deep_calls); /* low-voltage branch does not erase the AFE timeout */
 puts("PASS protective sleep: connected low voltage, OTA/bus/Flash/AFE failure override, invalid samples and independent AFE timeout, no repeated saves");
 reset();deepsleep_en=true;flash_ready=0;assert(!app_enter_command_power_off());assert(seq_len==0);
 reset();deepsleep_en=true;device_in_connection_state=1;ble_tx_pending=1;assert(!app_enter_command_power_off());assert(seq_len==0);
 reset();deepsleep_en=true;bus_busy=1;assert(!app_enter_command_power_off());assert(seq_len==0);
 reset();deepsleep_en=true;storage_ok=0;assert(!app_enter_command_power_off());assert(seq_len==1&&!cut_calls);
 reset();deepsleep_en=true;event_ok=0;assert(!app_enter_command_power_off());assert(seq_len==2&&!cut_calls);
 reset();deepsleep_en=true;shutdown_ok=0;assert(!app_enter_command_power_off());assert(seq_len==3&&!cut_calls);
 assert(!app_enter_command_power_off());assert(seq_len==3); /* retry must not wear Flash every loop */
 now+=5*32000;measurement.sample_tick_32k=now;seq_len=0;shutdown_ok=1;
 assert(app_enter_command_power_off());assert(cut_calls==1&&seq_len==5);
 for(int i=0;i<5;i++)assert(seq[i]==i+1);
 assert(!app_enter_command_power_off());assert(cut_calls==1);
 reset();deepsleep_en=true;s_power_off_retry_ready=1;s_power_off_retry_tick=UINT32_MAX-32000;
 now=s_power_off_retry_tick+160000u;measurement.sample_tick_32k=now;
 assert(app_enter_command_power_off());assert(cut_calls==1);
 /* Explicit command works at normal voltage while connected and with stale
  * sampling. Its acknowledgement must drain; busy/failed work stays pending. */
 reset();deepsleep_en=true;device_in_connection_state=1;valid=0;ble_tx_pending=1;
 app_power_process(&s_sample_due);assert(!cut_calls && seq_len==0 && deepsleep_en);
 ble_tx_pending=0;ota_is_working=1;app_power_process(&s_sample_due);assert(!cut_calls);
 ota_is_working=0;bus_busy=1;app_power_process(&s_sample_due);assert(!cut_calls);
 bus_busy=0;flash_ready=0;app_power_process(&s_sample_due);assert(!cut_calls);
 flash_ready=1;storage_ok=0;app_power_process(&s_sample_due);assert(seq_len==1 && !cut_calls);
 assert(observed_sleep_reason==DIAG_SLEEP_REASON_COMMAND);
 assert(observed_sleep_block==DIAG_SLEEP_BLOCK_STORAGE && observed_sleep_retry==5000u);
 for(int i=0;i<10;i++){app_power_process(&s_sample_due);} assert(seq_len==1);
 now+=160000u;seq_len=0;storage_ok=1;shutdown_ok=0;
 app_power_process(&s_sample_due);assert(seq_len==3 && !cut_calls && deepsleep_en);
 now+=160000u;seq_len=0;shutdown_ok=1;
 app_power_process(&s_sample_due);assert(cut_calls==1 && seq_len==5 && s_power_off_committed);
 for(int i=0;i<5;i++)assert(seq[i]==i+1);
 puts("PASS commanded shutdown: BLE response drain, connected/stale samples, OTA/bus/Flash wait, retry, final PC4 cut");
 puts("PASS PM: +/-500 mA, invalid/stale, OTA/bus/Flash gates, persistence/AFE failures, ordered cut-off, retry/wrap");
}
