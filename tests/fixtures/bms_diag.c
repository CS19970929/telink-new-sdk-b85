#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include "bms_diag.h"
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
struct PRT_E2ROM_PARAS{u16 u16VcellOvp_First;u16 rest[64];};
static struct {struct PRT_E2ROM_PARAS protect;}g_tParam;
static u32 tick, reads,writes;
u32 bms_diag_tick(void){return tick;}
static u16 u16be(const u8*p){return (u16)(((u16)p[0]<<8)|p[1]);}
static void put_u16be(u8*p,u16 v){p[0]=(u8)(v>>8);p[1]=(u8)v;}
static u16 mb_crc16(const u8*p,u32 n){u16 crc=0xffff;for(u32 i=0;i<n;i++){crc^=p[i];for(int k=0;k<8;k++)crc=(u16)((crc>>1)^((crc&1)?0xa001:0));}return crc;}
static int modbus_exception(u8 a,u8 f,u8 e,u8*r,u32*n){r[0]=a;r[1]=f|0x80;r[2]=e;u16 c=mb_crc16(r,3);r[3]=(u8)c;r[4]=(u8)(c>>8);*n=5;return a!=0;}
static int bms_afe_hw_access_modbus_on_frame(const u8*q,u32 n,u8*r,u32*l){writes++;return 0;}
static int read_event_log_frame(u8 a,u8 f,u16 reg,u16 qty,u8*r,u32*l){reads++;return 0;}
static u16 read_reg(u16 r){reads++;return 0;}
static int reg_requires_param_save(u16 r){return 0;}
static u8 write_reg(u16 r,u16 v){writes++;return 0;}
static u8 commit_protection_update(struct PRT_E2ROM_PARAS*p){writes++;return 0;}
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
 bms_diag_runtime_soc(73u,72u,2u,74u,69u,79u,90u,600u,1u,1u,580u,200u);
 bms_soc_diag_t sx={0};
 sx.chemistry=1;sx.profile_id=1;sx.profile_version=2;sx.endpoint_state=1;
 sx.endpoint_event_flags=3;sx.nominal_capacity_0p1ah=1000;
 sx.effective_capacity_0p1ah=950;sx.remaining_capacity_0p1ah=700;
 sx.filtered_current_ma=-10000;sx.current_variation_ma=120;
 sx.time_to_empty_min=180;sx.time_to_full_min=240;sx.eta_state=2;
 sx.eta_direction=1;sx.eta_confidence=88;sx.eta_valid=1;sx.soh=95;
 sx.soh_source=2;sx.soh_confidence=100;sx.capacity_learning_enable=1;
 sx.capacity_learning_candidate_valid=1;sx.candidate_capacity_0p1ah=910;
 sx.valid_learning_count=4;sx.rejected_learning_count=2;
 sx.last_learning_reject_reason=12;sx.capacity_learning_confidence=75;
 sx.ocv_cell_mv=3400;
 bms_diag_runtime_soc_extended(&sx);
 bms_diag_runtime_pm(0u,DIAG_PM_BLOCK_CURRENT|DIAG_PM_BLOCK_BUS,3u,120u,1u,0u,500u);
 bms_diag_runtime_faults(1u,2u,4u);
 bms_diag_runtime_mode(1u);
 assert(bms_diag_cached_word(193)==3u);
 assert((int32_t)((uint32_t)bms_diag_cached_word(194)|((uint32_t)bms_diag_cached_word(195)<<16))==-123);
 assert((int32_t)((uint32_t)bms_diag_cached_word(196)|((uint32_t)bms_diag_cached_word(197)<<16))==456);
 assert(bms_diag_cached_word(200)==200u&&bms_diag_cached_word(202)==73u&&bms_diag_cached_word(203)==72u);
 assert(bms_diag_cached_word(215)==0u&&bms_diag_cached_word(221)==500u);
 assert(bms_diag_cached_word(222)==1u&&bms_diag_cached_word(223)==2u&&bms_diag_cached_word(224)==4u);
 assert(bms_diag_cached_word(225)==1u);
 assert(bms_diag_cached_word(226)==1u&&bms_diag_cached_word(228)==2u);
 assert((int32_t)((uint32_t)bms_diag_cached_word(234)|((uint32_t)bms_diag_cached_word(235)<<16))==-10000);
 assert(bms_diag_cached_word(237)==180u&&bms_diag_cached_word(238)==240u);
 assert((bms_diag_cached_word(239)&0x0fu)==2u);
 assert(bms_diag_cached_word(243)==910u&&bms_diag_cached_word(246)==12u);
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
 assert(bms_diag_cached_word(12)==64&&bms_diag_cached_word(10)>0);
 assert(bms_diag_cached_word(37)==DIAG_LAYOUT);bms_diag_result(0,DIAG_OK);assert(bms_diag_cached_word(38)==DIAG_LAYOUT);
 assert(bms_diag_read(0x2d88,120,bytes));assert(!bms_diag_read(0x2d89,120,bytes));
 puts("PASS diagnostic ingress: endian, bounds, crossing writes atomic rejection, CRC, broadcast, no I/O, boot freeze, ring overwrite/tick wrap");
 return 0;
}
