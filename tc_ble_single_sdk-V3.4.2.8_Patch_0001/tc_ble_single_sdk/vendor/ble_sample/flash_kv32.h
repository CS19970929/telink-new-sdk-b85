#pragma once

#include "tl_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FLASH_KV32_SUCCESS  1
#define FLASH_KV32_FAILED   0

typedef void (*flash_kv32_read_fn)(void *ctx, u32 addr, u8 *buf, u32 len);
typedef int  (*flash_kv32_prog_fn)(void *ctx, u32 addr, const u8 *buf, u32 len);
typedef int  (*flash_kv32_erase_sector_fn)(void *ctx, u32 addr, u32 size);
typedef void (*flash_kv32_lock_fn)(void *ctx);
typedef void (*flash_kv32_unlock_fn)(void *ctx);

typedef struct {
    void *ctx;
    flash_kv32_read_fn read;
    flash_kv32_prog_fn prog;
    flash_kv32_erase_sector_fn erase_sector;
    flash_kv32_lock_fn lock;
    flash_kv32_unlock_fn unlock;
} flash_kv32_port_t;

typedef struct {
    u32 key;
    u32 default_value;
} flash_kv32_key_def_t;

typedef struct {
    u32 key;
    u32 value;
} flash_kv32_pair_t;

typedef struct {
    u32 value;
} flash_kv32_cache_entry_t;

typedef struct {
    const flash_kv32_port_t *port;
    const u32 *sector_addrs;
    const flash_kv32_key_def_t *keys;
    u16 sector_count;
    u16 sector_size;
    u16 write_align;
    u16 key_count;
} flash_kv32_cfg_t;

typedef struct {
    u32 active_base;
    u32 active_generation;
    u32 next_seq;
    u16 write_off;
    u16 active_sector;
    u8 loaded;
    u8 tail_dirty;
} flash_kv32_dbg_t;

typedef struct {
    flash_kv32_cfg_t cfg;
    flash_kv32_cache_entry_t *cache;
    flash_kv32_dbg_t dbg;
} flash_kv32_t;

int flash_kv32_init(flash_kv32_t *kv, const flash_kv32_cfg_t *cfg, flash_kv32_cache_entry_t *cache);
int flash_kv32_format(flash_kv32_t *kv);
int flash_kv32_get(const flash_kv32_t *kv, u32 key, u32 *value);
int flash_kv32_set(flash_kv32_t *kv, u32 key, u32 value);
int flash_kv32_write_pairs(flash_kv32_t *kv, const flash_kv32_pair_t *pairs, u16 pair_count);

#ifdef __cplusplus
}
#endif
