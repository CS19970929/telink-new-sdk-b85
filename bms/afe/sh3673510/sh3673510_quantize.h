#ifndef SH3673510_QUANTIZE_H_
#define SH3673510_QUANTIZE_H_
#include <stdint.h>

/* Chip current threshold coding, shared by programming and recovery validation.
 * requested_a10 is 0.1 A, shunt_uohm is micro-ohm, step_uv is microvolt. */
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
