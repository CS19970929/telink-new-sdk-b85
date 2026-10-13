#pragma once
#include <stdint.h>

/* 独立休眠参数记录；不改变既有 CFG2 payload、地址或参数含义。 */
#define BMS_SLEEP_CONFIG_WORDS 5u
#define BMS_SLEEP_AFE_FIXED_MINUTES 30u /* 固件固定策略，不允许在线修改。 */
typedef struct {
    uint16_t low_cell_mv;
    uint16_t low_delay_minutes;
    uint16_t normal_cell_mv;
    uint16_t normal_delay_minutes;
    uint16_t afe_delay_minutes; /* 保留旧记录/协议槽，读回恒为固定值。 */
} bms_sleep_config_t;

/* 只在启动加载；读取不访问 Flash，保存成功后才发布候选值。 */
void bms_sleep_config_init(void);
void bms_sleep_config_defaults(bms_sleep_config_t *config);
void bms_sleep_config_get(bms_sleep_config_t *config);
int bms_sleep_config_valid(const bms_sleep_config_t *config);
int bms_sleep_config_set(const bms_sleep_config_t *config);
