#pragma once

#include "tl_common.h"
#include "app_config.h"
#include "ble_flash.h"
#include "stack/ble/service/ota/ota_server.h"

/*
 * Legacy addresses are kept for compatibility and data migration only.
 * New code should use the getters below instead of hard-coded macros.
 */
#define FLASH_ADDR_USER_DATA_START1            (0x40000u)
#define FLASH_ADDR_USER_DATA_END1              (0x74000u)
#define FLASH_ADDR_USER_DATA_START2            (0x78000u)
#define FLASH_ADDR_USER_DATA_END2              (0x80000u)

#define FLASH_SECTOR_SIZE                      4096u
#define FLASH_PAGE_SIZE                        256u

#define FLASH_ADDR_USER_DATA_BASE1             0x40000u

#define FLASH_ADDR_SOFT_PROTECT_BASE           0x78000u
#define FLASH_ADDR_SOFT_PROTECT_BASE_LEGACY    FLASH_ADDR_SOFT_PROTECT_BASE

#define FLASH_ADDR_RUN_KV_BASE                 0x70000u
#define FLASH_ADDR_RUN_KV_BASE_LEGACY          FLASH_ADDR_RUN_KV_BASE
#define FLASH_ADDR_RUN_KV_SECTORS              2u

#define FLASH_ADDR_LOG_BASE                    0x72000u
#define FLASH_ADDR_LOG_BASE_LEGACY             FLASH_ADDR_LOG_BASE
#define FLASH_ADDR_LOG_SECTORS                 8u

#define FLASH_ADDR_BLE_NAME_BASE               0x50000u
#define FLASH_ADDR_BLE_NAME_BASE_LEGACY        FLASH_ADDR_BLE_NAME_BASE

#define FLASH_ADR_RUNTIME                      (FLASH_ADDR_BLE_NAME_BASE + 0x1000u)
#define FLASH_ADR_RUNTIME_LEGACY               FLASH_ADR_RUNTIME
#define FLASH_ADDR_RUNTIME_SECTORS             2u
#define RUNTIME_FLAG                           0xA5A5u

#define FLASH_ADDR_LAYOUT_512K_BTNAME_BASE     0x50000u
#define FLASH_ADDR_LAYOUT_512K_RUNTIME_BASE    0x51000u
#define FLASH_ADDR_LAYOUT_512K_RUN_KV_BASE     0x70000u
#define FLASH_ADDR_LAYOUT_512K_SOFT_PROTECT    0x72000u
#define FLASH_ADDR_LAYOUT_512K_LOG_BASE        0x40000u

#define FLASH_ADDR_LAYOUT_1M_BTNAME_BASE       0xC0000u
#define FLASH_ADDR_LAYOUT_1M_RUNTIME_BASE      0xC1000u
#define FLASH_ADDR_LAYOUT_1M_RUN_KV_BASE       0xC3000u
#define FLASH_ADDR_LAYOUT_1M_SOFT_PROTECT      0xC5000u
#define FLASH_ADDR_LAYOUT_1M_LOG_BASE          0xC7000u

#define FLASH_ADDR_LAYOUT_2M_BTNAME_BASE       0x1C0000u
#define FLASH_ADDR_LAYOUT_2M_RUNTIME_BASE      0x1C1000u
#define FLASH_ADDR_LAYOUT_2M_RUN_KV_BASE       0x1C3000u
#define FLASH_ADDR_LAYOUT_2M_SOFT_PROTECT      0x1C5000u
#define FLASH_ADDR_LAYOUT_2M_LOG_BASE          0x1C7000u

static inline int flash_store_cfg_layout_supported(void)
{
#if (BLE_OTA_SERVER_ENABLE)
    if ((blc_flash_capacity == FLASH_SIZE_512K) &&
        (blc_ota_getCurrentUsedMultipleBootAddress() != MULTI_BOOT_ADDR_0x20000)) {
        return 0;
    }
#endif
    return 1;
}

static inline u32 flash_store_cfg_get_bt_name_base(void)
{
    if (!flash_store_cfg_layout_supported()) {
        return 0u;
    }

    if (blc_flash_capacity == FLASH_SIZE_1M) {
        return FLASH_ADDR_LAYOUT_1M_BTNAME_BASE;
    }
    if (blc_flash_capacity == FLASH_SIZE_2M) {
        return FLASH_ADDR_LAYOUT_2M_BTNAME_BASE;
    }
    return FLASH_ADDR_LAYOUT_512K_BTNAME_BASE;
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

static inline u32 flash_store_cfg_get_legacy_param_base(void)
{
    return FLASH_ADDR_SOFT_PROTECT_BASE_LEGACY;
}

static inline u32 flash_store_cfg_get_legacy_runtime_base(void)
{
    return FLASH_ADR_RUNTIME_LEGACY;
}

static inline u32 flash_store_cfg_get_legacy_bt_name_base(void)
{
    return FLASH_ADDR_BLE_NAME_BASE_LEGACY;
}

static inline u32 flash_store_cfg_get_legacy_soc_kv_base(void)
{
    return FLASH_ADDR_RUN_KV_BASE_LEGACY;
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
