#include "flash_blob_store.h"

typedef struct
{
    u32 magic;
    u16 version;
    u16 payload_size;
    u32 seq;
    u16 crc;
    u16 reserved;
} flash_blob_hdr_t;

static u16 flash_blob_crc16(const u8 *data, u32 len)
{
    u16 crc = 0xFFFF;

    for (u32 i = 0; i < len; ++i)
    {
        crc ^= data[i];
        for (u8 j = 0; j < 8; ++j)
        {
            if (crc & 1) {
                crc = (u16)((crc >> 1) ^ 0xA001);
            } else {
                crc >>= 1;
            }
        }
    }

    return crc;
}

static int flash_blob_is_erased(const flash_blob_hdr_t *hdr)
{
    const u8 *p = (const u8 *)hdr;
    for (u32 i = 0; i < sizeof(*hdr); ++i)
    {
        if (p[i] != 0xFF) {
            return 0;
        }
    }
    return 1;
}

static int flash_blob_read_slot(const flash_blob_store_cfg_t *cfg, u32 addr, void *payload, flash_blob_hdr_t *hdr_out)
{
    flash_blob_hdr_t hdr;

    flash_read_page(addr, sizeof(hdr), (u8 *)&hdr);

    if (flash_blob_is_erased(&hdr)) {
        return 0;
    }

    if (hdr.magic != cfg->magic) {
        return 0;
    }
    if (hdr.version != cfg->version) {
        return 0;
    }
    if (hdr.payload_size != cfg->payload_size) {
        return 0;
    }
    if ((sizeof(hdr) + hdr.payload_size) > cfg->sector_size) {
        return 0;
    }

    flash_read_page(addr + sizeof(hdr), hdr.payload_size, (u8 *)payload);

    {
        u16 crc = flash_blob_crc16((const u8 *)payload, hdr.payload_size);
        if (crc != hdr.crc) {
            return 0;
        }
    }

    if (hdr_out) {
        *hdr_out = hdr;
    }

    return 1;
}

int flash_blob_store_load(const flash_blob_store_cfg_t *cfg, void *payload, flash_blob_store_state_t *state)
{
    u8 buf_a[256];
    u8 buf_b[256];
    flash_blob_hdr_t hdr_a;
    flash_blob_hdr_t hdr_b;
    int valid_a;
    int valid_b;

    if (!cfg || !payload || !state) {
        return 0;
    }
    if (cfg->payload_size > sizeof(buf_a)) {
        return 0;
    }

    valid_a = flash_blob_read_slot(cfg, cfg->sector_a, buf_a, &hdr_a);
    valid_b = flash_blob_read_slot(cfg, cfg->sector_b, buf_b, &hdr_b);

    state->active_addr = 0;
    state->seq = 0;
    state->valid = 0;

    if (!valid_a && !valid_b) {
        return 0;
    }

    if (valid_a && (!valid_b || hdr_a.seq >= hdr_b.seq))
    {
        memcpy(payload, buf_a, cfg->payload_size);
        state->active_addr = cfg->sector_a;
        state->seq = hdr_a.seq;
        state->valid = 1;
        return 1;
    }

    memcpy(payload, buf_b, cfg->payload_size);
    state->active_addr = cfg->sector_b;
    state->seq = hdr_b.seq;
    state->valid = 1;
    return 1;
}

int flash_blob_store_save(const flash_blob_store_cfg_t *cfg, const void *payload, flash_blob_store_state_t *state)
{
    flash_blob_hdr_t hdr;
    u32 target_addr;
    u32 next_seq;

    if (!cfg || !payload || !state) {
        return 0;
    }

    target_addr = (state->active_addr == cfg->sector_a) ? cfg->sector_b : cfg->sector_a;
    if (state->active_addr != cfg->sector_a && state->active_addr != cfg->sector_b) {
        target_addr = cfg->sector_a;
    }

    next_seq = state->valid ? (state->seq + 1) : 1;

    hdr.magic = cfg->magic;
    hdr.version = cfg->version;
    hdr.payload_size = cfg->payload_size;
    hdr.seq = next_seq;
    hdr.crc = flash_blob_crc16((const u8 *)payload, cfg->payload_size);
    hdr.reserved = 0xFFFF;

    flash_erase_sector(target_addr);
    flash_write_page(target_addr, sizeof(hdr), (u8 *)&hdr);
    flash_write_page(target_addr + sizeof(hdr), cfg->payload_size, (u8 *)payload);

    state->active_addr = target_addr;
    state->seq = next_seq;
    state->valid = 1;

    return 1;
}

int flash_blob_store_reset(const flash_blob_store_cfg_t *cfg)
{
    if (!cfg) {
        return 0;
    }

    flash_erase_sector(cfg->sector_a);
    flash_erase_sector(cfg->sector_b);
    return 1;
}
