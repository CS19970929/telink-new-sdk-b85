#pragma once

#include "tl_common.h"
#include "drivers.h"
#include "app_config.h"
#include "ble_flash.h"
#include "flash_prot.h"

#define FLASH_SECTOR_SIZE 4096u
#define FLASH_PAGE_SIZE 256u
#define D008_FLASH_CONFIG_BASE 0x40000u
#define D008_FLASH_STATE_BASE  0x44000u
#define D008_FLASH_EVENT_BASE  0x4C000u
#define D008_FLASH_DOMAIN_SIZE (2u * FLASH_SECTOR_SIZE)

u32 blc_ota_getCurrentUsedMultipleBootAddress(void);

static inline int flash_store_cfg_layout_supported(void)
{
    return blc_flash_capacity == FLASH_SIZE_512K && !blc_flashProt.init_err &&
        blc_ota_getCurrentUsedMultipleBootAddress() == MULTI_BOOT_ADDR_0x20000;
}
