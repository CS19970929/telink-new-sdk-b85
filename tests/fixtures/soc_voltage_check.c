/* 只检验生产诊断函数的推进和拒绝路径；寄存器/时间替身不能验证电气覆盖。 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#define BMS_FEATURE_BALANCE_SUPPORTED 1
#define BMS_PRODUCT_CELL_COUNT 16u
#define SH3673510_BOARD_CELL_COUNT 16u
static uint32_t now;
static unsigned operation,fail_at;
static uint8_t control,flag,abnormal;
static uint32_t bms_time_ms(void){return now;}
static uint8_t io_ok(void){return ++operation!=fail_at;}
static uint8_t bms_afe_driver_set_balance_mask(uint32_t mask,const uint32_t *known){assert(!mask&&!known);return io_ok();}
#if CHECK_DVC
#include "dvc1124_reg.h"
typedef struct {uint8_t voltage_fresh;} dvc1124_snapshot_t;
static struct {uint16_t cell_voltage_mv[24];}g_bms_report;
static uint8_t DVC1124_WriteRegisterFieldSafe(uint8_t reg,uint8_t mask,uint8_t shift,uint8_t value){
 assert(reg==0x6d&&mask==4u&&shift==2u);if(!io_ok())return 0;control=(control&~mask)|(value<<shift);return 1;
}
static uint8_t DVC1124_ReadRegisters(uint8_t reg,uint8_t *bytes,uint8_t count){assert(reg==0x6d&&count==1u);*bytes=control;return io_ok();}
#else
#include "sh3673520.h"
#include "sh3673520_reg.h"
typedef struct {uint8_t flag2;}sh3673510_control_status_t;
static sh3673520_status_t SH3673520_ReadReg(uint8_t reg,uint8_t *value){assert(reg==0x42);*value=control;return io_ok()?SH3673520_OK:SH3673520_ERR_SPI;}
static sh3673520_status_t SH3673520_WriteReg(uint8_t reg,uint8_t value){assert(reg==0x42);if(!io_ok())return SH3673520_ERR_SPI;control=value&0xfeu;return SH3673520_OK;}
static sh3673520_status_t SH3673520_ReadRegOnce(uint8_t reg,uint8_t *value){assert(reg==0x5a);*value=flag;flag=0;return io_ok()?SH3673520_OK:SH3673520_ERR_SPI;}
static sh3673520_status_t SH3673520_ReadRegs(uint8_t reg,uint8_t *bytes,sh3673520_size_t count){assert(reg==0x97&&count==3u);bytes[0]=0;bytes[1]=0;bytes[2]=abnormal;return io_ok()?SH3673520_OK:SH3673520_ERR_SPI;}
#endif
/* PRODUCTION */
static void reset_case(void){now=0;operation=fail_at=0;control=0x40;flag=abnormal=0;bms_afe_driver_reset_voltage_check();}
#if CHECK_DVC
int main(void){
 dvc1124_snapshot_t s={1};
 for(unsigned i=0;i<24;i++)g_bms_report.cell_voltage_mv[i]=3300;
 reset_case();bms_afe_driver_request_voltage_check();assert(dvc_voltage_check_service(&s,now)&&s_voltage_check.state==2);
 now=599;assert(dvc_voltage_check_service(&s,now)&&s_voltage_check.state==2);
 now=600;assert(dvc_voltage_check_service(&s,now)&&!(control&4)&&s_voltage_check.state==3);
 now=800;assert(dvc_voltage_check_service(&s,now)&&s_voltage_check.state==3);
 now=1000;assert(dvc_voltage_check_service(&s,now)&&!s_voltage_check.state&&s_voltage_check.healthy);
 reset_case();g_bms_report.cell_voltage_mv[7]=1000;bms_afe_driver_request_voltage_check();assert(dvc_voltage_check_service(&s,now));
 now=600;assert(dvc_voltage_check_service(&s,now)&&!s_voltage_check.healthy);
 now=800;assert(dvc_voltage_check_service(&s,now));now=1000;assert(dvc_voltage_check_service(&s,now));assert(!s_voltage_check.healthy);
 g_bms_report.cell_voltage_mv[7]=3300;
 reset_case();bms_afe_driver_request_voltage_check();assert(dvc_voltage_check_service(&s,now));
 now=900;assert(dvc_voltage_check_service(&s,now)&&!s_voltage_check.healthy);
 for(unsigned cut=1;cut<=2;cut++){
  reset_case();fail_at=cut;bms_afe_driver_request_voltage_check();assert(!dvc_voltage_check_service(&s,now));assert(!s_voltage_check.healthy);
 }
 reset_case();bms_afe_driver_request_voltage_check();assert(dvc_voltage_check_service(&s,now));
 assert(bms_afe_driver_stop_voltage_check()&&!(control&4)&&!s_voltage_check.state);
 puts("PASS DVC校准诊断：600ms窗口、900ms拒绝、异常串、I/O失败、清激励和两次正常VADC");return 0;
}
#else
int main(void){
 sh3673510_control_status_t s={SH3673520_FLAG2_VADC_MASK};
 for(unsigned bad=0;bad<=2;bad++){
  reset_case();bms_afe_driver_request_voltage_check();assert(sh_voltage_check_service(&s,now)&&s_voltage_check.state==2);
  flag=SH3673520_FLAG3_OWD_FLG_MASK;abnormal=bad==1?1u:0u;now=200;
  assert(sh_voltage_check_service(&s,now)&&s_voltage_check.state==2);
  flag=SH3673520_FLAG3_OWD_FLG_MASK|SH3673520_FLAG3_OWD_IND_MASK;abnormal=bad==2?2u:0u;now=400;
  assert(sh_voltage_check_service(&s,now)&&s_voltage_check.state==3&&!(control&3));
  now=600;assert(sh_voltage_check_service(&s,now)&&s_voltage_check.state==3);
  now=800;assert(sh_voltage_check_service(&s,now)&&!s_voltage_check.state);
  assert(s_voltage_check.healthy==(bad==0));assert(control&0x40);
 }
 reset_case();bms_afe_driver_request_voltage_check();assert(sh_voltage_check_service(&s,now));now=2000;
 assert(!sh_voltage_check_service(&s,now)&&!s_voltage_check.healthy);
 unsigned old=operation;bms_afe_driver_request_voltage_check();assert(!s_voltage_check.state&&operation==old);
 now+=5000;bms_afe_driver_request_voltage_check();assert(s_voltage_check.state==1);
 for(unsigned cut=1;cut<=5;cut++){
  reset_case();fail_at=cut;bms_afe_driver_request_voltage_check();assert(!sh_voltage_check_service(&s,now));assert(!s_voltage_check.healthy);
 }
 reset_case();bms_afe_driver_request_voltage_check();assert(sh_voltage_check_service(&s,now));
 assert(bms_afe_driver_stop_voltage_check()&&!(control&3)&&!s_voltage_check.state);
 puts("PASS SH校准诊断：双IND、两阶段异常位并集、逐事务失败、2s超时、5s退避及睡前清理");return 0;
}
#endif
