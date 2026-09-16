#include "bms_storage_platform.h"

#include "tl_common.h"
#include "drivers.h"
#include "flash_store_cfg.h"
#include "flash_store_safe.h"
#include "conf.h"

extern u8 ota_is_working;
static bms_storage_diagnostics_t s_flash_diag;
static u32 s_flash_failure_tick_32k;
static u8 s_flash_failed;

void bms_storage_platform_get_diagnostics(bms_storage_diagnostics_t *out)
{
    if (out != 0) *out = s_flash_diag;
}

static int bms_storage_verify_result(int ok)
{
    if (!ok) {
        if (s_flash_diag.verify_failures != 0xFFFFFFFFu) ++s_flash_diag.verify_failures;
        s_flash_failed = 1u;
        s_flash_failure_tick_32k = pm_get_32k_tick();
    }
    return ok;
}

static int bms_storage_telink_begin(void *ctx)
{
    (void)ctx;
    if (ota_is_working ||
#if (APP_FLASH_PROTECTION_ENABLE)
        !app_flash_lock_restore_enabled() ||
#endif
        (s_flash_failed && (u32)(pm_get_32k_tick() - s_flash_failure_tick_32k) < BMS_STORAGE_RETRY_INTERVAL_32K)) {
        if (s_flash_diag.deferred_writes != 0xFFFFFFFFu) ++s_flash_diag.deferred_writes;
        return 0;
    }
    s_flash_failed = 0u;
    flash_store_begin_modify();
    return 1;
}

static void bms_storage_telink_end(void *ctx)
{
    (void)ctx;
    flash_store_end_modify();
}

static int bms_storage_telink_read(void *ctx, uint32_t addr, uint8_t *buf, uint32_t len)
{
    (void)ctx;
    flash_read_page((u32)addr, (int)len, (u8 *)buf);
    return 1;
}

static int bms_storage_telink_program(void *ctx,
                                      uint32_t addr,
                                      const uint8_t *buf,
                                      uint32_t len)
{
    uint32_t write_addr = addr;
    const uint8_t *write_buf = buf;
    uint32_t write_len = len;
    u32 started = pm_get_32k_tick(), elapsed;
    int ok;
    if (s_flash_diag.program_calls != 0xFFFFFFFFu) ++s_flash_diag.program_calls;

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
    elapsed = (u32)(pm_get_32k_tick() - started);
    if (elapsed > s_flash_diag.max_program_ticks_32k) s_flash_diag.max_program_ticks_32k = elapsed;
    return bms_storage_verify_result(ok);
}

static int bms_storage_telink_erase(void *ctx, uint32_t addr, uint32_t len)
{
    u32 started, elapsed;
    int ok;
    (void)ctx;
    if ((len != FLASH_SECTOR_SIZE) || ((addr % FLASH_SECTOR_SIZE) != 0u)) return 0;
    started = pm_get_32k_tick();
    if (s_flash_diag.erase_calls != 0xFFFFFFFFu) ++s_flash_diag.erase_calls;
    flash_erase_sector((u32)addr);
    ok = flash_store_verify_erased((u32)addr, FLASH_SECTOR_SIZE);
    elapsed = (u32)(pm_get_32k_tick() - started);
    if (elapsed > s_flash_diag.max_erase_ticks_32k) s_flash_diag.max_erase_ticks_32k = elapsed;
    return bms_storage_verify_result(ok);
}

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

int bms_storage_platform_region(bms_storage_domain_t domain, storage_region_t *region)
{
    uint32_t base = 0u;
    uint16_t sectors = 0u;

    if (region == 0) return 0;
    switch (domain) {
    case BMS_STORAGE_DOMAIN_CONFIG:
        base = flash_store_cfg_get_config_base();
        sectors = flash_store_cfg_get_config_sectors();
        break;
    case BMS_STORAGE_DOMAIN_STATE:
        base = flash_store_cfg_get_state_base();
        sectors = flash_store_cfg_get_state_sectors();
        break;
    case BMS_STORAGE_DOMAIN_FACTORY:
        base = flash_store_cfg_get_factory_base();
        sectors = flash_store_cfg_get_factory_sectors();
        break;
    case BMS_STORAGE_DOMAIN_EVENT:
        base = flash_store_cfg_get_event_log_base();
        sectors = flash_store_cfg_get_event_log_sectors();
        break;
    default:
        return 0;
    }
    if ((base == 0u) || (sectors < 2u)) return 0;
    region->base = base;
    region->size = (uint32_t)sectors * FLASH_SECTOR_SIZE;
    return 1;
}
