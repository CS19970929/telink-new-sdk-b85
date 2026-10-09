#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include "bms_diag.h"
#include "bms_debug_log.h"
#include "bms_event_log.h"
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;
#define MODBUS_RTU_FRAME_CAPACITY 256u
#define MB_ADDR 1u
#define MB_EX_ILLEGAL_FUNCTION 1u
#define MB_EX_ILLEGAL_ADDRESS 2u
#define MB_EX_ILLEGAL_VALUE 3u
#define MB_EX_DEVICE_FAILURE 4u
#define BMS_AFE_HW_ACCESS_MODBUS_FUNC 0x42u
#define BMS_AFE_HW_REQUESTED_REG_BASE 0x2500u
#define BMS_AFE_HW_PROFILE_WORD_COUNT 35u
#define BTNAME_REG_BASE 0x100u
#define BTNAME_REG_WORDS 12u
typedef struct {u16 cell_ovp_first_mv;u16 rest[64];} bms_protection_params_t;
static bms_protection_params_t g_bms_protection_params;
static u32 tick, reads,writes;
u32 bms_diag_tick(void){return tick;}
static u16 u16be(const u8*p){return (u16)(((u16)p[0]<<8)|p[1]);}
static void put_u16be(u8*p,u16 v){p[0]=(u8)(v>>8);p[1]=(u8)v;}
static u16 mb_crc16(const u8*p,u32 n){u16 crc=0xffff;for(u32 i=0;i<n;i++){crc^=p[i];for(int k=0;k<8;k++)crc=(u16)((crc>>1)^((crc&1)?0xa001:0));}return crc;}
static int modbus_exception(u8 a,u8 f,u8 e,u8*r,u32*n){r[0]=a;r[1]=f|0x80;r[2]=e;u16 c=mb_crc16(r,3);r[3]=(u8)c;r[4]=(u8)(c>>8);*n=5;return a!=0;}
static int bms_afe_hw_access_modbus_on_frame(const u8*q,u32 n,u8*r,u32*l){writes++;return 0;}
u16 bms_event_log_read_reg(u16 reg){reads++;return 0;}
static u16 read_reg(u16 r){reads++;return 0;}
static int reg_requires_param_save(u16 r){return 0;}
static u8 write_reg(u16 r,u16 v){writes++;return 0;}
static u8 commit_protection_update(bms_protection_params_t*p){writes++;return 0;}
static u8 afe_hw_profile_write_block(const u8*p,u16 n){writes++;return 0;}
static int afe_hw_profile_is_reg(u16 r){return 0;}
static int read_address_supported(u16 r){return r>=0x2100 && r<=0x2140;}
static u8 bms_parameter_write(u16 r,u16 n,const u8*p){writes++;return 0;}
static int btname_modbus_on_write_holding(u16 a,u16 n,const u16*p){writes++;return 1;}
/* MODBUS */
static u32 request(u8*q,u8 addr,u8 f,u16 start,u16 count){
 q[0]=addr;q[1]=f;put_u16be(q+2,start);put_u16be(q+4,count);
 u32 n=6;if(f==0x10){q[6]=(u8)(count*2);n=7+count*2;memset(q+7,0,count*2);}
 u16 c=mb_crc16(q,n);q[n]=(u8)c;q[n+1]=(u8)(c>>8);return n+2;
}
int main(void){
 u8 q[256],r[256],bytes[250];u32 n,l;
 bms_diag_init();bms_diag_boot_u32(18,0x12345678u);
 assert(bms_diag_cached_word(2)==BMS_DIAG_CAPABILITIES);
 assert(bms_diag_cached_word(BMS_DIAG_RUNTIME_OFFSET)==BMS_DIAG_RUNTIME_VERSION);
 bms_diag_runtime_sample(1u,-123,456,0x11223344u,1u);
 bms_soc_diag_t sx={0};
 sx.soc_estimate=73;sx.soc_display=72;sx.ocv_state=2;sx.ocv_center=74;
 sx.ocv_low=69;sx.ocv_high=79;sx.ocv_confidence=90;sx.rest_seconds=600;sx.current_deadband_ma=200;
 sx.chemistry=1;sx.profile_id=1;sx.profile_version=2;sx.endpoint_state=1;
 sx.endpoint_event_flags=3;sx.nominal_capacity_0p1ah=1000;
 sx.effective_capacity_0p1ah=950;sx.remaining_capacity_0p1ah=700;
 sx.filtered_current_ma=-10000;sx.current_variation_ma=120;
 sx.time_to_empty_min=180;sx.time_to_full_min=240;sx.eta_state=2;
 sx.eta_direction=1;sx.eta_confidence=88;sx.eta_valid=1;sx.soh=95;
 sx.soh_source=2;sx.soh_confidence=100;
 sx.ocv_cell_mv=3400;
 bms_diag_runtime_soc(&sx);
 bms_diag_runtime_pm(0u,DIAG_PM_BLOCK_CURRENT|DIAG_PM_BLOCK_BUS,3u,120u,1u,0u,500u);
 bms_diag_runtime_faults(1u,2u,4u);
 assert(bms_diag_cached_word(225u)==0u);
 assert(bms_diag_cached_word(193)==3u);
 assert((int32_t)((uint32_t)bms_diag_cached_word(194)|((uint32_t)bms_diag_cached_word(195)<<16))==-123);
 assert((int32_t)((uint32_t)bms_diag_cached_word(196)|((uint32_t)bms_diag_cached_word(197)<<16))==456);
 assert(bms_diag_cached_word(200)==200u&&bms_diag_cached_word(202)==73u&&bms_diag_cached_word(203)==72u);
 assert(bms_diag_cached_word(215)==0u&&bms_diag_cached_word(221)==500u);
 assert(bms_diag_cached_word(222)==1u&&bms_diag_cached_word(223)==2u&&bms_diag_cached_word(224)==4u);
 assert(bms_diag_cached_word(225)==0u);
 assert(bms_diag_cached_word(226)==1u&&bms_diag_cached_word(228)==2u);
 assert((int32_t)((uint32_t)bms_diag_cached_word(234)|((uint32_t)bms_diag_cached_word(235)<<16))==-10000);
 assert(bms_diag_cached_word(237)==180u&&bms_diag_cached_word(238)==240u);
 assert((bms_diag_cached_word(239)&0x0fu)==2u);
 const unsigned reserved[]={210,211,212,243,244,245,246,247};
 for(unsigned i=0;i<sizeof(reserved)/sizeof(reserved[0]);++i)assert(bms_diag_cached_word(reserved[i])==0);
 assert((bms_diag_cached_word(230)&3u)==0u && (bms_diag_cached_word(230)&4u)==4u);
 {uint16_t sequence=bms_diag_cached_word(4);bms_diag_runtime_soc(NULL);
  bms_diag_runtime_soc(&sx);assert(bms_diag_cached_word(4)==sequence);}
 bms_diag_runtime_pm(1u,0u,0u,0u,1u,0u,500u);
 { uint32_t trace_before=(uint32_t)bms_diag_cached_word(8)|((uint32_t)bms_diag_cached_word(9)<<16);
   bms_diag_runtime_pm(0u,DIAG_PM_BLOCK_SAMPLE_PENDING,0u,0u,1u,1u,500u);
   bms_diag_runtime_pm(1u,0u,0u,0u,1u,0u,500u);
   assert(((uint32_t)bms_diag_cached_word(8)|((uint32_t)bms_diag_cached_word(9)<<16))==trace_before);
 }
 n=request(q,1,3,0x2a12,2);assert(modbus_on_frame(q,n,r,&l));
 assert(l==9 && r[3]==0x56 && r[4]==0x78 && r[5]==0x12 && r[6]==0x34);
 u16 seq=bms_diag_cached_word(4);for(int i=0;i<100;i++)assert(modbus_on_frame(q,n,r,&l));
 assert(reads==0&&writes==0&&seq==bms_diag_cached_word(4));
 u16 bad[]={0x29ff,0x2aff,0x2dff,0xffff};
 for(unsigned i=0;i<4;i++){n=request(q,1,3,bad[i],2);assert(modbus_on_frame(q,n,r,&l));assert(r[1]==0x83&&r[2]==2);}
 for(unsigned addr=0;addr<2;addr++)for(unsigned func=6;func<=16;func+=10){
  n=request(q,(u8)addr,(u8)func,func==16?0x29ff:0x2a00,2);
  int replied=modbus_on_frame(q,n,r,&l);assert(replied==(addr!=0));assert(writes==0&&r[2]==2);
 }
 n=request(q,1,3,0x2a00,0);assert(modbus_on_frame(q,n,r,&l)&&r[2]==3);
 n=request(q,1,3,0x2a00,126);assert(modbus_on_frame(q,n,r,&l)&&r[2]==3);
 q[n-1]^=1;assert(!modbus_on_frame(q,n,r,&l));
 n=request(q,1,0x10,0x2140,2);assert(modbus_on_frame(q,n,r,&l)&&r[2]==2&&writes==0);
 n=request(q,1,0x10,0x1005,2);assert(modbus_on_frame(q,n,r,&l)&&r[2]==2&&writes==0);
 n=request(q,1,3,0x7777,1);assert(modbus_on_frame(q,n,r,&l)&&r[2]==2&&writes==0);
 bms_diag_attempt(0);bms_diag_result(0,DIAG_LAYOUT);bms_diag_freeze_boot();
 tick=0xfffffff0u;for(unsigned i=0;i<100;i++){bms_diag_trace(DIAG_EV_STORAGE,i,~i);tick+=32;}
#if BMS_DIAG_TRACE_ENABLE
 assert(bms_diag_cached_word(12)==64&&bms_diag_cached_word(10)>0);
 assert((bms_diag_cached_word(2)&BMS_DIAG_CAP_TRACE)!=0);
#else
 assert(bms_diag_cached_word(8)==0&&bms_diag_cached_word(9)==0);
 assert(bms_diag_cached_word(10)==0&&bms_diag_cached_word(11)==0&&bms_diag_cached_word(12)==0);
 assert((bms_diag_cached_word(2)&BMS_DIAG_CAP_TRACE)==0);
 assert(bms_diag_read(BMS_DIAG_TRACE_BASE,120,bytes));
 for(unsigned i=0;i<240;i++)assert(bytes[i]==0);
#endif
 assert(bms_diag_cached_word(37)==DIAG_LAYOUT);bms_diag_result(0,DIAG_OK);assert(bms_diag_cached_word(38)==DIAG_LAYOUT);
 assert(bms_diag_read(0x2d88,120,bytes));assert(!bms_diag_read(0x2d89,120,bytes));
 /* Exercise the additive runtime-log window through real ingress, never through generic read/write. */
 reads=0;writes=0;
 n=request(q,1,3,BMS_DEBUG_LOG_BASE,16);assert(modbus_on_frame(q,n,r,&l));
 assert(l==37 && r[3]==0x4c && r[4]==0x47 && r[8]==BMS_DEBUG_LOG_ENABLE);
 for(unsigned addr=0;addr<2;addr++)for(unsigned func=6;func<=16;func+=10){
  n=request(q,(u8)addr,(u8)func,func==16?BMS_DEBUG_LOG_BASE-1:BMS_DEBUG_LOG_BASE,2);
  assert(modbus_on_frame(q,n,r,&l)==(addr!=0));assert(writes==0 && r[2]==2);
 }
 n=request(q,1,3,BMS_DEBUG_LOG_END-1,2);assert(modbus_on_frame(q,n,r,&l)&&r[2]==2);
 n=request(q,0,3,BMS_DEBUG_LOG_BASE,16);assert(!modbus_on_frame(q,n,r,&l));
 assert(reads==0 && writes==0);
 /* 休眠快照：倒计时、门禁、回绕、只读和跨边界写入拒绝。 */
 tick=0xfffffff0u;
 bms_diag_sleep(DIAG_SLEEP_REASON_LOW,0u,12000u,60000u,0u,1u);
 tick=32u;
 n=request(q,1,3,BMS_DIAG_SLEEP_BASE,BMS_DIAG_SLEEP_WORDS);
 assert(modbus_on_frame(q,n,r,&l) && l==45u && r[3]==0x53 && r[4]==0x4c);
 assert(u16be(r+7)==DIAG_SLEEP_COUNTING && u16be(r+9)==DIAG_SLEEP_REASON_LOW);
 assert(u16be(r+27)==48000u && u16be(r+29)==0u);
 bms_diag_sleep(DIAG_SLEEP_REASON_LOW,DIAG_SLEEP_BLOCK_FIXED_UART,60000u,60000u,3000u,0u);
 assert(modbus_on_frame(q,n,r,&l) && u16be(r+7)==DIAG_SLEEP_BLOCKED);
 assert(u16be(r+15)==DIAG_SLEEP_BLOCK_FIXED_UART && u16be(r+31)==3000u);
 bms_diag_sleep_committed();
 assert(modbus_on_frame(q,n,r,&l) && u16be(r+7)==DIAG_SLEEP_COMMITTED && u16be(r+27)==0u);
 for(unsigned func=6;func<=16;func+=10){
  n=request(q,1,(u8)func,func==16?BMS_DIAG_SLEEP_BASE-1:BMS_DIAG_SLEEP_BASE,2);
  assert(modbus_on_frame(q,n,r,&l) && r[2]==2 && writes==0);
 }
 n=request(q,1,3,BMS_DIAG_SLEEP_END-1,2);
 assert(modbus_on_frame(q,n,r,&l) && r[2]==2);
 assert(reads==0 && writes==0);
 /* MOS 独立历史：采样间短暂关/开、有效性与背景、只读和生产关闭。 */
 bms_diag_init();
 bms_diag_boot_word(14u,0x3510u);
 bms_diag_afe_failure(DIAG_AFE_FAIL_CADC_AGE,0,0x101u,32u,12801u,64u,22u,44u);
 bms_diag_mos(3u,0u,0u); bms_diag_command(3u,1u); bms_diag_driver(3u,1u);
 bms_diag_runtime_sample(1u,-123,-123,tick,0u); bms_diag_mos_capture();
 n=request(q,1,3,BMS_DIAG_MOS_BASE,48u);
 assert(modbus_on_frame(q,n,r,&l) && l==101u && u16be(r+3)==0x4d48u);
#if BMS_DIAG_TRACE_ENABLE
 assert(u16be(r+7)==1u && u16be(r+15)==1u); /* enabled / latest sequence */
 assert(bms_diag_read(BMS_DIAG_AFE_FAILURE_BASE,32u,bytes));
 assert(u16be(bytes)==0x4146u && u16be(bytes+4)==1u && u16be(bytes+30)==32u);
 assert(u16be(bytes+32)==1u && u16be(bytes+36)==1u && u16be(bytes+44)==DIAG_AFE_FAIL_CADC_AGE);
 assert(u16be(bytes+48)==0x101u && u16be(bytes+52)==12801u);
 assert(bms_diag_read(BMS_DIAG_AFE_FAILURE_BASE+32u,16u,bytes));
 assert(u16be(bytes)==1u && u16be(bytes+12)==DIAG_AFE_FAIL_CADC_AGE);
 tick=0xfffffff0u;
 bms_diag_runtime_sample(1u,999,999,tick,0u); bms_diag_mos_capture();
 assert(modbus_on_frame(q,n,r,&l) && u16be(r+15)==1u); /* current-only does not fill ring */
 bms_diag_driver(2u,1u); bms_diag_mos_capture();
 tick=32u; bms_diag_driver(3u,1u); bms_diag_mos_capture();
 n=request(q,1,3,BMS_DIAG_MOS_BASE+48u+32u,32u);
 assert(modbus_on_frame(q,n,r,&l) && u16be(r+3)==2u && u16be(r+19)==2u);
 assert(u16be(r+59)==3u && u16be(r+61)==1u); /* previous driver/valid retained */
 bms_diag_driver(0u,0u); bms_diag_mos_capture();
 n=request(q,1,3,BMS_DIAG_MOS_BASE,48u);
 assert(modbus_on_frame(q,n,r,&l) && u16be(r+15)==4u && u16be(r+53)==0u);
 for(unsigned i=0;i<20;i++){ bms_diag_driver((uint8_t)(i&1u),1u);bms_diag_mos_capture(); }
 assert(modbus_on_frame(q,n,r,&l) && u16be(r+13)==8u && u16be(r+19)>0u);
#else
 assert(u16be(r+7)==0u && (bms_diag_cached_word(2)&BMS_DIAG_CAP_MOS_HISTORY)==0u);
 for(unsigned i=16u;i<48u;i++) assert(u16be(r+3u+2u*i)==0u);
#endif
 for(unsigned func=6u;func<=16u;func+=10u){
  n=request(q,1,(u8)func,func==16u?BMS_DIAG_MOS_BASE-1u:BMS_DIAG_MOS_BASE,2u);
  assert(modbus_on_frame(q,n,r,&l) && r[2]==2u && writes==0u);
 }
 n=request(q,1,3,BMS_DIAG_MOS_END-1u,2u);
 assert(modbus_on_frame(q,n,r,&l) && r[2]==2u && reads==0u && writes==0u);
 for(unsigned func=6u;func<=16u;func+=10u){
  n=request(q,1,(u8)func,func==16u?BMS_DIAG_AFE_FAILURE_BASE-1u:BMS_DIAG_AFE_FAILURE_BASE,2u);
  assert(modbus_on_frame(q,n,r,&l)&&r[2]==2u&&writes==0u);
 }
 assert(!bms_diag_read(BMS_DIAG_AFE_FAILURE_END-1u,2u,bytes));
 assert(!bms_diag_read(BMS_DIAG_AFE_FAILURE_BASE,126u,bytes));
 assert(!bms_diag_read(BMS_DIAG_AFE_FAILURE_BASE,1u,NULL));
#if !BMS_DIAG_TRACE_ENABLE
 assert(bms_diag_read(BMS_DIAG_AFE_FAILURE_BASE,32u,bytes));
 assert(u16be(bytes+4)==0u);
 for(unsigned i=16u;i<32u;i++) assert(u16be(bytes+2u*i)==0u);
#endif
 /* 独立断线快照：能力、生产可读、低字优先、零 I/O、跨范围写入拒绝。 */
 uint16_t ow[BMS_DIAG_OPENWIRE_WORDS]={0};ow[0]=0x4f57;ow[1]=1;ow[6]=0x5678;ow[7]=0x1234;
 bms_diag_openwire(ow);
 assert(bms_diag_cached_word(2)&BMS_DIAG_CAP_OPENWIRE);
 n=request(q,1,3,BMS_DIAG_OPENWIRE_BASE,BMS_DIAG_OPENWIRE_WORDS);
 assert(modbus_on_frame(q,n,r,&l) && l==101 && u16be(r+3)==0x4f57 && u16be(r+15)==0x5678 && u16be(r+17)==0x1234);
 for(unsigned func=6;func<=16;func+=10){
  n=request(q,1,(u8)func,func==16?BMS_DIAG_OPENWIRE_BASE-1:BMS_DIAG_OPENWIRE_BASE,2);
  assert(modbus_on_frame(q,n,r,&l)&&r[2]==2&&writes==0);
 }
 n=request(q,1,3,BMS_DIAG_OPENWIRE_END-1,2);
 assert(modbus_on_frame(q,n,r,&l)&&r[2]==2&&reads==0&&writes==0);
 puts("PASS diagnostic ingress: endian, bounds, crossing writes atomic rejection, CRC, broadcast, no I/O, boot freeze, MOS history, openwire, ring overwrite/tick wrap");
 return 0;
}
