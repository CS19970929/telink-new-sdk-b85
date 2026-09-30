#include "bms_crc.h"
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
