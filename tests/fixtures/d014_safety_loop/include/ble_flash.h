#pragma once
/* SDK-only declarations for unused layout inlines. RAM port owns the test map. */
#include "common/types.h"
extern u8 blc_flash_capacity;
enum { FLASH_SIZE_512K = 0x13, FLASH_SIZE_1M = 0x14, FLASH_SIZE_2M = 0x15,
       MULTI_BOOT_ADDR_0x20000 = 0x20000, MULTI_BOOT_ADDR_0x80000 = 0x80000 };
