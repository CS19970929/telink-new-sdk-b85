"""DVC RM V1.2 p6/p8 RC ADC事件与独立R6 FET反馈；总线重试不能吃掉事件。"""
from validation_support import read, function, run_c, evidence
s=read('bms/afe/dvc1124/dvc1124.c')
code=r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define GPIO_PB1 1u /* host 输入编号；不操作物理 GPIO。 */
#include "dvc1124.h"
#include "bms_features.h"
#include "bms_diag.h"
#define DVC_MEAS_BYTES 0x4d
#define BMS_ERROR_TEMP_BREAK 0
#define BMS_ERROR_BALANCE 1
#define MODULE_WATCHDOG_ENABLE 0
#define DVC1124_BOOT_ZERO_SAMPLE_INTERVAL_MS 270u
#define BMS_CURRENT_UNRELIABLE_MAX_MA 200u
#ifndef BMS_D008_LOAD_RELEASE_CONFIRM_MS
#define BMS_D008_LOAD_RELEASE_CONFIRM_MS 200u
#define BMS_BOARD_LOAD_REMOVED_LEVEL 1u
#define BMS_BOARD_LOAD_DETECT_PIN 1u
#endif
static int gpio_read(unsigned pin){(void)pin;return 0;}
#define DIAG_EV_CURRENT_RECOVERY 1u
static dvc1124_snapshot_t s_snapshot;
static dvc1124_config_t s_cfg={.cell_count=4};
static uint8_t s_need_config,s_bus_initialized,s_i2c_raw[256];
static uint32_t now;
static uint8_t registers[256];
static int corrupt_reg=-1,corrupt_count;
static uint8_t auto_cc2_event;
static struct {uint16_t charge_current_a10,discharge_current_a10,cell_voltage_mv[32],pack_voltage_10mv,cell_max_mv,cell_min_mv,cell_delta_mv,cell_max_index,cell_min_index,temperature_x10[5],temperature_max_x10,temperature_min_x10,balance_bits_low,balance_bits_high;
 struct {struct {uint8_t charge_ocp,discharge_ocp;}bits;}fault_third;}g_bms_report;
static struct {struct {uint8_t charge_mos_status,discharge_mos_status;}bits;}g_bms_system_status;
static unsigned configuration_calls;
void DVC1124_UpdataAfeConfig(void){++configuration_calls;}
uint8_t DVC1124_GetWriteAddress(void){return 0x40;}
static void dvc_note_comm_result(uint8_t x){(void)x;}
static uint16_t dvc_be16(const uint8_t *x){return ((uint16_t)x[0]<<8)|x[1];}
static int32_t dvc_sign_extend20(uint32_t x){return (x&0x80000)?(int32_t)(x|0xfff00000):(int32_t)x;}
static int32_t dvc_cc2_to_raw_current_ma(int32_t x){return x;}
static int32_t bms_config_calibrate_current(int32_t x){return x;}
static void dvc_publish_current_report(int32_t x){(void)x;}
static uint16_t dvc_correct_cell_mv(uint16_t x,uint32_t cm){(void)cm;return x;}
static uint8_t dvc_ntc_resistance(uint16_t gp,uint16_t v,uint16_t rpu,uint32_t*r){(void)gp;(void)v;(void)rpu;*r=10000;return 1;}
static uint16_t ntc_encoded_temp=650u;
static uint16_t dvc_ntc_temp_report(uint32_t r){(void)r;return ntc_encoded_temp;}
static void bms_error_raise(uint32_t x){(void)x;}
void bms_afe_publish_fet_status(uint8_t x,uint8_t y){(void)x;(void)y;}
void bms_diag_trace(uint16_t e,uint32_t a,uint32_t b){(void)e;(void)a;(void)b;}
uint8_t DVC1124_ClearAlarmFlags(uint8_t m){registers[0]&=(uint8_t)~m;return 1;}
static uint32_t pm_get_32k_tick(void){return now;}
uint8_t DVC1124_WriteRegisters(uint8_t r,const uint8_t *p,uint8_t n){memcpy(registers+r,p,n);return 1;}
static uint8_t balance_read_ok=1;
static uint8_t dvc_refresh_balance_state(void){return balance_read_ok;}
static void dvc_bus_init(void){s_bus_initialized=1;}
static void dvc_bus_recover(void){}
static void dvc_delay_ms(uint32_t ms){now+=ms*32u;if(auto_cc2_event)registers[1]|=0x10u;}
/* 总线peer独立CRC-8：多项式07，初值00；首字节含两地址和寄存器。 */
static uint8_t peer_crc(const uint8_t *p,unsigned n){
 uint8_t c=0;for(unsigned i=0;i<n;++i){c^=p[i];for(unsigned b=0;b<8;++b)c=(c&128)?(uint8_t)((c<<1)^7):(uint8_t)(c<<1);}return c;
}
static uint8_t dvc_i2c_read_raw(uint8_t addr,uint8_t reg,uint8_t *out,uint16_t count){
 for(unsigned i=0;i<count/2;++i){uint8_t v=registers[reg+i];out[2*i]=v;
  uint8_t first[]={addr,reg,(uint8_t)(addr|1),v};out[2*i+1]=peer_crc(i?&v:first,i?1:4);
  if(reg+i==1)registers[1]&=(uint8_t)~0x50u; /* R1 VADF/CC2F RC */
  if((int)(reg+i)==corrupt_reg && corrupt_count){out[2*i+1]^=1;if(corrupt_count>0)--corrupt_count;}
 }return 1;
}
'''
# 实际私有ADC状态、CRC、重试读取及测量发布函数体，换算/平台外围是显式替身。
code+=s[s.index('#define DVC_ADC_MAX_AGE_TICKS'):s.index('#define DVC_BALANCE_REFRESH_INTERVAL_US')]
code+=function(s,'static uint8_t dvc_crc8(')
code+=function(s,'uint8_t DVC1124_ReadRegisters(')
code+=function(s,'static uint8_t dvc_boot_zero_wait_fresh_cc2(')
code+=function(s,'static void dvc_publish_current_measurement(')
code+=function(s,'static void dvc_publish_voltage_measurement(')
code+=function(s,'static void dvc_publish_temperature_measurement(')
code+=function(s,'void DVC1124_App_AFEGet(')
recovery=read('bms/afe/dvc1124/dvc1124_bms.c')
code+='\n'+recovery[recovery.index('#define DVC_LOAD_RELEASE_CONFIRM_TICKS'):recovery.index('static uint16_t dvc_get_configured_temperature')]
code+=function(recovery,'static uint8_t dvc_recover_current_faults(')
code+=r'''
static void acquire(uint32_t t,uint8_t event){now=t;registers[1]=event;DVC1124_App_AFEGet();}
static void reset(void){memset(&s_snapshot,0,sizeof(s_snapshot));memset(registers,0,sizeof(registers));
 s_pending_adc_events=s_voltage_seen=s_current_seen=s_sample_pending=s_voltage_since_current=0;
 s_voltage_tick=s_current_tick=s_adc_wait_started=0;memset(s_ntc_valid_samples,0,sizeof(s_ntc_valid_samples));corrupt_reg=-1;corrupt_count=0;}
int main(void){
 reset();acquire(100,0);acquire(6500,0);assert(!s_voltage_seen && !s_snapshot.valid && s_sample_pending);
 acquire(7000,0x40);assert(s_voltage_seen && s_snapshot.voltage_fresh && !s_snapshot.valid && s_sample_pending);
 acquire(8000,0x10);assert(s_snapshot.valid && s_snapshot.current_fresh && !s_sample_pending && s_snapshot.sample_tick_32k==8000);
 acquire(9000,0);assert(s_snapshot.valid && s_sample_pending && s_voltage_seen && s_snapshot.sample_tick_32k==8000);
 acquire(8000+512u*32u+1u,0);assert(!s_snapshot.valid && !s_sample_pending);
 /* RM p8 R6 bit1；R1 CST bit1不能冒充DSGF。 */
 reset();registers[6]=2;acquire(100,0x50);assert(s_snapshot.fet_status==2 && g_bms_system_status.bits.discharge_mos_status);
 registers[6]=0;acquire(200,0x52);assert(!s_snapshot.fet_status && !g_bms_system_status.bits.discharge_mos_status);
 /* 第一个读取已通过R1 CRC，后面的R2 CRC失败；重试R1已清零，事件仍须保留。 */
 reset();corrupt_reg=2;corrupt_count=1;acquire(100,0x50);
 assert(s_snapshot.valid && s_snapshot.voltage_fresh && s_snapshot.current_fresh && s_voltage_seen);
 /* R1自身CRC坏，不能把未经验证的事件当作新采样。 */
 reset();corrupt_reg=1;corrupt_count=1;acquire(100,0x50);
 assert(!s_snapshot.valid && !s_voltage_seen && !s_pending_adc_events);
 reset();acquire(0xfffff000u,0x50);acquire(0xfffff000u+512u*32u+1u,0);assert(!s_snapshot.valid);
 reset();registers[1]=0x10;assert(!dvc_boot_zero_wait_fresh_cc2()); /* 只有旧事件，等待后没有新转换。 */
 auto_cc2_event=1;assert(dvc_boot_zero_wait_fresh_cc2());assert(!s_pending_adc_events);auto_cc2_event=0;
 /* 新电压持续发布，零读数不再被诊断缓存替换；无效串位仍是 61001。 */
 reset();registers[DVC1124_REG_CELL1_H]=0x0d;registers[DVC1124_REG_CELL1_H+1]=0x48;
 acquire(100,0x50);assert(g_bms_report.cell_voltage_mv[0]==3400);
 registers[DVC1124_REG_CELL1_H]=registers[DVC1124_REG_CELL1_H+1]=0;
 acquire(6500,0x50);assert(g_bms_report.cell_voltage_mv[0]==0 && g_bms_report.cell_voltage_mv[4]==61001u);
 for(unsigned cst=0;cst<=6;++cst)for(unsigned driver=0;driver<=1;++driver){
  reset();memset(&s_current_recovery,0,sizeof(s_current_recovery));registers[6]=driver?2:0;
  acquire(100,(uint8_t)(0x50|cst));g_bms_report.fault_third.bits.discharge_ocp=1;
  dvc_observe_current_recovery(&s_snapshot,now);
  dvc_recover_current_faults(&s_snapshot,0,now);
  acquire(6500,(uint8_t)(0x50|cst));g_bms_report.fault_third.bits.discharge_ocp=0;
  dvc_observe_current_recovery(&s_snapshot,now);
  dvc_recover_current_faults(&s_snapshot,0,now);
  assert(s_snapshot.status==(uint8_t)(0x50|cst));
  assert(!!(s_snapshot.fet_status&DVC1124_CC2_DSGF_MASK)==driver);
  assert(s_current_recovery.discharge); /* R1/R6 均不能让零电流解除放电故障。 */
 }
 reset();g_bms_report.balance_bits_low=5;balance_read_ok=0;acquire(100,0x50);
 assert(g_bms_report.balance_bits_low==5);balance_read_ok=1;
 /* 编码0可代表有效 -40.0C，不应被全局温度极值汇总跳过。 */
 reset();ntc_encoded_temp=0u;
 registers[DVC1124_REG_VCT_H]=0x03u;
 registers[DVC1124_REG_VCT_H+1]=0xe8u; /* 有效die原始码1000，编码136。 */
 acquire(100u,0x50u);acquire(6500u,0x50u);acquire(12900u,0x50u);
 assert(s_snapshot.ntc_res_ohm[0]!=0u && g_bms_report.temperature_x10[0]==0u);
 assert(g_bms_report.temperature_min_x10==0u && g_bms_report.temperature_max_x10==136u);
 ntc_encoded_temp=650u;
 s_need_config=1;configuration_calls=0;
 for(unsigned i=0;i<100;++i)acquire(6400u*i,0x50u);
 assert(!s_snapshot.valid && !configuration_calls);
 s_need_config=0;
 puts("PASS DVC ADC完成事件、异步通道、RC/CRC重试、R1与R6分离、缓存到期与tick回绕");return 0;
}
'''
out=run_c(code,flags=['-DBMS_SERVICE_DIAG_ENABLE=1'],name='dvc-adc-events')
evidence({'domain':'adc_events','trace':out.strip(),'boundary':'实际CRC/ReadRegisters/App_AFEGet函数体；I2C peer非芯片仿真，外围换算替身；512ms为项目活性约束'})

# 同一真实事务验证回读、命令失败、续期、OFF、有效串和tick回绕。
balance_fixture = r'''
#include <stdint.h>
#include <assert.h>
#define DVC_BALANCE_REFRESH_INTERVAL_US 100u
static uint32_t s_balance_last_refresh_tick,now,hw;
static struct {uint16_t balance_bits_low,balance_bits_high;} g_bms_report;
static unsigned writes,reads;
static uint8_t cells=24,read_ok=1,write_ok=1;
static uint32_t dvc_valid_cell_mask(void){return (1u<<cells)-1u;}
static uint8_t dvc_refresh_balance_state(void){
 ++reads;if(!read_ok)return 0;
 g_bms_report.balance_bits_low=(uint16_t)(hw&dvc_valid_cell_mask());
 g_bms_report.balance_bits_high=(uint16_t)((hw&dvc_valid_cell_mask())>>16);return 1;
}
static uint8_t dvc_write_balance_hw(uint32_t mask){++writes;if(!write_ok)return 0;hw=mask;return dvc_refresh_balance_state();}
static uint32_t clock_time(void){return now;}
static int clock_time_exceed(uint32_t tick,uint32_t us){return (uint32_t)(now-tick)>us;}
'''
balance_fixture += function(read('bms/afe/dvc1124/dvc1124.c'),'uint8_t dvc1124_backend_set_balance_mask(')
balance_fixture += function(read('bms/afe/dvc1124/dvc1124.c'),'uint8_t dvc1124_backend_get_balance_mask(')
balance_fixture += r'''
int main(void){
 uint32_t mask=123u;
 assert(!dvc1124_backend_get_balance_mask(0)&&reads==0);
 assert(dvc1124_backend_set_balance_mask(0)&&writes==0);
 now=5;assert(dvc1124_backend_set_balance_mask(0x810001u)&&writes==1);
 assert(dvc1124_backend_get_balance_mask(&mask)&&mask==0x810001u);
 assert(dvc1124_backend_set_balance_mask(mask)&&writes==1);
 hw=0x010001u;assert(dvc1124_backend_set_balance_mask(mask)&&writes==2); /* 缺位 */
 now+=100;assert(dvc1124_backend_set_balance_mask(mask)&&writes==2); /* 原严格 > */
 now++;assert(dvc1124_backend_set_balance_mask(mask)&&writes==3); /* 到期 */
 write_ok=0;assert(!dvc1124_backend_set_balance_mask(0)&&hw==mask);
 assert(s_balance_last_refresh_tick==now);
 write_ok=1;assert(dvc1124_backend_set_balance_mask(0)&&!hw&&writes==5);
 hw=2;assert(dvc1124_backend_set_balance_mask(0)&&!hw&&writes==6); /* OFF残留 */
 read_ok=0;mask=123;assert(!dvc1124_backend_set_balance_mask(1)&&writes==6);
 assert(!dvc1124_backend_get_balance_mask(&mask)&&mask==123);
 hw=2;assert(!dvc1124_backend_set_balance_mask(0)&&!hw&&writes==7); /* 读失败仍尝试OFF，失败不确认 */
 read_ok=1;cells=16;assert(dvc1124_backend_set_balance_mask(0xff010001u)&&hw==1u);
 now=UINT32_MAX-50u;hw=0;assert(dvc1124_backend_set_balance_mask(5));
 now=49;assert(dvc1124_backend_set_balance_mask(5)&&s_balance_last_refresh_tick==UINT32_MAX-50u);
 now=50;write_ok=0;assert(!dvc1124_backend_set_balance_mask(5)&&s_balance_last_refresh_tick==UINT32_MAX-50u);
 write_ok=1;assert(dvc1124_backend_set_balance_mask(5)&&s_balance_last_refresh_tick==50);
 return 0;
}
'''
run_c(balance_fixture,name='dvc-balance-transaction')
