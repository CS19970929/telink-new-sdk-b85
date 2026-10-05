#pragma once

#include "param.h"
#include "bms_afe_hw_profile.h"
#include "bms_soc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* All products use CFG2 schema 2; old board records are intentionally rejected. */
typedef struct {
    u16 heater_enable;
    u16 heater_start_x10; /* temperature encoding: (degC + 40) * 10 */
    u16 heater_stop_x10;
    u16 balance_enable;
    u16 balance_start_mv;
    u16 balance_start_delta_mv;
    u16 balance_stop_delta_mv;
    int32_t current_offset_ma; /* subtract before gain; positive discharge, negative charge */
    u32 current_gain_ppm;
    char serial[32];
} bms_user_params_t;
void bms_config_user_defaults(bms_user_params_t *value);
int bms_config_user_valid(const bms_user_params_t *value);
int bms_config_get_user(bms_user_params_t *value);
int bms_config_get_current_calibration(int32_t *offset_ma, uint32_t *gain_ppm);
int bms_config_set_user(const bms_user_params_t *value);
int bms_config_reset_business(void);
int32_t bms_config_calibrate_current(int32_t raw_ma);


typedef struct {
    u32 bms_type;
    u32 series_num;
    u32 capacity_factory;
    u32 battery_chemistry;
    u32 soc_profile_id;
} bms_config_system_params_t;

/* Config owner: user/system/software-protection/AFE requested data. */
int bms_config_store_init(void);
int bms_config_store_validate_startup(void);
int bms_config_store_get_soc(bms_soc_config_t *config);
int bms_config_store_set_soc(const bms_soc_config_t *config);
int bms_config_store_get_protect(struct PRT_E2ROM_PARAS *protect);
int bms_config_store_set_protect(const struct PRT_E2ROM_PARAS *protect);
int bms_config_store_get_system(bms_config_system_params_t *system);
int bms_config_store_set_system(const bms_config_system_params_t *system);
int bms_config_store_get_afe_hw_profile(bms_afe_hw_profile_t *profile);
int bms_config_store_set_afe_hw_profile(const bms_afe_hw_profile_t *profile);
int bms_config_store_get_bt_name_suffix(char *suffix, u16 suffix_size);
int bms_config_store_set_bt_name_suffix(const char *suffix);
void bms_config_store_get_default_protect(struct PRT_E2ROM_PARAS *protect);
void bms_config_store_get_default_system(bms_config_system_params_t *system);

#ifdef __cplusplus
}
#endif
