/*
 * 文件功能：SOC 剩余充放电时间估算；根据方向、有效容量和电流建立置信度，
 * 结果供诊断显示。
 * bms/core/bms_soc_eta.h；实际编译归属见各产品 sources.txt。
 */
#pragma once
#include <stdint.h>

#define BMS_SOC_ETA_INVALID        0u
#define BMS_SOC_ETA_STABILIZING    1u
#define BMS_SOC_ETA_VALID          2u
#define BMS_SOC_ETA_LOW_CONFIDENCE 3u
#define BMS_SOC_ETA_DIR_NONE       0u
#define BMS_SOC_ETA_DIR_CHARGE     1u
#define BMS_SOC_ETA_DIR_DISCHARGE  2u
#define BMS_SOC_ETA_MINUTES_INVALID 0xFFFFu

/* 独立 ETA 状态；无 Flash、AFE、SDK 或 SOC 全局变量依赖。 */
typedef struct {
    int32_t eta_filtered_current_ma;
    uint32_t eta_variation_ma;
    uint32_t eta_peak_current_ma;
    uint16_t eta_stable_ticks;
    uint16_t time_to_empty_min;
    uint16_t time_to_full_min;
    uint8_t eta_state;
    uint8_t eta_direction;
    uint8_t eta_confidence;
    uint8_t eta_valid;
} bms_soc_eta_t;

typedef struct {
    int32_t current_ma;
    uint32_t remaining_as10;
    uint32_t full_as10;
    uint16_t deadband_ma;
    uint8_t direction; /* BMS_SOC_ETA_DIR_*；无有效电流时传 NONE */
    uint8_t endpoint_active;
    uint8_t near_full;
} bms_soc_eta_input_t;

/* 清除独立 ETA 滤波、方向和置信度状态。 */
void bms_soc_eta_reset(bms_soc_eta_t *eta);
/* 每个合格的 200 ms 策略周期调用；样本中断时复位。 */
void bms_soc_eta_update(bms_soc_eta_t *eta, const bms_soc_eta_input_t *input);
