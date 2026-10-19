"""真实寄存器调试 TU 的总线边界、CAS、保留位与 RC 独立场景；不操作板子。"""
import os
from validation_support import run_c, evidence

product = os.environ.get("BMS_PRODUCT", "d014")
code = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "bms_afe_registers.h"
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
#include "dvc1124.h"
#define CONFIG_REG 0x59u
#define RC_REG 0x01u
#define RO_REG 0x02u
#define FET_REG 0x51u
uint8_t DVC1124_ReadRegisters(uint8_t reg,uint8_t *out,uint8_t count);
#else
#include "sh3673520.h"
#define CONFIG_REG 0x4Du
#define RC_REG 0x59u
#define RO_REG 0x60u
#define FET_REG 0x41u
#endif
static uint8_t regs[256],held,access_ok=1,io_ok=1;
static unsigned reads,writes;
uint8_t bms_afe_register_debug_held(void) { return held; }
uint8_t bms_afe_register_debug_begin(void) { held=1;return 1; }
uint8_t bms_afe_register_debug_restore(void) { if(!io_ok)return 0;held=0;return 1; }
uint8_t bms_afe_register_bus_access_allowed(void) { return access_ok; }
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
uint8_t DVC1124_ReadRegisters(uint8_t reg,uint8_t *out,uint8_t count) {
    assert(count==1);reads++;if(!io_ok)return 0;*out=regs[reg];return 1;
}
uint8_t DVC1124_WriteRegisters(uint8_t reg,const uint8_t *in,uint8_t count) {
    assert(count==1);writes++;if(!io_ok)return 0;regs[reg]=*in;return 1;
}
uint8_t DVC1124_ReadRegisterOnce(uint8_t reg,uint8_t *out) {
    return DVC1124_ReadRegisters(reg,out,1u);
}
#else
sh3673520_status_t SH3673520_ReadReg(uint8_t reg,uint8_t *out) {
    reads++;if(!io_ok)return SH3673520_ERR_SPI;*out=regs[reg];return SH3673520_OK;
}
sh3673520_status_t SH3673520_WriteReg(uint8_t reg,uint8_t value) {
    writes++;if(!io_ok)return SH3673520_ERR_SPI;regs[reg]=value;return SH3673520_OK;
}
sh3673520_status_t SH3673520_ReadRegOnce(uint8_t reg,uint8_t *out) {
    return SH3673520_ReadReg(reg,out);
}
#endif
int main(void) {
    uint8_t req[11]={1,0x42,0x10},payload[12],length;
    assert(bms_afe_register_command(req,5,payload,&length)==0 && length==12 && payload[0]==1);
    req[2]=0x13;req[5]=CONFIG_REG;req[6]=15;req[7]=0x21;req[8]=3;regs[CONFIG_REG]=0x21;
    assert(bms_afe_register_command(req,11,payload,&length)==8 && reads==0 && writes==0);
    held=1;
    assert(bms_afe_register_command(req,10,payload,&length)==1 && reads==0 && writes==0);
    access_ok=0;
    assert(bms_afe_register_command(req,11,payload,&length)==7 && reads==0 && writes==0);
    access_ok=1;req[7]=0x20;
    assert(bms_afe_register_command(req,11,payload,&length)==6 && writes==0 && regs[CONFIG_REG]==0x21);
    req[7]=0x21;
    assert(bms_afe_register_command(req,11,payload,&length)==0 && length==2 && payload[1]==0x23);
    assert(regs[CONFIG_REG]==0x23 && writes==1); /* 上半字节必须保持。 */
    req[5]=RO_REG;
    assert(bms_afe_register_command(req,11,payload,&length)==5 && writes==1);
    req[5]=FET_REG;req[6]=3;
    assert(bms_afe_register_command(req,11,payload,&length)==5 && writes==1);
    req[2]=0x11;req[5]=RC_REG;req[6]=0;
    unsigned before=reads;
    assert(bms_afe_register_command(req,9,payload,&length)==5 && reads==before);
    req[6]=1;held=0;
    assert(bms_afe_register_command(req,9,payload,&length)==5 && reads==before);
    held=1;
    assert(bms_afe_register_command(req,9,payload,&length)==0 && reads==before+1);
    io_ok=0;
    assert(bms_afe_register_command(req,9,payload,&length)==7);
    req[2]=0x14;
    assert(bms_afe_register_command(req,7,payload,&length)==7 && held);
    io_ok=1;
    assert(bms_afe_register_command(req,7,payload,&length)==0 && !held);
    puts("AFE register debug: actual TU, CAS/reserved/RC/I/O/restore boundaries");
    return 0;
}
'''
run_c(code, sources=("bms/core/bms_afe_registers.c",),
      flags=("-DBMS_AFE_REGISTER_DEBUG_ENABLE=1",), name="afe-register-debug")

# 在产品头之后注入一条固定字段，编译真实 TU；不改工作树默认值。
presets = r'''
#include <stdint.h>
#include <assert.h>
#include "bms_afe_backend.h"
#undef BMS_AFE_REGISTER_PRESET_COUNT
#undef BMS_AFE_REGISTER_PRESETS
#define BMS_AFE_REGISTER_PRESET_COUNT 1u
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
#include "dvc1124.h"
#define PRESET_ADDRESS 0x52u
#else
#include "sh3673520.h"
#define PRESET_ADDRESS 0x46u
#endif
#define BMS_AFE_REGISTER_PRESETS(X) X(PRESET_ADDRESS,1u,1u)
#include "bms_afe_registers.c"
static uint8_t value=0xA0u, io_ok=1;
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
uint8_t DVC1124_ReadRegisters(uint8_t reg,uint8_t *out,uint8_t count){assert(reg==PRESET_ADDRESS&&count==1);if(!io_ok)return 0;*out=value;return 1;}
uint8_t DVC1124_WriteRegisters(uint8_t reg,const uint8_t *in,uint8_t count){assert(reg==PRESET_ADDRESS&&count==1);if(!io_ok)return 0;value=*in;return 1;}
#else
sh3673520_status_t SH3673520_ReadReg(uint8_t reg,uint8_t *out){assert(reg==PRESET_ADDRESS);if(!io_ok)return SH3673520_ERR_SPI;*out=value;return SH3673520_OK;}
sh3673520_status_t SH3673520_WriteReg(uint8_t reg,uint8_t in){assert(reg==PRESET_ADDRESS);if(!io_ok)return SH3673520_ERR_SPI;value=in;return SH3673520_OK;}
#endif
int main(void){assert(bms_afe_register_config_apply()&&value==0xA1u);io_ok=0;assert(!bms_afe_register_config_apply());return 0;}
'''
run_c(presets, name="afe-source-register-preset")
evidence(dict(domain="afe_register_debug", product=product, actual_translation_unit=True,
              board_validation=False))
