
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef enum { BMS_AFE_SAMPLE_OK, BMS_AFE_SAMPLE_NOT_READY, BMS_AFE_SAMPLE_ERROR, BMS_AFE_SAMPLE_READ_ERROR } bms_afe_sample_status_t;
typedef uint8_t u8; typedef uint16_t u16;
#define BMS_AFE_BACKEND 1
#define BMS_AFE_BACKEND_DVC1124 1
#define BMS_FEATURE_HEATER_SUPPORTED 1
#define BMS_FEATURE_BALANCE_SUPPORTED 1
#define BMS_ERROR_AFE1 1
#define BMS_AFE_DIRECTION_ALLOW_CHARGE_MASK 1u
#define BMS_AFE_DIRECTION_ALLOW_DISCHARGE_MASK 2u
typedef struct {int dummy;} bms_afe_aux_measurements_t;
typedef struct {int dummy;} bms_afe_feature_snapshot_t;
static const bms_afe_driver_policy_t bms_afe_driver_policy={5000u,0u,1u};
static int err,cmd_c,cmd_d,apply_calls,hardware_profile,fet_fail;
static uint8_t params_valid=1u;
static uint8_t bms_protection_params_valid(void){return params_valid;}
static int raised,init_fail,aux_ok=1;
static uint8_t direction_allow_mask;
static int balance_ok=1,shutdown_ok=1,shutdown_calls,sample_calls;
static uint8_t bms_error_get(int x){return err;}
static void bms_error_raise(int x){err=1;raised++;}
static void bms_error_clear(int x){(void)x;err=0;}
static void bms_features_stop(void){}
static void bms_features_init(void){}
static unsigned protection_inits;
static void bms_sw_protection_init(void){++protection_inits;}
static void bms_sw_protection_reset_filters(void){}
static void bms_features_service(void){}
static int bms_features_charge_direction_blocked(void){return 0;}
static void bms_afe_driver_init(void){}
static bms_afe_sample_status_t bms_afe_driver_sample(void){bms_afe_sample_status_t r=BMS_AFE_SAMPLE_OK;sample_calls++;return r;}
static int bms_afe_driver_apply_protection_config(void){apply_calls++;hardware_profile=99;return 0;}
static int bms_afe_driver_set_fets(int c,int d){if(init_fail){init_fail=0;return 0;}if(fet_fail)return 0;cmd_c=c;cmd_d=d;return 1;}
static int bms_afe_driver_disable_fets(int c,int d){if(fet_fail)return 0;if(c)cmd_c=0;if(d)cmd_d=0;return 1;}
static uint8_t bms_afe_driver_get_direction_allow_mask(void){return direction_allow_mask;}
static void bms_afe_driver_set_output_enabled(int x){}
static int bms_afe_driver_get_aux_measurements(bms_afe_aux_measurements_t*x){return aux_ok;}
static int bms_afe_driver_get_feature_snapshot(bms_afe_feature_snapshot_t*x){return 1;}
static int bms_afe_driver_get_charge_source_present(uint8_t*x){return 0;}
static int bms_afe_driver_set_balance_mask(uint32_t x){return balance_ok;}
static int bms_afe_driver_get_balance_mask(uint32_t*x){return 1;}
static int bms_afe_driver_sleep(void){shutdown_calls++;return shutdown_ok;}
struct {struct {uint8_t charge_mos_status,discharge_mos_status,cooler_status,afe1_status;}bits;}g_bms_system_status;

static uint32_t guard_tick;
uint32_t bms_time_ms(void){return guard_tick;}
uint32_t bms_time_now_32k(void){return guard_tick*32u;}
uint8_t bms_afe_sleep(void);
void bms_afe_publish_fet_status(uint8_t bits, uint8_t valid);
/* PRODUCTION_SOURCE */
static void sample_cycle(void){if(bms_afe_sample()==BMS_AFE_SAMPLE_OK)bms_afe_apply_outputs();}
static void reset(void){
 params_valid=1u;direction_allow_mask=0u;
 err=raised=init_fail=0;aux_ok=1;
 balance_ok=shutdown_ok=1;fet_fail=shutdown_calls=sample_calls=0;
 bms_afe_init();bms_afe_set_channel_allowed(0,1);bms_afe_set_channel_allowed(1,1);
 bms_afe_set_output_enabled(1);for(int i=0;i<3;i++)sample_cycle();
}
static void test_remote_control(void){
 uint8_t c=0,d=0;
 reset();bms_afe_set_fets(1,1);assert(cmd_c==1&&cmd_d==1);
 assert(bms_afe_set_channel_allowed(0,0));assert(bms_afe_get_remote_allowed_mask()==2u);
 bms_afe_get_requested_fets(&c,&d);assert(c==1u&&d==1u);assert(cmd_c==0&&cmd_d==1);
 bms_afe_init();bms_afe_set_output_enabled(1);for(int i=0;i<3;i++)sample_cycle();
 bms_afe_set_fets(1,1);assert(cmd_c==0&&cmd_d==1); /* AFE 重初始化不丢失关闭许可。 */
 fet_fail=1;assert(!bms_afe_set_channel_allowed(0,1));assert(bms_afe_get_remote_allowed_mask()==2u);
 fet_fail=0;assert(bms_afe_set_channel_allowed(0,1));assert(!s_guard.comm_inhibit && !err);
 for(int i=0;i<3;i++)sample_cycle();
 assert(cmd_c==1&&cmd_d==1); /* 偶发控制通信失败不撤销已有资格；完整采样后正常执行新许可。 */
 puts("PASS remote MOS permission: independent RAM intent, AFE reinit persistence and fail-safe enable rollback");
}
static void test_remote_direction_override(void){
 reset();bms_afe_set_fets(1,1);
 assert(bms_afe_set_channel_allowed(0,0));assert(cmd_c==0&&cmd_d==1);
 direction_allow_mask=1u;sample_cycle();assert(bms_afe_get_remote_allowed_mask()==2u&&cmd_c==1&&cmd_d==1);
 assert(bms_afe_set_channel_allowed(0,0));assert(bms_afe_get_remote_allowed_mask()==2u&&cmd_c==1&&cmd_d==1);
 direction_allow_mask=0u;sample_cycle();assert(bms_afe_get_remote_allowed_mask()==2u&&cmd_c==0&&cmd_d==1);
 assert(bms_afe_set_channel_allowed(0,1));
 assert(bms_afe_set_channel_allowed(1,0));assert(cmd_c==1&&cmd_d==0);
 direction_allow_mask=2u;sample_cycle();assert(bms_afe_get_remote_allowed_mask()==1u&&cmd_c==1&&cmd_d==1);
 assert(bms_afe_set_channel_allowed(1,0));assert(bms_afe_get_remote_allowed_mask()==1u&&cmd_c==1&&cmd_d==1);
 direction_allow_mask=0u;sample_cycle();assert(bms_afe_get_remote_allowed_mask()==1u&&cmd_c==1&&cmd_d==0);
 puts("PASS remote MOS direction override: user OFF intent persists, opposite direction opens temporarily and direction loss re-closes");
}
static void test_startup_qualification(void){
 /* 参数门禁独立于 feature；关闭任一授权均保持 MOS 请求为 OFF。 */
 reset();bms_afe_set_fets(1,1);
 for(unsigned enabled=0;enabled<2;enabled++)
 for(unsigned valid=0;valid<2;valid++)
 {
  params_valid=(uint8_t)valid;bms_afe_set_output_enabled((uint8_t)enabled);
  sample_cycle();assert(cmd_c==(int)(enabled && valid));assert(cmd_d==cmd_c);
 }
 reset();raised=0;bms_afe_init();bms_afe_set_output_enabled(1);bms_afe_set_fets(1,1);
 unsigned init_count=protection_inits;
 assert(s_guard.comm_inhibit && !err && !raised);
 sample_cycle();assert(!s_guard.comm_inhibit && !err && !raised && cmd_c==1 && cmd_d==1);
 /* 后端违反 OK/aux 契约仍是明确错误；完整有效读取即可重新取得资格。 */
 aux_ok=0;sample_cycle();assert(err && raised && s_guard.comm_inhibit);
 aux_ok=1;
 sample_cycle();assert(!err && !s_guard.comm_inhibit && cmd_c==1 && cmd_d==1);
 assert(protection_inits==init_count); /* DVC 失联恢复保持既有保护生命周期。 */
 /* Repeated failures still silence the bus for the hardware watchdog window. */
 guard_tick=UINT32_MAX-1000u;
 aux_ok=0;sample_cycle();sample_cycle();assert(s_guard.bus_silenced && err);
 int before=sample_calls;aux_ok=1;
 for(int i=0;i<1000;i++){sample_cycle();} assert(sample_calls==before && s_guard.bus_silenced);
 guard_tick+=5000u-1u;sample_cycle();assert(sample_calls==before && s_guard.bus_silenced);
 guard_tick+=2u;
 sample_cycle();assert(sample_calls==before && err && s_guard.comm_inhibit);
 sample_cycle();assert(!err && !s_guard.comm_inhibit && cmd_c==1 && cmd_d==1);
 /* A backend init/config failure is not mistaken for healthy startup. */
 reset();init_fail=1;bms_afe_init();assert(err);
 sample_cycle();assert(!err && !s_guard.comm_inhibit);init_fail=0;
 puts("PASS healthy boot: zero synthetic AFE1 errors, complete-read output gate; real init/read faults and watchdog recovery retained");
}
int main(void){
 test_startup_qualification();
 test_remote_control();
 test_remote_direction_override();
 reset();bms_afe_set_fets(1,1);assert(bms_afe_sleep());
 assert(shutdown_calls==1&&s_guard.comm_inhibit&&err&&s_guard.test_shutdown_hold);
 assert(cmd_c==0&&cmd_d==0&&!bms_afe_bus_access_allowed());
 assert(!bms_afe_sleep());assert(shutdown_calls==1);
 reset();shutdown_ok=0;assert(!bms_afe_sleep());
 assert(shutdown_calls==1&&s_guard.comm_failures==1&&!s_guard.test_shutdown_hold);
 assert(!bms_afe_sleep());assert(shutdown_calls==1); /* inhibit 后不重复发命令。 */
 reset();s_guard.bus_silenced=1;assert(!bms_afe_sleep());assert(shutdown_calls==0);
 puts("PASS DVC low power: Shutdown only, output OFF, terminal bus hold, communication inhibit and watchdog silence retained");
 bms_diag_init();reset();bms_afe_set_fets(1,1);bms_diag_params(0,0);
 bms_diag_backend(DIAG_BLOCK_HW,DIAG_BLOCK_SW);bms_afe_diag_poll();
 assert(bms_diag_cached_word(128)==3 && bms_diag_cached_word(129)==0);
 assert(bms_diag_cached_word(136)==(DIAG_BLOCK_PARAMS|DIAG_BLOCK_UPGRADE|DIAG_BLOCK_HW));
 assert(bms_diag_cached_word(138)==(DIAG_BLOCK_PARAMS|DIAG_BLOCK_UPGRADE|DIAG_BLOCK_SW));
 bms_diag_params(1,1);bms_diag_backend(0,0);bms_afe_publish_fet_status(0,1);bms_afe_diag_poll();
 assert(bms_diag_cached_word(129)==3 && bms_diag_cached_word(132)==0 && bms_diag_cached_word(133)==1);
 s_guard.comm_inhibit=1;bms_afe_diag_poll();assert(bms_diag_cached_word(133)==0);
 puts("PASS MOS diagnostics: simultaneous independent reasons, requested ON/driver OFF, invalid feedback");

 reset();balance_ok=0;assert(!bms_afe_sleep());assert(shutdown_calls==0&&!s_guard.test_shutdown_hold);
 reset();fet_fail=1;assert(!bms_afe_sleep());assert(shutdown_calls==0&&!s_guard.test_shutdown_hold);
 reset();shutdown_ok=0;assert(!bms_afe_sleep());assert(shutdown_calls==1&&!s_guard.test_shutdown_hold);
 reset();assert(bms_afe_sleep());assert(shutdown_calls==1&&s_guard.test_shutdown_hold);
 assert(cmd_c==0&&cmd_d==0);assert(!bms_afe_bus_access_allowed());
 int samples=sample_calls;for(int i=0;i<100;i++){sample_cycle();} assert(sample_calls==samples);
 assert(bms_afe_set_fets(1,1));assert(cmd_c==0&&cmd_d==0);assert(!bms_afe_apply_protection_config());
 assert(!bms_afe_sleep());assert(shutdown_calls==1);
 puts("PASS guard: balance/FET/shutdown failures, command OFF, terminal bus hold");
}
