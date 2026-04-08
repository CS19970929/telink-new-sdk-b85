#pragma once

#include "tl_common.h"
#include "drivers.h"
#include "conf.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SOC_KV_HOT_SECTOR_SIZE
#define SOC_KV_HOT_SECTOR_SIZE   FLASH_SECTOR_SIZE
#endif

#ifndef SOC_KV_HOT_SECTORS
#define SOC_KV_HOT_SECTORS   FLASH_ADDR_RUN_KV_SECTORS
#endif

/*
 * Unified SOC defaults:
 * - empty/formatted hot KV recovery
 * - one-shot FW upgrade SOC reset
 * - SocEnhance factory initialization
 *
 * New code should use SOC_PARAM_DEFAULT_*.
 * SOC_KV_DEFAULT_* stay as compatibility aliases only.
 */
#ifndef SOC_PARAM_DEFAULT_SOC
#define SOC_PARAM_DEFAULT_SOC    ((u32)FAC_INIT_soc)
#endif

#ifndef SOC_PARAM_DEFAULT_DSG
#define SOC_PARAM_DEFAULT_DSG    60u
#endif

#ifndef SOC_PARAM_DEFAULT_CYCLE
#define SOC_PARAM_DEFAULT_CYCLE  1
#endif

#ifndef SOC_KV_DEFAULT_SOC
#define SOC_KV_DEFAULT_SOC    SOC_PARAM_DEFAULT_SOC
#endif

#ifndef SOC_KV_DEFAULT_DSG
#define SOC_KV_DEFAULT_DSG    SOC_PARAM_DEFAULT_DSG
#endif

#ifndef SOC_KV_DEFAULT_CYCLE
#define SOC_KV_DEFAULT_CYCLE  SOC_PARAM_DEFAULT_CYCLE
#endif

typedef enum {
    SOC_ITEM_SOC   = 0,
    SOC_ITEM_DSG   = 1,
    SOC_ITEM_SOH   = SOC_ITEM_DSG,
    SOC_ITEM_CYCLE = 2,
} soc_item_t;

typedef struct {
    u32 soc;
    u32 dsg;
    u32 cycle;
} soc_kv_data_t;

typedef struct {
    u32 active_base;
    u32 write_off;
    u32 next_seq;
    u32 active_generation;
    u16 active_sector;
    u8  loaded;
    u8  tail_dirty;
} soc_kv_dbg_t;

int  soc_kv_store_init(void);
soc_kv_data_t soc_kv_store_get_default_data(void);
soc_kv_data_t soc_kv_store_get(void);
int  soc_kv_store_put(soc_item_t item, u32 value);
int  soc_kv_store_write_all(u32 soc, u32 dsg, u32 cycle);
void soc_kv_store_update_and_log_if_changed(u32 soc, u32 dsg, u32 cycle);
void soc_kv_store_factory_reset(void);
soc_kv_dbg_t soc_kv_store_get_dbg(void);

#ifdef __cplusplus
}
#endif
