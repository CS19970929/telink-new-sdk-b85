/*
 * 文件功能：Telink Flash 读写/擦除端口；处理 OTA/锁状态、地址边界、写后验证及耗时/失
 * 败诊断。
 * bms/platform/telink/bms_storage_platform_telink.c；
 * 实际编译归属见各产品 sources.txt。
 */
#include "bms_time.h"
#include "bms_afe_hw_profile.h"
#include "bms_storage_platform.h"

#include "tl_common.h"
#include "drivers.h"
#include "flash_store_safe.h"
#include "flash_store_cfg.h"
#include "bms_product.h"
#include "bms_update_policy.h"

extern u8 ota_is_working;
static u32 s_flash_failure_ms;
static u8 s_flash_failed;

/* 取得 Flash 操作结果及耗时诊断快照。 */

/* 记录写后或擦后验证结果。 */
static int bms_storage_verify_result(int ok)
{
    if (!ok) {
        s_flash_failed = 1u;
        s_flash_failure_ms = bms_time_ms();
    }
    return ok;
}

/* 存储事务门禁；Flash 解锁和 OTA 状态按现有策略决定能否写入，失败必须向上返回。 */
static int bms_storage_telink_begin(void *ctx)
{
    (void)ctx;
    if (ota_is_working ||
#if (APP_FLASH_PROTECTION_ENABLE)
        !app_flash_lock_restore_enabled() ||
#endif
        (s_flash_failed && (u32)(bms_time_ms() - s_flash_failure_ms) < BMS_STORAGE_RETRY_INTERVAL_MS)) {
        return 0;
    }
    s_flash_failed = 0u;
    flash_store_begin_modify();
    return 1;
}

/* 结束 Flash 修改事务并恢复保护状态。 */
static void bms_storage_telink_end(void *ctx)
{
    (void)ctx;
    flash_store_end_modify();
}

/* 读取 Telink Flash 字节；地址由记录层按分区 region 计算，本层不做额外范围判断。 */
static int bms_storage_telink_read(void *ctx, uint32_t addr, uint8_t *buf, uint32_t len)
{
    (void)ctx;
    flash_read_page((u32)addr, (int)len, (u8 *)buf);
    return 1;
}

/* 按编程边界写入 Flash 并记录结果。 */
static int bms_storage_telink_program(void *ctx,
                                      uint32_t addr,
                                      const uint8_t *buf,
                                      uint32_t len)
{
    uint32_t write_addr = addr;
    const uint8_t *write_buf = buf;
    uint32_t write_len = len;
    int ok;

    (void)ctx;
    while (write_len != 0u) {
        uint32_t page_off = write_addr % FLASH_PAGE_SIZE;
        uint32_t chunk = FLASH_PAGE_SIZE - page_off;
        if (chunk > write_len) chunk = write_len;
        flash_write_page((u32)write_addr, (int)chunk, (u8 *)write_buf);
        write_addr += chunk;
        write_buf += chunk;
        write_len -= chunk;
    }
    ok = flash_store_verify_bytes((u32)addr, (const u8 *)buf, (u32)len);
    return bms_storage_verify_result(ok);
}

/* 按擦除粒度擦除 Flash 并记录结果。 */
static int bms_storage_telink_erase(void *ctx, uint32_t addr, uint32_t len)
{
    int ok;
    (void)ctx;
    if ((len != FLASH_SECTOR_SIZE) || ((addr % FLASH_SECTOR_SIZE) != 0u)) return 0;
    flash_erase_sector((u32)addr);
    ok = flash_store_verify_erased((u32)addr, FLASH_SECTOR_SIZE);
    return bms_storage_verify_result(ok);
}

int bms_storage_platform_erase_factory_legacy(void)
{
    uint32_t base;
    uint16_t sector;
    int ok;
    if (!bms_ota_factory_reset_requested()) return 0;
    base = flash_store_cfg_get_legacy_base();
    if (base == 0u) return 0;
    for (sector = 0u; sector < FLASH_ADDR_LEGACY_SECTORS; ++sector) {
        if (!bms_storage_telink_begin(0)) return 0;
        ok = bms_storage_telink_erase(0, base + (uint32_t)sector * FLASH_SECTOR_SIZE,
                                     FLASH_SECTOR_SIZE);
        bms_storage_telink_end(0);
        if (!ok) return 0;
    }
    return 1;
}

/* 取得 Telink 存储端口操作集合。 */
const storage_port_t *bms_storage_platform_port(void)
{
    static const storage_port_t port = {
        0, FLASH_SECTOR_SIZE, 4u, 0xFFu,
        bms_storage_telink_begin, bms_storage_telink_end,
        bms_storage_telink_read, bms_storage_telink_program,
        bms_storage_telink_erase,
    };
    return &port;
}

/* 取得指定持久域的 Flash 区域配置。 */
int bms_storage_platform_region(bms_storage_domain_t domain, storage_region_t *region)
{
    uint32_t base = 0u;
    uint16_t sectors = 0u;

    if (region == 0) return 0;
    /* 出厂清理会覆盖完整业务区；未知容量或不匹配的 OTA 布局禁止写入。 */
    if (bms_ota_factory_reset_requested() && flash_store_cfg_get_legacy_base() == 0u) {
        return 0;
    }
    switch (domain) {
    case BMS_STORAGE_DOMAIN_CONFIG:
        base = flash_store_cfg_get_config_base();
        sectors = flash_store_cfg_get_config_sectors();
        break;
    case BMS_STORAGE_DOMAIN_STATE:
        base = flash_store_cfg_get_state_base();
        sectors = flash_store_cfg_get_state_sectors();
        break;
    case BMS_STORAGE_DOMAIN_EVENT:
        base = flash_store_cfg_get_event_log_base();
        sectors = flash_store_cfg_get_event_log_sectors();
        break;
    default:
        return 0;
    }
    if ((base == 0u) || (sectors < 2u)) {
        return 0;
    }
    region->base = base;
    region->size = (uint32_t)sectors * FLASH_SECTOR_SIZE;
    return 1;
}

/* 发布启动阶段的存储分区与操作诊断。 */
