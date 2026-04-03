#pragma once

#include "param.h"
#include "flash_kv32.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BMS_COLD_KV_BASE
#define BMS_COLD_KV_BASE   FLASH_ADDR_SOFT_PROTECT_BASE
#endif

#ifndef BMS_COLD_KV_SECTOR_SIZE
#define BMS_COLD_KV_SECTOR_SIZE   FLASH_SECTOR_SIZE
#endif

#ifndef BMS_COLD_KV_SECTORS
#define BMS_COLD_KV_SECTORS   2
#endif

typedef enum {
    BMS_SYS_PARAM_BMS_TYPE = 0,
    BMS_SYS_PARAM_SERIES_NUM,
    BMS_SYS_PARAM_CAPACITY_FACTORY,
    BMS_SYS_PARAM_AFE_ODC2,
    BMS_SYS_PARAM_FAC_INIT_SOC,
    BMS_SYS_PARAM_INIT_SOC,
    BMS_SYS_PARAM_FLAGS,
    BMS_SYS_PARAM_RSVD0,
} bms_cold_system_param_id_t;

typedef struct {
    u32 bms_type;
    u32 series_num;
    u32 capacity_factory;
    u32 afe_odc2;
    u32 fac_init_soc;
    u32 init_soc;
    u32 flags;
    u32 reserved0;
} bms_cold_system_params_t;

int bms_cold_kv_store_init(void);
int bms_cold_kv_store_get_protect(struct PRT_E2ROM_PARAS *protect);
int bms_cold_kv_store_set_protect(const struct PRT_E2ROM_PARAS *protect);
int bms_cold_kv_store_get_system(bms_cold_system_params_t *system);
int bms_cold_kv_store_set_system(const bms_cold_system_params_t *system);
int bms_cold_kv_store_get_system_value(bms_cold_system_param_id_t item, u32 *value);
int bms_cold_kv_store_set_system_value(bms_cold_system_param_id_t item, u32 value);
void bms_cold_kv_store_get_default_protect(struct PRT_E2ROM_PARAS *protect);
void bms_cold_kv_store_get_default_system(bms_cold_system_params_t *system);
void bms_cold_kv_store_factory_reset(void);
flash_kv32_dbg_t bms_cold_kv_store_get_dbg(void);

#ifdef __cplusplus
}
#endif
