/*
 * 文件功能：Flash 分区、记录容量及存储时间配置；变更会涉及历史数据与 Bootloader/OTA
 * 地址边界。
 * bms/platform/telink/flash_store_cfg.h；实际编译归属见各产品 sources.txt。
 */
#pragma once

#include "tl_common.h"
#include "app_config.h"
#include "ble_flash.h"

#if (BLE_OTA_SERVER_ENABLE)
/* 取得 SDK 当前使用的多启动 OTA 地址。 */
u32 blc_ota_getCurrentUsedMultipleBootAddress(void);
#endif

#define FLASH_SECTOR_SIZE                 4096u
#define FLASH_PAGE_SIZE                   256u

/* 三个业务持久域；旧开发布局失效，APP/OTA 和 SDK 区域不变。 */
#define FLASH_ADDR_EVENT_SECTORS          16u
#define FLASH_ADDR_STATE_SECTORS          8u
#define FLASH_ADDR_CONFIG_SECTORS         4u

#define FLASH_ADDR_LAYOUT_512K_EVENT_BASE    0x4C000u
#define FLASH_ADDR_LAYOUT_512K_STATE_BASE    0x44000u
#define FLASH_ADDR_LAYOUT_512K_CONFIG_BASE   0x40000u

#define FLASH_ADDR_LAYOUT_1M_EVENT_BASE      0xBC000u
#define FLASH_ADDR_LAYOUT_1M_STATE_BASE      0xB4000u
#define FLASH_ADDR_LAYOUT_1M_CONFIG_BASE     0xB0000u

#define FLASH_ADDR_LAYOUT_2M_EVENT_BASE      0x1BC000u
#define FLASH_ADDR_LAYOUT_2M_STATE_BASE      0x1B4000u
#define FLASH_ADDR_LAYOUT_2M_CONFIG_BASE     0x1B0000u


/* 检查当前 Flash 容量是否支持配置的持久分区。 */
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

/* 取得状态持久域的 Flash 起始地址。 */
static inline u32 flash_store_cfg_get_state_base(void)
{
    if (!flash_store_cfg_layout_supported()) return 0u;
    if (blc_flash_capacity == FLASH_SIZE_1M) return FLASH_ADDR_LAYOUT_1M_STATE_BASE;
    if (blc_flash_capacity == FLASH_SIZE_2M) return FLASH_ADDR_LAYOUT_2M_STATE_BASE;
    return FLASH_ADDR_LAYOUT_512K_STATE_BASE;
}
/* 取得状态持久域占用的擦除扇区数。 */
static inline u16 flash_store_cfg_get_state_sectors(void) { return FLASH_ADDR_STATE_SECTORS; }

/* 取得配置持久域的 Flash 起始地址。 */
static inline u32 flash_store_cfg_get_config_base(void)
{
    if (!flash_store_cfg_layout_supported()) return 0u;
    if (blc_flash_capacity == FLASH_SIZE_1M) return FLASH_ADDR_LAYOUT_1M_CONFIG_BASE;
    if (blc_flash_capacity == FLASH_SIZE_2M) return FLASH_ADDR_LAYOUT_2M_CONFIG_BASE;
    return FLASH_ADDR_LAYOUT_512K_CONFIG_BASE;
}
/* 取得配置持久域占用的擦除扇区数。 */
static inline u16 flash_store_cfg_get_config_sectors(void) { return FLASH_ADDR_CONFIG_SECTORS; }

/* 取得历史事件持久域的 Flash 起始地址。 */
static inline u32 flash_store_cfg_get_event_log_base(void)
{
    if (!flash_store_cfg_layout_supported()) return 0u;
    if (blc_flash_capacity == FLASH_SIZE_1M) return FLASH_ADDR_LAYOUT_1M_EVENT_BASE;
    if (blc_flash_capacity == FLASH_SIZE_2M) return FLASH_ADDR_LAYOUT_2M_EVENT_BASE;
    return FLASH_ADDR_LAYOUT_512K_EVENT_BASE;
}
/* 取得历史事件持久域占用的擦除扇区数。 */
static inline u16 flash_store_cfg_get_event_log_sectors(void) { return FLASH_ADDR_EVENT_SECTORS; }
