#ifndef CT_BOOT_CONTROL_H
#define CT_BOOT_CONTROL_H

#include <stdint.h>

typedef struct
{
    uint32_t magic;
    uint32_t magic_inv;
    uint32_t request;
    uint32_t request_inv;
    uint32_t crc;
} CtBootMailbox;

int CtBoot_RequestIap(void);
void CtBoot_ClearRequest(void);

#endif
