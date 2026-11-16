#include "ct_crc16.h"

uint16_t CtCrc16_Update(uint16_t crc, const uint8_t *data, size_t length)
{
    size_t i;
    uint8_t bit;

    if (data == 0)
    {
        return crc;
    }

    for (i = 0u; i < length; ++i)
    {
        crc ^= data[i];
        for (bit = 0u; bit < 8u; ++bit)
        {
            if ((crc & 0x0001u) != 0u)
            {
                crc = (uint16_t)((crc >> 1) ^ 0xA001u);
            }
            else
            {
                crc = (uint16_t)(crc >> 1);
            }
        }
    }

    return crc;
}

uint16_t CtCrc16_Calc(const uint8_t *data, size_t length)
{
    return CtCrc16_Update(0xFFFFu, data, length);
}

