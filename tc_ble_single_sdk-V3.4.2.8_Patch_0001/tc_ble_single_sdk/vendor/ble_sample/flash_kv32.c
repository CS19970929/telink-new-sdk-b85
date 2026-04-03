#include "flash_kv32.h"
#include <string.h>

#define FLASH_KV32_SECTOR_MAGIC        0x324B5646u
#define FLASH_KV32_SECTOR_VERSION      0x0001u
#define FLASH_KV32_SECTOR_HEADER_BYTES 32u
#define FLASH_KV32_SECTOR_DATA_OFF     FLASH_KV32_SECTOR_HEADER_BYTES
#define FLASH_KV32_PREPARE_MARK        0x50455250u
#define FLASH_KV32_ACTIVE_MARK         0x56544341u

#define FLASH_KV32_TX_MAGIC            0x5854u
#define FLASH_KV32_TX_VERSION          0x01u
#define FLASH_KV32_TX_HEADER_BYTES     16u
#define FLASH_KV32_TX_ITEM_BYTES       8u
#define FLASH_KV32_TX_COMMIT_BYTES     4u
#define FLASH_KV32_TX_COMMIT_MAGIC     0x54494D43u

typedef struct {
    u8 is_erased;
    u8 header_valid;
    u8 active;
    u32 generation;
} flash_kv32_sector_info_t;

static u16 kv_norm_align(u16 align)
{
    return (align < 4u) ? 4u : align;
}

static u32 kv_align_up(u32 value, u32 align)
{
    if (align == 0u) {
        return value;
    }

    return (value + align - 1u) / align * align;
}

static void kv_put_le16(u8 *buf, u16 value)
{
    buf[0] = (u8)(value & 0xFFu);
    buf[1] = (u8)((value >> 8) & 0xFFu);
}

static void kv_put_le32(u8 *buf, u32 value)
{
    buf[0] = (u8)(value & 0xFFu);
    buf[1] = (u8)((value >> 8) & 0xFFu);
    buf[2] = (u8)((value >> 16) & 0xFFu);
    buf[3] = (u8)((value >> 24) & 0xFFu);
}

static u16 kv_get_le16(const u8 *buf)
{
    return (u16)(buf[0] | ((u16)buf[1] << 8));
}

static u32 kv_get_le32(const u8 *buf)
{
    return ((u32)buf[0]) |
           ((u32)buf[1] << 8) |
           ((u32)buf[2] << 16) |
           ((u32)buf[3] << 24);
}

static int kv_is_erased_bytes(const u8 *buf, u32 len)
{
    u32 i;

    for (i = 0; i < len; ++i) {
        if (buf[i] != 0xFFu) {
            return 0;
        }
    }

    return 1;
}

static u32 kv_crc32_begin(void)
{
    return 0xFFFFFFFFu;
}

static u32 kv_crc32_step(u32 crc, const u8 *buf, u32 len)
{
    u32 i;
    u32 j;

    for (i = 0; i < len; ++i) {
        crc ^= buf[i];
        for (j = 0; j < 8u; ++j) {
            if ((crc & 1u) != 0u) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc >>= 1;
            }
        }
    }

    return crc;
}

static u32 kv_crc32_finish(u32 crc)
{
    return ~crc;
}

static u32 kv_crc32_calc(const u8 *buf, u32 len)
{
    return kv_crc32_finish(kv_crc32_step(kv_crc32_begin(), buf, len));
}

static int kv_port_prog(const flash_kv32_t *kv, u32 addr, const u8 *buf, u32 len)
{
    if ((kv == NULL) || (kv->cfg.port == NULL) || (kv->cfg.port->prog == NULL)) {
        return FLASH_KV32_FAILED;
    }

    if (kv->cfg.port->lock != NULL) {
        kv->cfg.port->lock(kv->cfg.port->ctx);
    }

    if (!kv->cfg.port->prog(kv->cfg.port->ctx, addr, buf, len)) {
        if (kv->cfg.port->unlock != NULL) {
            kv->cfg.port->unlock(kv->cfg.port->ctx);
        }
        return FLASH_KV32_FAILED;
    }

    if (kv->cfg.port->unlock != NULL) {
        kv->cfg.port->unlock(kv->cfg.port->ctx);
    }

    return FLASH_KV32_SUCCESS;
}

static int kv_port_erase(const flash_kv32_t *kv, u32 addr)
{
    if ((kv == NULL) || (kv->cfg.port == NULL) || (kv->cfg.port->erase_sector == NULL)) {
        return FLASH_KV32_FAILED;
    }

    if (kv->cfg.port->lock != NULL) {
        kv->cfg.port->lock(kv->cfg.port->ctx);
    }

    if (!kv->cfg.port->erase_sector(kv->cfg.port->ctx, addr, kv->cfg.sector_size)) {
        if (kv->cfg.port->unlock != NULL) {
            kv->cfg.port->unlock(kv->cfg.port->ctx);
        }
        return FLASH_KV32_FAILED;
    }

    if (kv->cfg.port->unlock != NULL) {
        kv->cfg.port->unlock(kv->cfg.port->ctx);
    }

    return FLASH_KV32_SUCCESS;
}

static void kv_port_read(const flash_kv32_t *kv, u32 addr, u8 *buf, u32 len)
{
    kv->cfg.port->read(kv->cfg.port->ctx, addr, buf, len);
}

static void kv_reset_cache_to_default(flash_kv32_t *kv)
{
    u16 i;

    for (i = 0; i < kv->cfg.key_count; ++i) {
        kv->cache[i].value = kv->cfg.keys[i].default_value;
    }
}

static int kv_find_key_index(const flash_kv32_t *kv, u32 key, u16 *out_index)
{
    u16 i;

    for (i = 0; i < kv->cfg.key_count; ++i) {
        if (kv->cfg.keys[i].key == key) {
            if (out_index != NULL) {
                *out_index = i;
            }
            return FLASH_KV32_SUCCESS;
        }
    }

    return FLASH_KV32_FAILED;
}

static u16 kv_calc_tx_total_len(const flash_kv32_t *kv, u16 item_count)
{
    u32 body_len;
    u32 total_len;

    body_len = FLASH_KV32_TX_HEADER_BYTES +
               ((u32)item_count * FLASH_KV32_TX_ITEM_BYTES) +
               FLASH_KV32_TX_COMMIT_BYTES;
    total_len = kv_align_up(body_len, kv->cfg.write_align);

    return (u16)total_len;
}

static int kv_validate_cfg(flash_kv32_t *kv)
{
    u16 min_tx_len;
    u32 max_payload_items;

    if ((kv == NULL) || (kv->cache == NULL)) {
        return FLASH_KV32_FAILED;
    }

    if ((kv->cfg.port == NULL) ||
        (kv->cfg.port->read == NULL) ||
        (kv->cfg.port->prog == NULL) ||
        (kv->cfg.port->erase_sector == NULL)) {
        return FLASH_KV32_FAILED;
    }

    if ((kv->cfg.sector_addrs == NULL) || (kv->cfg.keys == NULL)) {
        return FLASH_KV32_FAILED;
    }

    if ((kv->cfg.sector_count < 2u) || (kv->cfg.key_count == 0u) || (kv->cfg.key_count > 255u)) {
        return FLASH_KV32_FAILED;
    }

    kv->cfg.write_align = kv_norm_align(kv->cfg.write_align);

    min_tx_len = kv_calc_tx_total_len(kv, 1u);
    if ((u32)kv->cfg.sector_size <= (FLASH_KV32_SECTOR_HEADER_BYTES + min_tx_len)) {
        return FLASH_KV32_FAILED;
    }

    max_payload_items = ((u32)kv->cfg.sector_size - FLASH_KV32_SECTOR_HEADER_BYTES) / FLASH_KV32_TX_ITEM_BYTES;
    if ((u32)kv->cfg.key_count > max_payload_items) {
        return FLASH_KV32_FAILED;
    }

    if ((u32)FLASH_KV32_SECTOR_HEADER_BYTES + kv_calc_tx_total_len(kv, kv->cfg.key_count) > kv->cfg.sector_size) {
        return FLASH_KV32_FAILED;
    }

    return FLASH_KV32_SUCCESS;
}

static void kv_build_sector_header(u8 *buf, u32 generation)
{
    u32 crc;

    memset(buf, 0xFF, FLASH_KV32_SECTOR_HEADER_BYTES);
    kv_put_le32(buf + 0, FLASH_KV32_SECTOR_MAGIC);
    kv_put_le16(buf + 4, FLASH_KV32_SECTOR_VERSION);
    kv_put_le16(buf + 6, FLASH_KV32_SECTOR_HEADER_BYTES);
    kv_put_le32(buf + 8, generation);
    kv_put_le16(buf + 12, FLASH_KV32_SECTOR_DATA_OFF);
    kv_put_le16(buf + 14, 0u);
    crc = kv_crc32_calc(buf, 16u);
    kv_put_le32(buf + 16, crc);
    kv_put_le32(buf + 20, FLASH_KV32_PREPARE_MARK);
    kv_put_le32(buf + 24, 0xFFFFFFFFu);
    kv_put_le32(buf + 28, 0xFFFFFFFFu);
}

static int kv_parse_sector_header(const u8 *buf, flash_kv32_sector_info_t *info)
{
    u32 magic;
    u16 version;
    u16 header_size;
    u16 record_off;
    u32 header_crc;
    u32 calc_crc;
    u32 prepare_mark;
    u32 active_mark;

    memset(info, 0, sizeof(*info));

    if (kv_is_erased_bytes(buf, FLASH_KV32_SECTOR_HEADER_BYTES)) {
        info->is_erased = 1u;
        return FLASH_KV32_SUCCESS;
    }

    magic = kv_get_le32(buf + 0);
    version = kv_get_le16(buf + 4);
    header_size = kv_get_le16(buf + 6);
    record_off = kv_get_le16(buf + 12);
    header_crc = kv_get_le32(buf + 16);
    prepare_mark = kv_get_le32(buf + 20);
    active_mark = kv_get_le32(buf + 24);
    calc_crc = kv_crc32_calc(buf, 16u);

    if ((magic != FLASH_KV32_SECTOR_MAGIC) ||
        (version != FLASH_KV32_SECTOR_VERSION) ||
        (header_size != FLASH_KV32_SECTOR_HEADER_BYTES) ||
        (record_off != FLASH_KV32_SECTOR_DATA_OFF) ||
        (prepare_mark != FLASH_KV32_PREPARE_MARK) ||
        (header_crc != calc_crc)) {
        return FLASH_KV32_FAILED;
    }

    info->header_valid = 1u;
    info->active = (active_mark == FLASH_KV32_ACTIVE_MARK) ? 1u : 0u;
    info->generation = kv_get_le32(buf + 8);
    return FLASH_KV32_SUCCESS;
}

static int kv_read_sector_info(const flash_kv32_t *kv, u16 sector_idx, flash_kv32_sector_info_t *info)
{
    u8 header[FLASH_KV32_SECTOR_HEADER_BYTES];

    kv_port_read(kv, kv->cfg.sector_addrs[sector_idx], header, FLASH_KV32_SECTOR_HEADER_BYTES);
    return kv_parse_sector_header(header, info);
}

static int kv_prepare_sector(const flash_kv32_t *kv, u16 sector_idx, u32 generation)
{
    u8 header[FLASH_KV32_SECTOR_HEADER_BYTES];

    kv_build_sector_header(header, generation);
    return kv_port_prog(kv, kv->cfg.sector_addrs[sector_idx], header, FLASH_KV32_SECTOR_HEADER_BYTES);
}

static int kv_mark_sector_active(const flash_kv32_t *kv, u16 sector_idx)
{
    u8 active_mark[4];

    kv_put_le32(active_mark, FLASH_KV32_ACTIVE_MARK);
    return kv_port_prog(kv, kv->cfg.sector_addrs[sector_idx] + 24u, active_mark, sizeof(active_mark));
}

static int kv_pair_has_duplicates(const flash_kv32_t *kv, const flash_kv32_pair_t *pairs, u16 pair_count)
{
    u16 i;
    u16 j;

    (void)kv;

    for (i = 0; i < pair_count; ++i) {
        for (j = (u16)(i + 1u); j < pair_count; ++j) {
            if (pairs[i].key == pairs[j].key) {
                return FLASH_KV32_SUCCESS;
            }
        }
    }

    return FLASH_KV32_FAILED;
}

static u32 kv_snapshot_value(const flash_kv32_t *kv, u16 key_index, const flash_kv32_pair_t *pairs, u16 pair_count)
{
    u16 i;

    if (pairs != NULL) {
        for (i = 0; i < pair_count; ++i) {
            if (pairs[i].key == kv->cfg.keys[key_index].key) {
                return pairs[i].value;
            }
        }
    }

    return kv->cache[key_index].value;
}

static void kv_build_tx_header(u8 *buf, u8 item_count, u32 seq, u16 payload_len, u16 total_len, u32 crc)
{
    kv_put_le16(buf + 0, FLASH_KV32_TX_MAGIC);
    buf[2] = FLASH_KV32_TX_VERSION;
    buf[3] = item_count;
    kv_put_le32(buf + 4, seq);
    kv_put_le16(buf + 8, payload_len);
    kv_put_le16(buf + 10, total_len);
    kv_put_le32(buf + 12, crc);
}

static void kv_build_item(u8 *buf, u32 key, u32 value)
{
    kv_put_le32(buf + 0, key);
    kv_put_le32(buf + 4, value);
}

static int kv_write_tx(flash_kv32_t *kv,
                       u16 sector_idx,
                       u16 start_off,
                       const flash_kv32_pair_t *pairs,
                       u16 pair_count,
                       int snapshot,
                       u32 seq,
                       u16 *next_off)
{
    u8 header[FLASH_KV32_TX_HEADER_BYTES];
    u8 item_buf[FLASH_KV32_TX_ITEM_BYTES];
    u8 commit_buf[FLASH_KV32_TX_COMMIT_BYTES];
    u32 crc;
    u32 addr;
    u32 key;
    u32 value;
    u16 item_count;
    u16 payload_len;
    u16 total_len;
    u16 i;

    item_count = snapshot ? kv->cfg.key_count : pair_count;
    payload_len = (u16)(item_count * FLASH_KV32_TX_ITEM_BYTES);
    total_len = kv_calc_tx_total_len(kv, item_count);
    if ((u32)start_off + total_len > kv->cfg.sector_size) {
        return FLASH_KV32_FAILED;
    }

    kv_build_tx_header(header, (u8)item_count, seq, payload_len, total_len, 0u);
    crc = kv_crc32_begin();
    crc = kv_crc32_step(crc, header, 12u);

    if (snapshot) {
        for (i = 0; i < item_count; ++i) {
            key = kv->cfg.keys[i].key;
            value = kv_snapshot_value(kv, i, pairs, pair_count);
            kv_build_item(item_buf, key, value);
            crc = kv_crc32_step(crc, item_buf, FLASH_KV32_TX_ITEM_BYTES);
        }
    } else {
        for (i = 0; i < item_count; ++i) {
            kv_build_item(item_buf, pairs[i].key, pairs[i].value);
            crc = kv_crc32_step(crc, item_buf, FLASH_KV32_TX_ITEM_BYTES);
        }
    }

    kv_put_le32(header + 12, kv_crc32_finish(crc));

    addr = kv->cfg.sector_addrs[sector_idx] + start_off;
    if (!kv_port_prog(kv, addr, header, sizeof(header))) {
        return FLASH_KV32_FAILED;
    }

    addr += sizeof(header);
    if (snapshot) {
        for (i = 0; i < item_count; ++i) {
            key = kv->cfg.keys[i].key;
            value = kv_snapshot_value(kv, i, pairs, pair_count);
            kv_build_item(item_buf, key, value);
            if (!kv_port_prog(kv, addr, item_buf, sizeof(item_buf))) {
                return FLASH_KV32_FAILED;
            }
            addr += sizeof(item_buf);
        }
    } else {
        for (i = 0; i < item_count; ++i) {
            kv_build_item(item_buf, pairs[i].key, pairs[i].value);
            if (!kv_port_prog(kv, addr, item_buf, sizeof(item_buf))) {
                return FLASH_KV32_FAILED;
            }
            addr += sizeof(item_buf);
        }
    }

    kv_put_le32(commit_buf, FLASH_KV32_TX_COMMIT_MAGIC);
    addr = kv->cfg.sector_addrs[sector_idx] + start_off + total_len - FLASH_KV32_TX_COMMIT_BYTES;
    if (!kv_port_prog(kv, addr, commit_buf, sizeof(commit_buf))) {
        return FLASH_KV32_FAILED;
    }

    if (next_off != NULL) {
        *next_off = (u16)(start_off + total_len);
    }

    return FLASH_KV32_SUCCESS;
}

static void kv_apply_pairs(flash_kv32_t *kv, const flash_kv32_pair_t *pairs, u16 pair_count)
{
    u16 i;
    u16 key_index;

    for (i = 0; i < pair_count; ++i) {
        if (kv_find_key_index(kv, pairs[i].key, &key_index)) {
            kv->cache[key_index].value = pairs[i].value;
        }
    }
}

static int kv_scan_active_sector(flash_kv32_t *kv, u16 sector_idx, u32 *last_seq_out)
{
    u8 tx_header[FLASH_KV32_TX_HEADER_BYTES];
    u8 item_buf[FLASH_KV32_TX_ITEM_BYTES];
    u8 commit_buf[FLASH_KV32_TX_COMMIT_BYTES];
    u16 off = FLASH_KV32_SECTOR_DATA_OFF;
    u32 base = kv->cfg.sector_addrs[sector_idx];
    u32 crc;
    u32 calc_crc;
    u32 stored_crc;
    u32 seq;
    u16 payload_len;
    u16 total_len;
    u16 item_count;
    u16 i;
    u16 key_index;

    kv_reset_cache_to_default(kv);
    kv->dbg.tail_dirty = 0u;
    *last_seq_out = 0u;

    while ((u32)off + FLASH_KV32_TX_HEADER_BYTES + FLASH_KV32_TX_COMMIT_BYTES <= kv->cfg.sector_size) {
        kv_port_read(kv, base + off, tx_header, sizeof(tx_header));

        if (kv_is_erased_bytes(tx_header, sizeof(tx_header))) {
            kv->dbg.write_off = off;
            return FLASH_KV32_SUCCESS;
        }

        if ((kv_get_le16(tx_header + 0) != FLASH_KV32_TX_MAGIC) ||
            (tx_header[2] != FLASH_KV32_TX_VERSION)) {
            kv->dbg.write_off = off;
            kv->dbg.tail_dirty = 1u;
            return FLASH_KV32_SUCCESS;
        }

        item_count = tx_header[3];
        payload_len = kv_get_le16(tx_header + 8);
        total_len = kv_get_le16(tx_header + 10);
        stored_crc = kv_get_le32(tx_header + 12);
        seq = kv_get_le32(tx_header + 4);

        if ((item_count == 0u) ||
            (payload_len != (u16)(item_count * FLASH_KV32_TX_ITEM_BYTES)) ||
            (total_len != kv_calc_tx_total_len(kv, item_count)) ||
            ((u32)off + total_len > kv->cfg.sector_size)) {
            kv->dbg.write_off = off;
            kv->dbg.tail_dirty = 1u;
            return FLASH_KV32_SUCCESS;
        }

        kv_port_read(kv,
                     base + off + total_len - FLASH_KV32_TX_COMMIT_BYTES,
                     commit_buf,
                     sizeof(commit_buf));
        if (kv_get_le32(commit_buf) != FLASH_KV32_TX_COMMIT_MAGIC) {
            kv->dbg.write_off = off;
            kv->dbg.tail_dirty = 1u;
            return FLASH_KV32_SUCCESS;
        }

        calc_crc = kv_crc32_begin();
        calc_crc = kv_crc32_step(calc_crc, tx_header, 12u);
        for (i = 0; i < item_count; ++i) {
            kv_port_read(kv,
                         base + off + FLASH_KV32_TX_HEADER_BYTES + ((u32)i * FLASH_KV32_TX_ITEM_BYTES),
                         item_buf,
                         sizeof(item_buf));
            calc_crc = kv_crc32_step(calc_crc, item_buf, sizeof(item_buf));
        }

        crc = kv_crc32_finish(calc_crc);
        if (crc != stored_crc) {
            kv->dbg.write_off = off;
            kv->dbg.tail_dirty = 1u;
            return FLASH_KV32_SUCCESS;
        }

        for (i = 0; i < item_count; ++i) {
            kv_port_read(kv,
                         base + off + FLASH_KV32_TX_HEADER_BYTES + ((u32)i * FLASH_KV32_TX_ITEM_BYTES),
                         item_buf,
                         sizeof(item_buf));
            if (kv_find_key_index(kv, kv_get_le32(item_buf + 0), &key_index)) {
                kv->cache[key_index].value = kv_get_le32(item_buf + 4);
            }
        }

        *last_seq_out = seq;
        off = (u16)(off + total_len);
    }

    kv->dbg.write_off = kv->cfg.sector_size;
    kv->dbg.tail_dirty = 1u;
    return FLASH_KV32_SUCCESS;
}

static int kv_select_active_sector(flash_kv32_t *kv, u16 *active_idx, u32 *active_generation)
{
    flash_kv32_sector_info_t info;
    u16 i;
    u8 found = 0u;

    *active_idx = 0u;
    *active_generation = 0u;

    for (i = 0; i < kv->cfg.sector_count; ++i) {
        if (!kv_read_sector_info(kv, i, &info)) {
            continue;
        }

        if (!info.header_valid || !info.active) {
            continue;
        }

        if (!found || (info.generation >= *active_generation)) {
            *active_idx = i;
            *active_generation = info.generation;
            found = 1u;
        }
    }

    return found ? FLASH_KV32_SUCCESS : FLASH_KV32_FAILED;
}

static int kv_compact_internal(flash_kv32_t *kv, const flash_kv32_pair_t *pairs, u16 pair_count)
{
    u16 target_idx;
    u16 old_idx;
    u16 new_off = FLASH_KV32_SECTOR_DATA_OFF;
    u32 next_generation;
    u32 seq;

    if (kv->dbg.loaded) {
        target_idx = (u16)((kv->dbg.active_sector + 1u) % kv->cfg.sector_count);
        next_generation = kv->dbg.active_generation + 1u;
    } else {
        target_idx = 0u;
        next_generation = 1u;
    }

    if (!kv_port_erase(kv, kv->cfg.sector_addrs[target_idx])) {
        return FLASH_KV32_FAILED;
    }

    if (!kv_prepare_sector(kv, target_idx, next_generation)) {
        return FLASH_KV32_FAILED;
    }

    seq = kv->dbg.next_seq;
    if (seq == 0u) {
        seq = 1u;
    }

    if (!kv_write_tx(kv,
                     target_idx,
                     FLASH_KV32_SECTOR_DATA_OFF,
                     pairs,
                     pair_count,
                     1,
                     seq,
                     &new_off)) {
        return FLASH_KV32_FAILED;
    }

    if (!kv_mark_sector_active(kv, target_idx)) {
        return FLASH_KV32_FAILED;
    }

    if (pairs != NULL) {
        kv_apply_pairs(kv, pairs, pair_count);
    }

    old_idx = kv->dbg.active_sector;
    kv->dbg.active_sector = target_idx;
    kv->dbg.active_base = kv->cfg.sector_addrs[target_idx];
    kv->dbg.active_generation = next_generation;
    kv->dbg.write_off = new_off;
    kv->dbg.next_seq = seq + 1u;
    kv->dbg.loaded = 1u;
    kv->dbg.tail_dirty = 0u;

    if ((kv->dbg.loaded != 0u) && (old_idx != target_idx) && (kv->cfg.sector_addrs[old_idx] != kv->dbg.active_base)) {
        (void)kv_port_erase(kv, kv->cfg.sector_addrs[old_idx]);
    }

    return FLASH_KV32_SUCCESS;
}

int flash_kv32_init(flash_kv32_t *kv, const flash_kv32_cfg_t *cfg, flash_kv32_cache_entry_t *cache)
{
    u16 active_idx;
    u32 active_generation;
    u32 last_seq;
    u16 min_tx_len;

    if ((kv == NULL) || (cfg == NULL) || (cache == NULL)) {
        return FLASH_KV32_FAILED;
    }

    memset(kv, 0, sizeof(*kv));
    kv->cfg = *cfg;
    kv->cache = cache;

    if (!kv_validate_cfg(kv)) {
        return FLASH_KV32_FAILED;
    }

    kv_reset_cache_to_default(kv);
    kv->dbg.next_seq = 1u;

    if (!kv_select_active_sector(kv, &active_idx, &active_generation)) {
        return flash_kv32_format(kv);
    }

    kv->dbg.active_sector = active_idx;
    kv->dbg.active_base = kv->cfg.sector_addrs[active_idx];
    kv->dbg.active_generation = active_generation;
    kv->dbg.loaded = 1u;

    if (!kv_scan_active_sector(kv, active_idx, &last_seq)) {
        return FLASH_KV32_FAILED;
    }

    kv->dbg.next_seq = last_seq + 1u;
    if (kv->dbg.next_seq == 0u) {
        kv->dbg.next_seq = 1u;
    }

    min_tx_len = kv_calc_tx_total_len(kv, 1u);
    if (kv->dbg.tail_dirty || ((u32)kv->dbg.write_off + min_tx_len > kv->cfg.sector_size)) {
        if (!kv_compact_internal(kv, NULL, 0u)) {
            return FLASH_KV32_FAILED;
        }
    }

    return FLASH_KV32_SUCCESS;
}

int flash_kv32_format(flash_kv32_t *kv)
{
    u16 i;

    if ((kv == NULL) || !kv_validate_cfg(kv)) {
        return FLASH_KV32_FAILED;
    }

    for (i = 0; i < kv->cfg.sector_count; ++i) {
        if (!kv_port_erase(kv, kv->cfg.sector_addrs[i])) {
            return FLASH_KV32_FAILED;
        }
    }

    kv_reset_cache_to_default(kv);
    memset(&kv->dbg, 0, sizeof(kv->dbg));
    kv->dbg.next_seq = 1u;

    return kv_compact_internal(kv, NULL, 0u);
}

int flash_kv32_get(const flash_kv32_t *kv, u32 key, u32 *value)
{
    u16 key_index;

    if ((kv == NULL) || (value == NULL)) {
        return FLASH_KV32_FAILED;
    }

    if (!kv_find_key_index(kv, key, &key_index)) {
        return FLASH_KV32_FAILED;
    }

    *value = kv->cache[key_index].value;
    return FLASH_KV32_SUCCESS;
}

int flash_kv32_set(flash_kv32_t *kv, u32 key, u32 value)
{
    flash_kv32_pair_t pair;

    pair.key = key;
    pair.value = value;
    return flash_kv32_write_pairs(kv, &pair, 1u);
}

int flash_kv32_write_pairs(flash_kv32_t *kv, const flash_kv32_pair_t *pairs, u16 pair_count)
{
    u16 i;
    u16 changed_count = 0u;
    u16 key_index;
    u16 new_off;

    if ((kv == NULL) || (pairs == NULL) || (pair_count == 0u) || (pair_count > 255u)) {
        return FLASH_KV32_FAILED;
    }

    if (kv_pair_has_duplicates(kv, pairs, pair_count)) {
        return FLASH_KV32_FAILED;
    }

    for (i = 0; i < pair_count; ++i) {
        if (!kv_find_key_index(kv, pairs[i].key, &key_index)) {
            return FLASH_KV32_FAILED;
        }
        if (kv->cache[key_index].value != pairs[i].value) {
            ++changed_count;
        }
    }

    if (changed_count == 0u) {
        return FLASH_KV32_SUCCESS;
    }

    if (kv->dbg.tail_dirty ||
        ((u32)kv->dbg.write_off + kv_calc_tx_total_len(kv, pair_count) > kv->cfg.sector_size)) {
        return kv_compact_internal(kv, pairs, pair_count);
    }

    if (!kv_write_tx(kv,
                     kv->dbg.active_sector,
                     kv->dbg.write_off,
                     pairs,
                     pair_count,
                     0,
                     kv->dbg.next_seq,
                     &new_off)) {
        kv->dbg.tail_dirty = 1u;
        return FLASH_KV32_FAILED;
    }

    kv_apply_pairs(kv, pairs, pair_count);
    kv->dbg.write_off = new_off;
    kv->dbg.next_seq += 1u;
    if (kv->dbg.next_seq == 0u) {
        kv->dbg.next_seq = 1u;
    }
    kv->dbg.loaded = 1u;
    kv->dbg.tail_dirty = 0u;

    return FLASH_KV32_SUCCESS;
}

int flash_kv32_compact(flash_kv32_t *kv)
{
    if (kv == NULL) {
        return FLASH_KV32_FAILED;
    }

    return kv_compact_internal(kv, NULL, 0u);
}

flash_kv32_dbg_t flash_kv32_get_dbg(const flash_kv32_t *kv)
{
    flash_kv32_dbg_t dbg;

    memset(&dbg, 0, sizeof(dbg));
    if (kv != NULL) {
        dbg = kv->dbg;
    }
    return dbg;
}
