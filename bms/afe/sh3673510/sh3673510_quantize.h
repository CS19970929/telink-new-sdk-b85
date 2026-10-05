/* 文件功能：SH 电流保护阈值量化及编码共用算法。 */
#ifndef SH3673510_QUANTIZE_H_
#define SH3673510_QUANTIZE_H_
#include <stdint.h>

/*
 * 芯片电流阈值编码供配置与恢复校验共用；requested_a10 单位 0.1 A，
 * shunt_uohm 单位微欧，step_uv 单位微伏。
 */
static inline uint16_t sh3673510_quantize_current_a10(uint16_t requested_a10,
                                                      uint32_t shunt_uohm,
                                                      uint32_t step_uv,
                                                      uint8_t max_code,
                                                      uint8_t *code)
{
    uint32_t sense_uv, steps, actual_a10;
    if (!shunt_uohm || !step_uv) return 0u;
    sense_uv = ((uint32_t)requested_a10 * shunt_uohm + 5u) / 10u;
    steps = (sense_uv + step_uv - 1u) / step_uv;
    if (!steps) steps = 1u;
    if (steps > (uint32_t)max_code + 1u) steps = (uint32_t)max_code + 1u;
    if (code) *code = (uint8_t)(steps - 1u);
    actual_a10 = (steps * step_uv * 10u + shunt_uohm - 1u) / shunt_uohm;
    return (uint16_t)(actual_a10 > 65535u ? 65535u : actual_a10);
}
#endif
