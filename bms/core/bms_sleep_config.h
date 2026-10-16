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

/* RAM 策略与五 word 持久记录分离；以下内容不进入 Flash payload。 */
#define BMS_SLEEP_VOLTAGE_LOW_ACTIVE 0x01u
#define BMS_SLEEP_VOLTAGE_NORMAL_ACTIVE 0x02u
#define BMS_SLEEP_CHARGE_CURRENT_MA ((int32_t)200)
#define BMS_SLEEP_OVERDISCHARGE_SECONDS 180u
typedef enum {
    BMS_SLEEP_REASON_NONE = 0,
    BMS_SLEEP_REASON_LOW = 5,
    BMS_SLEEP_REASON_NORMAL = 6,
    BMS_SLEEP_REASON_AFE = 7,
    BMS_SLEEP_REASON_OVERDISCHARGE = 8
} bms_sleep_reason_t;
typedef struct {
    uint32_t low_voltage_seconds;
    uint32_t normal_voltage_seconds;
    uint32_t afe_error_seconds;
    uint32_t overdischarge_seconds;
    uint8_t overdischarge_active;
    uint8_t voltage_conditions;
} bms_sleep_policy_t;
typedef struct {
    int32_t current_ma;
    uint16_t cell_min_mv;
    uint16_t pack_voltage_10mv;
    uint16_t cell_uvp_recover_mv;
    uint16_t pack_uvp_recover_10mv;
    uint8_t valid;
    uint8_t afe_fault;
    uint8_t overdischarge_fault;
} bms_sleep_input_t;
typedef struct {
    uint32_t elapsed_ms;
    uint32_t delay_ms;
    uint8_t reason;
} bms_sleep_status_t;
/* 调用方提供非空参数；只推进业务秒数和决策，不访问外设或执行休眠。 */
void bms_sleep_policy_step(bms_sleep_policy_t *state, const bms_sleep_config_t *config,
                           const bms_sleep_input_t *input, uint32_t elapsed_sec,
                           bms_sleep_status_t *status);
