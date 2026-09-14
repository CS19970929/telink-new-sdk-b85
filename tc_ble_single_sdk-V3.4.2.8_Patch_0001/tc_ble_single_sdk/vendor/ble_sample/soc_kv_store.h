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

#ifndef SOC_PARAM_DEFAULT_SOC
#define SOC_PARAM_DEFAULT_SOC    ((u32)FAC_INIT_soc)
#endif
#ifndef SOC_PARAM_DEFAULT_DSG
#define SOC_PARAM_DEFAULT_DSG    0u
#endif
#ifndef SOC_PARAM_DEFAULT_CYCLE
#define SOC_PARAM_DEFAULT_CYCLE  0u
#endif
#ifndef SOC_PARAM_DEFAULT_LEARNED_CAPACITY
#define SOC_PARAM_DEFAULT_LEARNED_CAPACITY 0u
#endif
#ifndef SOC_PARAM_DEFAULT_FLAGS
#define SOC_PARAM_DEFAULT_FLAGS 0u
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

#define SOC_KV_FLAG_CAPACITY_LEARNED 0x00000001u

typedef struct {
    u32 soc;
    u32 dsg;
    u32 cycle;
    u32 learned_capacity_0p1ah;
    u32 flags;
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
int  soc_kv_store_write_all(u32 soc, u32 dsg, u32 cycle);
int  soc_kv_store_write_learning(u32 learned_capacity_0p1ah, u32 flags);
void soc_kv_store_update_and_log_if_changed(u32 soc, u32 dsg, u32 cycle);

#ifdef __cplusplus
}
#endif
