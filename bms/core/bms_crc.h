/*
 * 文件功能：共享 Modbus CRC16 算法；UART/BLE 使用相同计算，
 * CRC 不替代长度与地址校验。
 * bms/core/bms_crc.h；实际编译归属见各产品 sources.txt。
 */
#pragma once
#include <stdint.h>
/* 计算 Modbus RTU 使用的 CRC16 校验值。 */
uint16_t mb_crc16(const uint8_t *buf, uint32_t len);

/* CRC32/IEEE 的流式步骤。调用方用全一初值并在末尾取反。 */
static inline uint32_t bms_crc32_update(uint32_t crc, const uint8_t *data, uint32_t len)
{
    while (len--) {
        unsigned bit;
        crc ^= *data++;
        for (bit = 0u; bit < 8u; ++bit)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
    }
    return crc;
}
