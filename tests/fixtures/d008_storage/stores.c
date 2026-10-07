#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include "storage_record.h"
#include "bms_update_policy.h"
#include "bms_soc_eta.h"
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32;
#define BMS_PRODUCT_DEFAULT_CAPACITY_0P1AH 1000u
#define BMS_HEATER_START_TEMP_X10 400u
#define BMS_HEATER_STOP_TEMP_X10 450u
#define BMS_PRODUCT_WIRE_ID 8u
#define BMS_PRODUCT_CELL_COUNT 24u
#define BTNAME_SUFFIX_MAX_LEN 23u
#define BMS_PRODUCT_ID 8u
#define BMS_PRODUCT_CHEMISTRY 1u
#define BMS_PRODUCT_SOC_PROFILE_ID 1u
#define BMS_ERROR_EEPROM_STORE 1
/* MACROS */
/* TYPES */
static u32 now, errors, programs, erases;
static u32 sector_erases[28];
static int read_ok=1, erase_ok=1;
static int cut=-1, begin_ok=1, region_ok=1;
static u8 flash[28u*4096u];
static u8 backup[sizeof(flash)];
static u32 pm_get_32k_tick(void){return now;}
static void bms_error_raise(int e){(void)e;++errors;}
static int bms_sw_protection_validate_params(const bms_protection_params_t *p){return p!=0;}
void bms_afe_hw_profile_build_default(bms_afe_hw_profile_t*p){memset(p,0,sizeof(*p));p->schema_version=1;p->afe_model=0x1124;p->cov_mv=3650;}
u8 bms_afe_hw_profile_validate(const bms_afe_hw_profile_t*p){return p && p->schema_version==1;}
void bms_soc_get_default_config(bms_soc_config_t*c){memset(c,0,sizeof(*c));c->current_deadband_ma=200;c->ocv_rest_prepare_s=600;c->ocv_error_band_percent=5;}
u8 bms_soc_config_valid(const bms_soc_config_t*c){return c && c->ocv_rest_prepare_s>=60;}
static int begin(void*c){(void)c;return begin_ok;}
static void end(void*c){(void)c;}
static int read_flash(void*c,u32 a,u8*b,u32 n){(void)c;if(!read_ok)return 0;assert(a+n<=sizeof(flash));memcpy(b,flash+a,n);return 1;}
static int program(void*c,u32 a,const u8*b,u32 n){(void)c;++programs;assert(a+n<=sizeof(flash));for(u32 i=0;i<n;i++){if(cut==0)return 0;if(cut>0)--cut;assert((flash[a+i]|b[i])==flash[a+i]);flash[a+i]&=b[i];}return 1;}
static int erase(void*c,u32 a,u32 n){(void)c;++erases;++sector_erases[a/4096u];assert(a+n<=sizeof(flash));if(!erase_ok)return 0;memset(flash+a,255,n);return 1;}
static const storage_port_t port={0,4096,4,255,begin,end,read_flash,program,erase};
const storage_port_t*bms_storage_platform_port(void){return &port;}
int bms_storage_platform_region(bms_storage_domain_t d,storage_region_t*r){if(!region_ok){bms_diag_result((uint8_t)d,DIAG_LAYOUT);return 0;}if(d==BMS_STORAGE_DOMAIN_CONFIG){r->base=0;r->size=4*4096;}else if(d==BMS_STORAGE_DOMAIN_STATE){r->base=4*4096;r->size=8*4096;}else if(d==BMS_STORAGE_DOMAIN_EVENT){r->base=12*4096;r->size=16*4096;}else return 0;return 1;}
uint32_t bms_diag_tick(void){return now;}
/* PRODUCTION */
static void reboot(void){
 bms_diag_init();region_ok=1;read_ok=1;erase_ok=1;
 g_bms_config_ready=0;g_bms_state_ready=0;
 memset(&g_bms_config_store,0,sizeof(g_bms_config_store));
 memset(&g_bms_state_store,0,sizeof(g_bms_state_store));
 g_bms_state_attempted=0;g_bms_state_last_failed=0;
 memset(&g_bms_event_log,0,sizeof(g_bms_event_log));
 s_storage_startup_valid=0;s_protection_params_valid=0;cut=-1;begin_ok=1;
}
static void fresh(void){memset(flash,255,sizeof(flash));now=0;reboot();bms_parameters_init();assert(bms_protection_params_valid());}

/* 独立按 CFG2 payload 的固定字节位置构造期望，不调用生产 codec。 */
static void codec_expect_le(u8 *bytes, u32 value, unsigned width)
{
 for(unsigned i=0;i<width;++i) bytes[i]=(u8)(value>>(i*8u));
}
static u32 codec_random(u32 *seed, unsigned scenario)
{
 *seed=*seed*1664525u+1013904223u;
 if(scenario==0u) return 0u;
 if(scenario==1u) return UINT32_MAX;
 if(scenario==2u) return 0x80018001u;
 if(scenario==3u) return 0x80000000u;
 return *seed;
}
static void test_config_codec_layout(void)
{
 u32 seed=0x43464732u;
 assert(BMS_CONFIG_PAYLOAD_BYTES==322u);
 for(unsigned scenario=0;scenario<10000u;++scenario){
  bms_config_cache_t cfg,decoded;
  u8 expected[322],encoded[324];
  u16 business[7];
  u32 system[5];
  memset(&cfg,0,sizeof(cfg));memset(expected,0,sizeof(expected));
  codec_expect_le(expected,BMS_PRODUCT_ID,4u);
  for(unsigned i=0;i<65u;++i){
   u16 value=(u16)codec_random(&seed,scenario);
   memcpy((u8*)&cfg.protect+i*2u,&value,sizeof(value));
   codec_expect_le(expected+4u+i*2u,value,2u);
  }
  for(unsigned i=0;i<5u;++i) system[i]=codec_random(&seed,scenario);
  cfg.system.bms_type=system[0];cfg.system.series_num=system[1];
  cfg.system.capacity_factory=system[2];cfg.system.battery_chemistry=system[3];
  cfg.system.soc_profile_id=system[4];
  for(unsigned i=0;i<5u;++i) codec_expect_le(expected+134u+i*4u,system[i],4u);
  for(unsigned i=0;i<35u;++i){
   u16 value=(u16)codec_random(&seed,scenario);
   memcpy((u8*)&cfg.afe_hw+i*2u,&value,sizeof(value));
   codec_expect_le(expected+154u+i*2u,value,2u);
  }
  for(unsigned i=0;i<24u;++i) cfg.bt_name_suffix[i]=(char)codec_random(&seed,scenario);
  memcpy(expected+224u,cfg.bt_name_suffix,24u);
  cfg.soc.chemistry=(u8)system[3];cfg.soc.profile_id=(u8)system[4];
  cfg.soc.current_deadband_ma=(u16)codec_random(&seed,scenario);
  cfg.soc.ocv_rest_prepare_s=(u16)codec_random(&seed,scenario);
  cfg.soc.ocv_error_band_percent=(u8)codec_random(&seed,scenario);
  codec_expect_le(expected+248u,cfg.soc.current_deadband_ma,2u);
  codec_expect_le(expected+250u,cfg.soc.ocv_rest_prepare_s,2u);
  expected[252]=cfg.soc.ocv_error_band_percent;
  for(unsigned i=0;i<7u;++i) business[i]=(u16)codec_random(&seed,scenario);
  cfg.user.heater_enable=business[0];cfg.user.heater_start_x10=business[1];
  cfg.user.heater_stop_x10=business[2];cfg.user.balance_enable=business[3];
  cfg.user.balance_start_mv=business[4];cfg.user.balance_start_delta_mv=business[5];
  cfg.user.balance_stop_delta_mv=business[6];
  for(unsigned i=0;i<7u;++i) codec_expect_le(expected+256u+i*2u,business[i],2u);
  /* memcpy 覆盖负值和 INT32_MIN 的位模式，不依赖越界有符号转换。 */
  u32 offset=codec_random(&seed,scenario);
  memcpy(&cfg.user.current_offset_ma,&offset,sizeof(offset));
  cfg.user.current_gain_ppm=codec_random(&seed,scenario);
  codec_expect_le(expected+270u,offset,4u);
  codec_expect_le(expected+274u,cfg.user.current_gain_ppm,4u);
  for(unsigned i=0;i<32u;++i) cfg.user.serial[i]=(char)codec_random(&seed,scenario);
  memcpy(expected+278u,cfg.user.serial,32u);
  for(unsigned i=0;i<6u;++i){
   cfg.revisions[i]=(u16)codec_random(&seed,scenario);
   codec_expect_le(expected+310u+i*2u,cfg.revisions[i],2u);
  }
  memset(encoded,0xa5,sizeof(encoded));bms_config_encode(&cfg,encoded+1u);
  assert(encoded[0]==0xa5 && encoded[323]==0xa5);
  assert(!memcmp(encoded+1u,expected,sizeof(expected)));
  expected[253]=0xffu;expected[254]=0xffu;expected[255]=0xffu; /* 保留字节忽略，BLE 后缀末字节强制 NUL。 */
  cfg.bt_name_suffix[23]='\0';
  memset(&decoded,0xa5,sizeof(decoded));bms_config_decode(&decoded,expected);
  assert(!memcmp(&decoded,&cfg,sizeof(cfg)));
 }
 puts("PASS Config codec: 10000 independent 322-byte vectors, signed/high-bit fields, reserved byte, guards, native ABI padding");
 printf("BMS_EVIDENCE {\"config_codec\":{\"cases\":10000,\"payload_bytes\":322,\"user_native_bytes\":%u,\"calibration_native_offset\":%u}}\n",
        (unsigned)sizeof(bms_user_params_t),(unsigned)offsetof(bms_user_params_t,current_offset_ma));
}
static void test_config_schema(void){
 fresh(); bms_config_system_params_t cap=g_bms_config.system;
 cap.capacity_factory=BMS_SOC_CAPACITY_MAX_0P1AH+1u;assert(!bms_config_store_set_system(&cap));
 assert(g_bms_config.system.capacity_factory==1000);
 bms_config_cache_t old=g_bms_config,next=old;
 next.protect.cell_ovp_third_mv=3999;next.afe_hw.cov_mv=4100;
 next.soc.ocv_rest_prepare_s=777;strcpy(next.bt_name_suffix,"persist-name");
 memcpy(backup,flash,sizeof(flash));
 for(int byte=0;byte<(int)(24+BMS_CONFIG_PAYLOAD_BYTES+8);byte++){
  memcpy(flash,backup,sizeof(flash));reboot();assert(bms_config_store_init());cut=byte;
  assert(!bms_config_save_cache(&next));assert(!memcmp(&g_bms_config,&old,sizeof(old)));
  reboot();assert(bms_config_store_init());assert(!memcmp(&g_bms_config,&old,sizeof(old)));
 }
 assert(bms_config_save_cache(&next));reboot();assert(bms_config_store_init());
 assert(!memcmp(&g_bms_config,&next,sizeof(next)));
 /* A committed, CRC-valid payload belonging to another board is rejected. */
 u8 payload[BMS_CONFIG_PAYLOAD_BYTES];bms_config_encode(&next,payload);
 bms_config_put_u32le(payload,14u);assert(storage_record_save(&g_bms_config_store,payload));
 reboot();assert(bms_config_store_init());assert(!g_bms_config_store.has_latest);
 assert(g_bms_config.protect.cell_ovp_third_mv==0);assert(bms_config_store_validate_startup());
 /* Older schema/magic is never decoded into the new structure. */
 fresh();memset(flash,255,4*4096);reboot();assert(bms_config_store_init());
 g_bms_config_store.magic=0x43464731u;assert(bms_config_save_cache(&next));
 reboot();assert(bms_config_store_init());assert(!g_bms_config_store.has_latest);
 assert(g_bms_config.protect.cell_ovp_third_mv==0);
 puts("PASS Config: new schema, all byte cuts, atomic rollback, product isolation, old format rejection");
}

static void test_state(void){
 fresh();
 bms_state_store_update_and_log_if_changed(61,2,3);assert(g_bms_state.soc==60);
 now=60*32000;cut=0;bms_state_store_update_and_log_if_changed(62,4,5);u32 count=programs;
 for(unsigned i=0;i<100;i++)bms_state_store_update_and_log_if_changed(63,6,7);
 assert(programs==count);assert(errors);now+=5*32000;cut=-1;
 bms_state_store_update_and_log_if_changed(64,8,9);assert(g_bms_state.soc==64);
 assert(bms_state_store_write_all(65,9,10));reboot();assert(bms_state_store_init());assert(g_bms_state.soc==65);

 /* Keep schema 3 SOC/cycle/revision offsets. Ignore obsolete learning bytes. */
 u8 state_payload[BMS_STATE_PAYLOAD_BYTES];bms_state_encode(&g_bms_state,state_payload);
 for(unsigned i=12;i<40;++i)assert(state_payload[i]==0);
 memset(state_payload+12,0xff,28);assert(storage_record_save(&g_bms_state_store,state_payload));
 reboot();assert(bms_state_store_init());assert(g_bms_state.soc==65&&g_bms_state.cycle==10);
 assert(!bms_state_store_write_all(101,0,0));assert(!bms_state_store_write_all(60,101,0));
 assert(!bms_state_store_set_soc_cycle(60,0,65536));
 bms_state_persist_t old=g_bms_state,next=old;next.soc=77;
 memcpy(backup,flash,sizeof(flash));
 for(int byte=0;byte<(int)(24+BMS_STATE_PAYLOAD_BYTES+8);byte++){
  memcpy(flash,backup,sizeof(flash));reboot();assert(bms_state_store_init());cut=byte;
  assert(!bms_state_save(&next));assert(g_bms_state.soc==65);
  reboot();assert(bms_state_store_init());assert(g_bms_state.soc==65);
 }
 assert(bms_state_save(&next));reboot();assert(bms_state_store_init());
 assert(g_bms_state.soc==77);
 now=UINT32_MAX-32000;g_bms_state_last_attempt_32k=now;
 bms_state_store_update_and_log_if_changed(78,0,0);now+=(60*32000);bms_state_store_update_and_log_if_changed(79,0,0);assert(g_bms_state.soc==79);
 puts("PASS State: coalescing, failure/backoff, cycle state/reserved slots, atomic rollback, all byte cuts, wrap");
}
static void test_events(void){
 fresh();bms_event_log_sample_t sample={0};sample.vcell_ovp=1;
 u32 count=programs;bms_event_log_poll_1s(&sample);sample.vcell_ovp=0;bms_event_log_poll_1s(&sample);sample.vcell_ovp=1;bms_event_log_poll_1s(&sample);
 assert(programs==count && bms_event_log_read_repeat(0)==2);
 now=60*32000;cut=0;bms_event_log_poll_1s(&sample);count=programs;
 bms_event_log_poll_1s(&sample);assert(programs==count && g_bms_event_log.dirty);
 assert(!bms_event_log_note_sleep());now+=5*32000;cut=-1;assert(bms_event_log_note_sleep());reboot();assert(bms_event_log_init());assert((bms_event_log_read_reg(0)>>8)==BMS_SLEEP);

 memcpy(backup,flash,sizeof(flash));
 for(int byte=0;byte<(int)(24+BMS_EVENT_PAYLOAD_BYTES+8);byte++){
  memcpy(flash,backup,sizeof(flash));reboot();assert(bms_event_log_init());cut=byte;
  bms_event_log_note_startup();assert(!bms_event_log_write_snapshot());
  assert((bms_event_log_read_reg(0)>>8)==BMS_START_UP);
  reboot();assert(bms_event_log_init());assert((bms_event_log_read_reg(0)>>8)==BMS_SLEEP);
 }
 bms_event_log_note_startup();assert(bms_event_log_write_snapshot());reboot();assert(bms_event_log_init());
 assert((bms_event_log_read_reg(0)>>8)==BMS_START_UP);
 count=programs;assert(bms_event_log_init());assert(programs==count);
 puts("PASS Event: coalescing/repeats, failure retention, forced shutdown flush, atomic snapshots, byte cuts");
}
static void test_boot_gate(void){
 for(unsigned domain=0;domain<3;domain++){
  fresh();
  /* Erase just this domain: boot must first commit its new defaults. */
  unsigned base=domain==0?0:(domain==1?4*4096:12*4096);
  unsigned size=domain==0?4*4096:8*4096;
  memset(flash+base,255,size);reboot();cut=0;
  bms_parameters_init();assert(!bms_protection_params_valid());
  cut=-1;assert(bms_protection_params_commit(&g_bms_protection_params));assert(!bms_protection_params_valid());
  reboot();bms_parameters_init();assert(bms_protection_params_valid());
 }
 puts("PASS startup: Config/State/Event first-save failure gates, protection commit cannot bypass");
}

static void test_diag_boot(void){
 fresh();reboot();region_ok=0;u32 before=errors;
 bms_parameters_init();
 assert(errors-before==2);assert(bms_diag_cached_word(36)==2);
 assert(bms_diag_cached_word(37)==DIAG_LAYOUT && bms_diag_cached_word(38)==DIAG_LAYOUT);
 assert(bms_diag_cached_word(52)==0 && bms_diag_cached_word(84)==0);
 assert(!bms_event_log_init());assert(bms_diag_cached_word(84)==1);
 bms_parameters_diag_poll();assert(bms_diag_cached_word(144)==0);
 bms_diag_freeze_boot();region_ok=1;assert(bms_config_store_init());
 assert(bms_diag_cached_word(37)==DIAG_LAYOUT && bms_diag_cached_word(38)==DIAG_LAYOUT);
 fresh();assert(bms_diag_cached_word(39)==1 && bms_diag_cached_word(37)==0);
 puts("PASS diagnostics: two Config failures, short circuit, independent Event attempt, frozen first failure, blank defaults");
}
static int access_active=1;static unsigned identity_updates,capacity_updates;
static u8 live_soc=60;
bms_soc_state_t g_bms_soc;
typedef struct {int32_t raw_current_ma,current_ma;u32 sample_tick_32k;} bms_afe_aux_measurements_t;
static u8 bms_afe_get_aux_measurements(bms_afe_aux_measurements_t*s){memset(s,0,sizeof(*s));return 1;}
static u8 bms_board_heater_supported(void){return 1;}
static u8 bms_afe_hw_access_is_active(void){return access_active;}
u8 get_soc_real(void){return live_soc;}
void set_soc_param(u8 soc,u8 sync){(void)sync;live_soc=soc;}
void bms_soc_nominal_capacity_changed(void){capacity_updates++;}
static u8 bms_reset_software_parameters(void){return 0;}
static u8 bms_reset_afe_parameters(void){return access_active?0:2;}
void bms_product_info_refresh(void){identity_updates++;}
/* PARAMETER_PROTOCOL */
static void test_parameter_protocol(void){
 fresh();u8 retired[]={0,6};u32 flash_before=programs;u16 sequence=bms_parameter_read(0x2e04);
 access_active=0;assert(bms_parameter_write(0x2e10,1,retired)==3);
 access_active=1;assert(bms_parameter_write(0x2e10,1,retired)==3);
 assert(programs==flash_before && bms_parameter_read(0x2e04)==sequence);
 u8 heat[]={0,1,1,134,1,194},begin[]={0,0},chunk[8]={'S','N','0','1',0,0,0,0};
 assert(bms_parameter_read(0x2e00)==0xd008);assert(bms_parameter_write(0x2e20,1,heat)==3);
 assert(bms_parameter_write(0x2e20,3,heat)==0);assert(bms_parameter_read(0x2e21)==390);
 cut=0;heat[0]=0;heat[1]=0;assert(bms_parameter_write(0x2e20,3,heat)==4);assert(bms_parameter_read(0x2e20)==1);cut=-1;
 access_active=0;assert(bms_parameter_write(0x2e40,1,begin)==2);access_active=1;
 u32 before=programs;assert(bms_parameter_write(0x2e40,1,begin)==0);
 u16 gen=bms_parameter_read(0x2e06);u8 commit[]={(u8)(gen>>8),(u8)gen};
 assert(bms_parameter_write(0x2e40,1,commit)==3);
 for(u16 i=0;i<16;i+=4)assert(bms_parameter_write(0x2e50+i,4,chunk)==0);
 assert(programs==before);assert(bms_parameter_write(0x2e40,1,commit)==0);assert(identity_updates==1);
 reboot();bms_parameters_init();assert(bms_parameter_read(0x2e30)==0x534e);
 now=UINT32_MAX-100;assert(bms_parameter_write(0x2e40,1,begin)==0);now+=61u*32000u;
 assert(bms_parameter_write(0x2e50,4,chunk)==3);
 assert(bms_parameter_write(0x2e24,4,0)==3);assert(bms_parameter_write(0x2e20,0,heat)==3);
 u8 soc[]={0,88};cut=0;assert(bms_parameter_write(0x1005,1,soc)==4);assert(live_soc==60);cut=-1;
 puts("PASS parameter protocol: atomic heater, failed-save rollback, SN authorization/full bitmap/commit/reboot/expiry/wrap, no staging Flash, null/zero length, SOC RAM unchanged on failure");
}
static void test_user_parameters(void){
 fresh();bms_user_params_t v,old;assert(bms_config_get_user(&old));
 assert(old.heater_enable==1 && old.heater_start_x10==400 && old.heater_stop_x10==450);
 v=old;v.heater_start_x10=460;assert(!bms_config_set_user(&v));
 v=old;v.current_offset_ma=-123;v.current_gain_ppm=1100000;strcpy(v.serial,"D008-TEST");
 assert(bms_config_set_user(&v));reboot();assert(bms_config_store_init());assert(bms_config_get_user(&old));assert(!memcmp(&old,&v,sizeof(v)));
 memcpy(backup,flash,sizeof(flash));
 for(int byte=0;byte<(int)(24+BMS_CONFIG_PAYLOAD_BYTES+8);byte++){
  memcpy(flash,backup,sizeof(flash));reboot();assert(bms_config_store_init());assert(bms_config_get_user(&old));
  bms_user_params_t next=old;next.heater_enable=0;cut=byte;assert(!bms_config_set_user(&next));
  assert(bms_config_get_user(&next));assert(next.heater_enable==1);
  reboot();assert(bms_config_store_init());assert(bms_config_get_user(&next));assert(next.heater_enable==1 && !strcmp(next.serial,"D008-TEST"));
 }
 assert(bms_config_reset_business());assert(bms_config_get_user(&old));assert(old.current_offset_ma==-123 && !strcmp(old.serial,"D008-TEST"));
 uint32_t rng=19;
 for(unsigned i=0;i<10000;i++){
  rng=rng*1664525u+1013904223u;int32_t raw=(int32_t)rng;
  rng=rng*1664525u+1013904223u;v.current_offset_ma=(int32_t)(rng%2000001)-1000000;
  rng=rng*1664525u+1013904223u;v.current_gain_ppm=100000+rng%9900001;
  g_bms_config.user=v;
  int64_t delta=(int64_t)raw-v.current_offset_ma;
  if(delta>INT32_MAX)delta=INT32_MAX;
  if(delta<-INT32_MAX)delta=-INT32_MAX;
  int64_t expected=delta*v.current_gain_ppm/1000000;
  if(expected>INT32_MAX)expected=INT32_MAX;
  if(expected<-INT32_MAX)expected=-INT32_MAX;
  assert(bms_config_calibrate_current(raw)==expected);
 }
 fresh();bms_state_persist_t state=g_bms_state;cut=0;
 assert(!bms_state_store_set_soc_cycle(88,9,999));assert(g_bms_state.soc==state.soc);
 cut=-1;assert(g_bms_state_pending.soc==state.soc);reboot();assert(bms_state_store_init());assert(g_bms_state.soc==state.soc);
 assert(bms_state_store_set_soc_cycle(88,9,999));reboot();assert(bms_state_store_init());assert(g_bms_state.soc==88 && g_bms_state.cycle==999);
 puts("PASS new parameters: heater validation, SN persistence, Config byte cuts, independent reset, 10000 current arithmetic oracle cases, synchronous State rollback");
}
/* OTA 策略回归：通过真实记录制造待更新版本，启动流程负责提交。 */
static bms_config_cache_t configured(void)
{
    bms_config_cache_t cfg = g_bms_config;
    cfg.protect.cell_ovp_third_mv = 3999u;
    cfg.afe_hw.cov_mv = 4100u;
    cfg.system.capacity_factory = 1234u;
    cfg.user.heater_enable = 0u;
    cfg.user.balance_start_mv = 3500u;
    cfg.soc.ocv_rest_prepare_s = 777u;
    cfg.user.current_offset_ma = -123;
    cfg.user.current_gain_ppm = 1100000u;
    strcpy(cfg.user.serial, "KEEP-SN");
    strcpy(cfg.bt_name_suffix, "KEEP-NAME");
    return cfg;
}

static void assert_update_groups(unsigned mask)
{
    assert(g_bms_config.protect.cell_ovp_third_mv == ((mask & 1u) ? 0u : 3999u));
    assert(g_bms_config.afe_hw.cov_mv == ((mask & 2u) ? 3650u : 4100u));
    assert(g_bms_config.system.capacity_factory == ((mask & 4u) ? 1000u : 1234u));
    assert(g_bms_config.user.heater_enable == ((mask & 4u) ? 1u : 0u));
    assert(g_bms_config.user.balance_start_mv == ((mask & 4u) ? BMS_BALANCE_START_VOLTAGE_MV_DEFAULT : 3500u));
    assert(g_bms_config.soc.ocv_rest_prepare_s == ((mask & 8u) ? 600u : 777u));
    assert(g_bms_config.user.current_offset_ma == ((mask & 16u) ? 0 : -123));
    assert(g_bms_config.user.current_gain_ppm == ((mask & 16u) ? 1000000u : 1100000u));
    assert(!strcmp(g_bms_config.user.serial, (mask & 32u) ? "" : "KEEP-SN"));
    assert(!strcmp(g_bms_config.bt_name_suffix, (mask & 32u) ? "" : "KEEP-NAME"));
    for (unsigned group = 0u; group < BMS_UPDATE_CONFIG_GROUP_COUNT; ++group)
        assert(g_bms_config.revisions[group] == bms_update_revision((bms_update_group_t)group));
}

static u16 other_revision(bms_update_group_t group)
{
    return bms_update_revision(group) == 1u ? 2u : 1u;
}

static void test_ota_config_policy(void)
{
    for (unsigned mask = 0u; mask < 64u; ++mask) {
        fresh();
        bms_config_cache_t cfg = configured();
        for (unsigned group = 0u; group < BMS_UPDATE_CONFIG_GROUP_COUNT; ++group)
            if (mask & (1u << group)) cfg.revisions[group] = other_revision((bms_update_group_t)group);
        assert(bms_config_save_cache(&cfg));
        reboot(); bms_parameters_init();
        assert(bms_protection_params_valid()); assert_update_groups(mask);
        assert(g_bms_protection_params.cell_ovp_third_mv == g_bms_config.protect.cell_ovp_third_mv);
        u32 before = programs;
        reboot(); bms_parameters_init();
        assert(programs == before); assert_update_groups(mask);
    }
    for (unsigned group = 0u; group < BMS_UPDATE_CONFIG_GROUP_COUNT; ++group) {
        fresh(); bms_config_cache_t cfg = configured(); cfg.revisions[group] = other_revision((bms_update_group_t)group);
        assert(bms_config_save_cache(&cfg)); memcpy(backup, flash, sizeof(flash));
        for (int byte = 0; byte < (int)(24 + BMS_CONFIG_PAYLOAD_BYTES + 8); ++byte) {
            memcpy(flash, backup, sizeof(flash)); reboot(); cut = byte;
            bms_parameters_init(); assert(!bms_protection_params_valid());
            u8 payload[BMS_CONFIG_PAYLOAD_BYTES]; bms_config_cache_t persisted;
            assert(storage_record_load(&g_bms_config_store, payload));
            bms_config_decode(&persisted, payload); assert(!memcmp(&persisted, &cfg, sizeof(cfg)));
            reboot(); bms_parameters_init();
            assert(bms_protection_params_valid()); assert_update_groups(1u << group);
        }
    }
    assert(bms_parameter_read(0x2e05u) == 2u);
    for (unsigned group = 0; group < BMS_UPDATE_GROUP_COUNT; ++group)
        assert(bms_parameter_read((u16)(0x2e80u + group)) == bms_update_revision((bms_update_group_t)group));
    puts("PASS OTA Config: 64 combinations, keep/reset isolation, applied SW values, every byte cut, restart idempotence, policy readback");
}

static void test_ota_state_events(void)
{
    for (unsigned domain = 0; domain < 2; ++domain) {
        fresh();
        bms_state_persist_t cfg = g_bms_state;
        cfg.soc = 88u; cfg.cycle = 99u;
        if (domain == 0u) cfg.soc_revision = other_revision(BMS_UPDATE_SOC_STATE);
        assert(bms_state_save(&cfg));
        assert(bms_event_log_note_sleep());
        if (domain == 1u) {
            u8 payload[BMS_EVENT_PAYLOAD_BYTES]; bms_event_log_encode(payload);
            bms_event_log_put_u16le(&payload[BMS_EVENT_PAYLOAD_BYTES - 2u], other_revision(BMS_UPDATE_EVENTS));
            assert(storage_record_save(&g_bms_event_log.store, payload));
        }
        memcpy(backup, flash, sizeof(flash));
        unsigned payload_size = domain == 1u ? BMS_EVENT_PAYLOAD_BYTES : BMS_STATE_PAYLOAD_BYTES;
        for (int byte = 0; byte < (int)(24 + payload_size + 8); ++byte) {
            memcpy(flash, backup, sizeof(flash)); reboot(); cut = byte;
            bms_parameters_init(); assert(!bms_protection_params_valid());
            reboot(); bms_parameters_init(); assert(bms_protection_params_valid());
            assert(g_bms_state.soc == (domain == 0u ? 60u : 88u));
            assert(g_bms_state.cycle == (domain == 0u ? 0u : 99u));
            assert((bms_event_log_read_reg(0u) >> 8) == (domain == 1u ? 0u : BMS_SLEEP));
            u32 before = programs;
            reboot(); bms_parameters_init(); assert(programs == before);
        }
    }
    puts("PASS OTA State/Event: independent SOC/event resets, every byte cut, startup inhibit, restart idempotence");
}

static void test_protection_commit(void) {
 fresh();
 bms_protection_params_t old=g_bms_protection_params, next=old, loaded;
 next.cell_ovp_third_mv=3999;
 for(int byte=0;byte<(int)(24+BMS_CONFIG_PAYLOAD_BYTES+8);++byte) {
  memcpy(backup,flash,sizeof(flash));cut=byte;
  assert(!bms_protection_params_commit(&next));
  assert(!memcmp(&g_bms_protection_params,&old,sizeof(old)) && bms_protection_params_valid());
  assert(bms_config_store_get_protect(&loaded) && !memcmp(&loaded,&old,sizeof(old)));
  memcpy(flash,backup,sizeof(flash));reboot();bms_parameters_init();
 }
 assert(!bms_protection_params_commit(0));assert(bms_protection_params_valid());
 assert(bms_protection_params_commit(&next));assert(!memcmp(&g_bms_protection_params,&next,sizeof(next)));
 reboot();bms_parameters_init();assert(!memcmp(&g_bms_protection_params,&next,sizeof(next)));
 puts("PASS SW candidate: every journal byte cut leaves live/cache unchanged, validity retained, success/reboot consistent");
}



static void test_storage_reliability(void){
 fresh();u32 seq=g_bms_config_store.next_sequence;
 for(unsigned i=0;i<10;++i)assert(bms_config_reset_business());
 assert(g_bms_config_store.next_sequence==seq);
 unsigned slots=g_bms_state_store.slots_per_sector;
 for(unsigned i=1;i<slots;++i)assert(bms_state_store_write_all(i%100,0,i));
 u32 old=g_bms_state.soc,old_addr=g_bms_state_store.latest_addr;
 u32 before_erase=erases,before_program=programs;cut=0;now=60u*32000u;
 for(unsigned i=0;i<120;++i){bms_state_store_update_and_log_if_changed(77,0,77);now+=5u*32000u;}
 assert(erases-before_erase==1);assert(programs-before_program==3);
 assert(g_bms_state_store.latest_addr==old_addr && g_bms_state.soc==old);
 reboot();assert(bms_state_store_init());assert(g_bms_state.soc==old);
 fresh();old_addr=g_bms_state_store.latest_addr;cut=10;
 assert(!bms_state_store_write_all(77,0,77));cut=-1;now+=5u*32000u;before_erase=erases;
 assert(bms_state_store_write_all(78,0,78));assert(erases==before_erase);
 assert(g_bms_state_store.latest_addr==old_addr+2u*g_bms_state_store.slot_size);
 fresh();slots=g_bms_state_store.slots_per_sector;
 for(unsigned i=1;i<slots;++i)assert(bms_state_store_write_all(i%100,0,i));
 old_addr=g_bms_state_store.latest_addr;before_erase=erases;before_program=programs;erase_ok=0;
 for(unsigned i=0;i<120;++i){assert(!bms_state_store_write_all(77,0,77));now+=5u*32000u;}
 assert(erases-before_erase==3u && programs==before_program);
 assert(g_bms_state_store.latest_addr==old_addr);
 fresh();begin_ok=0;before_erase=erases;before_program=programs;
 for(unsigned i=0;i<120;++i){assert(!bms_state_store_write_all(77,0,77));now+=5u*32000u;}
 assert(erases==before_erase && programs==before_program && g_bms_state_store.write_failures==0u);
 begin_ok=1;assert(bms_state_store_write_all(77,0,77));
 for(unsigned domain=0;domain<3;++domain){
  fresh();reboot();read_ok=0;before_program=programs;before_erase=erases;
  assert(!bms_config_get_user(&(bms_user_params_t){0}));
  assert(bms_event_log_read_reg(0)==0 && bms_event_log_read_repeat(0)==0);
  if(domain==0)assert(!bms_config_store_init());
  if(domain==1)assert(!bms_state_store_init());
  if(domain==2)assert(!bms_event_log_init());
  assert(programs==before_program && erases==before_erase);
 }
 puts("PASS storage: no-op Config, 120 failures bounded to 3 writes/1 erase, partial-slot reuse, IO errors never default-write, cache-only reads");
}
static void test_event_semantics(void){
 bms_event_log_sample_t sample={0};
 fresh();sample.vcell_ovp=1;now=59u*32000u;bms_event_log_poll_1s(&sample);
 sample.vcell_ovp=0;now=60u*32000u;bms_event_log_poll_1s(&sample);
 assert(!g_bms_event_log.dirty);u16 pos=g_bms_event_log.write_pos;
 sample.vcell_ovp=1;now=61u*32000u;bms_event_log_poll_1s(&sample);
 assert(pos==g_bms_event_log.write_pos && bms_event_log_read_repeat(0)==2 && g_bms_event_log.dirty);
 fresh();memset(&sample,0,sizeof(sample));bms_event_log_note_startup();
 now=3700u*32000u;sample.vcell_ovp=1;bms_event_log_poll_1s(&sample);
 assert(bms_event_log_read_reg(0)==((VCELL_OVP<<8)|2u));
 fresh();memset(&sample,0,sizeof(sample));sample.cbc_err=1;bms_event_log_poll_1s(&sample);
 sample.vcell_ovp=1;bms_event_log_poll_1s(&sample);pos=g_bms_event_log.write_pos;
 sample.cbc_err=0;bms_event_log_poll_1s(&sample);assert(pos==g_bms_event_log.write_pos);
 assert(bms_event_log_note_sleep());bms_event_log_cancel_sleep();
 assert(bms_event_log_note_sleep());assert((bms_event_log_read_reg(0)>>8)==BMS_SLEEP);
 assert(g_bms_event_log.write_pos==pos+2u);
 fresh();memset(&sample,0,sizeof(sample));sample.vcell_ovp=1;bms_event_log_poll_1s(&sample);
 reboot();assert(bms_event_log_init());assert(bms_event_log_read_reg(0)==0);
 fresh();memset(&sample,0,sizeof(sample));
 now=UINT32_MAX-16000u;g_bms_event_log.clock_tick_32k=now;
 now+=32000u;bms_event_log_poll_1s(&sample);assert(g_bms_event_log.interval_s==1u);
 now+=16000u;bms_event_log_poll_1s(&sample);assert(g_bms_event_log.interval_s==1u);
 now+=16000u;bms_event_log_poll_1s(&sample);assert(g_bms_event_log.interval_s==2u);
 sample.vcell_ovp=1;bms_event_log_poll_1s(&sample);u8 payload[BMS_EVENT_PAYLOAD_BYTES];
 bms_event_log_encode(payload);payload[2]=255;assert(!bms_event_log_decode(payload));
 puts("PASS events: checkpoint-independent merging, elapsed time, CBC onset only, sleep cancellation, abrupt reset window, fractional tick/wrap, semantic decode");
}

int main(void){test_storage_reliability();test_event_semantics();test_config_codec_layout();test_protection_commit();test_ota_config_policy();test_ota_state_events();test_parameter_protocol();test_user_parameters();test_diag_boot();test_config_schema();test_state();test_events();test_boot_gate();return 0;}
