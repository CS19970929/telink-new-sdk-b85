#pragma once
#include "gpio.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t ntc2_mv;     // PB4: 电池 NTC2 节点电压
    uint32_t vbus_mv;     // PB5: 还原后的 VBUS 母线电压
    uint32_t ntc3_mv;     // PC4: MOS NTC3 节点电压

    uint32_t ntc2_ohm;    // 电池 NTC2 电阻（估算）
    uint32_t ntc3_ohm;    // MOS NTC3 电阻（估算）
} bms_adc_sample_t;

/**
 * @brief 初始化 ADC 子系统（只需调用一次）
 */
void bms_adc_init(void);

/**
 * @brief 读取单路：返回该引脚 ADC 节点电压（mV）
 */
uint32_t bms_adc_read_pin_mv(GPIO_PinTypeDef pin);

/**
 * @brief 读取电池 NTC2 节点电压（mV）
 */
uint32_t bms_adc_read_ntc2_mv(void);

/**
 * @brief 读取 MOS NTC3 节点电压（mV）
 */
uint32_t bms_adc_read_ntc3_mv(void);

/**
 * @brief 读取 VBUS（mV，已按分压还原）
 */
uint32_t bms_adc_read_vbus_mv(void);

/**
 * @brief 一次性读取三路（并计算 NTC 电阻）
 */
void bms_adc_read_all(bms_adc_sample_t *out);

#ifdef __cplusplus
}
#endif
