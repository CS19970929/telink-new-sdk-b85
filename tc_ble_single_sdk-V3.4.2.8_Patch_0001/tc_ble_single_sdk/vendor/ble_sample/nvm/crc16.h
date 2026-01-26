/**
 * @file    crc16.h
 *
 * @brief   CRC16 接口声明（用于记录校验）
 */

#pragma once
#include <stdint.h>
// #include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// CRC16-CCITT (poly 0x1021, init 0xFFFF)
uint16_t crc16_ccitt(const void *data, unsigned int len);

#ifdef __cplusplus
}
#endif
