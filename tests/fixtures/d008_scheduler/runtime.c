#include <stdint.h>
#include <assert.h>
typedef uint32_t u32;
typedef uint8_t u8;
typedef enum {MODE_FACTORY,MODE_NORMAL} bms_mode_t;
#define FACTORY_TIME_LIMIT_MIN (60u*24u*3u)
#define BMS_ERROR_EEPROM_STORE 1
static u32 now, saved, writes, reads, errors;
static int store_ok=1;
static u32 pm_get_32k_tick(void){reads++;return now;}
static int bms_state_store_init(void){return store_ok;}
static u32 bms_state_store_get_runtime_min(void){return saved;}
static int bms_state_store_write_runtime_min(u32 v){writes++;if(!store_ok)return 0;saved=v;return 1;}
static int bms_state_store_reset_runtime(void){if(!store_ok)return 0;saved=0;return 1;}
static void bms_error_raise(int e){(void)e;errors++;}
/* PRODUCTION_SOURCE */
int main(void){
 saved=FACTORY_TIME_LIMIT_MIN;Runtime_Init();reads=0;
 for(int i=0;i<1000;i++){now+=32000;Runtime_Poll();}
 assert(reads==0&&writes==0&&Runtime_GetMode()==MODE_NORMAL);
 assert(Runtime_ReenterFactoryMode());assert(Runtime_GetMode()==MODE_FACTORY);
 now+=RUNTIME_PM_TICKS_PER_MIN-1;Runtime_Poll();assert(saved==0);
 now++;Runtime_Poll();assert(saved==1&&writes==1);
 store_ok=0;now+=RUNTIME_PM_TICKS_PER_MIN;Runtime_Poll();assert(errors==1&&saved==1);
 store_ok=1;now+=RUNTIME_PM_TICKS_PER_MIN;Runtime_Poll();assert(saved==3);
 saved=FACTORY_TIME_LIMIT_MIN-1;now=0xffff0000u;Runtime_Init();
 now+=RUNTIME_PM_TICKS_PER_MIN;Runtime_Poll();assert(saved==FACTORY_TIME_LIMIT_MIN&&Runtime_GetMode()==MODE_NORMAL);
 reads=0;Runtime_Poll();assert(reads==0);
 assert(Runtime_FactoryReset());now+=123;Runtime_PrepareForDeepSleep();
 now+=RUNTIME_PM_TICKS_PER_MIN;Runtime_CancelPendingDeepSleep();Runtime_Poll();assert(saved==0);
 return 0;
}
