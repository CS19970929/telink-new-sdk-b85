"""DVC RM V1.2 p6/p8 RC ADC事件与独立R6 FET反馈；总线重试不能吃掉事件。"""
from validation_support import read, function, run_c, evidence
s=read('bms/afe/dvc1124/dvc1124.c')
code=r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "dvc1124.h"
#include "bms_features.h"
#include "bms_diag.h"
#define DVC_MEAS_BYTES 0x4d
#define BMS_ERROR_TEMP_BREAK 0
#define MODULE_WATCHDOG_ENABLE 0
#define DVC1124_BOOT_ZERO_SAMPLE_INTERVAL_MS 270u
#define BMS_CURRENT_UNRELIABLE_MAX_MA 200u
#define DIAG_EV_CURRENT_RECOVERY 1u
#define DVC_OPENWIRE_SETTLE_US 200000u
#define DVC_OPENWIRE_TIMEOUT_US 900000u
static dvc1124_snapshot_t s_snapshot;
static dvc1124_config_t s_cfg={.cell_count=4,.battery_ntc_gp=2,.mos_ntc_gp=3};
static uint8_t s_need_config,s_bus_initialized,s_i2c_raw[256];
static uint32_t s_snapshot_generation,now;
static uint32_t s_openwire_start_generation,s_openwire_start_tick,s_balance_requested_mask;
static uint8_t s_balance_suspended;
static dvc1124_openwire_result_t s_openwire_result;
static uint8_t diagnostic_active;
void bms_features_get_status(bms_features_status_t *s){memset(s,0,sizeof(*s));s->openwire_sample_active=diagnostic_active;}
static uint8_t registers[256];
static int corrupt_reg=-1,corrupt_count;
static uint8_t auto_cc2_event;
static struct {uint16_t charge_current_a10,discharge_current_a10,cell_voltage_mv[32],pack_voltage_10mv,cell_max_mv,cell_min_mv,cell_delta_mv,cell_max_index,cell_min_index,temperature_x10[5],temperature_max_x10,temperature_min_x10,balance_bits_low,balance_bits_high;
 struct {struct {uint8_t charge_ocp,discharge_ocp;}bits;}fault_third;}g_bms_report;
static struct {struct {uint8_t charge_mos_status,discharge_mos_status;}bits;}g_bms_system_status;
void DVC1124_UpdataAfeConfig(void){}
uint8_t DVC1124_GetWriteAddress(void){return 0x40;}
static void dvc_note_comm_result(uint8_t x){(void)x;}
static uint16_t dvc_be16(const uint8_t *x){return ((uint16_t)x[0]<<8)|x[1];}
static int32_t dvc_sign_extend20(uint32_t x){return (x&0x80000)?(int32_t)(x|0xfff00000):(int32_t)x;}
static int32_t dvc_cc2_to_raw_current_ma(int32_t x){return x;}
static int32_t bms_config_calibrate_current(int32_t x){return x;}
static int32_t dvc_apply_boot_zero(int32_t x){return x;}
static void dvc_publish_current_report(int32_t x){(void)x;}
static uint16_t dvc_correct_cell_mv(uint16_t x,uint32_t cm){(void)cm;return x;}
static uint8_t dvc_ntc_resistance(uint16_t gp,uint16_t v,uint16_t rpu,uint32_t*r){(void)gp;(void)v;(void)rpu;*r=10000;return 1;}
static uint16_t dvc_ntc_temp_report(uint32_t r){(void)r;return 650;}
static void bms_error_clear(uint32_t x){(void)x;}
static void bms_error_raise(uint32_t x){(void)x;}
void bms_diag_driver(uint8_t x,uint8_t y){(void)x;(void)y;}
void bms_diag_trace(uint16_t e,uint32_t a,uint32_t b){(void)e;(void)a;(void)b;}
uint8_t DVC1124_ClearAlarmFlags(uint8_t m){registers[0]&=(uint8_t)~m;return 1;}
static uint32_t pm_get_32k_tick(void){return now;}
static uint32_t clock_time(void){return now*500u;}
static uint8_t clock_time_exceed(uint32_t t,uint32_t us){return (uint32_t)(clock_time()-t)>us*16u;}
static uint8_t dvc_write_balance_hw(uint32_t m){(void)m;return 1;}
uint8_t DVC1124_WriteRegisters(uint8_t r,const uint8_t *p,uint8_t n){memcpy(registers+r,p,n);return 1;}
uint8_t DVC1124_StartOpenWireCheck(void){registers[0x6d]|=4;return 1;}
static uint8_t dvc_refresh_balance_state(void){return 1;}
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
code+=s[s.index('#define DVC_ADC_MAX_AGE_TICKS'):s.index('static uint32_t s_snapshot_generation;')]
code+=function(s,'static uint8_t dvc_crc8(')
code+=function(s,'uint8_t DVC1124_ReadRegisters(')
code+=function(s,'static uint8_t dvc_boot_zero_wait_fresh_cc2(')
code+=function(s,'void DVC1124_OpenWireReset(')
code+=function(s,'uint8_t DVC1124_OpenWireStop(')
code+=function(s,'uint8_t DVC1124_OpenWireBegin(')
code+=function(s,'void DVC1124_OpenWirePoll(')
code+=function(s,'void DVC1124_App_AFEGet(')
recovery=read('bms/afe/dvc1124/dvc1124_bms.c')
code+='\n'+recovery[recovery.index('#define DVC_OCC_RECOVERY_TICKS'):recovery.index('static uint16_t dvc_get_configured_temperature')]
code+=function(recovery,'static uint8_t dvc_recover_current_faults(')
code+=r'''
static void acquire(uint32_t t,uint8_t event){now=t;registers[1]=event;DVC1124_App_AFEGet();}
static void reset(void){memset(&s_snapshot,0,sizeof(s_snapshot));memset(registers,0,sizeof(registers));memset(&s_openwire_result,0,sizeof(s_openwire_result));
 s_pending_adc_events=s_voltage_seen=s_current_seen=s_sample_pending=s_voltage_since_current=0;
 s_snapshot_generation=s_voltage_tick=s_current_tick=s_adc_wait_started=0;corrupt_reg=-1;corrupt_count=0;diagnostic_active=0;}
int main(void){
 reset();acquire(100,0);acquire(6500,0);assert(!s_snapshot_generation && !s_snapshot.valid && s_sample_pending);
 acquire(7000,0x40);assert(s_snapshot_generation==1 && s_snapshot.voltage_fresh && !s_snapshot.valid && s_sample_pending);
 acquire(8000,0x10);assert(s_snapshot.valid && s_snapshot.current_fresh && !s_sample_pending && s_snapshot.sample_tick_32k==8000);
 acquire(9000,0);assert(s_snapshot.valid && s_sample_pending && s_snapshot_generation==1 && s_snapshot.sample_tick_32k==8000);
 acquire(8000+512u*32u+1u,0);assert(!s_snapshot.valid && !s_sample_pending);
 /* RM p8 R6 bit1；R1 CST bit1不能冒充DSGF。 */
 reset();registers[6]=2;acquire(100,0x50);assert(s_snapshot.fet_status==2 && g_bms_system_status.bits.discharge_mos_status);
 registers[6]=0;acquire(200,0x52);assert(!s_snapshot.fet_status && !g_bms_system_status.bits.discharge_mos_status);
 /* 第一个读取已通过R1 CRC，后面的R2 CRC失败；重试R1已清零，事件仍须保留。 */
 reset();corrupt_reg=2;corrupt_count=1;acquire(100,0x50);
 assert(s_snapshot.valid && s_snapshot.voltage_fresh && s_snapshot.current_fresh && s_snapshot_generation==1);
 /* R1自身CRC坏，不能把未经验证的事件当作新采样。 */
 reset();corrupt_reg=1;corrupt_count=1;acquire(100,0x50);
 assert(!s_snapshot.valid && !s_snapshot_generation && !s_pending_adc_events);
 reset();acquire(0xfffff000u,0x50);acquire(0xfffff000u+512u*32u+1u,0);assert(!s_snapshot.valid);
 reset();registers[1]=0x10;assert(!dvc_boot_zero_wait_fresh_cc2()); /* 只有旧事件，等待后没有新转换。 */
 auto_cc2_event=1;assert(dvc_boot_zero_wait_fresh_cc2());assert(!s_pending_adc_events);auto_cc2_event=0;
 reset();acquire(100,0x50);registers[1]=0x40;assert(DVC1124_OpenWireBegin());
 acquire(6501,0x10);assert(s_openwire_result.state==DVC1124_OPENWIRE_WAITING); /* drain前事件不授权诊断。 */
 acquire(12901,0x50);assert(s_openwire_result.state==DVC1124_OPENWIRE_READY && s_openwire_result.valid);
 assert(!(registers[0x6d]&4)); /* 新VADC完成后才停止COW。 */
 reset();registers[DVC1124_REG_CELL1_H]=0x0d;registers[DVC1124_REG_CELL1_H+1]=0x48;
 acquire(100,0x50);assert(g_bms_report.cell_voltage_mv[0]==3400);
 assert(DVC1124_OpenWireBegin());diagnostic_active=1;
 registers[DVC1124_REG_CELL1_H]=registers[DVC1124_REG_CELL1_H+1]=0;
 acquire(12901,0x50);assert(s_openwire_result.valid && s_openwire_result.cell_mv[0]==0);
 assert(g_bms_report.cell_voltage_mv[0]==3400 && s_snapshot.cell_mv[0]==3400);
 assert(DVC1124_OpenWireStop() && !(registers[0x6d]&4));
 reset();acquire(100,0x50);assert(DVC1124_OpenWireBegin());
 now=100+901u*32u;DVC1124_OpenWirePoll();
 assert(s_openwire_result.state==DVC1124_OPENWIRE_ERROR && s_openwire_result.error==BMS_OW_ERR_TIMEOUT);
 assert(DVC1124_OpenWireStop());
 for(unsigned cst=0;cst<=6;++cst)for(unsigned driver=0;driver<=1;++driver){
  reset();memset(&s_current_recovery,0,sizeof(s_current_recovery));registers[6]=driver?2:0;
  acquire(100,(uint8_t)(0x50|cst));g_bms_report.fault_third.bits.discharge_ocp=1;
  dvc_observe_current_recovery(&s_snapshot,1,now);
  dvc_recover_current_faults(&s_snapshot,0,1,now);
  acquire(6500,(uint8_t)(0x50|cst));g_bms_report.fault_third.bits.discharge_ocp=0;
  dvc_observe_current_recovery(&s_snapshot,1,now);
  dvc_recover_current_faults(&s_snapshot,0,1,now);
  assert(s_current_recovery.discharge==driver); /* 真实解码→恢复，14个正交输入。 */
 }
 puts("PASS DVC ADC完成事件、异步通道、RC/CRC重试、R1与R6分离、缓存到期与tick回绕");return 0;
}
'''
out=run_c(code,name='dvc-adc-events')
evidence({'domain':'adc_events','trace':out.strip(),'boundary':'实际CRC/ReadRegisters/App_AFEGet函数体；I2C peer非芯片仿真，外围换算替身；512ms为项目活性约束'})
