#ifndef CONFIG_STORE_H_
#define CONFIG_STORE_H_

#include "tl_common.h"
#include "param.h"
#include "flash_storage_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    flash_config_meta_t meta;
    PARAM_T             param;
    char                bt_name_suffix[FLASH_BTNAME_SUFFIX_MAX_LEN + 1];
} config_store_blob_t;

int config_store_init(void);
int config_store_load(config_store_blob_t *blob);
int config_store_save(const config_store_blob_t *blob);
int config_store_factory_reset(void);

#ifdef __cplusplus
}
#endif

#endif
