#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bms_crc.h"
#include "bms_protection_params.h"
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;
/* CONSTANTS */
#define BMS_LOG(...) ((void)0)
bms_protection_params_t g_bms_protection_params;
static unsigned writes, calls, iteration;
static const char *scenario;
static void check(unsigned actual,unsigned expected)
{
    ++calls;if(actual!=expected){fprintf(stderr,"协议失败 scenario=%s iteration=%u expected=%u actual=%u writes=%u\n",scenario,iteration,expected,actual,writes);exit(1);}
}
static int bms_afe_hw_access_modbus_on_frame(const u8*r,u32 n,u8*s,u32*l){*l=0;return 0;}
static int bms_debug_log_overlaps(u16 r,u16 n){return 0;}
static int bms_diag_overlaps(u16 r,u16 n){return 0;}
static int bms_debug_log_read(u16 r,u16 n,u8*d){return 0;}
static int bms_diag_read(u16 r,u16 n,u8*d){return 0;}
static u16 bms_event_log_read_reg(u16 r){return (u16)(r^0x55aa);}
static int read_address_supported(u16 r){return r>=0x2100&&r<=0x2140;}
static u16 read_reg(u16 r){return (u16)(r^0x55aa);}
static int reg_requires_param_save(u16 r){return r>=0x2100&&r<=0x2140;}
static u8 commit_protection_update(const bms_protection_params_t*c){++writes;g_bms_protection_params=*c;return 0;}
static u8 write_reg(u16 r,u16 v){++writes;return 2;}
static u8 afe_hw_profile_write_block(const u8*d,u16 n){++writes;return 0;}
static int afe_hw_profile_is_reg(u16 r){return r>=BMS_AFE_HW_REQUESTED_REG_BASE&&r<BMS_AFE_HW_REQUESTED_REG_BASE+BMS_AFE_HW_REQUESTED_REG_COUNT;}
static u8 bms_parameter_write(u16 r,u16 n,const u8*d){++writes;return 2;}
static int btname_modbus_on_write_holding(u16 r,u16 n,const uint16_t*d){++writes;return 1;}

/* PARSER */

static uint32_t random_state=20261005u;
static uint32_t random_word(void){random_state=random_state*1664525u+1013904223u;return random_state;}
static void crc_frame(u8*r,unsigned n){u16 c=mb_crc16(r,n-2);r[n-2]=(u8)c;r[n-1]=(u8)(c>>8);}
static unsigned run(const u8*req,unsigned n)
{
    u8 guarded[MODBUS_RTU_FRAME_CAPACITY+32];u32 length=0;
    memset(guarded,0xa5,sizeof(guarded));
    int result=modbus_on_frame(req,n,guarded+16,&length);
    check(length<=MODBUS_RTU_FRAME_CAPACITY,1);
    for(unsigned i=0;i<16;i++){check(guarded[i],0xa5);check(guarded[16+MODBUS_RTU_FRAME_CAPACITY+i],0xa5);}
    if(result){check(length>=4,1);check(mb_crc16(guarded+16,length),0);}
    return (unsigned)result;
}
int main(void)
{
    u8 req[MODBUS_RTU_FRAME_CAPACITY+1];
    scenario="合法读与数量边界";
    for(iteration=0;iteration<=130;iteration++){
        memset(req,0,sizeof(req));req[0]=1;req[1]=3;req[2]=0x21;req[4]=(u8)(iteration>>8);req[5]=(u8)iteration;
        crc_frame(req,8);writes=0;check(run(req,8),1);check(writes,0);
    }
    scenario="合法写的全部半包与坏 CRC";
    req[0]=1;req[1]=6;req[2]=0x21;req[3]=0;req[4]=1;req[5]=2;crc_frame(req,8);
    for(iteration=0;iteration<8;iteration++){writes=0;check(run(req,iteration),0);check(writes,0);}
    for(iteration=0;iteration<8;iteration++)for(unsigned bit=0;bit<8;bit++){
        req[iteration]^=(u8)(1u<<bit);writes=0;check(run(req,8),0);check(writes,0);req[iteration]^=(u8)(1u<<bit);
    }
    scenario="重复单寄存器写及广播";
    for(iteration=0;iteration<100;iteration++){writes=0;check(run(req,8),1);check(writes,1);check(g_bms_protection_params.cell_ovp_first_mv,0x0102);}
    req[0]=0;crc_frame(req,8);writes=0;check(run(req,8),0);check(writes,1);
    scenario="跨所有者多写拒绝且无副作用";
    memset(req,0,sizeof(req));req[0]=1;req[1]=0x10;req[2]=0x21;req[3]=0x40;req[5]=2;req[6]=4;crc_frame(req,13);
    writes=0;check(run(req,13),1);check(writes,0);
    scenario="确定性随机与有效 CRC 畸形帧";
    for(iteration=0;iteration<100000;iteration++){
        unsigned length=random_word()%(sizeof(req)+1u);
        for(unsigned i=0;i<sizeof(req);i++)req[i]=(u8)(random_word()>>24);
        if(length>=4&&length<=sizeof(req)){
            req[0]=iteration%3==0?0:1;
            static const u8 funcs[]={3,6,0x10,0x7f,0x41,0xff};req[1]=funcs[iteration%6];
            if(iteration&1u)crc_frame(req,length);
        }
        writes=0;run(req,length);
        if(length<4||length>MODBUS_RTU_FRAME_CAPACITY)check(writes,0);
        if(length>=4&&length<=MODBUS_RTU_FRAME_CAPACITY&&mb_crc16(req,length)!=0)check(writes,0);
    }
    scenario="空指针";u8 rsp[MODBUS_RTU_FRAME_CAPACITY];u32 n=0;
    check(modbus_on_frame(NULL,8,rsp,&n),0);check(modbus_on_frame(req,8,NULL,&n),0);check(modbus_on_frame(req,8,rsp,NULL),0);
    printf("seed=20261005 frames=100000 assertions=%u\n",calls);return 0;
}
