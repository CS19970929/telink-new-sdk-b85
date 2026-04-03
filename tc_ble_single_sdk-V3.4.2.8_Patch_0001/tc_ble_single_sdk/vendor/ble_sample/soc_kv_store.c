#include "soc_kv_store.h"
#include "flash_kv32.h"
#include <string.h>

#define SOC_KV_KEY_SOC    0x0001u
#define SOC_KV_KEY_DSG    0x0002u
#define SOC_KV_KEY_CYCLE  0x0003u

static flash_kv32_t g_soc_kv;
static flash_kv32_cache_entry_t g_soc_cache[3];
static u32 g_soc_sector_addrs[(SOC_KV_HOT_SECTORS > 0) ? SOC_KV_HOT_SECTORS : 1];

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
    flash_write_page(addr, (int)len, (u8 *)buf);
    return 1;
}

static int soc_flash_erase_sector(void *ctx, u32 addr, u32 size)
{
    (void)ctx;
    (void)size;
    flash_erase_sector(addr);
    return 1;
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

    for (i = 0; i < SOC_KV_HOT_SECTORS; ++i) {
        g_soc_sector_addrs[i] = SOC_KV_HOT_BASE + ((u32)i * SOC_KV_HOT_SECTOR_SIZE);
    }
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

    if (SOC_KV_HOT_SECTORS < 2) {
        return 0;
    }

    soc_kv_fill_sector_addrs();

    memset(&cfg, 0, sizeof(cfg));
    cfg.port = soc_kv_port();
    cfg.sector_addrs = g_soc_sector_addrs;
    cfg.keys = g_soc_keys;
    cfg.sector_count = SOC_KV_HOT_SECTORS;
    cfg.sector_size = SOC_KV_HOT_SECTOR_SIZE;
    cfg.write_align = 4u;
    cfg.key_count = (u16)(sizeof(g_soc_keys) / sizeof(g_soc_keys[0]));

    return flash_kv32_init(&g_soc_kv, &cfg, g_soc_cache);
}

soc_kv_data_t soc_kv_store_get_default_data(void)
{
    soc_kv_data_t data;

    data.soc = SOC_PARAM_DEFAULT_SOC;
    data.dsg = SOC_PARAM_DEFAULT_DSG;
    data.cycle = SOC_PARAM_DEFAULT_CYCLE;
    return data;
}

soc_kv_data_t soc_kv_store_get(void)
{
    soc_kv_data_t data = soc_kv_store_get_default_data();

    data.soc = soc_kv_get_value(SOC_KV_KEY_SOC, data.soc);
    data.dsg = soc_kv_get_value(SOC_KV_KEY_DSG, data.dsg);
    data.cycle = soc_kv_get_value(SOC_KV_KEY_CYCLE, data.cycle);
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

    pairs[0].key = SOC_KV_KEY_SOC;
    pairs[0].value = soc;
    pairs[1].key = SOC_KV_KEY_DSG;
    pairs[1].value = dsg;
    pairs[2].key = SOC_KV_KEY_CYCLE;
    pairs[2].value = cycle;
    (void)flash_kv32_write_pairs(&g_soc_kv, pairs, (u16)(sizeof(pairs) / sizeof(pairs[0])));
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
