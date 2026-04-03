#pragma once
#include "tl_common.h"

//todo 鍙屽?囦唤閫昏緫
#define FLASH_ADDR_USER_DATA_START1     (0x40000)
#define FLASH_ADDR_USER_DATA_END1       (0x74000)
#define FLASH_ADDR_USER_DATA_START2     (0x78000)
#define FLASH_ADDR_USER_DATA_END2       (0x80000)

#define FLASH_SECTOR_SIZE      4096
#define FLASH_PAGE_SIZE        256

#define FLASH_ADDR_USER_DATA_BASE1   0x40000   // 2 sectors: 0x70000 ~ 0x71FFF

#define FLASH_ADDR_SOFT_PROTECT_BASE   0x78000   // 2 sectors: 0x70000 ~ 0x71FFF

// !!! 浣犺?佺‘璁よ繖浜涘湴鍧�涓嶅拰浠ｇ爜/OTA/閰嶅?瑰尯鍐茬獊
#define FLASH_ADDR_RUN_KV_BASE   0x70000   // 2 sectors: 0x70000 ~ 0x71FFF
#define FLASH_ADDR_RUN_KV_SECTORS 2

#define FLASH_ADDR_LOG_BASE      0x72000   // 8 sectors: 0x72000 ~ 0x79FFF
#define FLASH_ADDR_LOG_SECTORS   8

/********************ble name area***********************/
#define FLASH_ADDR_BLE_NAME_BASE      0x50000   // 8 sectors: 0x72000 ~ 0x79FFF


//todo 测试 修改蓝牙名，是否会擦除，多次test
#define FLASH_ADR_RUNTIME   (FLASH_ADDR_BLE_NAME_BASE + 0x1000)
#define RUNTIME_FLAG        0xA5A5
// #define RUNTIME_FLAG        0x5A5A