/* 文件功能：SH 电流保护阈值量化及编码共用算法。 */
#ifndef SH3673510_QUANTIZE_H_
#define SH3673510_QUANTIZE_H_
#include <stdint.h>

/*
 * 芯片电流阈值编码供配置与恢复校验共用；requested_a10 单位 0.1 A，
 * shunt_uohm 单位微欧，step_uv 单位微伏。
 */
static inline uint16_t sh3673510_quantize_current_ratio_a10(uint16_t requested_a10,
                                                      uint32_t element_uohm,
                                                      uint8_t parallel_count,
                                                      uint32_t step_uv,
                                                      uint8_t max_code,
                                                      uint8_t *code)
{
    uint32_t numerator, denominator, steps, actual_a10;
    if (!element_uohm || element_uohm > 60000u || !parallel_count ||
        parallel_count > 16u || !step_uv || step_uv > 160000u || max_code > 31u) return 0u;
    /* SH V1.0A p37-38。先交叉相乘，不把 2000/3 uOhm 舍入为 667。
     * 最大乘积 65535*60000 < UINT32_MAX；避免 ceil 的加法溢出。 */
    numerator = (uint32_t)requested_a10 * element_uohm;
    denominator = step_uv * 10u * parallel_count;
    steps = numerator / denominator + ((numerator % denominator) != 0u);
    if (!steps) steps = 1u;
    if (steps > (uint32_t)max_code + 1u) steps = (uint32_t)max_code + 1u;
    if (code) *code = (uint8_t)(steps - 1u);
    numerator = steps * denominator;
    actual_a10 = numerator / element_uohm + ((numerator % element_uohm) != 0u);
    return (uint16_t)(actual_a10 > 65535u ? 65535u : actual_a10);
}

/* 整数微欧调用者保留原 API；同样不先舍入感测电压。 */
static inline uint16_t sh3673510_quantize_current_a10(uint16_t requested_a10,
    uint32_t shunt_uohm, uint32_t step_uv, uint8_t max_code, uint8_t *code)
{
    return sh3673510_quantize_current_ratio_a10(requested_a10, shunt_uohm,
                                               1u, step_uv, max_code, code);
}
#endif
