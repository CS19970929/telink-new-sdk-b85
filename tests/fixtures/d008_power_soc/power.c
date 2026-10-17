#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
typedef uint8_t u8;typedef uint32_t u32;
typedef int GPIO_PinTypeDef;
#define BMS_AFE_BACKEND 1
#define BMS_AFE_BACKEND_DVC1124 1
#define APP_PM_MS_PER_SEC 1000u
/* CURRENT_FLOOR */
/* SUSPEND_CURRENT_LIMIT */
#define BMS_SOC_MAX_SAMPLE_GAP_MS 400u
#define BMS_SAMPLE_MAX_POLL_GAP_MS 400u
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
static u8 s_acc_sleep_committed;
static int acc_high,ldo_high,acc_wake,load_wake,deep_calls,timer_hold_calls,reboot_calls,disconnect_calls,adv_enabled=1;
static int acc_low_during_shutdown,acc_low_during_sleep,ble_control_ok=1;
static int gpio_read(int pin){assert(pin==BMS_BOARD_ACC_PIN||pin==BMS_BOARD_LOAD_DETECT_PIN);return pin==BMS_BOARD_ACC_PIN?acc_high:0;}
static void start_reboot(void){reboot_calls++;}
static void cpu_set_gpio_wakeup(int pin,int level,int en){assert(level==Level_Low||level==Level_High);if(pin==BMS_BOARD_ACC_PIN)acc_wake=en;else{assert(pin==BMS_BOARD_LOAD_DETECT_PIN);load_wake=en;}}
static int cpu_sleep_wakeup(int mode,int src,u32 tick);
static int bls_ll_terminateConnection(int reason){assert(reason==HCI_ERR_REMOTE_USER_TERM_CONN);disconnect_calls++;return ble_control_ok?BLE_SUCCESS:1;}
static int bls_ll_setAdvEnable(int en){adv_enabled=en;return ble_control_ok?BLE_SUCCESS:1;}
#define BUS_STATE_OWC_IDLE 0
#define BUS_STATE_OWC_TX 1
#define BUS_STATE_UART_MODBUS 2
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
typedef struct{uint32_t sample_time_ms;int32_t current_ma;}bms_afe_aux_measurements_t;
typedef struct{int unused;}app_pm_elapsed_ctx_t;
static volatile u8 s_sample_due;
static u32 now,elapsed;
static int valid=1,flash_ready=1,ota_is_working,device_in_connection_state,bus_busy,mask;
static int storage_ok=1,event_ok=1,shutdown_ok=1,cut_calls,seq[64],seq_len;
static int afe_error, output_enabled=1,afe_shutdown_calls;
#define BMS_ERROR_AFE1 0
static int bms_error_get(int error){assert(error==BMS_ERROR_AFE1);return afe_error;}
static void bms_afe_set_output_enabled(u8 en){output_enabled=en;}
static int bms_afe_sleep(void);

static bool deepsleep_en;
static u8 ble_tx_pending,uart_busy,modbus_tx_busy;
static u8 blc_ll_getTxFifoNumber(void){return ble_tx_pending;}
static int uart_tx_is_busy(void){return uart_busy;}
static u8 modbus_uart_tx_active(void){return modbus_tx_busy;}
static bms_afe_aux_measurements_t measurement;
static bool s_low_power_mode;
static u8 observed_sleep_reason;
static u32 observed_sleep_block, observed_sleep_elapsed, observed_sleep_delay, observed_sleep_retry;
void bms_diag_sleep(u8 reason,u32 block,u32 elapsed_ms,u32 delay_ms,u32 retry_ms,u8 allowed){
 (void)allowed;
 observed_sleep_reason=reason;observed_sleep_block=block;
 observed_sleep_elapsed=elapsed_ms;observed_sleep_delay=delay_ms;observed_sleep_retry=retry_ms;
}
void bms_diag_sleep_committed(void){}
static struct{uint16_t cell_min_mv, pack_voltage_10mv; struct { struct { uint8_t cell_uvp, pack_uvp; } bits; } fault_third;}g_bms_report={.cell_min_mv=3300};
static struct{uint8_t soc_estimate_percent,discharge_fraction_percent;uint32_t cycle_count;}g_bms_soc;
static u32 bms_time_ms(void){return now;}
static unsigned afe_snapshot_reads;
static int bms_afe_get_aux_measurements(bms_afe_aux_measurements_t*m){
 ++afe_snapshot_reads;*m=measurement;return valid;
}
static int app_flash_lock_restore_enabled(void){return flash_ready;}
static int bus_mux_get_state(void){return bus_busy;}
static int bms_state_store_save(int s,int d,uint32_t c){seq[seq_len++]=1;return storage_ok;}
static int bms_event_log_note_sleep(void){seq[seq_len++]=2;return event_ok;}
static int bms_afe_sleep(void){afe_shutdown_calls++;seq[seq_len++]=3;if(acc_low_during_shutdown)acc_high=0;return shutdown_ok;}
static void bls_pm_setAppWakeupLowPower(u32 t,int en){assert(en==0);seq[seq_len++]=4;}
static void gpio_write(int pin,int level){assert(pin==BMS_BOARD_MCU_LDO_PIN);ldo_high=level;if(!level)cut_calls++;seq[seq_len++]=5;}
static u32 clock_time(void){return now;}
/* 记录低功耗调用；断电提交后不得进入定时 Suspend 或 MCU DeepSleep。 */
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
static unsigned diag_poll_calls;
static void bms_parameters_diag_poll(void){++diag_poll_calls;}
static void bms_storage_platform_diag_poll(void){++diag_poll_calls;}
static void bms_afe_diag_poll(void){++diag_poll_calls;}
#include "bms_sleep_config.h"
static bms_sleep_config_t sleep_config;
static struct { uint16_t cell_uvp_recover_mv, pack_uvp_recover_10mv; } g_bms_protection_params={2900u,2900u};
void bms_config_get_sleep(bms_sleep_config_t *config){*config=sleep_config;}

/* PRODUCTION_SOURCE */
static void reset(void){
 diag_poll_calls=0;
 acc_high=acc_wake=load_wake=deep_calls=timer_hold_calls=reboot_calls=disconnect_calls=0;ldo_high=adv_enabled=1;
 acc_low_during_shutdown=acc_low_during_sleep=0;
 s_acc_sleep_committed=0;
 deepsleep_en=false;ble_tx_pending=uart_busy=modbus_tx_busy=0;ble_control_ok=1;
 s_command_seen=0;s_command_ms=0;
 memset(&s_protective_sleep,0,sizeof(s_protective_sleep));s_protective_sleep_committed=0u;
 sleep_config=(bms_sleep_config_t){2800,60,3000,1440,30};
 g_bms_report.fault_third.bits.cell_uvp=g_bms_report.fault_third.bits.pack_uvp=0;
 g_bms_report.pack_voltage_10mv=3300;afe_error=0;output_enabled=1;
 afe_shutdown_calls=0;
 s_sample_due=0;
 storage_ok=event_ok=shutdown_ok=valid=flash_ready=1;
 ota_is_working=device_in_connection_state=bus_busy=seq_len=cut_calls=0;
 now=measurement.sample_time_ms=100;measurement.current_ma=0;elapsed=0;
 g_bms_report.cell_min_mv=3300;afe_snapshot_reads=0;
 app_power_process(&s_sample_due);
 assert(afe_snapshot_reads==1u); /* 同一轮深睡/普通Suspend/诊断只采一份快照。 */
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
 assert(seq_len==5&&seq[0]==3&&seq[1]==1&&seq[2]==2&&seq[3]==4&&seq[4]==5);
 int saved=seq_len;app_acc_sleep_hold();assert(seq_len==saved&&deep_calls==2);
 acc_high=0;app_acc_sleep_hold();assert(reboot_calls==1&&seq_len==saved);
 /* 真实 PM 入口：持续总线/收发、OTA、Flash及各项失败均不得拒绝开关深睡。 */
 for(int bus_state=BUS_STATE_OWC_TX;bus_state<=BUS_STATE_UART_MODBUS;bus_state++)
 for(int failure=0;failure<10;failure++){
  reset();acc_high=1;device_in_connection_state=1;bus_busy=bus_state;
  ble_tx_pending=uart_busy=modbus_tx_busy=1;
  if(failure==1)ota_is_working=1;
  if(failure==2)flash_ready=0;
  if(failure==3)storage_ok=0;
  if(failure==4)event_ok=0;
  if(failure==5)shutdown_ok=0;
  if(failure==6)ble_control_ok=0;
  if(failure==7){valid=0;s_sample_due=1;measurement.current_ma=INT32_MAX;}
  if(failure==8)measurement.current_ma=INT32_MAX;
  if(failure==9)measurement.current_ma=INT32_MIN;
  app_power_process(&s_sample_due);
  assert(s_acc_sleep_committed&&deep_calls==1&&!cut_calls&&ldo_high&&!output_enabled);
  assert(observed_sleep_reason==DIAG_SLEEP_REASON_ACC&&!observed_sleep_block&&!observed_sleep_retry);
  assert(afe_shutdown_calls==1&&disconnect_calls==1&&seq[0]==3);
  if(failure==1||failure==2)assert(seq_len==3&&seq[1]==4&&seq[2]==5);
  else assert(seq_len==5&&seq[1]==1&&seq[2]==2&&seq[3]==4&&seq[4]==5);
  saved=seq_len;
  assert(app_power_prepare_loop()&&app_power_prepare_loop());
  assert(deep_calls==3&&afe_shutdown_calls==1&&seq_len==saved&&!diag_poll_calls);
 }
 /* ON时不提交；临睡前/SDK返回时ON都走完整重启，不恢复旧AFE业务状态。 */
 reset();assert(!app_enter_acc_sleep()&&!seq_len);
 reset();acc_high=1;acc_low_during_shutdown=1;
 assert(app_enter_acc_sleep()&&reboot_calls==1&&!deep_calls&&!cut_calls);
 reset();acc_high=1;acc_low_during_sleep=1;
 assert(app_enter_acc_sleep()&&reboot_calls==1&&deep_calls==1&&!cut_calls);
 reset();now=UINT32_MAX-100;acc_high=1;assert(app_acc_sleep_requested());
 now+=200u;assert(app_acc_sleep_requested());
 reset();acc_high=1;deepsleep_en=true;app_power_process(&s_sample_due);assert(cut_calls==1&&!deep_calls);
 puts("PASS ACC: immediate commit/configuration, PC4 high, PA0 wake, traffic/failure overrides, one-shot cleanup, ON race reboot, command priority");
}
static void test_low_voltage_sample_wait(void){
 reset();g_bms_report.cell_min_mv=2700;
 /* 实板 PM 轨迹：正常低压采样间反复出现 >400 ms 的旧电流缓存。
  * 每次等待都不能撤销已经确认的低压，一小时墙钟到期仍必须断电。 */
 for(unsigned seconds=1;seconds<=3600;seconds++){
  now=seconds*1000u;measurement.sample_time_ms=now;elapsed=1;
  app_power_process(&s_sample_due);
  assert(observed_sleep_elapsed==seconds*1000u);
  if(seconds==3600){assert(cut_calls==1&&timer_hold_calls==0&&!deep_calls&&!output_enabled);break;}
  now+=401u;elapsed=0;app_power_process(&s_sample_due);
  /* 无新鲜样本时诊断可能优先显示 AFE 倒计时；低压计时所有者必须保持。 */
  assert(s_protective_sleep.voltage_conditions==(BMS_SLEEP_VOLTAGE_LOW_ACTIVE|BMS_SLEEP_VOLTAGE_NORMAL_ACTIVE)&&s_protective_sleep.low_voltage_seconds==seconds);
  assert(mask==SUSPEND_DISABLE&&!deep_calls);
 }
 /* 无效缓存的“恢复电压”不能清零；有效恢复和不同延时区才可以。 */
 reset();g_bms_report.cell_min_mv=2700;elapsed=100;app_power_process(&s_sample_due);
 valid=0;g_bms_report.cell_min_mv=3300;elapsed=1;app_power_process(&s_sample_due);
 assert(s_protective_sleep.low_voltage_seconds==101u);
 valid=1;elapsed=0;app_power_process(&s_sample_due);
 assert(!s_protective_sleep.low_voltage_seconds&&!s_protective_sleep.voltage_conditions);
 g_bms_report.cell_min_mv=2700;elapsed=100;app_power_process(&s_sample_due);
 g_bms_report.cell_min_mv=2900;elapsed=0;app_power_process(&s_sample_due);
 assert(s_protective_sleep.voltage_conditions==BMS_SLEEP_VOLTAGE_NORMAL_ACTIVE&&!s_protective_sleep.low_voltage_seconds);
 /* 小电流偏移处于 SOC 不可靠区，不能冒充可靠充电取消 24 小时计时。 */
 measurement.current_ma=-90;elapsed=5;app_power_process(&s_sample_due);
 assert(s_protective_sleep.voltage_conditions==BMS_SLEEP_VOLTAGE_NORMAL_ACTIVE&&s_protective_sleep.normal_voltage_seconds==105u);
 measurement.current_ma=-200;app_power_process(&s_sample_due);
 assert(s_protective_sleep.normal_voltage_seconds==110u);
 assert(observed_voltage_seconds==110u);
 measurement.current_ma=-201;elapsed=0;app_power_process(&s_sample_due);
 assert(!s_protective_sleep.voltage_conditions&&!s_protective_sleep.low_voltage_seconds);
 puts("PASS low-voltage timer: repeated ADC waits cannot postpone one-hour expiry; only valid recovery cancels; reliable charge threshold");
}
static void test_protective_low_voltage_shutdown(void){
 const struct {uint16_t cell_mv;u32 seconds;u8 reason;} cases[]={
  {2400u,BMS_SLEEP_LOW_SECONDS,DIAG_SLEEP_REASON_LOW},
  {2700u,BMS_SLEEP_LOW_SECONDS,DIAG_SLEEP_REASON_LOW},
  {2900u,BMS_SLEEP_NORMAL_SECONDS,DIAG_SLEEP_REASON_NORMAL}
 };
 for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++){
  reset();g_bms_report.cell_min_mv=cases[i].cell_mv;elapsed=cases[i].seconds;
  app_power_process(&s_sample_due);
  assert(observed_sleep_reason==cases[i].reason);
  assert(afe_shutdown_calls==1&&cut_calls==1&&!ldo_high&&!deep_calls&&timer_hold_calls==0);
  assert(seq_len==5&&seq[0]==3&&seq[1]==1&&seq[2]==2&&seq[3]==4&&seq[4]==5);
  int saved=seq_len;assert(app_power_prepare_loop() && !diag_poll_calls);
  app_power_process(&s_sample_due);
  assert(timer_hold_calls==0&&cut_calls==1&&afe_shutdown_calls==1&&seq_len==saved);
  acc_high=1;app_power_process(&s_sample_due);
  assert(timer_hold_calls==0&&!reboot_calls&&!deep_calls&&cut_calls==1&&seq_len==saved);
 }
 /* 允许保存时，State、Event 或 Shutdown 各自失败都不能撤销保护性断电。 */
 for(unsigned failure=0;failure<3;failure++){
  reset();storage_ok=failure!=0;event_ok=failure!=1;shutdown_ok=failure!=2;
  g_bms_report.cell_min_mv=2700;elapsed=BMS_SLEEP_LOW_SECONDS;
  app_power_process(&s_sample_due);
  assert(cut_calls==1&&!ldo_high&&!deep_calls&&timer_hold_calls==0&&s_protective_sleep_committed);
  assert(afe_shutdown_calls==1&&seq_len==5&&seq[seq_len-1]==5);
 }
 puts("PASS D008 low-voltage power-off: all voltage regions, save/AFE failures still cut PC4, external supply hold never repeats AFE/Flash/cut or enables PAD wake");
}
int main(void){

 reset();sleep_config.low_delay_minutes=10u;g_bms_report.cell_min_mv=2600u;
 g_bms_report.fault_third.bits.cell_uvp=1u;
 elapsed=179u;app_power_process(&s_sample_due);assert(!cut_calls);
 sleep_config.low_delay_minutes=20u; /* 改可配置时间不得清除独立180秒。 */
 g_bms_report.fault_third.bits.cell_uvp=0u;
 elapsed=1u;app_power_process(&s_sample_due);
 assert(cut_calls==1&&observed_sleep_reason==DIAG_SLEEP_REASON_OVERDISCHARGE);
 reset();g_bms_report.fault_third.bits.pack_uvp=1u;
 elapsed=179u;app_power_process(&s_sample_due);assert(!cut_calls);
 valid=0;g_bms_report.fault_third.bits.pack_uvp=0u;
 elapsed=1u;app_power_process(&s_sample_due);
 assert(cut_calls==1&&observed_sleep_reason==DIAG_SLEEP_REASON_OVERDISCHARGE);
 reset();g_bms_report.fault_third.bits.cell_uvp=1u;
 elapsed=179u;app_power_process(&s_sample_due);assert(!cut_calls);
 g_bms_report.fault_third.bits.cell_uvp=0u;
 elapsed=1u;app_power_process(&s_sample_due);assert(!cut_calls&&!s_protective_sleep.overdischarge_seconds);
 reset();sleep_config.low_delay_minutes=1u;g_bms_report.cell_min_mv=2500u;
 elapsed=59u;app_power_process(&s_sample_due);assert(!cut_calls);
 elapsed=1u;app_power_process(&s_sample_due);
 assert(cut_calls==1&&observed_sleep_reason==DIAG_SLEEP_REASON_LOW);

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
 reset();now+=401;app_power_process(&s_sample_due);assert(mask==SUSPEND_DISABLE && !cut_calls && seq_len==0);
 reset();ota_is_working=device_in_connection_state=bus_busy=1;flash_ready=storage_ok=event_ok=shutdown_ok=0;
 elapsed=3600;g_bms_report.cell_min_mv=2400;app_power_process(&s_sample_due);
 assert(cut_calls==1&&!deep_calls&&timer_hold_calls==0&&s_protective_sleep_committed&&!output_enabled);
 assert(afe_shutdown_calls==1); /* shutdown 失败不能阻止低压断电。 */
 assert(seq_len==3&&seq[0]==3&&seq[1]==4&&seq[2]==5); /* OTA/Flash skip saves; AFE failure cannot block */
 int saved=seq_len;app_power_process(&s_sample_due);assert(timer_hold_calls==0&&cut_calls==1&&seq_len==saved);
 reset();valid=0;elapsed=900;g_bms_report.cell_min_mv=2400;app_power_process(&s_sample_due);
 assert(!cut_calls&&!timer_hold_calls&&observed_sleep_reason==DIAG_SLEEP_REASON_AFE);
 app_power_process(&s_sample_due);assert(cut_calls==1&&timer_hold_calls==0&&!deep_calls&&s_protective_sleep_committed);
 assert(afe_shutdown_calls==1); /* AFE 异常入口也请求 Shutdown。 */
 reset();g_bms_report.cell_min_mv=2400;afe_error=1;elapsed=900;app_power_process(&s_sample_due);
 g_bms_report.cell_min_mv=2770;app_power_process(&s_sample_due);
 assert(cut_calls==1&&timer_hold_calls==0&&!deep_calls); /* low-voltage branch does not erase the AFE timeout */
 puts("PASS protective sleep: connected low voltage, OTA/bus/Flash/AFE failure override, invalid samples and independent AFE timeout, no repeated saves");
 /* 命令走真实PM入口：持续一线通、OTA、Flash未就绪和各种失败都不能拒睡。 */
 for(int failure=0;failure<8;failure++){
  reset();deepsleep_en=true;device_in_connection_state=1;bus_busy=1;
  if(failure==1)ota_is_working=1;
  if(failure==2)flash_ready=0;
  if(failure==3)storage_ok=0;
  if(failure==4)event_ok=0;
  if(failure==5)shutdown_ok=0;
  if(failure==6)ble_control_ok=0;
  if(failure==7)valid=0;
  app_power_process(&s_sample_due);
  assert(cut_calls==1&&s_protective_sleep_committed&&!output_enabled&&!deep_calls);
  assert(observed_sleep_reason==DIAG_SLEEP_REASON_COMMAND&&!observed_sleep_block&&!observed_sleep_retry);
  assert(afe_shutdown_calls==1);
  assert(seq[0]==3&&seq[seq_len-1]==5); /* AFE先尝试，PC4断电最后执行。 */
  if(failure==1||failure==2)assert(seq_len==3);
  else assert(seq_len==5&&seq[1]==1&&seq[2]==2); /* 保存失败仍尝试事件一次。 */
  saved=seq_len;
  assert(app_power_prepare_loop()&&app_power_prepare_loop());
  app_power_process(&s_sample_due);
  assert(cut_calls==1&&seq_len==saved&&!diag_poll_calls&&!timer_hold_calls);
 }
 /* BLE/UART/DE各自卡住最多500ms；同时覆盖32位tick回绕。 */
 for(int pending=0;pending<3;pending++){
  reset();now=UINT32_MAX-8000u;deepsleep_en=true;device_in_connection_state=1;
  bus_busy=ota_is_working=1;flash_ready=storage_ok=event_ok=shutdown_ok=0;
  ble_tx_pending=pending==0;uart_busy=pending==1;modbus_tx_busy=pending==2;
  app_power_process(&s_sample_due);assert(!cut_calls&&!seq_len);
  assert(observed_sleep_reason==DIAG_SLEEP_REASON_COMMAND&&observed_sleep_delay==500u);
  now+=499u*32u;app_power_process(&s_sample_due);assert(!cut_calls&&!seq_len);
  assert(observed_sleep_elapsed==499u&&!observed_sleep_block);
  now+=32u;app_power_process(&s_sample_due);
  assert(cut_calls==1&&s_protective_sleep_committed&&seq_len==3);
 }
 /* 应答提前发完时立即关机，无需等满500ms或关闭开关。 */
 reset();deepsleep_en=true;device_in_connection_state=1;ble_tx_pending=1;
 app_power_process(&s_sample_due);assert(!cut_calls);
 ble_tx_pending=0;bus_busy=1;app_power_process(&s_sample_due);assert(cut_calls==1);
 puts("PASS commanded shutdown: OWC/OTA/Flash/failure override, bounded BLE/UART reply, tick wrap, one-shot PC4 cut");
 puts("PASS PM: ordinary suspend gates retained; ACC/protective/command requests cannot be blocked");
}
