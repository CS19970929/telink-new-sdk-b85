#include "soc_kv_store.h"
#include <string.h>

#define SOC_KV_RECORD_MAGIC    0x4B56u
#define SOC_KV_RECORD_VERSION  0x0001u

typedef struct {
    u16 magic;
    u16 version;
    u32 seq;
    u16 soc;
    u16 dsg;
    u16 cycle;
    u16 checksum;
} soc_kv_record_t;

#define SOC_KV_RECORD_BYTES  ((u32)sizeof(soc_kv_record_t))

typedef struct {
    soc_kv_data_t data;
    u32 next_off;
    u32 last_seq;
    u8 has_valid;
    u8 tail_dirty;
} soc_kv_sector_state_t;

static soc_kv_data_t g_cache;
static soc_kv_data_t g_last_logged;
static soc_kv_dbg_t  g_dbg;
static u32           g_next_seq = 1;

static void soc_kv_set_default_data(soc_kv_data_t *data)
{
    data->soc = SOC_KV_DEFAULT_SOC;
    data->dsg = SOC_KV_DEFAULT_DSG;
    data->cycle = SOC_KV_DEFAULT_CYCLE;
}

static inline void flash_read_bytes(u32 addr, u8 *buf, u32 len)
{
    flash_read_page(addr, (int)len, buf);
}

static inline void flash_write_bytes(u32 addr, const u8 *buf, u32 len)
{
    flash_write_page(addr, (int)len, (u8 *)buf);
}

static inline void flash_erase_sector_safe(u32 base)
{
    flash_erase_sector(base);
}

static u32 other_sector(u32 base)
{
    return (base == FLASH_ADR_SOC_A) ? FLASH_ADR_SOC_B : FLASH_ADR_SOC_A;
}

static u16 soc_kv_record_checksum(const soc_kv_record_t *rec)
{
    const u8 *ptr = (const u8 *)rec;
    u16 sum = 0x5A5Au;

    for (u32 i = 0; i < (SOC_KV_RECORD_BYTES - sizeof(rec->checksum)); ++i) {
        sum = (u16)(sum + ptr[i]);
    }

    return (u16)(sum ^ 0xA55Au);
}

static int soc_kv_record_is_erased(const soc_kv_record_t *rec)
{
    const u8 *ptr = (const u8 *)rec;

    for (u32 i = 0; i < SOC_KV_RECORD_BYTES; ++i) {
        if (ptr[i] != 0xFFu) {
            return 0;
        }
    }

    return 1;
}

static int soc_kv_record_is_valid(const soc_kv_record_t *rec)
{
    if (rec->magic != SOC_KV_RECORD_MAGIC) {
        return 0;
    }

    if (rec->version != SOC_KV_RECORD_VERSION) {
        return 0;
    }

    if (rec->checksum != soc_kv_record_checksum(rec)) {
        return 0;
    }

    return 1;
}

static void soc_kv_build_record(soc_kv_record_t *rec, const soc_kv_data_t *data, u32 seq)
{
    rec->magic = SOC_KV_RECORD_MAGIC;
    rec->version = SOC_KV_RECORD_VERSION;
    rec->seq = seq;
    rec->soc = data->soc;
    rec->dsg = data->dsg;
    rec->cycle = data->cycle;
    rec->checksum = 0;
    rec->checksum = soc_kv_record_checksum(rec);
}

static void soc_kv_record_to_data(const soc_kv_record_t *rec, soc_kv_data_t *data)
{
    data->soc = rec->soc;
    data->dsg = rec->dsg;
    data->cycle = rec->cycle;
}

static void scan_sector(u32 base, soc_kv_sector_state_t *state)
{
    soc_kv_set_default_data(&state->data);
    state->next_off = 0;
    state->last_seq = 0;
    state->has_valid = 0;
    state->tail_dirty = 0;

    for (u32 off = 0; off + SOC_KV_RECORD_BYTES <= SOC_SECTOR_SIZE; off += SOC_KV_RECORD_BYTES) {
        soc_kv_record_t rec;

        flash_read_bytes(base + off, (u8 *)&rec, SOC_KV_RECORD_BYTES);

        if (soc_kv_record_is_erased(&rec)) {
            state->next_off = off;
            return;
        }

        if (!soc_kv_record_is_valid(&rec)) {
            state->next_off = SOC_SECTOR_SIZE;
            state->tail_dirty = 1;
            return;
        }

        soc_kv_record_to_data(&rec, &state->data);
        state->last_seq = rec.seq;
        state->has_valid = 1;
        state->next_off = off + SOC_KV_RECORD_BYTES;
    }

    state->next_off = SOC_SECTOR_SIZE;
}

static int append_snapshot(u32 base, u32 *io_off, const soc_kv_data_t *data, u32 seq)
{
    u32 off = *io_off;
    soc_kv_record_t rec;
    soc_kv_record_t verify;

    if (off + SOC_KV_RECORD_BYTES > SOC_SECTOR_SIZE) {
        return 0;
    }

    soc_kv_build_record(&rec, data, seq);
    flash_write_bytes(base + off, (const u8 *)&rec, SOC_KV_RECORD_BYTES);
    flash_read_bytes(base + off, (u8 *)&verify, SOC_KV_RECORD_BYTES);

    if (memcmp(&rec, &verify, sizeof(rec)) != 0) {
        return 0;
    }

    if (!soc_kv_record_is_valid(&verify)) {
        return 0;
    }

    *io_off = off + SOC_KV_RECORD_BYTES;
    return 1;
}

static int rollover(void)
{
    u32 old_base = g_dbg.active_base;
    u32 new_base = other_sector(old_base);
    u32 off = 0;

    flash_erase_sector_safe(new_base);

    if (!append_snapshot(new_base, &off, &g_cache, g_next_seq)) {
        return 0;
    }

    g_dbg.active_base = new_base;
    g_dbg.write_off = off;
    g_dbg.loaded = 1;
    g_dbg.tail_dirty = 0;

    g_last_logged = g_cache;
    ++g_next_seq;
    g_dbg.next_seq = g_next_seq;

    flash_erase_sector_safe(old_base);
    return 1;
}

static int persist_cache(void)
{
    if (g_dbg.tail_dirty || (g_dbg.write_off + SOC_KV_RECORD_BYTES > SOC_SECTOR_SIZE)) {
        return rollover();
    }

    if (!append_snapshot(g_dbg.active_base, &g_dbg.write_off, &g_cache, g_next_seq)) {
        g_dbg.tail_dirty = 1;
        return 0;
    }

    g_dbg.loaded = 1;
    g_dbg.tail_dirty = 0;
    g_last_logged = g_cache;
    ++g_next_seq;
    g_dbg.next_seq = g_next_seq;

    return 1;
}

static void select_active_sector(const soc_kv_sector_state_t *a, const soc_kv_sector_state_t *b)
{
    if (a->has_valid && (!b->has_valid || a->last_seq >= b->last_seq)) {
        g_cache = a->data;
        g_dbg.active_base = FLASH_ADR_SOC_A;
        g_dbg.write_off = a->next_off;
        g_dbg.loaded = 1;
        g_dbg.tail_dirty = a->tail_dirty;
        return;
    }

    if (b->has_valid) {
        g_cache = b->data;
        g_dbg.active_base = FLASH_ADR_SOC_B;
        g_dbg.write_off = b->next_off;
        g_dbg.loaded = 1;
        g_dbg.tail_dirty = b->tail_dirty;
        return;
    }

    soc_kv_set_default_data(&g_cache);
    g_dbg.loaded = 0;

    if (!a->tail_dirty) {
        g_dbg.active_base = FLASH_ADR_SOC_A;
        g_dbg.write_off = a->next_off;
        g_dbg.tail_dirty = 0;
        return;
    }

    if (!b->tail_dirty) {
        g_dbg.active_base = FLASH_ADR_SOC_B;
        g_dbg.write_off = b->next_off;
        g_dbg.tail_dirty = 0;
        return;
    }

    g_dbg.active_base = FLASH_ADR_SOC_A;
    g_dbg.write_off = SOC_SECTOR_SIZE;
    g_dbg.tail_dirty = 1;
}

int soc_kv_store_init(void)
{
    soc_kv_sector_state_t a;
    soc_kv_sector_state_t b;
    u32 max_seq = 0;

    memset(&g_cache, 0, sizeof(g_cache));
    memset(&g_last_logged, 0, sizeof(g_last_logged));
    memset(&g_dbg, 0, sizeof(g_dbg));

    scan_sector(FLASH_ADR_SOC_A, &a);
    scan_sector(FLASH_ADR_SOC_B, &b);

    if (a.has_valid && a.last_seq > max_seq) {
        max_seq = a.last_seq;
    }

    if (b.has_valid && b.last_seq > max_seq) {
        max_seq = b.last_seq;
    }

    select_active_sector(&a, &b);

    g_last_logged = g_cache;
    g_next_seq = max_seq + 1;
    if (g_next_seq == 0) {
        g_next_seq = 1;
    }
    g_dbg.next_seq = g_next_seq;

    if (g_dbg.tail_dirty || (g_dbg.write_off + SOC_KV_RECORD_BYTES > SOC_SECTOR_SIZE)) {
        if (!rollover()) {
            return 0;
        }
    }

    return 1;
}

soc_kv_data_t soc_kv_store_get(void)
{
    return g_cache;
}

int soc_kv_store_put(soc_item_t item, u16 value)
{
    if (item == SOC_ITEM_SOC) {
        g_cache.soc = value;
    } else if (item == SOC_ITEM_SOH) {
        g_cache.dsg = value;
    } else if (item == SOC_ITEM_CYCLE) {
        g_cache.cycle = value;
    } else {
        return 0;
    }

    return persist_cache();
}

void soc_kv_store_update_and_log_if_changed(u16 soc, u16 dsg, u16 cycle)
{
    g_cache.soc = soc;
    g_cache.dsg = dsg;
    g_cache.cycle = cycle;

    if ((g_last_logged.soc != g_cache.soc) ||
        (g_last_logged.dsg != g_cache.dsg) ||
        (g_last_logged.cycle != g_cache.cycle)) {
        (void)persist_cache();
    }
}

void soc_kv_store_factory_reset(void)
{
    flash_erase_sector_safe(FLASH_ADR_SOC_A);
    flash_erase_sector_safe(FLASH_ADR_SOC_B);

    soc_kv_set_default_data(&g_cache);
    g_last_logged = g_cache;

    memset(&g_dbg, 0, sizeof(g_dbg));
    g_dbg.active_base = FLASH_ADR_SOC_A;
    g_dbg.next_seq = 1;
    g_next_seq = 1;
}

soc_kv_dbg_t soc_kv_store_get_dbg(void)
{
    return g_dbg;
}
