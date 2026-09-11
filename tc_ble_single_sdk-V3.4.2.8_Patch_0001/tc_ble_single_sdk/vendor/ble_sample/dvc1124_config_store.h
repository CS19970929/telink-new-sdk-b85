#ifndef DVC1124_CONFIG_STORE_H_
#define DVC1124_CONFIG_STORE_H_

#include "dvc1124.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DVC1124_CONFIG_STORE_SCHEMA_VERSION  0x0001u

typedef struct
{
    dvc1124_operating_config_t operating;

    uint16_t current_wake_threshold_uv;
    uint16_t body_diode_threshold_uv;
    uint8_t dsg_pulldown_strength;       /* DPC: 0..30, 31 is N/A */
    uint8_t i2c_timeout_close_chg;       /* 1 = timeout closes CHG */
    uint8_t i2c_timeout_close_dsg;       /* 1 = timeout closes DSG */
    uint8_t core_ot_code;                /* COTT[6:0], 0 disables */

    uint16_t scd_threshold_mv;           /* 0 disables; otherwise 10..630mV */
    uint16_t scd_delay_us;               /* encoded with 7.81us/LSB */
} dvc1124_persistent_config_t;

/* Defaults come from dvc1124_project_config.h and preserve current product behavior. */
void DVC1124_ConfigStoreGetDefaults(dvc1124_persistent_config_t *cfg);

/* Flash KV lifecycle. */
int DVC1124_ConfigStoreInit(void);
int DVC1124_ConfigStoreLoad(dvc1124_persistent_config_t *cfg);
int DVC1124_ConfigStoreSave(const dvc1124_persistent_config_t *cfg);

/* Runtime validation/application helpers. */
int DVC1124_ConfigStoreValidate(const dvc1124_persistent_config_t *cfg);
int DVC1124_ConfigStoreApply(const dvc1124_persistent_config_t *cfg);
int DVC1124_ConfigStoreCaptureCurrent(dvc1124_persistent_config_t *cfg);
int DVC1124_ConfigStoreCaptureAndSave(void);
int DVC1124_ConfigStoreRestore(void);

/*
 * Persistent raw access used by factory/debug communication paths.
 * Only DVC1124_RegPersistentConfigMask() owned bits may be modified.
 * On success the effective AFE configuration is captured and committed to KV.
 */
int DVC1124_ConfigStoreWritePersistentRegister(uint8_t reg, uint8_t requested);

/* Legacy application compatibility wrappers used from conf.h. */
void DVC1124_ConfigStore_AFE_Reset(void);
void DVC1124_ConfigStore_UpdataAfeConfig(void);
void DVC1124_ConfigStore_BmsApp_AFEGet(void);

#ifdef __cplusplus
}
#endif

#endif /* DVC1124_CONFIG_STORE_H_ */
