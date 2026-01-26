/**
 * @file    crc16.c
 *
 * @brief   CRC16 实现（轻量）
 */

#include "crc16.h"

uint16_t crc16_ccitt(const void *data, size_t len){
    const uint8_t *p = (const uint8_t*)data;
    uint16_t crc = 0xFFFFu;
    for(size_t i=0;i<len;i++){
        crc ^= (uint16_t)p[i] << 8;
        for(int b=0;b<8;b++){
            if(crc & 0x8000u) crc = (crc << 1) ^ 0x1021u;
            else crc <<= 1;
        }
    }
    return crc;
}
