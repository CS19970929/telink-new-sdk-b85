#pragma once

#include "tl_common.h"
#include "conf.h"

#define SOC_PARAM_DEFAULT_SOC    ((u32)FAC_INIT_soc)
#define SOC_PARAM_DEFAULT_DSG    0u
#define SOC_PARAM_DEFAULT_CYCLE  0u

typedef struct
{
    u32 soc;
    u32 dsg;
    u32 cycle;
} soc_kv_data_t;

int soc_kv_store_init(void);
soc_kv_data_t soc_kv_store_get_default_data(void);
soc_kv_data_t soc_kv_store_get(void);
int soc_kv_store_write_all(u32 soc, u32 dsg, u32 cycle);
void soc_kv_store_update_and_log_if_changed(u32 soc, u32 dsg, u32 cycle);
