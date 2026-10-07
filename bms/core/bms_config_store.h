/*
 * 文件功能：CFG2 持久配置的缓存、校验与编解码；按产品 tag 和独立更新编号恢复/更新各
 * 参数组。
 * bms/core/bms_config_store.h；实际编译归属见各产品 sources.txt。
 */
#pragma once

#include "bms_parameters.h"
#include "bms_afe_hw_profile.h"
#include "bms_soc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* CFG2 payload 格式不变；内部 journal schema 3 拒绝旧开发布局。 */
typedef struct {
    u16 heater_enable;
    u16 heater_start_x10; /* 温度编码：(degC + 40) * 10。 */
    u16 heater_stop_x10;
    u16 balance_enable;
    u16 balance_start_mv;
    u16 balance_start_delta_mv;
    u16 balance_stop_delta_mv;
    int32_t current_offset_ma; /* 先减偏置再应用增益，正值放电、负值充电。 */
    u32 current_gain_ppm;
    char serial[32];
} bms_user_params_t;
/* 检查用户业务参数范围与一致性。 */
int bms_config_user_valid(const bms_user_params_t *value);
/* 从配置缓存取得用户业务参数。 */
int bms_config_get_user(bms_user_params_t *value);
/* 读取持久化电流校准偏移和比例。 */
int bms_config_get_current_calibration(int32_t *offset_ma, uint32_t *gain_ppm);
/* 校验并保存用户业务参数。 */
int bms_config_set_user(const bms_user_params_t *value);
/* 仅恢复业务参数类别，保留独立持久域边界。 */
int bms_config_reset_business(void);
/* 对有符号电流应用限幅、零点与比例校准。 */
int32_t bms_config_calibrate_current(int32_t raw_ma);


typedef struct {
    u32 bms_type;
    u32 series_num;
    u32 capacity_factory;
    u32 battery_chemistry;
    u32 soc_profile_id;
} bms_config_system_params_t;

/* Config 拥有用户、系统、软件保护和 AFE 请求数据。 */
int bms_config_store_init(void);
/* 输出授权前验证各配置域、更新编号与存储提交状态。 */
int bms_config_store_validate_startup(void);
/* 取得缓存的SOC 算法配置。 */
int bms_config_store_get_soc(bms_soc_config_t *config);
/* 校验并保存SOC 算法配置，按保存结果更新缓存。 */
int bms_config_store_set_soc(const bms_soc_config_t *config);
/* 取得缓存的软件保护配置。 */
int bms_config_store_get_protect(bms_protection_params_t *protect);
/* 校验并保存软件保护配置，按保存结果更新缓存。 */
int bms_config_store_set_protect(const bms_protection_params_t *protect);
/* 取得缓存的系统业务配置。 */
int bms_config_store_get_system(bms_config_system_params_t *system);
/* 校验并保存系统业务配置，按保存结果更新缓存。 */
int bms_config_store_set_system(const bms_config_system_params_t *system);
/* 取得缓存的独立 AFE 硬件保护配置。 */
int bms_config_store_get_afe_hw_profile(bms_afe_hw_profile_t *profile);
/* 校验并保存独立 AFE 硬件保护配置，按保存结果更新缓存。 */
int bms_config_store_set_afe_hw_profile(const bms_afe_hw_profile_t *profile);
/* 取得缓存的BLE 名称后缀。 */
int bms_config_store_get_bt_name_suffix(char *suffix, u16 suffix_size);
/* 校验并保存BLE 名称后缀，按保存结果更新缓存。 */
int bms_config_store_set_bt_name_suffix(const char *suffix);
/* 取得产品软件保护默认配置。 */
void bms_config_store_get_default_protect(bms_protection_params_t *protect);

#ifdef __cplusplus
}
#endif
