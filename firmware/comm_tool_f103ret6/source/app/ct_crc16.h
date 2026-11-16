#ifndef CT_CRC16_H
#define CT_CRC16_H

#include <stdint.h>
#include <stddef.h>

uint16_t CtCrc16_Update(uint16_t crc, const uint8_t *data, size_t length);
uint16_t CtCrc16_Calc(const uint8_t *data, size_t length);

#endif

