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
 * TLSR8251F512 application NVM layout (default OTA boot address 0x20000):
 *
 *   0x00000 - 0x1FFFF  active firmware
 *   0x20000 - 0x3FFFF  OTA firmware slot
 *   0x40000 - 0x41FFF  cold/config KV (2 sectors, A/B generations)
 *   0x42000 - 0x45FFF  SOC hot KV journal (4 sectors)
 *   0x46000 - 0x55FFF  BMS event log ring (16 sectors)
 *   0x56000 - 0x57FFF  runtime journal (2 sectors)
 *   0x58000 - 0x73FFF  BMS expansion reserve
 *   0x74000 - 0x7FFFF  Telink/SDK reserved top area
 *
 * The 512K layout is valid only with the SDK's 0x20000 OTA scheme.  A
 * 0x40000 multiple-boot image would overlap the NVM area and is rejected by
 * flash_store_cfg_layout_supported().
 */
#define FLASH_ADDR_RUNTIME_SECTORS             2u
#define FLASH_ADDR_RUN_KV_SECTORS              4u
#define FLASH_ADDR_LOG_SECTORS                 16u
#define FLASH_ADDR_COLD_KV_SECTORS             2u
#define RUNTIME_FLAG                           0xA5A5u

#define FLASH_ADDR_LAYOUT_512K_SOFT_PROTECT          0x40000u
#define FLASH_ADDR_LAYOUT_512K_RUN_KV_BASE           0x42000u
#define FLASH_ADDR_LAYOUT_512K_LOG_BASE              0x46000u
#define FLASH_ADDR_LAYOUT_512K_RUNTIME_BASE          0x56000u
#define FLASH_ADDR_LAYOUT_512K_RESERVE_BASE          0x58000u
#define FLASH_ADDR_LAYOUT_512K_RESERVE_END           0x74000u

/*
 * Compatibility-only vacant anchor.  Bluetooth name persistence is part of
 * the cold/config KV store; no active code writes this standalone sector.
 */
#define FLASH_ADDR_LAYOUT_512K_BTNAME_BASE           0x58000u

/*
 * Historical top-of-flash parameter anchor.  Keep it documented so old code
 * cannot accidentally reuse it; this region belongs to Telink/SDK data.
 */
#define FLASH_ADDR_SOFT_PROTECT_BASE                 0x78000u

/* Existing larger-flash layouts are retained for SDK portability. */
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

/* 512K compile-time partition contracts. */
#if ((FLASH_ADDR_LAYOUT_512K_SOFT_PROTECT % FLASH_SECTOR_SIZE) != 0u) || \
    ((FLASH_ADDR_LAYOUT_512K_RUN_KV_BASE % FLASH_SECTOR_SIZE) != 0u) || \
    ((FLASH_ADDR_LAYOUT_512K_LOG_BASE % FLASH_SECTOR_SIZE) != 0u) || \
    ((FLASH_ADDR_LAYOUT_512K_RUNTIME_BASE % FLASH_SECTOR_SIZE) != 0u)
#error "BMS NVM partitions must be 4K-sector aligned"
#endif

#if ((FLASH_ADDR_LAYOUT_512K_SOFT_PROTECT + FLASH_ADDR_COLD_KV_SECTORS * FLASH_SECTOR_SIZE) > FLASH_ADDR_LAYOUT_512K_RUN_KV_BASE)
#error "BMS cold/config KV overlaps SOC KV"
#endif

#if ((FLASH_ADDR_LAYOUT_512K_RUN_KV_BASE + FLASH_ADDR_RUN_KV_SECTORS * FLASH_SECTOR_SIZE) > FLASH_ADDR_LAYOUT_512K_LOG_BASE)
#error "BMS SOC KV overlaps event log"
#endif

#if ((FLASH_ADDR_LAYOUT_512K_LOG_BASE + FLASH_ADDR_LOG_SECTORS * FLASH_SECTOR_SIZE) > FLASH_ADDR_LAYOUT_512K_RUNTIME_BASE)
#error "BMS event log overlaps runtime journal"
#endif

#if ((FLASH_ADDR_LAYOUT_512K_RUNTIME_BASE + FLASH_ADDR_RUNTIME_SECTORS * FLASH_SECTOR_SIZE) > FLASH_ADDR_LAYOUT_512K_RESERVE_BASE)
#error "BMS runtime journal overlaps expansion reserve"
#endif

#if (FLASH_ADDR_LAYOUT_512K_RESERVE_END > 0x74000u)
#error "BMS NVM crosses into Telink 512K reserved top area"
#endif

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

static inline u16 flash_store_cfg_get_cold_kv_sectors(void)
{
    return FLASH_ADDR_COLD_KV_SECTORS;
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

static inline int flash_store_cfg_range_inside(u32 addr, u32 len, u32 base, u32 bytes)
{
    u32 end;
    u32 region_end;

    if ((len == 0u) || (bytes == 0u)) {
        return 0;
    }

    end = addr + len;
    region_end = base + bytes;
    if ((end < addr) || (region_end < base)) {
        return 0;
    }

    return (addr >= base) && (end <= region_end);
}

static inline int flash_store_cfg_range_allowed(u32 addr, u32 len)
{
    u32 runtime_base;
    u32 soc_base;
    u32 cold_base;
    u32 log_base;

    if (!flash_store_cfg_layout_supported()) {
        return 0;
    }

    runtime_base = flash_store_cfg_get_runtime_base();
    soc_base = flash_store_cfg_get_soc_kv_base();
    cold_base = flash_store_cfg_get_cold_kv_base();
    log_base = flash_store_cfg_get_event_log_base();

    if (flash_store_cfg_range_inside(addr, len, cold_base,
                                     (u32)FLASH_ADDR_COLD_KV_SECTORS * FLASH_SECTOR_SIZE)) {
        return 1;
    }
    if (flash_store_cfg_range_inside(addr, len, soc_base,
                                     (u32)FLASH_ADDR_RUN_KV_SECTORS * FLASH_SECTOR_SIZE)) {
        return 1;
    }
    if (flash_store_cfg_range_inside(addr, len, log_base,
                                     (u32)FLASH_ADDR_LOG_SECTORS * FLASH_SECTOR_SIZE)) {
        return 1;
    }
    if (flash_store_cfg_range_inside(addr, len, runtime_base,
                                     (u32)FLASH_ADDR_RUNTIME_SECTORS * FLASH_SECTOR_SIZE)) {
        return 1;
    }

    return 0;
}
