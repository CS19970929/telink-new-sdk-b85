
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef uint8_t u8; typedef uint16_t u16;
#define BMS_AFE_BACKEND 1
#define BMS_AFE_BACKEND_DVC1124 1
#define BMS_ERROR_AFE1 1
typedef struct {int dummy;} bms_afe_aux_measurements_t;
typedef struct {int dummy;} bms_afe_feature_snapshot_t;
typedef struct {int dummy;} bms_afe_openwire_result_t;
typedef int bms_afe_diag_state_t;
#define BMS_AFE_DIAG_ERROR 3
static int err,block,cmd_c,cmd_d,apply_calls,hardware_profile,fet_fail;
static int raised,init_fail,aux_ok=1;
static int balance_ok=1,shutdown_ok=1,shutdown_calls,sample_calls;
static uint8_t bms_error_get(int x){return err;}
static void bms_error_raise(int x){err=1;raised++;}
static void bms_features_on_afe_invalid(void){}
static void bms_features_init(void){}
static void bms_features_service(void){}
static int bms_features_charge_hard_blocked(void){return block;}
static int bms_features_charge_direction_blocked(void){return 0;}
static int bms_features_charge_blocked(void){return block;}
static int bms_features_discharge_blocked(void){return block;}
static void dvc1124_backend_init(void){if(init_fail)bms_error_raise(BMS_ERROR_AFE1);}
static void dvc1124_backend_sample(void){sample_calls++;err=0;}
static void dvc1124_backend_sleep(void){}
static int dvc1124_backend_apply_protection_config(void){apply_calls++;hardware_profile=99;return 0;}
static int dvc1124_backend_set_fets(int c,int d){if(fet_fail)return 0;cmd_c=c;cmd_d=d;return 1;}
static void dvc1124_backend_set_output_enabled(int x){}
static int dvc1124_backend_get_aux_measurements(bms_afe_aux_measurements_t*x){return aux_ok;}
static int dvc1124_backend_get_feature_snapshot(bms_afe_feature_snapshot_t*x){return 1;}
static int dvc1124_backend_get_charge_source_present(uint8_t*x){return 0;}
static int dvc1124_backend_set_balance_mask(uint32_t x){return balance_ok;}
static int dvc1124_backend_get_balance_mask(uint32_t*x){return 1;}
static int dvc1124_backend_openwire_start(void){return 1;}
static int dvc1124_backend_openwire_poll(bms_afe_openwire_result_t*x){return 1;}
static int dvc1124_backend_enter_shutdown(void){shutdown_calls++;return shutdown_ok;}
struct {struct {uint8_t b1Status_MOS_CHG,b1Status_MOS_DSG,b1Status_Cool;}bits;}g_bms_system_status;

uint32_t bms_diag_tick(void){return 0u;}
static uint32_t bms_features_diag_reasons(uint8_t charge){(void)charge;return 0u;}
/* PRODUCTION_SOURCE */
static void reset(void){
 err=raised=init_fail=0;aux_ok=1;
 balance_ok=shutdown_ok=1;fet_fail=shutdown_calls=sample_calls=0;
 bms_afe_init();bms_afe_set_output_enabled(1);for(int i=0;i<3;i++)bms_afe_sample();
}
static void test_startup_qualification(void){
 reset();raised=0;bms_afe_init();bms_afe_set_output_enabled(1);bms_afe_set_fets(1,1);
 assert(s_guard.comm_inhibit && !err && !raised);
 for(int i=0;i<2;i++){
  bms_afe_sample();assert(s_guard.comm_inhibit && !err && !raised);
  assert(cmd_c==0 && cmd_d==0);
 }
 bms_afe_sample();assert(!s_guard.comm_inhibit && !err && !raised && cmd_c==1 && cmd_d==1);
 /* An actual failed read still reports immediately and needs three good reads. */
 aux_ok=0;bms_afe_sample();assert(err && raised && s_guard.comm_inhibit);
 aux_ok=1;
 for(int i=0;i<2;i++){bms_afe_sample();assert(err && s_guard.comm_inhibit && cmd_c==0 && cmd_d==0);}
 bms_afe_sample();assert(!err && !s_guard.comm_inhibit && cmd_c==1 && cmd_d==1);
 /* Repeated failures still silence the bus for the hardware watchdog window. */
 aux_ok=0;bms_afe_sample();bms_afe_sample();assert(s_guard.bus_silenced && err);
 int before=sample_calls;aux_ok=1;
 for(int i=0;i<25;i++){bms_afe_sample();} assert(sample_calls==before && s_guard.bus_silenced);
 bms_afe_sample();assert(sample_calls==before && err && s_guard.comm_inhibit);
 for(int i=0;i<2;i++){bms_afe_sample();assert(err && s_guard.comm_inhibit);}
 bms_afe_sample();assert(!err && !s_guard.comm_inhibit);
 /* A backend init/config failure is not mistaken for healthy startup. */
 reset();init_fail=1;bms_afe_init();assert(err);
 for(int i=0;i<2;i++){bms_afe_sample();assert(err && s_guard.comm_inhibit);}
 bms_afe_sample();assert(!err && !s_guard.comm_inhibit);init_fail=0;
 puts("PASS healthy boot: zero synthetic AFE1 errors, three-sample output inhibit; real init/read faults and watchdog recovery retained");
}
int main(void){
 test_startup_qualification();
 bms_diag_init();reset();bms_afe_set_fets(1,1);bms_diag_params(0,0);
 bms_diag_backend(DIAG_BLOCK_HW,DIAG_BLOCK_SW);bms_afe_diag_poll();
 assert(bms_diag_cached_word(128)==3 && bms_diag_cached_word(129)==0);
 assert(bms_diag_cached_word(136)==(DIAG_BLOCK_PARAMS|DIAG_BLOCK_UPGRADE|DIAG_BLOCK_HW));
 assert(bms_diag_cached_word(138)==(DIAG_BLOCK_PARAMS|DIAG_BLOCK_UPGRADE|DIAG_BLOCK_SW));
 bms_diag_params(1,1);bms_diag_backend(0,0);bms_diag_driver(0,1);bms_afe_diag_poll();
 assert(bms_diag_cached_word(129)==3 && bms_diag_cached_word(132)==0 && bms_diag_cached_word(133)==1);
 s_guard.comm_inhibit=1;bms_afe_diag_poll();assert(bms_diag_cached_word(133)==0);
 puts("PASS MOS diagnostics: simultaneous independent reasons, requested ON/driver OFF, invalid feedback");

 reset();balance_ok=0;assert(!bms_afe_enter_shutdown());assert(shutdown_calls==0&&!s_guard.test_shutdown_hold);
 reset();fet_fail=1;assert(!bms_afe_enter_shutdown());assert(shutdown_calls==0&&!s_guard.test_shutdown_hold);
 reset();shutdown_ok=0;assert(!bms_afe_enter_shutdown());assert(shutdown_calls==1&&!s_guard.test_shutdown_hold);
 reset();assert(bms_afe_enter_shutdown());assert(shutdown_calls==1&&s_guard.test_shutdown_hold);
 assert(cmd_c==0&&cmd_d==0);assert(!bms_afe_bus_access_allowed());
 int samples=sample_calls;for(int i=0;i<100;i++){bms_afe_sample();} assert(sample_calls==samples);
 assert(bms_afe_set_fets(1,1));assert(cmd_c==0&&cmd_d==0);assert(!bms_afe_apply_protection_config());
 assert(!bms_afe_enter_shutdown());assert(shutdown_calls==1);
 puts("PASS guard: balance/FET/shutdown failures, command OFF, terminal bus hold");
}
