#pragma once

#include "tl_common.h"
#include "app_config.h"
#include "ble_flash.h"

#if (BLE_OTA_SERVER_ENABLE)
u32 blc_ota_getCurrentUsedMultipleBootAddress(void);
#endif

#define FLASH_SECTOR_SIZE                      4096u
#define FLASH_PAGE_SIZE                        256u

/*
 * Historical compatibility anchors that remain documented on purpose:
 * - PARAM_ADDR still points at the old top-of-flash parameter base.
 * - Legacy btname single-sector bases stay here as explicit vacant regions.
 */
#define FLASH_ADDR_SOFT_PROTECT_BASE           0x78000u

#define FLASH_ADDR_RUNTIME_SECTORS             2u
#define FLASH_ADDR_RUN_KV_SECTORS              8u
#define FLASH_ADDR_LOG_SECTORS                 6u
#define FLASH_ADDR_SAFETY_LOG_SECTORS          2u
#define RUNTIME_FLAG                           0xA5A5u

#define FLASH_ADDR_LAYOUT_512K_BTNAME_BASE           0x50000u
#define FLASH_ADDR_LAYOUT_512K_RUNTIME_BASE          0x51000u
#define FLASH_ADDR_LAYOUT_512K_RUN_KV_BASE           0x53000u
#define FLASH_ADDR_LAYOUT_512K_SOFT_PROTECT          0x5B000u
#define FLASH_ADDR_LAYOUT_512K_LOG_BASE              0x40000u

#define FLASH_ADDR_LAYOUT_1M_BTNAME_BASE             0xC0000u
#define FLASH_ADDR_LAYOUT_1M_RUNTIME_BASE            0xC1000u
#define FLASH_ADDR_LAYOUT_1M_RUN_KV_BASE             0xB0000u
#define FLASH_ADDR_LAYOUT_1M_SOFT_PROTECT            0xB8000u
#define FLASH_ADDR_LAYOUT_1M_LOG_BASE                0xC7000u

#define FLASH_ADDR_LAYOUT_2M_BTNAME_BASE             0x1C0000u
#define FLASH_ADDR_LAYOUT_2M_RUNTIME_BASE            0x1C1000u
#define FLASH_ADDR_LAYOUT_2M_RUN_KV_BASE             0x1B0000u
#define FLASH_ADDR_LAYOUT_2M_SOFT_PROTECT            0x1B8000u
#define FLASH_ADDR_LAYOUT_2M_LOG_BASE                0x1C7000u

static inline int flash_store_cfg_layout_supported(void)
{
#if (BLE_OTA_SERVER_ENABLE)
    u32 multi_boot_addr = blc_ota_getCurrentUsedMultipleBootAddress();

    if ((blc_flash_capacity == FLASH_SIZE_512K) &&
        (multi_boot_addr != MULTI_BOOT_ADDR_0x20000)) {
        return 0;
    }

#if (MCU_CORE_TYPE == MCU_CORE_827x || MCU_CORE_TYPE == MCU_CORE_TC321X)
    if ((blc_flash_capacity == FLASH_SIZE_1M) &&
        (multi_boot_addr == MULTI_BOOT_ADDR_0x80000)) {
        return 0;
    }
#endif
#endif
    return 1;
}

static inline u32 flash_store_cfg_get_runtime_base(void)
{
    if (!flash_store_cfg_layout_supported()) {
        return 0u;
    }

    if (blc_flash_capacity == FLASH_SIZE_1M) {
        return FLASH_ADDR_LAYOUT_1M_RUNTIME_BASE;
    }
    if (blc_flash_capacity == FLASH_SIZE_2M) {
        return FLASH_ADDR_LAYOUT_2M_RUNTIME_BASE;
    }
    return FLASH_ADDR_LAYOUT_512K_RUNTIME_BASE;
}

static inline u16 flash_store_cfg_get_runtime_sectors(void)
{
    return FLASH_ADDR_RUNTIME_SECTORS;
}

static inline u32 flash_store_cfg_get_soc_kv_base(void)
{
    if (!flash_store_cfg_layout_supported()) {
        return 0u;
    }

    if (blc_flash_capacity == FLASH_SIZE_1M) {
        return FLASH_ADDR_LAYOUT_1M_RUN_KV_BASE;
    }
    if (blc_flash_capacity == FLASH_SIZE_2M) {
        return FLASH_ADDR_LAYOUT_2M_RUN_KV_BASE;
    }
    return FLASH_ADDR_LAYOUT_512K_RUN_KV_BASE;
}

static inline u16 flash_store_cfg_get_soc_kv_sectors(void)
{
    return FLASH_ADDR_RUN_KV_SECTORS;
}

static inline u32 flash_store_cfg_get_cold_kv_base(void)
{
    if (!flash_store_cfg_layout_supported()) {
        return 0u;
    }

    if (blc_flash_capacity == FLASH_SIZE_1M) {
        return FLASH_ADDR_LAYOUT_1M_SOFT_PROTECT;
    }
    if (blc_flash_capacity == FLASH_SIZE_2M) {
        return FLASH_ADDR_LAYOUT_2M_SOFT_PROTECT;
    }
    return FLASH_ADDR_LAYOUT_512K_SOFT_PROTECT;
}

static inline u32 flash_store_cfg_get_event_log_base(void)
{
    if (!flash_store_cfg_layout_supported()) {
        return 0u;
    }

    if (blc_flash_capacity == FLASH_SIZE_1M) {
        return FLASH_ADDR_LAYOUT_1M_LOG_BASE;
    }
    if (blc_flash_capacity == FLASH_SIZE_2M) {
        return FLASH_ADDR_LAYOUT_2M_LOG_BASE;
    }
    return FLASH_ADDR_LAYOUT_512K_LOG_BASE;
}

static inline u16 flash_store_cfg_get_event_log_sectors(void)
{
    return FLASH_ADDR_LOG_SECTORS;
}

static inline u32 flash_store_cfg_get_safety_log_base(void)
{
    u32 base = flash_store_cfg_get_event_log_base();
    return base ? (base + (FLASH_ADDR_LOG_SECTORS * FLASH_SECTOR_SIZE)) : 0u;
}

static inline u16 flash_store_cfg_get_safety_log_sectors(void)
{
    return FLASH_ADDR_SAFETY_LOG_SECTORS;
}
