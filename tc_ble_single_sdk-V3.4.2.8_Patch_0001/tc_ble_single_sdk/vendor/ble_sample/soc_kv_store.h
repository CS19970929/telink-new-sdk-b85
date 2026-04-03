#pragma once

#include "tl_common.h"
#include "drivers.h"
#include "conf.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef FLASH_ADR_SOC_A
#define FLASH_ADR_SOC_A   FLASH_ADDR_USER_DATA_BASE1
#endif

#ifndef FLASH_ADR_SOC_B
#define FLASH_ADR_SOC_B   (FLASH_ADR_SOC_A + FLASH_SECTOR_SIZE)
#endif

#ifndef SOC_SECTOR_SIZE
#define SOC_SECTOR_SIZE   4096
#endif

#ifndef SOC_KV_DEFAULT_SOC
#define SOC_KV_DEFAULT_SOC    60
#endif

#ifndef SOC_KV_DEFAULT_DSG
#define SOC_KV_DEFAULT_DSG    0
#endif

#ifndef SOC_KV_DEFAULT_CYCLE
#define SOC_KV_DEFAULT_CYCLE  100
#endif

typedef enum {
    SOC_ITEM_SOC   = 0,
    SOC_ITEM_SOH   = 1,
    SOC_ITEM_CYCLE = 2,
} soc_item_t;

typedef struct {
    u16 soc;
    u16 dsg;
    u16 cycle;
} soc_kv_data_t;

typedef struct {
    u32 active_base;
    u32 write_off;
    u32 next_seq;
    u8  loaded;
    u8  tail_dirty;
} soc_kv_dbg_t;

int  soc_kv_store_init(void);
soc_kv_data_t soc_kv_store_get(void);
int  soc_kv_store_put(soc_item_t item, u16 value);
void soc_kv_store_update_and_log_if_changed(u16 soc, u16 dsg, u16 cycle);
void soc_kv_store_factory_reset(void);
soc_kv_dbg_t soc_kv_store_get_dbg(void);

#ifdef __cplusplus
}
#endif
