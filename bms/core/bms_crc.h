/* 文件功能：共享 Modbus CRC16 算法；UART/BLE 使用相同计算，CRC 不替代长度与地址校验。
 * bms/core/bms_crc.h；实际编译归属见各产品 sources.txt。
 */
#pragma once
#include <stdint.h>
uint16_t mb_crc16(const uint8_t *buf, uint32_t len);
