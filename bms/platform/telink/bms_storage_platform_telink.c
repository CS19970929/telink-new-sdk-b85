/* D008 只允许访问独立的三个业务域。SDK 和两套启动区由原驱动管理。 */
#include "bms_storage_platform.h"
#include "bms_time.h"
#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"
#include "app.h"
#include "flash_store_cfg.h"
#include "flash_store_safe.h"

static const uint32_t s_bases[] = {D008_FLASH_CONFIG_BASE, D008_FLASH_STATE_BASE,
                                  D008_FLASH_EVENT_BASE};
static uint32_t s_failure_ms;
static uint8_t s_failed;

static int business_range(uint32_t addr, uint32_t size)
{
    unsigned i;
    if (!size || size > D008_FLASH_DOMAIN_SIZE) return 0;
    for (i = 0u; i < sizeof(s_bases) / sizeof(s_bases[0]); ++i)
        if (addr >= s_bases[i] && addr - s_bases[i] <= D008_FLASH_DOMAIN_SIZE - size)
            return 1;
    return 0;
}

static int read_witness(void)
{
    uint32_t signature = 0u;
    uint32_t running;

    if (ota_program_offset != 0u && ota_program_offset != 0x20000u) return 0;
    running = ota_program_offset ? 0u : 0x20000u;
    /* 同一 Flash READ 路径的 B85 启动标识仅检测整体失读，例如返回全 FF。 */
    flash_read_page(running + 8u, sizeof(signature), (u8 *)&signature);
    return signature == 0x544C4E4Bu;
}

static int bus_ready(void)
{
    return flash_store_cfg_layout_supported() && blc_flash_mid != 0u &&
        flash_read_mid() == blc_flash_mid &&
        !(flash_read_status(FLASH_READ_STATUS_CMD_LOWBYTE) & 1u) && read_witness();
}

static int remember_result(int ok)
{
    if (!ok) {
        s_failed = 1u;
        s_failure_ms = bms_time_ms();
    }
    return ok;
}

static int begin(void *ctx)
{
    (void)ctx;
    if (ota_is_working || !app_flash_lock_restore_enabled() ||
        (s_failed && bms_time_ms() - s_failure_ms < BMS_STORAGE_RETRY_INTERVAL_MS) ||
        !bus_ready()) return 0;
    s_failed = 0u;
    flash_store_begin_modify();
    return 1;
}

static void end(void *ctx)
{
    (void)ctx;
    flash_store_end_modify();
}

static int read(void *ctx, uint32_t addr, uint8_t *buf, uint32_t len)
{
    (void)ctx;
    if (!business_range(addr, len)) return 0;
    flash_read_page(addr, (int)len, (u8 *)buf);
    /* 读到可能的空白或总线故障值时，同时验证可读的启动标识。 */
    if ((buf[0] == 0xFFu && buf[len - 1u] == 0xFFu) ||
        (buf[0] == 0u && buf[len - 1u] == 0u)) return read_witness();
    return 1;
}

static int program(void *ctx, uint32_t addr, const uint8_t *buf, uint32_t len)
{
    (void)ctx;
    if (!business_range(addr, len)) return 0;
    while (len) {
        uint32_t chunk = FLASH_PAGE_SIZE - addr % FLASH_PAGE_SIZE;
        if (chunk > len) chunk = len;
        flash_write_page(addr, (int)chunk, (u8 *)buf);
        if (flash_read_status(FLASH_READ_STATUS_CMD_LOWBYTE) & 1u)
            return remember_result(0);
        addr += chunk;
        buf += chunk;
        len -= chunk;
    }
    return 1;
}

static int erase(void *ctx, uint32_t addr, uint32_t len)
{
    (void)ctx;
    if (len != FLASH_SECTOR_SIZE || addr % FLASH_SECTOR_SIZE ||
        !business_range(addr, len)) return 0;
    flash_erase_sector(addr);
    return remember_result(read_witness() && flash_store_verify_erased(addr, len));
}

const storage_port_t *bms_storage_platform_port(void)
{
    static const storage_port_t port = {
        0, FLASH_SECTOR_SIZE, 4u, 0xFFu, begin, end, read, program, erase,
    };
    return bus_ready() ? &port : 0;
}

int bms_storage_platform_region(bms_storage_domain_t domain, storage_region_t *region)
{
    if ((unsigned)domain >= BMS_STORAGE_DOMAIN_COUNT || !flash_store_cfg_layout_supported())
        return 0;
    region->base = s_bases[domain];
    region->size = D008_FLASH_DOMAIN_SIZE;
    return 1;
}
