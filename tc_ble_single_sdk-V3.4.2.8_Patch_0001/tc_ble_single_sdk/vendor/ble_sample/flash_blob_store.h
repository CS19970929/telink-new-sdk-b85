#ifndef FLASH_BLOB_STORE_H_
#define FLASH_BLOB_STORE_H_

#include "tl_common.h"
#include "drivers.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    u32 sector_a;
    u32 sector_b;
    u32 sector_size;
    u32 magic;
    u16 version;
    u16 payload_size;
} flash_blob_store_cfg_t;

typedef struct
{
    u32 active_addr;
    u32 seq;
    u8 valid;
} flash_blob_store_state_t;

int flash_blob_store_load(const flash_blob_store_cfg_t *cfg, void *payload, flash_blob_store_state_t *state);
int flash_blob_store_save(const flash_blob_store_cfg_t *cfg, const void *payload, flash_blob_store_state_t *state);
int flash_blob_store_reset(const flash_blob_store_cfg_t *cfg);

#ifdef __cplusplus
}
#endif

#endif
