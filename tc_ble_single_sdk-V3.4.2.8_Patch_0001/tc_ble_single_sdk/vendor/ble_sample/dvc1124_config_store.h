#ifndef DVC1124_CONFIG_STORE_H_
#define DVC1124_CONFIG_STORE_H_

#include "dvc1124.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DVC1124_CONFIG_STORE_SCHEMA_VERSION 0x0001u

typedef struct
{
    dvc1124_operating_config_t operating;

    uint16_t current_wake_threshold_uv;
    uint16_t body_diode_threshold_uv;
    uint8_t dsg_pulldown_strength;
    uint8_t i2c_timeout_close_chg;
    uint8_t i2c_timeout_close_dsg;
    uint8_t core_ot_code;

    uint16_t scd_threshold_mv;
    uint16_t scd_delay_us;
} dvc1124_persistent_config_t;

void DVC1124_ConfigStoreGetDefaults(dvc1124_persistent_config_t *cfg);

int DVC1124_ConfigStoreInit(void);
int DVC1124_ConfigStoreLoad(dvc1124_persistent_config_t *cfg);
int DVC1124_ConfigStoreSave(const dvc1124_persistent_config_t *cfg);
int DVC1124_ConfigStoreValidate(const dvc1124_persistent_config_t *cfg);
int DVC1124_ConfigStoreApply(const dvc1124_persistent_config_t *cfg);
int DVC1124_ConfigStoreCaptureCurrent(dvc1124_persistent_config_t *cfg);
int DVC1124_ConfigStoreRestore(void);

/* Factory/debug persistent raw-register write. */
int DVC1124_ConfigStoreWritePersistentRegister(uint8_t reg, uint8_t requested);

/* Temporary legacy application wrappers used by conf.h aliases. */
void DVC1124_ConfigStore_AFE_Reset(void);
void DVC1124_ConfigStore_UpdataAfeConfig(void);
void DVC1124_ConfigStore_BmsApp_AFEGet(void);

#ifdef __cplusplus
}
#endif

#endif /* DVC1124_CONFIG_STORE_H_ */
