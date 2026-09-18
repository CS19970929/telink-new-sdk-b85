#pragma once

#include "param.h"
#include "bms_afe_hw_profile.h"
#include "SocEnhance.h"

#ifdef __cplusplus
extern "C" {
#endif

/* User-editable values remain in the existing Config record, not a new KV. */
typedef struct {
    u16 heater_enable;
    u16 heater_start_x10; /* legacy temperature encoding: (degC + 40) * 10 */
    u16 heater_stop_x10;
    int32_t current_offset_ma; /* subtract before gain; raw DVC sign */
    u32 current_gain_ppm;
    char serial[32];
} bms_user_params_t;
void bms_config_user_defaults(bms_user_params_t *value);
int bms_config_user_valid(const bms_user_params_t *value);
int bms_config_get_user(bms_user_params_t *value);
int bms_config_set_user(const bms_user_params_t *value);
int bms_config_reset_business(void);
int32_t bms_config_calibrate_current(int32_t raw_ma);

typedef enum {
    BMS_SYS_PARAM_BMS_TYPE = 0,
    BMS_SYS_PARAM_SERIES_NUM,
    BMS_SYS_PARAM_CAPACITY_FACTORY,
    BMS_SYS_PARAM_AFE_ODC2,
    BMS_SYS_PARAM_FAC_INIT_SOC,
    BMS_SYS_PARAM_INIT_SOC,
    BMS_SYS_PARAM_FLAGS,
    BMS_SYS_PARAM_RSVD0,
    BMS_SYS_PARAM_BATTERY_CHEMISTRY,
    BMS_SYS_PARAM_SOC_PROFILE_ID,
} bms_config_system_param_id_t;

typedef enum {
    BMS_CONFIG_CTRL_PROTECT_RESET_EPOCH = 0,
    BMS_CONFIG_CTRL_SYSTEM_RESET_EPOCH,
    BMS_CONFIG_CTRL_SOC_RESET_EPOCH,       /* reserved, State owns its revision */
    BMS_CONFIG_CTRL_EVENT_LOG_RESET_EPOCH, /* reserved, Event owns its revision */
    BMS_CONFIG_CTRL_RUNTIME_RESET_EPOCH,   /* reserved, State owns its revision */
    BMS_CONFIG_CTRL_AFE_HW_RESET_EPOCH,
    BMS_CONFIG_CTRL_SOC_CONFIG_RESET_EPOCH,
    BMS_CONFIG_CTRL_COUNT
} bms_config_control_param_id_t;

typedef struct {
    u32 bms_type;
    u32 series_num;
    u32 capacity_factory;
    u32 afe_odc2;
    u32 fac_init_soc;
    u32 init_soc;
    u32 flags;
    u32 reserved0;
    u32 battery_chemistry;
    u32 soc_profile_id;
} bms_config_system_params_t;

/* Storage V1 Config owner: user/system/software-protection/AFE requested data. */
int bms_config_store_init(void);
int bms_config_store_apply_revisions(void);
int bms_config_store_get_soc(bms_soc_config_t *config);
int bms_config_store_set_soc(const bms_soc_config_t *config);
int bms_config_store_get_protect(struct PRT_E2ROM_PARAS *protect);
int bms_config_store_set_protect(const struct PRT_E2ROM_PARAS *protect);
int bms_config_store_get_system(bms_config_system_params_t *system);
int bms_config_store_set_system(const bms_config_system_params_t *system);
int bms_config_store_get_afe_hw_profile(bms_afe_hw_profile_t *profile);
int bms_config_store_set_afe_hw_profile(const bms_afe_hw_profile_t *profile);
int bms_config_store_get_control_value(bms_config_control_param_id_t item, u32 *value);
int bms_config_store_set_control_value(bms_config_control_param_id_t item, u32 value);
int bms_config_store_get_bt_name_suffix(char *suffix, u16 suffix_size);
int bms_config_store_set_bt_name_suffix(const char *suffix);
void bms_config_store_get_default_protect(struct PRT_E2ROM_PARAS *protect);
void bms_config_store_get_default_system(bms_config_system_params_t *system);

#ifdef __cplusplus
}
#endif
