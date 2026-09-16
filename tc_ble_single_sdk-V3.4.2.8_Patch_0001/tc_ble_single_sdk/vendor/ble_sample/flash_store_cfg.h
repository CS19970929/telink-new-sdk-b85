#pragma once

#include "tl_common.h"
#include "app_config.h"
#include "ble_flash.h"

#if (BLE_OTA_SERVER_ENABLE)
u32 blc_ota_getCurrentUsedMultipleBootAddress(void);
#endif

#define FLASH_SECTOR_SIZE                 4096u
#define FLASH_PAGE_SIZE                   256u

/* Storage V1 persistent domains. Old KV/runtime data is intentionally not migrated. */
#define FLASH_ADDR_EVENT_SECTORS          8u
#define FLASH_ADDR_STATE_SECTORS          8u
#define FLASH_ADDR_CONFIG_SECTORS         4u
#define FLASH_ADDR_FACTORY_SECTORS        2u

#define FLASH_ADDR_LAYOUT_512K_EVENT_BASE    0x40000u
#define FLASH_ADDR_LAYOUT_512K_STATE_BASE    0x53000u
#define FLASH_ADDR_LAYOUT_512K_CONFIG_BASE   0x5B000u
#define FLASH_ADDR_LAYOUT_512K_FACTORY_BASE  0x5F000u

#define FLASH_ADDR_LAYOUT_1M_EVENT_BASE      0xC7000u
#define FLASH_ADDR_LAYOUT_1M_STATE_BASE      0xB0000u
#define FLASH_ADDR_LAYOUT_1M_CONFIG_BASE     0xB8000u
#define FLASH_ADDR_LAYOUT_1M_FACTORY_BASE    0xBC000u

#define FLASH_ADDR_LAYOUT_2M_EVENT_BASE      0x1C7000u
#define FLASH_ADDR_LAYOUT_2M_STATE_BASE      0x1B0000u
#define FLASH_ADDR_LAYOUT_2M_CONFIG_BASE     0x1B8000u
#define FLASH_ADDR_LAYOUT_2M_FACTORY_BASE    0x1BC000u

/* param.h still exports PARAM_ADDR; runtime persistence must never use it directly. */
#define FLASH_ADDR_SOFT_PROTECT_BASE         FLASH_ADDR_LAYOUT_512K_CONFIG_BASE

static inline int flash_store_cfg_layout_supported(void)
{
#if (BLE_OTA_SERVER_ENABLE)
    u32 multi_boot_addr = blc_ota_getCurrentUsedMultipleBootAddress();
    if ((blc_flash_capacity == FLASH_SIZE_512K) &&
        (multi_boot_addr != MULTI_BOOT_ADDR_0x20000)) return 0;
#if (MCU_CORE_TYPE == MCU_CORE_827x || MCU_CORE_TYPE == MCU_CORE_TC321X)
    if ((blc_flash_capacity == FLASH_SIZE_1M) &&
        (multi_boot_addr == MULTI_BOOT_ADDR_0x80000)) return 0;
#endif
#endif
    return 1;
}

static inline u32 flash_store_cfg_get_state_base(void)
{
    if (!flash_store_cfg_layout_supported()) return 0u;
    if (blc_flash_capacity == FLASH_SIZE_1M) return FLASH_ADDR_LAYOUT_1M_STATE_BASE;
    if (blc_flash_capacity == FLASH_SIZE_2M) return FLASH_ADDR_LAYOUT_2M_STATE_BASE;
    return FLASH_ADDR_LAYOUT_512K_STATE_BASE;
}
static inline u16 flash_store_cfg_get_state_sectors(void) { return FLASH_ADDR_STATE_SECTORS; }

static inline u32 flash_store_cfg_get_config_base(void)
{
    if (!flash_store_cfg_layout_supported()) return 0u;
    if (blc_flash_capacity == FLASH_SIZE_1M) return FLASH_ADDR_LAYOUT_1M_CONFIG_BASE;
    if (blc_flash_capacity == FLASH_SIZE_2M) return FLASH_ADDR_LAYOUT_2M_CONFIG_BASE;
    return FLASH_ADDR_LAYOUT_512K_CONFIG_BASE;
}
static inline u16 flash_store_cfg_get_config_sectors(void) { return FLASH_ADDR_CONFIG_SECTORS; }

static inline u32 flash_store_cfg_get_factory_base(void)
{
    if (!flash_store_cfg_layout_supported()) return 0u;
    if (blc_flash_capacity == FLASH_SIZE_1M) return FLASH_ADDR_LAYOUT_1M_FACTORY_BASE;
    if (blc_flash_capacity == FLASH_SIZE_2M) return FLASH_ADDR_LAYOUT_2M_FACTORY_BASE;
    return FLASH_ADDR_LAYOUT_512K_FACTORY_BASE;
}
static inline u16 flash_store_cfg_get_factory_sectors(void) { return FLASH_ADDR_FACTORY_SECTORS; }

static inline u32 flash_store_cfg_get_event_log_base(void)
{
    if (!flash_store_cfg_layout_supported()) return 0u;
    if (blc_flash_capacity == FLASH_SIZE_1M) return FLASH_ADDR_LAYOUT_1M_EVENT_BASE;
    if (blc_flash_capacity == FLASH_SIZE_2M) return FLASH_ADDR_LAYOUT_2M_EVENT_BASE;
    return FLASH_ADDR_LAYOUT_512K_EVENT_BASE;
}
static inline u16 flash_store_cfg_get_event_log_sectors(void) { return FLASH_ADDR_EVENT_SECTORS; }
