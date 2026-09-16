#include "bms_storage_platform.h"

#include "tl_common.h"
#include "drivers.h"
#include "flash_store_cfg.h"
#include "flash_store_safe.h"

static int bms_storage_telink_begin(void *ctx)
{
    (void)ctx;
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
    return flash_store_verify_bytes((u32)addr, (const u8 *)buf, (u32)len);
}

static int bms_storage_telink_erase(void *ctx, uint32_t addr, uint32_t len)
{
    (void)ctx;
    if ((len != FLASH_SECTOR_SIZE) || ((addr % FLASH_SECTOR_SIZE) != 0u)) return 0;
    flash_erase_sector((u32)addr);
    return flash_store_verify_erased((u32)addr, FLASH_SECTOR_SIZE);
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
