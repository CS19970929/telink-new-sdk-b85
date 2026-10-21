#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include "storage_record.h"
#include "bms_soc_eta.h"
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32;
#define BMS_PRODUCT_DEFAULT_CAPACITY_0P1AH 1000u
#define BMS_HEATER_START_TEMP_X10 400u
#define BMS_HEATER_STOP_TEMP_X10 450u
#define BMS_HEATER_ENABLE_DEFAULT 1u
#define BMS_BATTERY_BALANCE_START_MV_FOR(c) ((c)==1u ? 3400u : 4100u)
#define BMS_BATTERY_BALANCE_START_DELTA_MV 50u
#define BMS_BATTERY_BALANCE_STOP_DELTA_MV 30u
#define BMS_PRODUCT_WIRE_ID 8u
#define BMS_PRODUCT_CELL_COUNT 24u
#define BTNAME_SUFFIX_MAX_LEN 23u
#define BMS_PRODUCT_ID 8u
#define BMS_PRODUCT_CHEMISTRY 1u
#define BMS_PRODUCT_SOC_PROFILE_ID 1u
/* 夹具在重启之间选择镜像的更新范围；生产固件仍使用编译期常量。 */
static unsigned test_ota_mask;
#define BMS_OTA_UPDATE_MASK test_ota_mask
#define BMS_OTA_UPDATE_ID_0 0u
#define BMS_OTA_UPDATE_ID_1 0x1234u
#define BMS_OTA_UPDATE_ID_2 0x5678u
#define BMS_OTA_UPDATE_ID_3 0x9abcu
#define BMS_OTA_UPDATE_ID_4 0xdef0u
#define BMS_OTA_UPDATE_ID_5 0x2468u
#include "bms_update_policy.h"
#define BMS_ERROR_EEPROM_STORE 1
#define BMS_ERROR_EEPROM_COM 2
#define BMS_SLEEP_LOW_CELL_MV 2800u
#define BMS_SLEEP_LOW_SECONDS 60u
#define BMS_SLEEP_NORMAL_CELL_MV 3000u
#define BMS_SLEEP_NORMAL_SECONDS 86400u
/* MACROS */
/* TYPES */
static u32 now, errors, programs, erases;
static u32 sector_erases[34];
static int read_ok=1, erase_ok=1, config_read_fail;
static int cut=-1, begin_ok=1, region_ok=1;
static int erase_cut_sector=-1;
static unsigned erase_cut_bytes;
static u8 flash[34u*4096u]; /* 32个业务扇区，另2个扇区作越界哨兵。 */
static u8 backup[sizeof(flash)];
static void bms_error_raise(int e){(void)e;++errors;}
static int bms_sw_protection_validate_params(const bms_protection_params_t *p){return p!=0;}
void bms_afe_hw_profile_build_default(bms_afe_hw_profile_t*p){memset(p,0,sizeof(*p));p->schema_version=1;p->afe_model=0x1124;p->cov_mv=3650;}
void bms_afe_hw_profile_build_chemistry_default(u8 c,bms_afe_hw_profile_t*p){bms_afe_hw_profile_build_default(p);p->cov_mv=c==1u?3750u:4200u;}
static unsigned config_inhibited;
void bms_afe_invalidate_configuration(void){++config_inhibited;}
u8 bms_soc_reload_configuration(void){return 1u;}
u8 bms_afe_hw_profile_validate(const bms_afe_hw_profile_t*p){return p && p->schema_version==1;}
static int begin(void*c){(void)c;return begin_ok;}
static void end(void*c){(void)c;}
static int read_flash(void*c,u32 a,u8*b,u32 n){(void)c;if(!read_ok || (config_read_fail && a<4u*4096u))return 0;assert(a+n<=sizeof(flash));memcpy(b,flash+a,n);return 1;}
static int program(void*c,u32 a,const u8*b,u32 n){(void)c;++programs;assert(a+n<=sizeof(flash));for(u32 i=0;i<n;i++){if(cut==0)return 0;if(cut>0)--cut;assert((flash[a+i]|b[i])==flash[a+i]);flash[a+i]&=b[i];}return 1;}
static int erase(void*c,u32 a,u32 n){(void)c;++erases;++sector_erases[a/4096u];assert(a+n<=sizeof(flash));if(!erase_ok)return 0;if((int)(a/4096u)==erase_cut_sector){assert(erase_cut_bytes<n);memset(flash+a,255,erase_cut_bytes);return 0;}memset(flash+a,255,n);return 1;}
static const storage_port_t port={0,4096,4,255,begin,end,read_flash,program,erase};
const storage_port_t*bms_storage_platform_port(void){return &port;}
int bms_storage_platform_erase_factory_legacy(void){if(!bms_ota_factory_reset_requested()||!begin(0))return 0;for(u32 sector=28u;sector<32u;++sector)if(!erase(0,sector*4096u,4096u)){end(0);return 0;}end(0);return 1;}
int bms_storage_platform_region(bms_storage_domain_t d,storage_region_t*r){if(!region_ok){bms_diag_result((uint8_t)d,DIAG_LAYOUT);return 0;}if(d==BMS_STORAGE_DOMAIN_CONFIG){r->base=0;r->size=4*4096;}else if(d==BMS_STORAGE_DOMAIN_STATE){r->base=4*4096;r->size=8*4096;}else if(d==BMS_STORAGE_DOMAIN_EVENT){r->base=12*4096;r->size=16*4096;}else return 0;return 1;}
uint32_t bms_time_ms(void){return now;}
uint32_t bms_time_now_32k(void){return now*32u;}
/* PRODUCTION */
static void reboot(void){
 bms_diag_init();region_ok=1;read_ok=1;erase_ok=1;config_read_fail=0;
 g_bms_config_ready=0;g_bms_state_ready=0;s_state_loaded=0;s_migration_pending=0;
 memset(&g_bms_config_store,0,sizeof(g_bms_config_store));
 memset(&g_bms_state_store,0,sizeof(g_bms_state_store));
 g_bms_state_last_failed=0;
 memset(&g_bms_event_log,0,sizeof(g_bms_event_log));
 memset(&g_bms_event_store,0,sizeof(g_bms_event_store));
 s_storage_startup_valid=0;s_protection_params_valid=0;cut=-1;begin_ok=1;erase_cut_sector=-1;
}
static void fresh(void){
 test_ota_mask=0u;
 /* TC32 的 program_bytes 会整字读取 store->port，生产 RAM 对象必须四字节对齐。 */
 assert(((uintptr_t)&g_bms_event_store % 4u)==0u);
 memset(flash,255,sizeof(flash));now=0;reboot();bms_parameters_init();assert(bms_protection_params_valid());
}

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
 u32 seed=0x43464734u;
 assert(BMS_CONFIG_PAYLOAD_BYTES==414u);
 for(unsigned scenario=0;scenario<10000u;++scenario){
  bms_config_cache_t cfg,decoded;
  u8 expected[414],encoded[416];
  memset(&cfg,0,sizeof(cfg));memset(expected,0,sizeof(expected));
  for(unsigned byte=0;byte<sizeof(cfg);++byte)((u8*)&cfg)[byte]=(u8)codec_random(&seed,scenario);
  cfg.bt_name_suffix[23]=0;cfg.sleep.afe_delay_minutes=30u;
  codec_expect_le(expected,BMS_PRODUCT_ID,4u);
  for(unsigned i=0;i<65u;++i){u16 v;memcpy(&v,(u8*)&cfg.protect+2u*i,2u);codec_expect_le(expected+4u+2u*i,v,2u);}
  for(unsigned i=0;i<3u;++i){u32 v;memcpy(&v,(u8*)&cfg.system+4u*i,4u);codec_expect_le(expected+134u+4u*i,v,4u);}
  for(unsigned i=0;i<35u;++i){u16 v;memcpy(&v,(u8*)&cfg.afe_hw+2u*i,2u);codec_expect_le(expected+146u+2u*i,v,2u);}
  memcpy(expected+216u,cfg.bt_name_suffix,24u);
  for(unsigned i=0;i<7u;++i){u16 v;memcpy(&v,(u8*)&cfg.user+2u*i,2u);codec_expect_le(expected+240u+2u*i,v,2u);}
  codec_expect_le(expected+254u,(u32)cfg.user.current_offset_ma,4u);
  codec_expect_le(expected+258u,cfg.user.current_gain_ppm,4u);memcpy(expected+262u,cfg.user.serial,32u);
  for(unsigned i=0;i<6u;++i)codec_expect_le(expected+294u+2u*i,cfg.ota_update_id[i],2u);
  for(unsigned i=0;i<50u;++i){u16 v;memcpy(&v,(u8*)&cfg.soc+2u*i,2u);codec_expect_le(expected+306u+2u*i,v,2u);}
  for(unsigned i=0;i<4u;++i){u16 v;memcpy(&v,(u8*)&cfg.sleep+2u*i,2u);codec_expect_le(expected+406u+2u*i,v,2u);}
  memset(encoded,0xa5,sizeof(encoded));bms_config_encode(&cfg,encoded+1u);
  assert(encoded[0]==0xa5 && encoded[415]==0xa5 && !memcmp(encoded+1u,expected,sizeof(expected)));
  memset(&decoded,0,sizeof(decoded));bms_config_decode(&decoded,expected);
  assert(bms_config_cache_equal(&cfg,&decoded));
 }
 puts("PASS Config codec: 10000 independent 414-byte vectors, one SOC identity, full curve, sleep, calibration and guards");
}
static void test_config_cache_equality(void)
{
 bms_config_cache_t base,candidate;
 u8 encoded_base[BMS_CONFIG_PAYLOAD_BYTES],encoded_candidate[BMS_CONFIG_PAYLOAD_BYTES];
 /* 用真实 codec 作行为依据；逐位覆盖持久字段和 native ABI 填充。 */
 memset(&base,0xa5,sizeof(base));bms_config_encode(&base,encoded_base);
 assert(bms_config_cache_equal(&base,&base));
 for(unsigned byte=0;byte<sizeof(base);++byte){
  for(unsigned bit=0;bit<8u;++bit){
   memcpy(&candidate,&base,sizeof(candidate));
   ((u8*)&candidate)[byte]^=(u8)(1u<<bit);
   bms_config_encode(&candidate,encoded_candidate);
   assert(bms_config_cache_equal(&base,&candidate)==
          (memcmp(encoded_base,encoded_candidate,sizeof(encoded_base))==0));
  }
 }
 fresh();assert(g_bms_config_store.has_latest);
 u32 before=programs;
 assert(bms_config_save_cache(&g_bms_config));assert(programs==before);
 /* OTA/默认配置待提交时，即使字段相同也不能被 no-op 跳过。 */
 g_bms_config_needs_save=1u;
 assert(bms_config_save_cache(&g_bms_config));
 assert(programs>before && !g_bms_config_needs_save);
}
static void test_config_schema(void){
 /* CRC 有效也不能让超宽化学体系/profile 截断为 u8 AUTO 后放行。 */
 fresh();bms_config_cache_t malformed=g_bms_config;
 malformed.soc.chemistry=0x100u;
 assert(bms_config_save_cache(&malformed));reboot();assert(!bms_config_store_init());
 assert(!bms_config_current_soc_profile());
 assert(!bms_config_store_validate_startup());
 fresh();malformed=g_bms_config;malformed.soc.profile_id=0x100u;
 assert(bms_config_save_cache(&malformed));reboot();assert(!bms_config_store_init());
 assert(!bms_config_current_soc_profile() && !bms_config_store_validate_startup());
 fresh();malformed=g_bms_config;malformed.soc.ocv_rest_prepare_s=777u;
 assert(bms_config_save_cache(&malformed));reboot();assert(!bms_config_store_init());
 assert(!bms_config_current_soc_profile() && !bms_config_store_validate_startup());
 /* 系统入口保存选型，SOC getter 读取同一来源；失败不能发布新值。 */
 fresh();bms_config_system_params_t selection;assert(bms_config_store_get_system(&selection));
 selection.battery_chemistry=2u;selection.soc_profile_id=2u;
 assert(!bms_config_store_set_system(&selection));
 assert(bms_config_current_soc_profile() && bms_config_current_soc_profile()->chemistry==1u && bms_config_current_soc_profile()->profile_id==1u);
 fresh(); bms_config_system_params_t cap;assert(bms_config_store_get_system(&cap));
 cap.capacity_factory=BMS_SOC_CAPACITY_MAX_0P1AH+1u;assert(!bms_config_store_set_system(&cap));
 assert(g_bms_config.system.capacity_factory==1000);
 bms_config_cache_t old=g_bms_config,next=old;
 next.protect.cell_ovp_third_mv=3999;next.afe_hw.cov_mv=4100;
 next.soc.profile_version=2u;next.soc.ocv_rest_prepare_s=777;strcpy(next.bt_name_suffix,"persist-name");
 memcpy(backup,flash,sizeof(flash));
 for(int byte=0;byte<(int)(32+((BMS_CONFIG_PAYLOAD_BYTES+3u)&~3u));byte++){
  memcpy(flash,backup,sizeof(flash));reboot();assert(bms_config_store_init());cut=byte;
  assert(!bms_config_save_cache(&next));assert(!memcmp(&g_bms_config,&old,sizeof(old)));
  reboot();assert(bms_config_store_init());assert(!memcmp(&g_bms_config,&old,sizeof(old)));
 }
 assert(bms_config_save_cache(&next));reboot();assert(bms_config_store_init());
 assert(!memcmp(&g_bms_config,&next,sizeof(next)));
 /* A committed, CRC-valid payload belonging to another board is rejected. */
 u8 payload[BMS_CONFIG_PAYLOAD_BYTES];bms_config_encode(&next,payload);
 bms_config_put_u32le(payload,14u);assert(storage_record_save(&g_bms_config_store,payload));
 reboot();u32 written=programs;assert(!bms_config_store_init());
 assert(!bms_config_store_validate_startup() && programs==written);
 /* Older schema/magic is never decoded into the new structure. */
 fresh();memset(flash,255,4*4096);reboot();assert(bms_config_store_init());
 g_bms_config_store.magic=0x43464731u;assert(bms_config_save_cache(&next));
 reboot();assert(!bms_config_store_init());
 assert(!bms_config_store_validate_startup());
 /* 无有效记录时，CRC 损坏和只剩 payload/尾部均不得当作出厂空白。 */
 for(unsigned mode=0;mode<3u;++mode){
  fresh();memset(flash,255,4u*4096u);
  if(mode==0u)flash[32u]=0u;
  if(mode==1u)flash[4u*4096u-1u]=0u;
  if(mode==2u){bms_config_encode(&next,payload);assert(storage_record_save(&g_bms_config_store,payload));flash[g_bms_config_store.latest_addr+16u]^=1u;}
  reboot();written=programs;bms_parameters_init();
  assert(!bms_protection_params_valid() && !g_bms_config_ready && programs==written);
 }
 puts("PASS Config: new schema, all byte cuts, atomic rollback, product isolation, old format rejection");
}

static void test_state(void){
 fresh();
 bms_state_store_poll(61,2,3);assert(g_bms_state.soc==60);
 now=60*1000;cut=0;bms_state_store_poll(62,4,5);u32 count=programs;
 for(unsigned i=0;i<100;i++)bms_state_store_poll(63,6,7);
 assert(programs==count);assert(errors);now+=5*1000;cut=-1;
 bms_state_store_poll(64,8,9);assert(g_bms_state.soc==64);
 assert(bms_state_store_save(65,9,10));reboot();assert(bms_state_store_init());assert(g_bms_state.soc==65);

 /* schema 5 原24字节后追加满电资格和依据。 */
 assert(BMS_STATE_PAYLOAD_BYTES==32u);
 u8 state_payload[BMS_STATE_PAYLOAD_BYTES];bms_state_encode(&g_bms_state,state_payload);
 for(unsigned i=12;i<24;++i)assert(state_payload[i]==0);
 assert(!bms_state_store_save(101,0,0));assert(!bms_state_store_save(60,101,0));
 assert(!bms_state_store_save(60,0,65536));
 bms_state_persist_t old=g_bms_state,next=old;next.soc=77;
 memcpy(backup,flash,sizeof(flash));
 for(int byte=0;byte<(int)(24+BMS_STATE_PAYLOAD_BYTES+8);byte++){
  memcpy(flash,backup,sizeof(flash));reboot();assert(bms_state_store_init());cut=byte;
  assert(!bms_state_save(&next));assert(g_bms_state.soc==65);
  reboot();assert(bms_state_store_init());assert(g_bms_state.soc==65);
 }
 assert(bms_state_save(&next));reboot();assert(bms_state_store_init());
 assert(g_bms_state.soc==77);
 now=UINT32_MAX-1000;g_bms_state_last_attempt_ms=now;
 bms_state_store_poll(78,0,0);now+=(60*1000);bms_state_store_poll(79,0,0);assert(g_bms_state.soc==79);
 puts("PASS State: coalescing, failure/backoff, compact cycle state, atomic rollback, all byte cuts, wrap");
}
/* 独立构造旧ST4，验证首条ST5落盘前旧最新扇区不会被擦掉。 */
static void test_state_v4_migration(void){
 fresh();storage_region_t region={4u*4096u,8u*4096u};storage_record_store_t legacy;
 memset(flash+region.base,255,region.size);memset(&legacy,0,sizeof(legacy));
 assert(storage_record_open(&legacy,&port,region,0x53543400u+BMS_PRODUCT_ID,4u,24u));
 uint8_t bytes[24]={0};codec_expect_le(bytes,100u,4u);codec_expect_le(bytes+4,23u,4u);codec_expect_le(bytes+8,456u,4u);
 for(unsigned i=12;i<24;i++)bytes[i]=(uint8_t)(i+11u);
 assert(storage_record_save(&legacy,bytes));uint32_t old_addr=legacy.latest_addr;
 memcpy(backup,flash,sizeof(flash));
 for(int byte=0;byte<(int)(24u+BMS_STATE_PAYLOAD_BYTES+8u);byte++){
  memcpy(flash,backup,sizeof(flash));reboot();cut=byte;
  assert(!bms_state_store_init());bms_state_store_data_t state=bms_state_store_get();
  assert(state.soc==100u&&state.dsg==23u&&state.cycle==456u&&!state.full_confirmed);
  assert(!memcmp(flash+old_addr,backup+old_addr,56u));
  reboot();assert(bms_state_store_init());state=bms_state_store_get();
  assert(state.soc==100u&&state.cycle==456u&&!state.full_confirmed&&!state.full_basis);
  for(unsigned i=0;i<BMS_OTA_UPDATE_ID_WORDS;i++)assert(g_bms_state.ota_update_id[i]==(uint16_t)(bytes[12+2*i]|((uint16_t)bytes[13+2*i]<<8)));
  assert((g_bms_state_store.latest_addr-region.base)/4096u!=(old_addr-region.base)/4096u);
 }
 /* 同一启动迁移失败保留写游标；三次后不继续物理写，不重复擦同一扇区。 */
 memcpy(flash,backup,sizeof(flash));reboot();cut=0;assert(!bms_state_store_init());
 uint32_t erase_count=erases;
 for(unsigned attempt=0;attempt<2u;attempt++){now+=5000u;cut=0;assert(!bms_state_store_init());assert(erases==erase_count);}
 uint32_t program_count=programs;now+=5000u;assert(!bms_state_store_init());assert(programs==program_count&&erases==erase_count);
 memcpy(flash,backup,sizeof(flash));reboot();assert(bms_state_store_init());
 /* 资格和SOC必须同一记录提交；失败只恢复旧的完整组合。 */
 bms_state_store_data_t qualified={100u,23u,456u,1u,0x12345678u};
 assert(bms_state_store_save_snapshot(&qualified));memcpy(backup,flash,sizeof(flash));
 bms_state_store_data_t cleared=qualified;cleared.soc=98u;cleared.full_confirmed=0u;cleared.full_basis=0u;
 for(int byte=0;byte<(int)(24u+BMS_STATE_PAYLOAD_BYTES+8u);byte++){
  memcpy(flash,backup,sizeof(flash));reboot();assert(bms_state_store_init());cut=byte;
  assert(!bms_state_store_save_snapshot(&cleared));reboot();assert(bms_state_store_init());
  bms_state_store_data_t state=bms_state_store_get();assert(state.soc==100u&&state.full_confirmed==1u&&state.full_basis==qualified.full_basis);
 }
 now+=1u;cut=-1;qualified.full_confirmed=0u;qualified.full_basis=0u;
 bms_state_store_poll_snapshot(&qualified);assert(g_bms_state.full_confirmed==0u);
 puts("PASS State v5: v4 migration, first-save byte cuts, legacy sector retention, OTA ID and atomic full qualification");
}
static void test_events(void){
 fresh();bms_event_log_sample_t sample={0};sample.vcell_ovp=1;
 u32 count=programs;bms_event_log_poll_1s(&sample);sample.vcell_ovp=0;bms_event_log_poll_1s(&sample);sample.vcell_ovp=1;bms_event_log_poll_1s(&sample);
 assert(programs==count);
 now=60*1000;cut=0;bms_event_log_poll_1s(&sample);count=programs;
 bms_event_log_poll_1s(&sample);assert(programs==count && g_bms_event_log.dirty);
 assert(!bms_event_log_note_sleep());now+=5*1000;cut=-1;assert(bms_event_log_note_sleep());reboot();assert(bms_event_log_init());assert((bms_event_log_read_reg(0)>>8)==BMS_SLEEP);

 memcpy(backup,flash,sizeof(flash));
 for(int byte=0;byte<(int)(32+((BMS_EVENT_PAYLOAD_BYTES+3u)&~3u));byte++){
  memcpy(flash,backup,sizeof(flash));reboot();assert(bms_event_log_init());cut=byte;
  bms_event_log_note_startup();assert(!bms_event_log_write_snapshot());
  assert((bms_event_log_read_reg(0)>>8)==BMS_START_UP);
  reboot();assert(bms_event_log_init());assert((bms_event_log_read_reg(0)>>8)==BMS_SLEEP);
 }
 bms_event_log_note_startup();assert(bms_event_log_write_snapshot());reboot();assert(bms_event_log_init());
 assert((bms_event_log_read_reg(0)>>8)==BMS_START_UP);
 count=programs;assert(bms_event_log_init());assert(programs==count);
 puts("PASS Event: coalescing without hidden writes, failure retention, forced shutdown flush, atomic snapshots, byte cuts");
}
static void test_boot_gate(void){
 for(unsigned domain=0;domain<3;domain++){
  fresh();
  /* Erase just this domain: boot must first commit its new defaults. */
  unsigned base=domain==0?0:(domain==1?4*4096:12*4096);
  unsigned size=domain==0?4*4096:8*4096;
  memset(flash+base,255,size);reboot();cut=0;
  bms_parameters_init();assert(bms_protection_params_valid()==(domain!=0));
  cut=-1;assert(bms_protection_params_commit(&g_bms_protection_params));assert(bms_protection_params_valid()==(domain!=0));
  reboot();bms_parameters_init();assert(bms_protection_params_valid());
 }
 puts("PASS startup: Config/State/Event first-save failure gates, protection commit cannot bypass");
}

static void test_diag_boot(void){
 fresh();reboot();region_ok=0;u32 before=errors;
 bms_parameters_init();
 assert(errors-before==3);assert(bms_diag_cached_word(36)==1);
 assert(bms_diag_cached_word(37)==DIAG_LAYOUT && bms_diag_cached_word(38)==DIAG_LAYOUT);
 assert(bms_diag_cached_word(52)==1 && bms_diag_cached_word(84)==1);
 assert(!bms_event_log_init());assert(bms_diag_cached_word(84)==2);
 bms_parameters_diag_poll();assert(bms_diag_cached_word(144)==0);
 bms_diag_freeze_boot();region_ok=1;assert(bms_config_store_init());
 assert(bms_diag_cached_word(37)==DIAG_LAYOUT && bms_diag_cached_word(38)==DIAG_LAYOUT);
 fresh();assert(bms_diag_cached_word(39)==1 && bms_diag_cached_word(37)==0);
 puts("PASS diagnostics: one Config failure, independent State/Event attempts, frozen first failure, blank defaults");
}
static void test_config_failure_preserves_state_cache(void){
 fresh();assert(bms_state_store_save(63u,27u,123u));
 reboot();config_read_fail=1;bms_parameters_init();
 assert(!bms_protection_params_valid());assert(g_bms_state_ready && g_bms_event_log.ready);
 bms_state_store_data_t loaded=bms_state_store_get();
 assert(loaded.soc==63u && loaded.dsg==27u && loaded.cycle==123u);
 assert(bms_diag_cached_word(36)==1 && bms_diag_cached_word(52)==1 && bms_diag_cached_word(84)==1);
 config_read_fail=0;assert(bms_protection_params_commit(&g_bms_protection_params));
 assert(!bms_protection_params_valid());
 loaded=bms_state_store_get();assert(loaded.soc==63u && loaded.cycle==123u);
 puts("PASS Config read failure: real State cache preserves SOC/discharge/cycles, every domain initialized once, online commit cannot clear gate");
}
static int access_active=1;static unsigned identity_updates,capacity_updates;
static u8 live_soc=60;
bms_soc_state_t g_bms_soc;
typedef struct {int32_t raw_current_ma,current_ma;u32 sample_time_ms,sample_tick_32k;} bms_afe_aux_measurements_t;
static int32_t switch_current_ma;
static u32 switch_sample_age;
static u8 switch_sample_valid=1u;
static u8 switch_apply_ok=1u;
static u8 bms_afe_get_aux_measurements(bms_afe_aux_measurements_t*s){memset(s,0,sizeof(*s));s->current_ma=switch_current_ma;s->sample_time_ms=now-switch_sample_age;return switch_sample_valid;}
static u8 bms_afe_bus_access_allowed(void){return 1u;}
static u16 bms_afe_get_guard_diagnostic_bits(void){return DIAG_GUARD_SAMPLES_QUALIFIED;}
u8 bms_afe_hw_profile_apply_committed(void){return switch_apply_ok;}
static u8 bms_board_heater_supported(void){return 1;}
static u8 bms_afe_hw_access_is_active(void){return access_active;}
u8 get_soc_real(void){return live_soc;}
bms_state_store_data_t bms_soc_manual_state_snapshot(u8 soc){
 bms_state_store_data_t data={soc,g_bms_soc.discharge_fraction_percent,g_bms_soc.cycle_count,soc==100u,soc==100u?0x11223344u:0u};
 return data;
}
void set_soc_param(u8 soc){
 live_soc=soc;g_bms_soc.full_confirmed=soc==100u;g_bms_soc.full_basis=soc==100u?0x11223344u:0u;
}
void bms_soc_nominal_capacity_changed(void){capacity_updates++;}
void bms_soc_cycle_count_changed(uint32_t cycle){g_bms_soc.cycle_count=cycle;capacity_updates++;}
static u8 bms_reset_software_parameters(void){return 0;}
static u8 bms_reset_afe_parameters(void){return access_active?0:2;}
void bms_product_info_refresh(void){identity_updates++;}
/* PARAMETER_PROTOCOL */
static u16 parameter_read_one(u16 reg) {
    u8 data[2]; assert(bms_parameter_read_block(reg,1u,data));
    return (u16)(((u16)data[0]<<8)|data[1]);
}
static void test_parameter_protocol(void){
 fresh();u8 retired[]={0,6};u32 flash_before=programs;u16 sequence=parameter_read_one(0x2e04);
 assert((parameter_read_one(0x2e02u)&0x0200u)!=0u);
 assert(parameter_read_one(0x2eb0u)==0x534fu&&parameter_read_one(0x2eb1u)==1u);
 u8 profile_command[]={0,1,0,0,'h','s','4','5','6',0};
 assert(bms_parameter_write(0x2ef8u,4u,profile_command)==3u);
 profile_command[9]=1u;assert(bms_parameter_write(0x2ef8u,5u,profile_command)==2u);profile_command[9]=0u;
 assert(bms_parameter_write(0x2ef8u,5u,profile_command)==0u);
 u16 profile_generation=parameter_read_one(0x2ee5u);u8 deadband[]={0,250};
 assert(bms_parameter_write(0x2f24u,1u,deadband)==0u);
 assert(programs==flash_before); /* 分帧暂存不写 Flash，只有完整提交才持久化。 */
 profile_command[0]=0u;profile_command[1]=2u;profile_command[2]=(u8)(profile_generation>>8);profile_command[3]=(u8)profile_generation;
 assert(bms_parameter_write(0x2ef8u,5u,profile_command)==0u&&parameter_read_one(0x2eb6u)==250u&&parameter_read_one(0x2ee4u)==1u);
 sequence=parameter_read_one(0x2e04u);
 assert(programs>flash_before);flash_before=programs;
 access_active=0;assert(bms_parameter_write(0x2e10,1,retired)==3);
 access_active=1;assert(bms_parameter_write(0x2e10,1,retired)==3);
 assert(programs==flash_before && parameter_read_one(0x2e04)==sequence);
 u8 heat[]={0,1,1,134,1,194},begin[]={0,0},chunk[8]={'S','N','0','1',0,0,0,0};
 assert(parameter_read_one(0x2e00)==0xd008);assert(bms_parameter_write(0x2e20,1,heat)==3);
 assert(bms_parameter_write(0x2e20,3,heat)==0);assert(parameter_read_one(0x2e21)==390);
 cut=0;heat[0]=0;heat[1]=0;assert(bms_parameter_write(0x2e20,3,heat)==4);assert(parameter_read_one(0x2e20)==1);cut=-1;
 access_active=0;assert(bms_parameter_write(0x2e40,1,begin)==2);access_active=1;
 u32 before=programs;assert(bms_parameter_write(0x2e40,1,begin)==0);
 u16 gen=parameter_read_one(0x2e06);u8 commit[]={(u8)(gen>>8),(u8)gen};
 assert(bms_parameter_write(0x2e40,1,commit)==3);
 for(u16 i=0;i<16;i+=4)assert(bms_parameter_write(0x2e50+i,4,chunk)==0);
 assert(programs==before);assert(bms_parameter_write(0x2e40,1,commit)==0);assert(identity_updates==1);
 reboot();bms_parameters_init();assert(parameter_read_one(0x2e30)==0x534e);
 now=UINT32_MAX-100;assert(bms_parameter_write(0x2e40,1,begin)==0);now+=61u*1000u;
 assert(bms_parameter_write(0x2e50,4,chunk)==3);
 assert(bms_parameter_write(0x2e24,4,0)==3);assert(bms_parameter_write(0x2e20,0,heat)==3);
 /* 人工100%的SOC/许可/basis必须原子持久化，再发布运行值。 */
 live_soc=60u;g_bms_soc.full_confirmed=0u;g_bms_soc.full_basis=0u;
 u8 soc[]={0,100};cut=0;assert(bms_parameter_write(0x1005,1,soc)==4);
 assert(live_soc==60&&!g_bms_soc.full_confirmed&&g_bms_state.soc==60u);
 cut=-1;now+=5000u;assert(bms_parameter_write(0x1005,1,soc)==0);
 assert(live_soc==100u&&g_bms_soc.full_confirmed&&g_bms_state.soc==100u&&g_bms_state.full_confirmed);
 reboot();bms_parameters_init();assert(g_bms_state.soc==100u&&g_bms_state.full_confirmed&&g_bms_state.full_basis==0x11223344u);
 soc[1]=40u;assert(bms_parameter_write(0x1005,1,soc)==0);
 assert(live_soc==40u&&!g_bms_state.full_confirmed&&!g_bms_state.full_basis);
 puts("PASS parameter protocol: atomic heater, failed-save rollback, SN authorization/full bitmap/commit/reboot/expiry/wrap, no staging Flash, null/zero length, SOC RAM unchanged on failure");
}
static void test_user_parameters(void){
 fresh();bms_user_params_t v,old;assert(bms_config_get_user(&old));
 assert(old.heater_enable==1 && old.heater_start_x10==400 && old.heater_stop_x10==450);
 v=old;v.heater_start_x10=460;assert(!bms_config_set_user(&v));
 v=old;v.current_offset_ma=-123;v.current_gain_ppm=1100000;strcpy(v.serial,"D008-TEST");
 assert(bms_config_set_user(&v));reboot();assert(bms_config_store_init());assert(bms_config_get_user(&old));assert(!memcmp(&old,&v,sizeof(v)));
 memcpy(backup,flash,sizeof(flash));
 for(int byte=0;byte<(int)(32+((BMS_CONFIG_PAYLOAD_BYTES+3u)&~3u));byte++){
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
 assert(!bms_state_store_save(88,9,999));assert(g_bms_state.soc==state.soc);
 cut=-1;assert(g_bms_state.soc==state.soc);reboot();assert(bms_state_store_init());assert(g_bms_state.soc==state.soc);
 assert(bms_state_store_save(88,9,999));reboot();assert(bms_state_store_init());assert(g_bms_state.soc==88 && g_bms_state.cycle==999);
 puts("PASS new parameters: heater validation, SN persistence, Config byte cuts, independent reset, 10000 current arithmetic oracle cases, synchronous State rollback");
}
static void test_balance_permission(void)
{
 fresh();u8 words[]={0,0,0x0d,0xac,0,30,0,15}; /* 3500/30/15；许可单独切换。 */
 for(unsigned allowed=0u;allowed<2u;++allowed){
  words[1]=(u8)allowed;
  assert(bms_parameter_write(0x2e70u,4u,words)==0u);
  reboot();bms_parameters_init();assert(parameter_read_one(0x2e70u)==allowed);
  assert(bms_config_reset_business());assert(parameter_read_one(0x2e70u)==allowed);
  test_ota_mask=4u;bms_config_cache_t next=g_bms_config;
  memset(next.ota_update_id,0xff,sizeof(next.ota_update_id));
  assert(bms_config_save_cache(&next));
  reboot();bms_parameters_init();assert(bms_protection_params_valid());
  assert(parameter_read_one(0x2e70u)==allowed);
  assert(parameter_read_one(0x2e71u)==BMS_BALANCE_START_VOLTAGE_MV_DEFAULT);
 }
 words[1]=0u;assert(bms_parameter_write(0x2e70u,4u,words)==0u);
 memcpy(backup,flash,sizeof(flash));words[1]=1u;
 /* 每个写中断点都必须保留旧禁止状态，不能先发布尚未提交的允许。 */
 for(int byte=0;byte<(int)(32+((BMS_CONFIG_PAYLOAD_BYTES+3u)&~3u));++byte){
  memcpy(flash,backup,sizeof(flash));reboot();bms_parameters_init();cut=byte;
  assert(bms_parameter_write(0x2e70u,4u,words)==4u);
  assert(parameter_read_one(0x2e70u)==0u);
  reboot();bms_parameters_init();assert(parameter_read_one(0x2e70u)==0u);
 }
 assert(bms_parameter_write(0x2e70u,4u,words)==0u);
 reboot();bms_parameters_init();assert(parameter_read_one(0x2e70u)==1u);
 puts("PASS balance permission: explicit disable/allow, reboot, business reset, OTA retention and interrupted allow rollback");
}
/* OTA 策略回归：通过真实记录模拟客户旧值，启动流程负责按 BIN 选择提交。 */
static bms_config_cache_t configured(void)
{
    bms_config_cache_t cfg = g_bms_config;
    cfg.protect.cell_ovp_third_mv = 3999u;
    cfg.afe_hw.cov_mv = 4100u;
    cfg.system.capacity_factory = 1234u;
    cfg.user.heater_enable = 0u;
    cfg.user.balance_enable = 0u; /* 用户禁止不随任何 OTA 组重新允许。 */
    cfg.user.balance_start_mv = 3500u;
    /* 模拟客户保留的v2模型；v3固定静置3600秒，777不能冒充合法v3。 */
    cfg.soc.profile_version = 2u;
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
    assert(g_bms_config.user.balance_enable == 0u);
    assert(g_bms_config.user.balance_start_mv == ((mask & 4u) ? BMS_BALANCE_START_VOLTAGE_MV_DEFAULT : 3500u));
    assert(g_bms_config.soc.ocv_rest_prepare_s == ((mask & 8u) ? BMS_SOC_OCV_REST_MS / 1000u : 777u));
    assert(g_bms_config.soc.profile_version == ((mask & 8u) ? BMS_SOC_POLICY_VERSION : 2u));
    assert(g_bms_config.user.current_offset_ma == -123);
    assert(g_bms_config.user.current_gain_ppm == 1100000u);
    assert(!strcmp(g_bms_config.user.serial, "KEEP-SN"));
    assert(!strcmp(g_bms_config.bt_name_suffix, "KEEP-NAME"));
    if (mask) assert(bms_ota_update_id_matches(g_bms_config.ota_update_id));
}

static void test_ota_config_policy(void)
{
    for (unsigned mask = 0u; mask < 16u; ++mask) {
        fresh();
        bms_config_cache_t cfg = configured();
        for (unsigned group = 0u; group < BMS_UPDATE_CONFIG_GROUP_COUNT; ++group)
            cfg.ota_update_id[group] = (u16)(65535u - group); /* 未知客户旧编号。 */
        assert(bms_config_save_cache(&cfg));
        test_ota_mask = mask;
        reboot(); bms_parameters_init();
        assert(bms_protection_params_valid()); assert_update_groups(mask);
        assert(g_bms_protection_params.cell_ovp_third_mv == g_bms_config.protect.cell_ovp_third_mv);
        u32 before = programs;
        reboot(); bms_parameters_init();
        assert(programs == before); assert_update_groups(mask);
    }
    for (unsigned group = 0u; group < 4u; ++group) {
        fresh(); bms_config_cache_t cfg = configured();
        assert(bms_config_save_cache(&cfg)); memcpy(backup, flash, sizeof(flash));
        test_ota_mask = 1u << group;
        for (int byte = 0; byte < (int)(32+((BMS_CONFIG_PAYLOAD_BYTES+3u)&~3u)); ++byte) {
            memcpy(flash, backup, sizeof(flash)); reboot(); cut = byte;
            bms_parameters_init(); assert(!bms_protection_params_valid());
            u8 payload[BMS_CONFIG_PAYLOAD_BYTES]; bms_config_cache_t persisted;
            assert(storage_record_load(&g_bms_config_store, payload));
            bms_config_decode(&persisted, payload); assert(!memcmp(&persisted, &cfg, sizeof(cfg)));
            reboot(); bms_parameters_init();
            assert(bms_protection_params_valid()); assert_update_groups(1u << group);
        }
    }
    assert(parameter_read_one(0x2e05u) == 2u);
    for (unsigned group = 0; group < BMS_UPDATE_GROUP_COUNT; ++group)
        assert(parameter_read_one((u16)(0x2e80u + group)) == bms_update_revision((bms_update_group_t)group));
    /* 同一个 BIN 执行后，客户再次修改参数，重启不能再次覆盖。 */
    fresh(); test_ota_mask = 15u; reboot(); bms_parameters_init();
    bms_config_cache_t custom = configured(); assert(bms_config_save_cache(&custom));
    reboot(); bms_parameters_init(); assert(!memcmp(&g_bms_config, &custom, sizeof(custom)));
    /* 换一个更新 BIN 标记时重新执行；保留模式不消耗以前的标记。 */
    custom.ota_update_id[1] ^= 1u; assert(bms_config_save_cache(&custom));
    reboot(); bms_parameters_init(); assert_update_groups(15u);
    /* 只有明确更新四组时才接受串数变化，且不重置身份/校准。 */
    fresh(); custom = configured(); custom.system.series_num = 16u;
    assert(bms_config_save_cache(&custom));
    reboot(); bms_parameters_init(); assert(!bms_protection_params_valid());
    test_ota_mask = 15u; reboot(); bms_parameters_init(); assert(bms_protection_params_valid());
    assert(g_bms_config.system.series_num == 24u); assert_update_groups(15u);
    /* SOC 类型改变但未更新电压保护时拒绝放行。 */
    fresh(); custom = configured(); custom.soc.chemistry = 2u;
    custom.soc.profile_id = 2u;
    assert(bms_config_save_cache(&custom)); test_ota_mask = 8u;
    reboot(); bms_parameters_init(); assert(!bms_protection_params_valid());
    test_ota_mask = 15u; reboot(); bms_parameters_init(); assert(bms_protection_params_valid());
    assert_update_groups(15u);
    puts("PASS OTA Config: 16 group combinations, unknown legacy identifiers, every byte cut, idempotence and custom-value retention");
}

static void test_ota_state_events(void)
{
    for (unsigned reset_state = 0; reset_state < 2; ++reset_state) {
        fresh();
        bms_state_persist_t cfg = g_bms_state;
        cfg.soc = 88u; cfg.cycle = 99u;
        assert(bms_state_save(&cfg));
        assert(bms_event_log_note_sleep());
        {
            u8 payload[BMS_EVENT_PAYLOAD_BYTES]; bms_event_log_encode(payload);
            assert(storage_record_save(&g_bms_event_store, payload));
        }
        test_ota_mask = reset_state ? 64u : 0u;
        memcpy(backup, flash, sizeof(flash));
        unsigned payload_size = BMS_STATE_PAYLOAD_BYTES;
        for (int byte = 0; byte < (int)(24 + payload_size + 8); ++byte) {
            memcpy(flash, backup, sizeof(flash)); reboot(); cut = byte;
            bms_parameters_init(); assert(bms_protection_params_valid());
            assert(g_bms_state_ready == !reset_state); /* State 失败不撤销安全配置资格。 */
            if (reset_state) {
                u8 payload[BMS_STATE_PAYLOAD_BYTES]; bms_state_persist_t persisted;
                assert(storage_record_load(&g_bms_state_store, payload));
                bms_state_decode(&persisted, payload, 0u);
                assert(persisted.soc == 88u && persisted.cycle == 99u);
                assert(!bms_ota_update_id_matches(persisted.ota_update_id));
            }
            reboot(); bms_parameters_init(); assert(bms_protection_params_valid());
            assert(g_bms_state.soc == (reset_state ? 60u : 88u));
            assert(g_bms_state.cycle == (reset_state ? 0u : 99u));
            assert((bms_event_log_read_reg(0u) >> 8) == BMS_SLEEP);
            u32 before = programs;
            reboot(); bms_parameters_init(); assert(programs == before);
        }
        /* 更新执行后客户在线修改状态，同一 BIN 重启不能再清零循环或 SOC。 */
        assert(bms_state_store_save(77u, 3u, 123u));
        u32 before = programs;
        reboot(); bms_parameters_init(); assert(programs == before);
        assert(g_bms_state.soc == 77u && g_bms_state.dsg == 3u && g_bms_state.cycle == 123u);
    }
    puts("PASS OTA State/Event: selected SOC reset, legacy revisions ignored, events retained, byte cuts and idempotence");
}

static void test_protection_commit(void) {
 fresh();
 bms_protection_params_t old=g_bms_protection_params, next=old, loaded;
 next.cell_ovp_third_mv=3999;
 for(int byte=0;byte<(int)(32+((BMS_CONFIG_PAYLOAD_BYTES+3u)&~3u));++byte) {
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
 for(unsigned i=1;i<slots;++i)assert(bms_state_store_save(i%100,0,i));
 u32 old=g_bms_state.soc,old_addr=g_bms_state_store.latest_addr;
 u32 before_erase=erases,before_program=programs;cut=0;now=60u*1000u;
 for(unsigned i=0;i<120;++i){bms_state_store_poll(77,0,77);now+=5u*1000u;}
 assert(erases-before_erase==1);assert(programs-before_program==3);
 assert(g_bms_state_store.latest_addr==old_addr && g_bms_state.soc==old);
 reboot();assert(bms_state_store_init());assert(g_bms_state.soc==old);
 fresh();old_addr=g_bms_state_store.latest_addr;cut=10;
 assert(!bms_state_store_save(77,0,77));cut=-1;now+=5u*1000u;before_erase=erases;
 assert(bms_state_store_save(78,0,78));assert(erases==before_erase);
 assert(g_bms_state_store.latest_addr==old_addr+2u*g_bms_state_store.slot_size);
 fresh();slots=g_bms_state_store.slots_per_sector;
 for(unsigned i=1;i<slots;++i)assert(bms_state_store_save(i%100,0,i));
 old_addr=g_bms_state_store.latest_addr;before_erase=erases;before_program=programs;erase_ok=0;
 for(unsigned i=0;i<120;++i){assert(!bms_state_store_save(77,0,77));now+=5u*1000u;}
 assert(erases-before_erase==3u && programs==before_program);
 assert(g_bms_state_store.latest_addr==old_addr);
 fresh();begin_ok=0;before_erase=erases;before_program=programs;
 for(unsigned i=0;i<120;++i){assert(!bms_state_store_save(77,0,77));now+=5u*1000u;}
 assert(erases==before_erase && programs==before_program && g_bms_state_store.write_failures==0u);
 begin_ok=1;assert(bms_state_store_save(77,0,77));
 for(unsigned domain=0;domain<3;++domain){
  fresh();reboot();read_ok=0;before_program=programs;before_erase=erases;
  assert(!bms_config_get_user(&(bms_user_params_t){0}));
  assert(bms_event_log_read_reg(0)==0);
  if(domain==0)assert(!bms_config_store_init());
  if(domain==1)assert(!bms_state_store_init());
  if(domain==2)assert(!bms_event_log_init());
  assert(programs==before_program && erases==before_erase);
 }
 puts("PASS storage: no-op Config, 120 failures bounded to 3 writes/1 erase, partial-slot reuse, IO errors never default-write, cache-only reads");
}
static void test_event_semantics(void){
 assert(bms_event_log_map_interval(0u)==171u && bms_event_log_map_interval(60u)==171u);
 assert(bms_event_log_map_interval(61u)==1u && bms_event_log_map_interval(3600u)==1u);
 assert(bms_event_log_map_interval(3601u)==2u && bms_event_log_map_interval(168u*3600u)==168u);
 assert(bms_event_log_map_interval(168u*3600u+1u)==170u && bms_event_log_map_interval(UINT32_MAX)==170u);
 bms_event_log_sample_t sample={0};
 fresh();sample.vcell_ovp=1;now=59u*1000u;bms_event_log_poll_1s(&sample);
 sample.vcell_ovp=0;now=60u*1000u;bms_event_log_poll_1s(&sample);
 assert(!g_bms_event_log.dirty);u16 pos=g_bms_event_log.write_pos;
 sample.vcell_ovp=1;now=61u*1000u;bms_event_log_poll_1s(&sample);
 assert(pos==g_bms_event_log.write_pos && !g_bms_event_log.dirty);
 fresh();memset(&sample,0,sizeof(sample));bms_event_log_note_startup();
 now=3700u*1000u;sample.vcell_ovp=1;bms_event_log_poll_1s(&sample);
 assert(bms_event_log_read_reg(0)==((VCELL_OVP<<8)|2u));
 fresh();memset(&sample,0,sizeof(sample));sample.cbc_err=1;bms_event_log_poll_1s(&sample);
 sample.vcell_ovp=1;bms_event_log_poll_1s(&sample);pos=g_bms_event_log.write_pos;
 sample.cbc_err=0;bms_event_log_poll_1s(&sample);assert(pos==g_bms_event_log.write_pos);
 assert(bms_event_log_note_sleep());
 assert(bms_event_log_note_sleep());assert((bms_event_log_read_reg(0)>>8)==BMS_SLEEP);
 assert(g_bms_event_log.write_pos==pos+1u);
 fresh();memset(&sample,0,sizeof(sample));sample.vcell_ovp=1;bms_event_log_poll_1s(&sample);
 reboot();assert(bms_event_log_init());assert(bms_event_log_read_reg(0)==0);
 fresh();memset(&sample,0,sizeof(sample));
 now=UINT32_MAX-500u;g_bms_event_log.clock_tick_ms=now;
 now+=1000u;bms_event_log_poll_1s(&sample);assert(g_bms_event_log.interval_ms==1000u);
 now+=500u;bms_event_log_poll_1s(&sample);assert(g_bms_event_log.interval_ms==1500u);
 now+=500u;bms_event_log_poll_1s(&sample);assert(g_bms_event_log.interval_ms==2000u);
 sample.vcell_ovp=1;bms_event_log_poll_1s(&sample);u8 payload[BMS_EVENT_PAYLOAD_BYTES];
 bms_event_log_encode(payload);payload[2]=255;assert(!bms_event_log_decode(payload));
 /* 原时间编码按完整秒取整：60.999s仍合并，61.000s追加。 */
 fresh();memset(&sample,0,sizeof(sample));
 sample.vcell_ovp=1;bms_event_log_poll_1s(&sample);pos=g_bms_event_log.write_pos;
 now+=60999u;sample.vcell_ovp=0;bms_event_log_poll_1s(&sample);
 sample.vcell_ovp=1;bms_event_log_poll_1s(&sample);
 assert(g_bms_event_log.write_pos==pos && g_bms_event_log.interval_ms==60999u);
 ++now;sample.vcell_ovp=0;bms_event_log_poll_1s(&sample);
 sample.vcell_ovp=1;bms_event_log_poll_1s(&sample);
 assert(g_bms_event_log.write_pos==pos+1u && g_bms_event_log.interval_ms==0u);
 assert(bms_event_log_read_reg(0)==((VCELL_OVP<<8)|1u));
 /* 追加事件清除不足一秒余量；饱和及大跨度不溢出。 */
 now+=999u;sample.vbus_ovp=1;bms_event_log_poll_1s(&sample);
 assert(g_bms_event_log.interval_ms==0u);
 now+=1u;bms_event_log_poll_1s(&sample);assert(g_bms_event_log.interval_ms==1u);
 now+=168u*3600u*1000u-1u;bms_event_log_poll_1s(&sample);
 assert(g_bms_event_log.interval_ms==168u*3600u*1000u);
 assert(bms_event_log_map_interval(g_bms_event_log.interval_ms/1000u)==168u);
 now+=999u;bms_event_log_poll_1s(&sample);
 assert(bms_event_log_map_interval(g_bms_event_log.interval_ms/1000u)==168u);
 ++now;bms_event_log_poll_1s(&sample);
 assert(g_bms_event_log.interval_ms==(168u*3600u+1u)*1000u);
 assert(bms_event_log_map_interval(g_bms_event_log.interval_ms/1000u)==170u);
 now+=UINT32_MAX;bms_event_log_poll_1s(&sample);
 assert(g_bms_event_log.interval_ms==(168u*3600u+1u)*1000u);
 puts("PASS events: checkpoint-independent merging, elapsed time, CBC onset only, sleep cancellation, abrupt reset window, fractional milliseconds/wrap/saturation, semantic decode");
}


/* 执行真实独立 journal 与参数入口；检查旧 CFG2 不变、整组保存及每字节掉电。 */
static void test_sleep_config(void)
{
 fresh();bms_sleep_config_t cfg;
 bms_config_get_sleep(&cfg);assert(cfg.low_cell_mv==2800u && cfg.low_delay_minutes==1u);
 u8 values[]={0x0a,0x28,0,2,0x0b,0xb8,0x05,0xa0,0,30};
 assert(bms_parameter_write(0x2e90u,4u,values)==3u);
 assert(bms_parameter_write(0x2e91u,1u,values)==2u);
 assert(bms_parameter_write(0x2e98u,5u,values)==2u);
 assert(bms_parameter_write(0x2e90u,5u,values)==0u);
 u32 written=programs;assert(bms_parameter_write(0x2e90u,5u,values)==0u&&programs==written);
 bms_config_get_sleep(&cfg);cfg.low_cell_mv=cfg.normal_cell_mv;assert(!bms_config_set_sleep(&cfg));
 cfg.low_cell_mv=2600u;cfg.low_delay_minutes=1441u;assert(!bms_config_set_sleep(&cfg));
 cfg.low_delay_minutes=2u;cfg.afe_delay_minutes=1u;assert(!bms_config_set_sleep(&cfg));
 reboot();bms_parameters_init();bms_config_get_sleep(&cfg);
 assert(cfg.afe_delay_minutes==30u && cfg.low_delay_minutes==2u);
 assert(bms_config_reset_business());assert(parameter_read_one(0x2e91u)==2u);
 memcpy(backup,flash,sizeof(flash));
 for(unsigned interrupted=0;interrupted<=32u+((BMS_CONFIG_PAYLOAD_BYTES+3u)&~3u);++interrupted){
  memcpy(flash,backup,sizeof(flash));reboot();bms_parameters_init();
  bms_config_get_sleep(&cfg);cfg.low_delay_minutes=10u;cut=(int)interrupted;
  int saved=bms_config_set_sleep(&cfg);
  if(!saved){bms_config_get_sleep(&cfg);assert(cfg.low_delay_minutes==2u);}
  cut=-1;reboot();bms_parameters_init();bms_config_get_sleep(&cfg);
  assert(cfg.low_delay_minutes==(saved?10u:2u));
  assert(!memcmp(flash+4u*4096u,backup+4u*4096u,sizeof(flash)-4u*4096u));
 }
 reboot();read_ok=0;written=programs;assert(!bms_config_store_init());
 bms_config_get_sleep(&cfg);assert(!bms_config_set_sleep(&cfg)&&programs==written);read_ok=1;
 puts("PASS merged sleep: unchanged five-word protocol, fixed AFE constant, all Config byte cuts, State/Event untouched");
}

static void test_chemistry_switch(void)
{
 fresh();
 u8 command[]={0,1,'h','s','4','5','6',0};
 u32 written=programs;
 assert(parameter_read_one(0x2e02u)&0x100u);
 assert(parameter_read_one(0x2ea0u)==0x4348u && parameter_read_one(0x2ea1u)==1u);
 assert(bms_parameter_write(0x2ea8u,3u,command)==3u && programs==written);
 command[7]=1u;
 assert(bms_parameter_write(0x2ea8u,4u,command)==2u && programs==written);
 command[7]=0u;command[1]=2u;
 assert(bms_parameter_write(0x2ea8u,4u,command)==3u && programs==written); /* 24S NMC 拒绝。 */
 command[1]=1u;switch_current_ma=1000;
 assert(bms_parameter_write(0x2ea8u,4u,command)==4u && programs==written);
 switch_current_ma=0;switch_sample_valid=0u;
 assert(bms_parameter_write(0x2ea8u,4u,command)==4u && programs==written);
 switch_sample_valid=1u;
 bms_soc_profile_config_t original_profile;
 bms_config_get_soc_profile(&original_profile);
 original_profile.current_deadband_ma=123u;original_profile.profile_version=17u;
 assert(bms_config_set_soc_profile(&original_profile));
 switch_current_ma=124;written=programs;
 assert(bms_parameter_write(0x2ea8u,4u,command)==4u && programs==written);
 switch_current_ma=0;
 bms_config_cache_t original=g_bms_config;
 original.protect.cell_ovp_third_mv=3999u;
 original.system.capacity_factory=777u;
 original.user.balance_enable=0u;
 original.user.current_offset_ma=42;
 original.user.current_gain_ppm=1200000u;
 memcpy(original.user.serial,"PRESERVE",9u);
 original.soc.current_deadband_ma=100u;
 assert(bms_config_save_cache(&original));
 memcpy(backup,flash,sizeof(flash));
 for(unsigned interrupted=0;interrupted<=32u+((BMS_CONFIG_PAYLOAD_BYTES+3u)&~3u);++interrupted) {
  memcpy(flash,backup,sizeof(flash));reboot();bms_parameters_init();
  config_inhibited=0u;cut=interrupted;
  int saved=bms_parameter_write(0x2ea8u,4u,command)==0u;
  assert(config_inhibited>0u);
  if(!saved) assert(g_bms_config.protect.cell_ovp_third_mv==3999u);
  cut=-1;reboot();bms_parameters_init();
  assert(g_bms_config.protect.cell_ovp_third_mv==(saved?0u:3999u)); /* 软件默认显式 stub。 */
  assert(g_bms_config.system.capacity_factory==777u && g_bms_config.user.balance_enable==0u);
  assert(g_bms_config.user.current_offset_ma==42 && g_bms_config.user.current_gain_ppm==1200000u);
  assert(!memcmp(g_bms_config.user.serial,"PRESERVE",9u));
  assert(!memcmp(g_bms_config.ota_update_id,original.ota_update_id,sizeof(original.ota_update_id)));
  assert(!memcmp(flash+4u*4096u,backup+4u*4096u,sizeof(flash)-4u*4096u));
  if(saved) {
   assert(g_bms_config.afe_hw.cov_mv==3750u);
   assert(g_bms_config.soc.current_deadband_ma==200u);
   assert(g_bms_config.soc.chemistry==1u && g_bms_config.soc.profile_id==1u);
  }
 }
 fresh();switch_apply_ok=0u;config_inhibited=0u;
 assert(bms_parameter_write(0x2ea8u,4u,command)==4u);
 assert(config_inhibited>0u && parameter_read_one(0x2ea4u)==3u);
 switch_apply_ok=1u;
 fresh();switch_sample_age=BMS_SAMPLE_MAX_POLL_GAP_MS+1u;
 assert(bms_parameter_write(0x2ea8u,4u,command)==0u); /* 年龄不再是门禁，完整有效静置读取仍可切换。 */
 switch_sample_age=0u;
 puts("PASS chemistry transaction: password, length, 24S limit, valid idle sample without ADC age, byte cuts, retained domains, apply failure inhibit");
}

static void assert_factory_region_clean(void)
{
 u32 cfg_sector=g_bms_config_store.latest_addr/4096u*4096u;
 for(u32 a=0u;a<32u*4096u;++a){
  if(a>=cfg_sector && a<cfg_sector+2u*g_bms_config_store.slot_size)continue;
  if(a>=4u*4096u && a<4u*4096u+g_bms_state_store.slot_size)continue;
  if(a>=12u*4096u && a<12u*4096u+g_bms_event_store.slot_size)continue;
  assert(flash[a]==255u);
 }
 assert(!memcmp(flash+32u*4096u,backup+32u*4096u,2u*4096u));
}

/* 恢复出厂包清空所有业务持久内容；正常固件不得把旧/损坏记录当空白。 */
static void test_factory_reset(void)
{
 fresh();
 bms_config_cache_t custom=configured();
 assert(bms_config_save_cache(&custom));
 assert(bms_state_store_save(88u, 23u, 99u));
 assert(bms_event_log_note_sleep());
 assert((bms_event_log_read_reg(0u)>>8)==BMS_SLEEP);
 for(unsigned sector=0u;sector<32u;++sector)flash[(sector+1u)*4096u-1u]=0xa5u;
 memset(flash+32u*4096u,0x5a,2u*4096u);
 memcpy(backup,flash,sizeof(flash));
 test_ota_mask=BMS_OTA_FACTORY_RESET_MASK;
 unsigned factory_bytes=3u*(32u+((BMS_CONFIG_PAYLOAD_BYTES+3u)&~3u))+
                        32u+((BMS_STATE_PAYLOAD_BYTES+3u)&~3u)+
                        32u+((BMS_EVENT_PAYLOAD_BYTES+3u)&~3u);
 /* Config pending + 搬迁pending + State + Event + Config done；
    每个切点后重启，都必须继续执行而不能提前授权 MOS。 */
 for(unsigned byte=0;byte<=factory_bytes;++byte){
  memcpy(flash,backup,sizeof(flash));reboot();cut=byte;
  bms_parameters_init();
  if(bms_protection_params_valid()){
   assert(g_bms_config.system.capacity_factory==1000u);
   assert(g_bms_state.soc==60u && g_bms_state.dsg==0u && g_bms_state.cycle==0u);
   assert(bms_event_log_read_reg(0u)==0u);
  }
  cut=-1;reboot();bms_parameters_init();
  assert(bms_protection_params_valid());
  assert(g_bms_config.system.capacity_factory==1000u);
  assert(g_bms_config.user.current_offset_ma==0);
  assert(g_bms_config.user.current_gain_ppm==1000000u);
  assert(g_bms_config.user.serial[0]==0 && g_bms_config.bt_name_suffix[0]==0);
  assert(g_bms_config.user.balance_enable==BMS_BALANCE_ENABLE_DEFAULT);
  assert(g_bms_config.soc.chemistry==BMS_PRODUCT_CHEMISTRY);
  assert(g_bms_config.sleep.low_cell_mv==BMS_SLEEP_LOW_CELL_MV);
  assert(g_bms_state.soc==60u && g_bms_state.dsg==0u && g_bms_state.cycle==0u);
  assert(bms_event_log_read_reg(0u)==0u);
  assert(g_bms_config.ota_update_id[0]==0u);
  assert(bms_ota_update_id_matches(g_bms_config.ota_update_id));
  assert_factory_region_clean();
  u32 written=programs,erased=erases;
  reboot();bms_parameters_init();assert(bms_protection_params_valid() && programs==written);
  assert(erases==erased);
 }
 /* 每个扇区的擦除前/部分擦除均失效关闭；恢复供电后续做，保留域外哨兵。 */
 const unsigned erase_cuts[]={0u,1u,2048u,4095u};
 for(int sector=0;sector<32;++sector)for(unsigned i=0;i<4u;++i){
  memcpy(flash,backup,sizeof(flash));reboot();erase_cut_sector=sector;erase_cut_bytes=erase_cuts[i];
  bms_parameters_init();assert(!bms_protection_params_valid());
  reboot();bms_parameters_init();assert(bms_protection_params_valid());
  assert_factory_region_clean();
 }
 /* 同一出厂包执行后，客户在线修改不应被再次恢复默认。 */
 bms_config_system_params_t system;
 assert(bms_config_store_get_system(&system));
 system.capacity_factory=1234u;
 assert(bms_config_store_set_system(&system));
 assert(bms_ota_update_id_matches(g_bms_config.ota_update_id));
 assert(bms_state_store_save(79u,9u,18u));
 flash[28u*4096u]=0xa5u; /* 模拟同ID旧出厂包留下的旧版数据，不自动补擦。 */
 u32 retained_erases=erases;
 reboot();bms_parameters_init();
 assert(bms_protection_params_valid());
 assert(g_bms_config.system.capacity_factory==1234u && g_bms_state.soc==79u);
 assert(erases==retained_erases && flash[28u*4096u]==0xa5u);
 /* 回到普通保参固件后，仍能读取出厂包写入的健康 CFG4。 */
 test_ota_mask=0u;reboot();bms_parameters_init();
 assert(bms_protection_params_valid());
 assert(g_bms_config.system.capacity_factory==1234u && g_bms_state.soc==79u);

 /* 不解析 CFG2：旧数据正常启动禁止输出，明确出厂包直接恢复默认。 */
 fresh();memset(flash,255,4u*4096u);flash[0]=0x32u;
 reboot();u32 count=programs;bms_parameters_init();
 assert(!bms_protection_params_valid() && programs==count);
 test_ota_mask=BMS_OTA_FACTORY_RESET_MASK;
 reboot();bms_parameters_init();
 assert(bms_protection_params_valid() && g_bms_config.system.capacity_factory==1000u);

 /* 出厂事务已写入 pending 但未完成 State，普通镜像不能绕过该标记。 */
 fresh();custom=configured();assert(bms_config_save_cache(&custom));
 test_ota_mask=BMS_OTA_FACTORY_RESET_MASK;
 reboot();cut=448;bms_parameters_init();assert(!bms_protection_params_valid());
 test_ota_mask=0u;reboot();bms_parameters_init();assert(!bms_protection_params_valid());
 test_ota_mask=BMS_OTA_FACTORY_RESET_MASK;
 reboot();bms_parameters_init();assert(bms_protection_params_valid());
 printf("PASS factory reset: %u write-byte cuts, 128 partial-erase cases, full business purge, guard sectors, idempotence and pending fail-closed\n",factory_bytes+1u);
}

static void test_merged_profile(void)
{
 test_factory_reset();
 fresh();bms_soc_profile_config_t old,next;
 bms_config_get_soc_profile(&old);next=old;
 next.current_deadband_ma=321u;++next.profile_version;
 const bms_soc_profile_config_t *view=bms_config_current_soc_profile();
 memcpy(backup,flash,sizeof(flash));
 for(unsigned cut_byte=0;cut_byte<32u+((BMS_CONFIG_PAYLOAD_BYTES+3u)&~3u);++cut_byte){
  memcpy(flash,backup,sizeof(flash));reboot();bms_parameters_init();
  u16 generation=bms_config_soc_profile_generation();cut=(int)cut_byte;
  assert(!bms_config_set_soc_profile(&next));
  assert(generation==bms_config_soc_profile_generation() && !memcmp(view,&old,sizeof(old)));
  cut=-1;reboot();bms_parameters_init();assert(!memcmp(view,&old,sizeof(old)));
 }
 assert(bms_config_set_soc_profile(&next));assert(!memcmp(view,&next,sizeof(next)));
 u32 before=programs;u16 generation=bms_config_soc_profile_generation();
 assert(bms_config_set_soc_profile(&next));assert(programs==before && generation==bms_config_soc_profile_generation());
 reboot();bms_parameters_init();assert(!memcmp(view,&next,sizeof(next)));
 next.chemistry=257u;assert(!bms_config_set_soc_profile(&next));
 puts("PASS merged profile: all 448 write-byte cuts, stable view, cache/generation rollback, duplicate suppression, reboot");
}

int main(void){test_merged_profile();test_chemistry_switch();test_sleep_config();test_storage_reliability();test_event_semantics();test_config_codec_layout();test_config_cache_equality();test_protection_commit();test_ota_config_policy();test_ota_state_events();test_parameter_protocol();test_user_parameters();test_balance_permission();test_diag_boot();test_config_failure_preserves_state_cache();test_config_schema();test_state();test_state_v4_migration();test_events();test_boot_gate();return 0;}
