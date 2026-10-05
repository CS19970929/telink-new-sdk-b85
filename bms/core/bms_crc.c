/*
 * 文件功能：共享 Modbus CRC16 算法；UART/BLE 使用相同计算，
 * CRC 不替代长度与地址校验。
 * bms/core/bms_crc.c；实际编译归属见各产品 sources.txt。
 */
#include "bms_crc.h"
/* 计算 Modbus RTU 使用的 CRC16 校验值。 */
uint16_t mb_crc16(const uint8_t *buf, uint32_t len)
{
    uint16_t crc = 0xFFFFu;
    uint32_t i;
    uint8_t j;

    for (i = 0u; i < len; i++)
    {
        crc ^= buf[i];
        for (j = 0u; j < 8u; j++)
        {
            if (crc & 1u)
                crc = (uint16_t)((crc >> 1) ^ 0xA001u);
            else
                crc >>= 1;
        }
    }
    return crc;
}
