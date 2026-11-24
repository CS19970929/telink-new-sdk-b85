/* 只检验生产诊断函数的推进和拒绝路径；寄存器/时间替身不能验证电气覆盖。 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#define BMS_FEATURE_BALANCE_SUPPORTED 1
#define BMS_PRODUCT_CELL_COUNT 16u
static uint32_t now;
static unsigned operation,fail_at;
static uint8_t control;
static uint32_t bms_time_ms(void){return now;}
static uint8_t io_ok(void){return ++operation!=fail_at;}
static uint8_t bms_afe_driver_set_balance_mask(uint32_t mask,const uint32_t *known){assert(!mask&&!known);return io_ok();}
#include "dvc1124_reg.h"
typedef struct {uint8_t voltage_fresh;} dvc1124_snapshot_t;
static struct {uint16_t cell_voltage_mv[24];}g_bms_report;
static uint8_t DVC1124_WriteRegisterFieldSafe(uint8_t reg,uint8_t mask,uint8_t shift,uint8_t value){
 assert(reg==0x6d&&mask==4u&&shift==2u);if(!io_ok())return 0;control=(control&~mask)|(value<<shift);return 1;
}
static uint8_t DVC1124_ReadRegisters(uint8_t reg,uint8_t *bytes,uint8_t count){assert(reg==0x6d&&count==1u);*bytes=control;return io_ok();}
/* PRODUCTION */
static void reset_case(void){now=0;operation=fail_at=0;control=0x40;bms_afe_driver_reset_voltage_check();}
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
