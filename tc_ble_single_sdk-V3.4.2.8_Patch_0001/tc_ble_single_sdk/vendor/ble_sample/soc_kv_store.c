#include "soc_kv_store.h"
#include "flash_kv32.h"
#include "flash_store_cfg.h"
#include "flash_store_safe.h"
#include <string.h>

#define SOC_KV_KEY_SOC    0x0001u
#define SOC_KV_KEY_DSG    0x0002u
#define SOC_KV_KEY_CYCLE  0x0003u
#define SOC_KV_SECTOR_MAGIC 0x324B5646u

static flash_kv32_t g_soc_kv;
static flash_kv32_cache_entry_t g_soc_cache[3];
static u32 g_soc_sector_addrs[(SOC_KV_HOT_SECTORS > 0) ? SOC_KV_HOT_SECTORS : 1];
static u32 g_soc_last_flush_tick = 0u;

static const flash_kv32_key_def_t g_soc_keys[] = {
    { SOC_KV_KEY_SOC,   SOC_KV_DEFAULT_SOC   },
    { SOC_KV_KEY_DSG,   SOC_KV_DEFAULT_DSG   },
    { SOC_KV_KEY_CYCLE, SOC_KV_DEFAULT_CYCLE },
};

static void soc_flash_read(void *ctx, u32 addr, u8 *buf, u32 len)
{
    (void)ctx;
    flash_read_page(addr, (int)len, buf);
}

static int soc_flash_prog(void *ctx, u32 addr, const u8 *buf, u32 len)
{
    (void)ctx;
    return flash_store_prog_checked(addr, buf, len);
}

static int soc_flash_erase_sector(void *ctx, u32 addr, u32 size)
{
    (void)ctx;
    return flash_store_erase_sector_checked(addr, size);
}

static const flash_kv32_port_t *soc_kv_port(void)
{
    static const flash_kv32_port_t port = {
        0,
        soc_flash_read,
        soc_flash_prog,
        soc_flash_erase_sector,
        0,
        0,
    };

    return &port;
}

static void soc_kv_fill_sector_addrs(void)
{
    u16 i;
    u32 base = flash_store_cfg_get_soc_kv_base();

    for (i = 0; i < flash_store_cfg_get_soc_kv_sectors(); ++i) {
        g_soc_sector_addrs[i] = base + ((u32)i * SOC_KV_HOT_SECTOR_SIZE);
    }
}

static u32 soc_kv_get_le32(const u8 *buf)
{
    return ((u32)buf[0]) |
           ((u32)buf[1] << 8) |
           ((u32)buf[2] << 16) |
           ((u32)buf[3] << 24);
}

static int soc_kv_legacy_header_present(u32 base_addr)
{
    u8 header[4];

    if (base_addr == 0u) {
        return 0;
    }

    flash_read_page(base_addr, sizeof(header), header);
    return (soc_kv_get_le32(header) == SOC_KV_SECTOR_MAGIC);
}

static void soc_kv_try_migrate_legacy(void)
{
    flash_kv32_t legacy_kv;
    flash_kv32_cache_entry_t legacy_cache[3];
    flash_kv32_cfg_t legacy_cfg;
    u32 legacy_sector_addrs[2];
    flash_kv32_pair_t pairs[3];
    soc_kv_data_t current;
    u32 legacy_base = flash_store_cfg_get_legacy_soc_kv_base();

    if ((legacy_base == 0u) || (legacy_base == flash_store_cfg_get_soc_kv_base())) {
        return;
    }

    if (!soc_kv_legacy_header_present(legacy_base) &&
        !soc_kv_legacy_header_present(legacy_base + SOC_KV_HOT_SECTOR_SIZE)) {
        return;
    }

    current = soc_kv_store_get();
    if ((current.soc != SOC_KV_DEFAULT_SOC) ||
        (current.dsg != SOC_KV_DEFAULT_DSG) ||
        (current.cycle != SOC_KV_DEFAULT_CYCLE)) {
        return;
    }

    memset(&legacy_cfg, 0, sizeof(legacy_cfg));
    legacy_sector_addrs[0] = legacy_base;
    legacy_sector_addrs[1] = legacy_base + SOC_KV_HOT_SECTOR_SIZE;
    legacy_cfg.port = soc_kv_port();
    legacy_cfg.sector_addrs = legacy_sector_addrs;
    legacy_cfg.keys = g_soc_keys;
    legacy_cfg.sector_count = SOC_KV_HOT_SECTORS;
    legacy_cfg.sector_size = SOC_KV_HOT_SECTOR_SIZE;
    legacy_cfg.write_align = 4u;
    legacy_cfg.key_count = (u16)(sizeof(g_soc_keys) / sizeof(g_soc_keys[0]));
    if (!flash_kv32_init(&legacy_kv, &legacy_cfg, legacy_cache)) {
        return;
    }

    pairs[0].key = SOC_KV_KEY_SOC;
    pairs[0].value = soc_kv_get_value(SOC_KV_KEY_SOC, SOC_KV_DEFAULT_SOC);
    (void)flash_kv32_get(&legacy_kv, SOC_KV_KEY_SOC, &pairs[0].value);
    pairs[1].key = SOC_KV_KEY_DSG;
    pairs[1].value = soc_kv_get_value(SOC_KV_KEY_DSG, SOC_KV_DEFAULT_DSG);
    (void)flash_kv32_get(&legacy_kv, SOC_KV_KEY_DSG, &pairs[1].value);
    pairs[2].key = SOC_KV_KEY_CYCLE;
    pairs[2].value = soc_kv_get_value(SOC_KV_KEY_CYCLE, SOC_KV_DEFAULT_CYCLE);
    (void)flash_kv32_get(&legacy_kv, SOC_KV_KEY_CYCLE, &pairs[2].value);

    (void)flash_kv32_write_pairs(&g_soc_kv, pairs, (u16)(sizeof(pairs) / sizeof(pairs[0])));
}

static u32 soc_kv_item_to_key(soc_item_t item)
{
    if (item == SOC_ITEM_SOC) {
        return SOC_KV_KEY_SOC;
    }

    if ((item == SOC_ITEM_DSG) || (item == SOC_ITEM_SOH)) {
        return SOC_KV_KEY_DSG;
    }

    if (item == SOC_ITEM_CYCLE) {
        return SOC_KV_KEY_CYCLE;
    }

    return 0u;
}

static u32 soc_kv_get_value(u32 key, u32 default_value)
{
    u32 value = default_value;

    (void)flash_kv32_get(&g_soc_kv, key, &value);
    return value;
}

int soc_kv_store_init(void)
{
    flash_kv32_cfg_t cfg;

    if ((flash_store_cfg_get_soc_kv_base() == 0u) || (flash_store_cfg_get_soc_kv_sectors() < 2u)) {
        return 0;
    }

    soc_kv_fill_sector_addrs();

    memset(&cfg, 0, sizeof(cfg));
    cfg.port = soc_kv_port();
    cfg.sector_addrs = g_soc_sector_addrs;
    cfg.keys = g_soc_keys;
    cfg.sector_count = flash_store_cfg_get_soc_kv_sectors();
    cfg.sector_size = SOC_KV_HOT_SECTOR_SIZE;
    cfg.write_align = 4u;
    cfg.key_count = (u16)(sizeof(g_soc_keys) / sizeof(g_soc_keys[0]));
    if (!flash_kv32_init(&g_soc_kv, &cfg, g_soc_cache)) {
        return 0;
    }

    g_soc_last_flush_tick = clock_time();
    soc_kv_try_migrate_legacy();
    return 1;
}

soc_kv_data_t soc_kv_store_get(void)
{
    soc_kv_data_t data;

    data.soc = soc_kv_get_value(SOC_KV_KEY_SOC, SOC_KV_DEFAULT_SOC);
    data.dsg = soc_kv_get_value(SOC_KV_KEY_DSG, SOC_KV_DEFAULT_DSG);
    data.cycle = soc_kv_get_value(SOC_KV_KEY_CYCLE, SOC_KV_DEFAULT_CYCLE);
    return data;
}

int soc_kv_store_put(soc_item_t item, u32 value)
{
    u32 key = soc_kv_item_to_key(item);

    if (key == 0u) {
        return 0;
    }

    return flash_kv32_set(&g_soc_kv, key, value);
}

void soc_kv_store_update_and_log_if_changed(u32 soc, u32 dsg, u32 cycle)
{
    flash_kv32_pair_t pairs[3];
    soc_kv_data_t current;

    if (g_soc_kv.cfg.port == NULL) {
        return;
    }

    current = soc_kv_store_get();
    if ((current.soc == soc) && (current.dsg == dsg) && (current.cycle == cycle)) {
        return;
    }

    if ((current.cycle != cycle) || clock_time_exceed(g_soc_last_flush_tick, 5000000u)) {
        pairs[0].key = SOC_KV_KEY_SOC;
        pairs[0].value = soc;
        pairs[1].key = SOC_KV_KEY_DSG;
        pairs[1].value = dsg;
        pairs[2].key = SOC_KV_KEY_CYCLE;
        pairs[2].value = cycle;
        if (flash_kv32_write_pairs(&g_soc_kv, pairs, (u16)(sizeof(pairs) / sizeof(pairs[0])))) {
            g_soc_last_flush_tick = clock_time();
        }
    }
}

void soc_kv_store_factory_reset(void)
{
    (void)flash_kv32_format(&g_soc_kv);
}

soc_kv_dbg_t soc_kv_store_get_dbg(void)
{
    flash_kv32_dbg_t kv_dbg = flash_kv32_get_dbg(&g_soc_kv);
    soc_kv_dbg_t dbg;

    memset(&dbg, 0, sizeof(dbg));
    dbg.active_base = kv_dbg.active_base;
    dbg.write_off = kv_dbg.write_off;
    dbg.next_seq = kv_dbg.next_seq;
    dbg.active_generation = kv_dbg.active_generation;
    dbg.active_sector = kv_dbg.active_sector;
    dbg.loaded = kv_dbg.loaded;
    dbg.tail_dirty = kv_dbg.tail_dirty;
    return dbg;
}
