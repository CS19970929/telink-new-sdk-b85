#ifndef CT_FLASH_STORE_H
#define CT_FLASH_STORE_H

#include <stdint.h>

typedef struct
{
    uint32_t magic;
    uint32_t app_addr;
    uint32_t size;
    uint16_t crc16;
    uint16_t reserved;
    uint32_t crc32;
    uint32_t valid;
} CtFirmwareInfo;

void CtFlash_Init(void);
const CtFirmwareInfo *CtFlash_GetInfo(void);
int CtFlash_Begin(uint32_t app_addr, uint32_t size, uint16_t crc16, uint32_t crc32);
int CtFlash_Write(uint32_t offset, const uint8_t *data, uint16_t length);
int CtFlash_End(uint32_t size, uint16_t crc16, uint32_t crc32);
int CtFlash_Read(uint32_t offset, uint8_t *data, uint16_t length);

#endif

