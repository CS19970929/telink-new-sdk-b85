#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include "storage_record.h"
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint32_t UINT32;
#define FAC_INIT_soc 60u
#define CapacityFactory 1000u
#define FD_BMS_TYPE 8u
#define SeriesNum 24u
#define BTNAME_SUFFIX_MAX_LEN 23u
#define D008_PRODUCT_CHEMISTRY 1u
#define D008_PRODUCT_SOC_PROFILE_ID 1u
#define E2P_PROTECT_DEFAULT_PRT {0}
#define PARAM_VER 1u
#define BMS_ERROR_EEPROM_STORE 1
/* MACROS */
/* TYPES */
typedef struct {u16 ParamVer;struct PRT_E2ROM_PARAS protect;} PARAM_T;
static u32 now, errors, programs, erases;
static int cut=-1, begin_ok=1, region_ok=1;
static u8 flash[20u*4096u];
static u8 backup[sizeof(flash)];
static u32 pm_get_32k_tick(void){return now;}
static void bms_error_raise(int e){(void)e;++errors;}
static int bms_sw_protection_validate_params(const struct PRT_E2ROM_PARAS *p){return p!=0;}
void bms_afe_hw_profile_build_default(bms_afe_hw_profile_t*p){memset(p,0,sizeof(*p));p->schema_version=1;p->afe_model=0x1124;p->cov_mv=3650;}
u8 bms_afe_hw_profile_validate(const bms_afe_hw_profile_t*p){return p && p->schema_version==1;}
void bms_soc_get_default_config(bms_soc_config_t*c){memset(c,0,sizeof(*c));c->current_deadband_ma=200;c->ocv_rest_prepare_s=600;c->ocv_error_band_percent=5;}
u8 bms_soc_config_valid(const bms_soc_config_t*c){return c && c->ocv_rest_prepare_s>=60;}
static int begin(void*c){(void)c;return begin_ok;}
static void end(void*c){(void)c;}
static int read_flash(void*c,u32 a,u8*b,u32 n){(void)c;assert(a+n<=sizeof(flash));memcpy(b,flash+a,n);return 1;}
static int program(void*c,u32 a,const u8*b,u32 n){(void)c;++programs;assert(a+n<=sizeof(flash));for(u32 i=0;i<n;i++){if(cut==0)return 0;if(cut>0)--cut;assert((flash[a+i]|b[i])==flash[a+i]);flash[a+i]&=b[i];}return 1;}
static int erase(void*c,u32 a,u32 n){(void)c;++erases;assert(a+n<=sizeof(flash));memset(flash+a,255,n);return 1;}
static const storage_port_t port={0,4096,4,255,begin,end,read_flash,program,erase};
const storage_port_t*bms_storage_platform_port(void){return &port;}
int bms_storage_platform_region(bms_storage_domain_t d,storage_region_t*r){if(!region_ok){bms_diag_result((uint8_t)d,DIAG_LAYOUT);return 0;}if(d==BMS_STORAGE_DOMAIN_CONFIG){r->base=0;r->size=4*4096;}else if(d==BMS_STORAGE_DOMAIN_STATE){r->base=4*4096;r->size=8*4096;}else if(d==BMS_STORAGE_DOMAIN_EVENT){r->base=12*4096;r->size=8*4096;}else return 0;return 1;}
uint32_t bms_diag_tick(void){return now;}
/* PRODUCTION */
static void reboot(void){
 bms_diag_init();region_ok=1;
 g_bms_config_ready=0;g_bms_state_ready=0;
 g_bms_state_attempted=0;g_bms_state_last_failed=0;
 memset(&g_bms_event_log,0,sizeof(g_bms_event_log));
 s_storage_upgrade_valid=0;s_protection_params_valid=0;cut=-1;begin_ok=1;
}
static void fresh(void){memset(flash,255,sizeof(flash));now=0;reboot();Param_UpgradeReset_Apply();LoadParam();assert(bms_protection_params_valid());}
static void test_config_atomic_revisions(void){
 fresh(); bms_config_system_params_t cap=g_bms_config.system;
 cap.capacity_factory=BMS_SOC_CAPACITY_MAX_0P1AH+1u;assert(!bms_config_store_set_system(&cap));
 assert(g_bms_config.system.capacity_factory==1000);
 bms_config_cache_t custom=g_bms_config;
 custom.protect.u16VcellOvp_Third=3999;custom.afe_hw.cov_mv=4100;
 custom.soc.ocv_rest_prepare_s=777;custom.system.flags=55;
 strcpy(custom.bt_name_suffix,"persist-name");
 for(unsigned category=0;category<4;category++){
  unsigned ids[]={BMS_CONFIG_CTRL_PROTECT_RESET_EPOCH,BMS_CONFIG_CTRL_AFE_HW_RESET_EPOCH,BMS_CONFIG_CTRL_SOC_CONFIG_RESET_EPOCH,BMS_CONFIG_CTRL_SYSTEM_RESET_EPOCH};
  bms_config_cache_t old=custom;old.control[ids[category]]=0;
  assert(bms_config_save_cache(&old));memcpy(backup,flash,sizeof(flash));
  for(int byte=0;byte<(int)(24+BMS_CONFIG_PAYLOAD_BYTES+8);byte++){
   memcpy(flash,backup,sizeof(flash));reboot();assert(bms_config_store_init());cut=byte;
   assert(!bms_config_store_apply_revisions());
   assert(g_bms_config.control[ids[category]]==0);assert(g_bms_config.protect.u16VcellOvp_Third==3999);
   reboot();assert(bms_config_store_init());assert(g_bms_config.control[ids[category]]==0);
   assert(bms_config_store_apply_revisions());
   unsigned expected[]={FW_UPGRADE_RESET_PROTECT_EPOCH,FW_UPGRADE_RESET_AFE_HW_EPOCH,FW_UPGRADE_RESET_SOC_CONFIG_EPOCH,FW_UPGRADE_RESET_SYSTEM_EPOCH};
   assert(g_bms_config.control[ids[category]]==expected[category]);
   assert(g_bms_config.protect.u16VcellOvp_Third==(category==0?0:3999));
   assert(g_bms_config.afe_hw.cov_mv==(category==1?3650:4100));
   assert(g_bms_config.soc.ocv_rest_prepare_s==(category==2?600:777));
   assert(g_bms_config.system.flags==(category==3?0:55));
   assert(strcmp(g_bms_config.bt_name_suffix,"persist-name")==0);
   u32 count=programs;assert(bms_config_store_apply_revisions());assert(programs==count);
  }
 }
 puts("PASS Config: all byte cuts, four independent revisions, retained name, idempotence");
}
static void test_state(void){
 fresh();assert(bms_state_store_write_learning(900,1));
 bms_state_store_update_and_log_if_changed(61,2,3);assert(g_bms_state.soc==60);
 now=60*32000;cut=0;bms_state_store_update_and_log_if_changed(62,4,5);u32 count=programs;
 for(unsigned i=0;i<100;i++)bms_state_store_update_and_log_if_changed(63,6,7);
 assert(programs==count);assert(errors);now+=5*32000;cut=-1;
 bms_state_store_update_and_log_if_changed(64,8,9);assert(g_bms_state.soc==64);assert(g_bms_state.learned_capacity_0p1ah==900);
 assert(bms_state_store_write_all(65,9,10));reboot();assert(bms_state_store_init());assert(g_bms_state.soc==65);
 bms_state_persist_t old=g_bms_state;old.soc_revision=0;old.runtime_min=33;assert(bms_state_save(&old));memcpy(backup,flash,sizeof(flash));
 for(int byte=0;byte<64;byte++){
  memcpy(flash,backup,sizeof(flash));reboot();cut=byte;assert(!bms_state_store_init());
  reboot();assert(bms_state_store_init());assert(g_bms_state.soc==60 && g_bms_state.flags==0 && g_bms_state.learned_capacity_0p1ah==0);assert(g_bms_state.runtime_min==33);
 }
 old=g_bms_state;old.runtime_revision=0;old.soc=77;old.runtime_min=44;assert(bms_state_save(&old));reboot();assert(bms_state_store_init());assert(g_bms_state.soc==77 && g_bms_state.runtime_min==0);
 now=UINT32_MAX-32000;g_bms_state_last_attempt_32k=now;
 bms_state_store_update_and_log_if_changed(78,0,0);now+=(60*32000);bms_state_store_update_and_log_if_changed(79,0,0);assert(g_bms_state.soc==79);
 puts("PASS State: coalescing, failure/backoff, learning flush, reset independence, byte cuts, wrap");
}
static void test_events(void){
 fresh();bms_event_log_sample_t sample={0};sample.vcell_ovp=1;
 u32 count=programs;bms_event_log_poll_1s(&sample);sample.vcell_ovp=0;bms_event_log_poll_1s(&sample);sample.vcell_ovp=1;bms_event_log_poll_1s(&sample);
 assert(programs==count && bms_event_log_read_repeat(0)==2);
 now=60*32000;cut=0;bms_event_log_poll_1s(&sample);count=programs;
 bms_event_log_poll_1s(&sample);assert(programs==count && g_bms_event_log.dirty);
 assert(!bms_event_log_note_sleep());now+=5*32000;cut=-1;assert(bms_event_log_note_sleep());reboot();assert(bms_event_log_init());assert((bms_event_log_read_reg(0)>>8)==BMS_SLEEP);
 g_bms_event_log.revision=0;assert(bms_event_log_write_snapshot());memcpy(backup,flash,sizeof(flash));
 for(int byte=0;byte<(int)(24+BMS_EVENT_PAYLOAD_BYTES+8);byte++){
  memcpy(flash,backup,sizeof(flash));reboot();cut=byte;assert(!bms_event_log_init());
  reboot();assert(bms_event_log_init());assert(bms_event_log_read_reg(0)==0 && g_bms_event_log.revision==FW_UPGRADE_RESET_EVENT_LOG_EPOCH);
  count=programs;assert(bms_event_log_init());assert(programs==count);
 }
 puts("PASS Event: coalescing/repeats, failure retention, forced shutdown flush, atomic reset, byte cuts");
}
static void test_boot_gate(void){
 fresh();bms_config_cache_t old=g_bms_config;old.control[BMS_CONFIG_CTRL_PROTECT_RESET_EPOCH]=0;old.protect.u16VcellOvp_Third=3999;assert(bms_config_save_cache(&old));
 reboot();cut=0;Param_UpgradeReset_Apply();LoadParam();assert(!bms_protection_params_valid());assert(g_tParam.protect.u16VcellOvp_Third==3999);
 cut=-1;assert(SaveParam());assert(!bms_protection_params_valid());
 reboot();Param_UpgradeReset_Apply();LoadParam();assert(bms_protection_params_valid());
 for(unsigned domain=0;domain<2;domain++){
  fresh();
  if(domain==0){bms_state_persist_t state=g_bms_state;state.soc_revision=0;assert(bms_state_save(&state));}
  else {g_bms_event_log.revision=0;assert(bms_event_log_write_snapshot());}
  reboot();cut=0;Param_UpgradeReset_Apply();LoadParam();assert(!bms_protection_params_valid());
  cut=-1;assert(SaveParam());assert(!bms_protection_params_valid());
  reboot();Param_UpgradeReset_Apply();LoadParam();assert(bms_protection_params_valid());
 }
 puts("PASS startup: Config/State/Event failure gates, RAM protection retained, SaveParam cannot bypass");
}
static void test_diag_boot(void){
 fresh();reboot();region_ok=0;u32 before=errors;
 Param_UpgradeReset_Apply();LoadParam();
 assert(errors-before==2);assert(bms_diag_cached_word(36)==2);
 assert(bms_diag_cached_word(37)==DIAG_LAYOUT && bms_diag_cached_word(38)==DIAG_LAYOUT);
 assert(bms_diag_cached_word(52)==0 && bms_diag_cached_word(84)==0);
 assert(!bms_event_log_init());assert(bms_diag_cached_word(84)==1);
 bms_param_diag_poll();assert(bms_diag_cached_word(144)==0);
 bms_diag_freeze_boot();region_ok=1;assert(bms_config_store_init());
 assert(bms_diag_cached_word(37)==DIAG_LAYOUT && bms_diag_cached_word(38)==DIAG_LAYOUT);
 fresh();assert(bms_diag_cached_word(39)==1 && bms_diag_cached_word(37)==0);
 puts("PASS diagnostics: two Config failures, short circuit, independent Event attempt, frozen first failure, blank defaults");
}
int main(void){test_diag_boot();test_config_atomic_revisions();test_state();test_events();test_boot_gate();return 0;}
