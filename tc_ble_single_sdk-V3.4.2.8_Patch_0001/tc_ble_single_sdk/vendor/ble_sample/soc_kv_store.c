#include "soc_kv_store.h"
#include "flash_kv32.h"
#include "flash_store_cfg.h"
#include "flash_store_safe.h"
#include <string.h>

#define SOC_KV_KEY_SOC               0x0001u
#define SOC_KV_KEY_DSG               0x0002u
#define SOC_KV_KEY_CYCLE             0x0003u
#define SOC_KV_KEY_LEARNED_CAPACITY  0x0004u
#define SOC_KV_KEY_FLAGS             0x0005u

static flash_kv32_t g_soc_kv;
static flash_kv32_cache_entry_t g_soc_cache[5];
static u32 g_soc_sector_addrs[(SOC_KV_HOT_SECTORS > 0) ? SOC_KV_HOT_SECTORS : 1];

static const flash_kv32_key_def_t g_soc_keys[] = {
    { SOC_KV_KEY_SOC,              SOC_KV_DEFAULT_SOC },
    { SOC_KV_KEY_DSG,              SOC_KV_DEFAULT_DSG },
    { SOC_KV_KEY_CYCLE,            SOC_KV_DEFAULT_CYCLE },
    { SOC_KV_KEY_LEARNED_CAPACITY, SOC_PARAM_DEFAULT_LEARNED_CAPACITY },
    { SOC_KV_KEY_FLAGS,            SOC_PARAM_DEFAULT_FLAGS },
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
    static const flash_kv32_port_t port = { 0, soc_flash_read, soc_flash_prog, soc_flash_erase_sector, 0, 0 };
    return &port;
}

static void soc_kv_fill_sector_addrs(void)
{
    u16 i;
    u32 base = flash_store_cfg_get_soc_kv_base();
    for (i = 0; i < flash_store_cfg_get_soc_kv_sectors(); ++i)
        g_soc_sector_addrs[i] = base + ((u32)i * SOC_KV_HOT_SECTOR_SIZE);
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
    if (flash_store_cfg_get_soc_kv_base() == 0u) return 0;
    soc_kv_fill_sector_addrs();
    memset(&cfg, 0, sizeof(cfg));
    cfg.port = soc_kv_port();
    cfg.sector_addrs = g_soc_sector_addrs;
    cfg.keys = g_soc_keys;
    cfg.sector_count = flash_store_cfg_get_soc_kv_sectors();
    cfg.sector_size = SOC_KV_HOT_SECTOR_SIZE;
    cfg.write_align = 4u;
    cfg.key_count = (u16)(sizeof(g_soc_keys) / sizeof(g_soc_keys[0]));
    if (!flash_kv32_init(&g_soc_kv, &cfg, g_soc_cache)) return 0;
    return 1;
}

soc_kv_data_t soc_kv_store_get_default_data(void)
{
    soc_kv_data_t data;
    data.soc = SOC_PARAM_DEFAULT_SOC;
    data.dsg = SOC_PARAM_DEFAULT_DSG;
    data.cycle = SOC_PARAM_DEFAULT_CYCLE;
    data.learned_capacity_0p1ah = SOC_PARAM_DEFAULT_LEARNED_CAPACITY;
    data.flags = SOC_PARAM_DEFAULT_FLAGS;
    return data;
}

soc_kv_data_t soc_kv_store_get(void)
{
    soc_kv_data_t data = soc_kv_store_get_default_data();
    data.soc = soc_kv_get_value(SOC_KV_KEY_SOC, data.soc);
    data.dsg = soc_kv_get_value(SOC_KV_KEY_DSG, data.dsg);
    data.cycle = soc_kv_get_value(SOC_KV_KEY_CYCLE, data.cycle);
    data.learned_capacity_0p1ah = soc_kv_get_value(SOC_KV_KEY_LEARNED_CAPACITY, data.learned_capacity_0p1ah);
    data.flags = soc_kv_get_value(SOC_KV_KEY_FLAGS, data.flags);
    return data;
}

int soc_kv_store_write_all(u32 soc, u32 dsg, u32 cycle)
{
    flash_kv32_pair_t pairs[3];
    if ((g_soc_kv.cfg.port == NULL) && !soc_kv_store_init()) return 0;
    pairs[0].key = SOC_KV_KEY_SOC; pairs[0].value = soc;
    pairs[1].key = SOC_KV_KEY_DSG; pairs[1].value = dsg;
    pairs[2].key = SOC_KV_KEY_CYCLE; pairs[2].value = cycle;
    return flash_kv32_write_pairs(&g_soc_kv, pairs, 3u) ? 1 : 0;
}

int soc_kv_store_write_learning(u32 learned_capacity_0p1ah, u32 flags)
{
    flash_kv32_pair_t pairs[2];
    if ((g_soc_kv.cfg.port == NULL) && !soc_kv_store_init()) return 0;
    pairs[0].key = SOC_KV_KEY_LEARNED_CAPACITY; pairs[0].value = learned_capacity_0p1ah;
    pairs[1].key = SOC_KV_KEY_FLAGS; pairs[1].value = flags;
    return flash_kv32_write_pairs(&g_soc_kv, pairs, 2u) ? 1 : 0;
}

void soc_kv_store_update_and_log_if_changed(u32 soc, u32 dsg, u32 cycle)
{
    soc_kv_data_t current;
    if (g_soc_kv.cfg.port == NULL) return;
    current = soc_kv_store_get();
    if ((current.soc == soc) && (current.dsg == dsg) && (current.cycle == cycle)) return;
    (void)soc_kv_store_write_all(soc, dsg, cycle);
}
