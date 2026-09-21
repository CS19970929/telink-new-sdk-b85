#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;
#define APP_FLASH_PROTECTION_ENABLE 1
#define FLASH_PAGE_SIZE 256u
#define FLASH_SECTOR_SIZE 4096u
/* MACROS */
/* TYPES */
u8 ota_is_working;
static u32 tick, page_calls, erase_calls;
static int stack_available=1, verify_ok=1, unlocked;
static u32 pm_get_32k_tick(void){return tick;}
static int app_flash_lock_restore_enabled(void){return stack_available;}
static void flash_store_begin_modify(void){unlocked=1;}
static void flash_store_end_modify(void){unlocked=0;}
static void flash_read_page(u32 a,int n,u8*b){(void)a;memset(b,255,(unsigned)n);}
static void flash_write_page(u32 a,int n,u8*b){(void)b;assert(n>0 && (a%256)+(u32)n<=256);++page_calls;tick+=32;}
static void flash_erase_sector(u32 a){assert(a%4096==0);++erase_calls;tick+=16000;}
static int flash_store_verify_bytes(u32 a,const u8*b,u32 n){(void)a;(void)b;(void)n;return verify_ok;}
static int flash_store_verify_erased(u32 a,u32 n){(void)a;(void)n;return verify_ok;}
static uint16_t last_reason;
void bms_diag_storage_error(uint16_t r,uint32_t a){(void)a;last_reason=r;}
/* PRODUCTION */
int main(void){

 u8 bytes[406]={0};bms_storage_diagnostics_t d;
 ota_is_working=1;assert(!bms_storage_telink_begin(0));assert(last_reason==DIAG_OTA);ota_is_working=0;
 stack_available=0;assert(!bms_storage_telink_begin(0));assert(last_reason==DIAG_LOCK);stack_available=1;
 assert(bms_storage_telink_begin(0) && unlocked);
 assert(bms_storage_telink_program(0,250,bytes,sizeof(bytes)));assert(page_calls==3);
 assert(bms_storage_telink_erase(0,4096,4096));assert(erase_calls==1);
 bms_storage_telink_end(0);assert(!unlocked);
 tick=UINT32_MAX-100;verify_ok=0;assert(!bms_storage_telink_program(0,0,bytes,1));
 assert(!bms_storage_telink_begin(0));tick+=5*32000-1;assert(!bms_storage_telink_begin(0));++tick;
 assert(bms_storage_telink_begin(0));verify_ok=1;assert(bms_storage_telink_program(0,0,bytes,1));bms_storage_telink_end(0);
 bms_storage_platform_get_diagnostics(&d);
 assert(d.verify_failures==1 && d.deferred_writes==4 && d.max_erase_ticks_32k==16000 && d.max_program_ticks_32k==96);
 puts("PASS platform: OTA/session exclusion, page split, verify failure, retry/wrap, latency diagnostics (mock time)");
}
