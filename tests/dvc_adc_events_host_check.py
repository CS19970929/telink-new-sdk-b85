"""DVC RM V1.2 p6/p8 RC ADC事件与独立R6 FET反馈；总线重试不能吃掉事件。"""
from validation_support import read, function, run_c, evidence
import re
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
static uint32_t time_last_raw,time_ms,time_fraction;
uint32_t bms_time_ms(void){uint32_t delta=now-time_last_raw;time_last_raw=now;time_fraction+=delta%32u;time_ms+=delta/32u+time_fraction/32u;time_fraction%=32u;return time_ms;}
uint32_t bms_time_now_32k(void){return now;}

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
code+=s[s.index('#define DVC_ADC_MAX_AGE_MS'):s.index('#define DVC_BALANCE_REFRESH_INTERVAL_MS')]
code+=function(s,'static uint8_t dvc_crc8(')
code+=function(s,'uint8_t DVC1124_ReadRegisters(')
code+=function(s,'static uint8_t dvc_boot_zero_wait_fresh_cc2(')
code+=function(s,'static void dvc_publish_current_measurement(')
code+=function(s,'static void dvc_publish_voltage_measurement(')
code+=function(s,'static void dvc_publish_temperature_measurement(')
code+=function(s,'bms_afe_sample_result_t DVC1124_App_AFEGet(')
recovery=read('bms/afe/dvc1124/dvc1124_bms.c')
code+='\n'+recovery[recovery.index('#define DVC_LOAD_RELEASE_CONFIRM_MS'):recovery.index('static uint16_t dvc_get_configured_temperature')]
code+=function(recovery,'static uint8_t dvc_recover_current_faults(')
code+=r'''
static bms_afe_sample_result_t result;
static void acquire(uint32_t t,uint8_t event){now=t;registers[1]=event;result=DVC1124_App_AFEGet();}
static void reset(void){memset(&s_snapshot,0,sizeof(s_snapshot));memset(registers,0,sizeof(registers));
 s_pending_adc_events=s_voltage_seen=s_current_seen=s_voltage_since_current=0;
 s_voltage_ms=s_current_ms=s_adc_wait_started_ms=0;now=time_last_raw=time_ms=time_fraction=0;memset(s_ntc_valid_samples,0,sizeof(s_ntc_valid_samples));corrupt_reg=-1;corrupt_count=0;}
int main(void){
 reset();acquire(100,0);acquire(6500,0);assert(!s_voltage_seen && !s_snapshot.valid && result.status==BMS_AFE_SAMPLE_NOT_READY && !result.confirm_sample);
 acquire(7000,0x40);assert(s_voltage_seen && s_snapshot.voltage_fresh && !s_snapshot.valid && result.status==BMS_AFE_SAMPLE_NOT_READY && !result.confirm_sample);
 acquire(8000,0x10);assert(s_snapshot.valid && s_snapshot.current_fresh && result.confirm_sample && s_snapshot.sample_tick_32k==8000);
 acquire(9000,0);assert(s_snapshot.valid && result.status==BMS_AFE_SAMPLE_OK && !result.confirm_sample && s_voltage_seen && s_snapshot.sample_tick_32k==8000);
 acquire(8000+512u*32u+32u,0);assert(!s_snapshot.valid && result.status==BMS_AFE_SAMPLE_ERROR);
 /* RM p8 R6 bit1；R1 CST bit1不能冒充DSGF。 */
 reset();registers[6]=2;acquire(100,0x50);assert(s_snapshot.fet_status==2 && g_bms_system_status.bits.discharge_mos_status);
 registers[6]=0;acquire(200,0x52);assert(!s_snapshot.fet_status && !g_bms_system_status.bits.discharge_mos_status);
 /* 第一个读取已通过R1 CRC，后面的R2 CRC失败；重试R1已清零，事件仍须保留。 */
 reset();corrupt_reg=2;corrupt_count=1;acquire(100,0x50);
 assert(s_snapshot.valid && s_snapshot.voltage_fresh && s_snapshot.current_fresh && s_voltage_seen);
 /* R1自身CRC坏，不能把未经验证的事件当作新采样。 */
 reset();corrupt_reg=1;corrupt_count=1;acquire(100,0x50);
 assert(!s_snapshot.valid && !s_voltage_seen && !s_pending_adc_events);
 reset();acquire(0xfffff000u,0x50);acquire(0xfffff000u+512u*32u+32u,0);assert(!s_snapshot.valid);
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
  dvc_observe_current_recovery(&s_snapshot,bms_time_ms());
  dvc_recover_current_faults(&s_snapshot,0,bms_time_ms());
  acquire(6500,(uint8_t)(0x50|cst));g_bms_report.fault_third.bits.discharge_ocp=0;
  dvc_observe_current_recovery(&s_snapshot,bms_time_ms());
  dvc_recover_current_faults(&s_snapshot,0,bms_time_ms());
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
#include <string.h>
#include <stdio.h>
#define DVC_BALANCE_REFRESH_INTERVAL_MS 100u
#define DVC1124_REG_BAL_24_17 0u
static uint32_t s_balance_last_refresh_ms,now,hw;
static struct {uint16_t balance_bits_low,balance_bits_high;} g_bms_report;
static unsigned writes,reads,delays;
static uint8_t cells=24,read_ok=1,write_ok=1,stuck_xor,verify_pending;
static unsigned write_failures,read_failures,verify_failures;
static uint32_t stuck_extra_bits;
static uint32_t dvc_valid_cell_mask(void){return (1u<<cells)-1u;}
static uint8_t DVC1124_ReadRegisters(uint8_t reg,uint8_t *data,uint8_t len){
 (void)reg;assert(len==3u);++reads;
 if(verify_pending){verify_pending=0;if(verify_failures){--verify_failures;return 0;}}
 if(!read_ok)return 0;
 if(read_failures){--read_failures;return 0;}
 data[0]=(uint8_t)(hw>>16);data[1]=(uint8_t)(hw>>8);data[2]=(uint8_t)hw;return 1;
}
/* 在真实写入与最终确认之间注入缺位、OFF 反弹和范围外残留。 */
static uint8_t DVC1124_WriteRegisters(uint8_t reg,const uint8_t *data,uint8_t len){
 (void)reg;assert(len==3u);++writes;if(!write_ok)return 0;
 if(write_failures){--write_failures;return 0;}
 hw=((uint32_t)data[0]<<16)|((uint32_t)data[1]<<8)|data[2];
 hw^=stuck_xor;hw|=stuck_extra_bits;verify_pending=1;
 return 1;
}
static void dvc_delay_ms(uint16_t ms){assert(ms==1u);++delays;}
static uint32_t bms_time_ms(void){return now;}
'''
balance_fixture += re.search(r'^#define DVC1124_I2C_RETRY_COUNT[^\n]*',read('bms/products/d008/dvc1124_product_defaults.h'),re.M).group(0)+'\n'
balance_fixture += function(s,'static uint8_t dvc_write_verified_block(')
balance_fixture += function(s,'static uint8_t dvc_refresh_balance_state(')
balance_fixture += function(read('bms/afe/dvc1124/dvc1124.c'),'static uint8_t dvc_write_balance_hw(')
balance_fixture += function(read('bms/afe/dvc1124/dvc1124.c'),'uint8_t bms_afe_driver_set_balance_mask(')
balance_fixture += function(read('bms/afe/dvc1124/dvc1124.c'),'uint8_t bms_afe_driver_get_balance_mask(')
balance_fixture += r'''
static void expect(uint32_t mask,uint8_t good,unsigned expected_reads,unsigned expected_writes){
 unsigned r=reads,w=writes;
 assert(bms_afe_driver_set_balance_mask(mask)==good);
 assert(reads-r==expected_reads && writes-w==expected_writes);
}
int main(void){
 uint32_t mask=123u;
 assert(!bms_afe_driver_get_balance_mask(0)&&reads==0);
 expect(0,1,1,0); /* 已 OFF 只读一次。 */
 now=5;expect(0x810001u,1,2,1); /* 初始读、写、唯一最终确认。 */
 assert(bms_afe_driver_get_balance_mask(&mask)&&mask==0x810001u);
 expect(mask,1,1,0);
 hw=0x010001u;expect(mask,1,2,1); /* 缺位重写。 */
 now+=100;expect(mask,1,1,0); /* 原严格 > */
 now++;expect(mask,1,2,1); /* 到期续期。 */
 write_ok=0;expect(0,0,1,DVC1124_I2C_RETRY_COUNT);assert(hw==mask);
 assert(s_balance_last_refresh_ms==now);
 write_ok=1;expect(0,1,2,1);assert(!hw);
 hw=2;expect(0,1,2,1);assert(!hw); /* OFF残留。 */
 read_ok=0;mask=123;expect(1,0,1,0); /* 读失败不能凭缓存打开。 */
 assert(!bms_afe_driver_get_balance_mask(&mask)&&mask==123);
 hw=2;expect(0,0,1+DVC1124_I2C_RETRY_COUNT,DVC1124_I2C_RETRY_COUNT);assert(!hw);
 read_ok=1;hw=2;read_failures=1;expect(0,1,2,1); /* 首读失败仍尝试并确认 OFF。 */
 read_ok=1;cells=16;assert(bms_afe_driver_set_balance_mask(0xff010001u)&&hw==1u);
 now=UINT32_MAX-50u;hw=0;assert(bms_afe_driver_set_balance_mask(5));
 now=49;assert(bms_afe_driver_set_balance_mask(5)&&s_balance_last_refresh_ms==UINT32_MAX-50u);
 now=50;write_ok=0;assert(!bms_afe_driver_set_balance_mask(5)&&s_balance_last_refresh_ms==UINT32_MAX-50u);
 write_ok=1;assert(bms_afe_driver_set_balance_mask(5)&&s_balance_last_refresh_ms==50);
 /* 最终确认缺位/OFF反弹：报告实际值，失败不续期。 */
 stuck_xor=1;now=60;hw=0;
 expect(5,0,1+DVC1124_I2C_RETRY_COUNT,DVC1124_I2C_RETRY_COUNT);
 assert(g_bms_report.balance_bits_low==4u && s_balance_last_refresh_ms==50u);
 expect(0,0,1+DVC1124_I2C_RETRY_COUNT,DVC1124_I2C_RETRY_COUNT);
 assert(g_bms_report.balance_bits_low==1u && s_balance_last_refresh_ms==50u);
 stuck_xor=0;expect(0,1,2,1);assert(g_bms_report.balance_bits_low==0u);
 /* 屏蔽范围外串位不能掩盖原始三字节不一致。 */
 stuck_extra_bits=0x010000u;now=70;
 expect(5,0,1+DVC1124_I2C_RETRY_COUNT,DVC1124_I2C_RETRY_COUNT);
 assert(g_bms_report.balance_bits_low==5u && s_balance_last_refresh_ms==60u);
 stuck_extra_bits=0;hw=0;write_failures=1;unsigned d=delays;
 expect(5,1,2,2);assert(delays==d+1u); /* 一次写失败后有界重试。 */
 hw=0;verify_failures=1;d=delays;expect(5,1,3,2);assert(delays==d+1u);
 puts("PASS DVC balance: normal change/renew 2 reads + 1 write; OFF 1 read; bounded failure retries, raw-mask mismatch, actual report and wrap");
 return 0;
}
'''
run_c(balance_fixture,flags=['-Wno-unused-function'],name='dvc-balance-transaction')
