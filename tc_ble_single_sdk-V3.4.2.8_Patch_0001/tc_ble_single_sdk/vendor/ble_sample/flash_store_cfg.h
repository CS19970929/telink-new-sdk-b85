#pragma once
#include "tl_common.h"

//todo 双备份逻辑
#define FLASH_ADDR_USER_DATA_START1     (0x40000)
#define FLASH_ADDR_USER_DATA_END1       (0x74000)
#define FLASH_ADDR_USER_DATA_START2     (0x78000)
#define FLASH_ADDR_USER_DATA_END2       (0x80000)

#define FLASH_SECTOR_SIZE      4096
#define FLASH_PAGE_SIZE        256

#define FLASH_ADDR_USER_DATA_BASE1   0x40000   // 2 sectors: 0x70000 ~ 0x71FFF
#define FLASH_ADDR_USER_DATA_END1    0x74000   // 2 sectors: 0x70000 ~ 0x71FFF

#define FLASH_ADDR_SOFT_PROTECT_BASE   0x78000   // 2 sectors: 0x70000 ~ 0x71FFF

// !!! 你要确认这些地址不和代码/OTA/配对区冲突
#define FLASH_ADDR_RUN_KV_BASE   0x70000   // 2 sectors: 0x70000 ~ 0x71FFF
#define FLASH_ADDR_RUN_KV_SECTORS 2

#define FLASH_ADDR_LOG_BASE      0x72000   // 8 sectors: 0x72000 ~ 0x79FFF
#define FLASH_ADDR_LOG_SECTORS   8
