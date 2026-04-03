#include "bms_event_log.h"

#include "drivers.h"
#include <string.h>

#define BMS_EVENT_LOG_MAGIC           0x424C4F47u
#define BMS_EVENT_LOG_VERSION         0x0001u
#define BMS_EVENT_LOG_COMMIT          0x434D4954u
#define BMS_EVENT_LOG_INVALID_SLOT    0xFFFFu

#define BMS_EVENT_LOG_MAGIC_OFF       0u
#define BMS_EVENT_LOG_VERSION_OFF     4u
#define BMS_EVENT_LOG_LENGTH_OFF      6u
#define BMS_EVENT_LOG_SEQ_OFF         8u
#define BMS_EVENT_LOG_WRITE_POS_OFF   12u
#define BMS_EVENT_LOG_RSVD_OFF        14u
#define BMS_EVENT_LOG_RECORDS_OFF     16u
#define BMS_EVENT_LOG_CRC_OFF         216u
#define BMS_EVENT_LOG_COMMIT_OFF      220u

#define BMS_EVENT_LOG_PAYLOAD_BYTES   208u
#define BMS_EVENT_LOG_SNAPSHOT_BYTES  224u

typedef struct {
    u8 records[BMS_EVENT_LOG_ENTRY_COUNT][2];
    u16 write_pos;
    u16 current_slot;
    u32 next_seq;
    u32 interval_s;
    u8 ready;
    u8 event_latched[EVENT_NUM];
    u8 cbc_last;
} bms_event_log_ctx_t;

static bms_event_log_ctx_t g_bms_event_log;

static void bms_event_log_put_u16le(u8 *buf, u16 value)
{
    buf[0] = (u8)(value & 0xFFu);
    buf[1] = (u8)(value >> 8);
}

static void bms_event_log_put_u32le(u8 *buf, u32 value)
{
    buf[0] = (u8)(value & 0xFFu);
    buf[1] = (u8)((value >> 8) & 0xFFu);
    buf[2] = (u8)((value >> 16) & 0xFFu);
    buf[3] = (u8)((value >> 24) & 0xFFu);
}

static u16 bms_event_log_get_u16le(const u8 *buf)
{
    return (u16)(buf[0] | ((u16)buf[1] << 8));
}

static u32 bms_event_log_get_u32le(const u8 *buf)
{
    return ((u32)buf[0]) |
           ((u32)buf[1] << 8) |
           ((u32)buf[2] << 16) |
           ((u32)buf[3] << 24);
}

static u32 bms_event_log_crc32(const u8 *data, u32 len)
{
    u32 crc = 0xFFFFFFFFu;
    u32 i;
    u8 bit;

    for (i = 0; i < len; ++i) {
        crc ^= data[i];
        for (bit = 0; bit < 8u; ++bit) {
            if (crc & 1u) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc >>= 1;
            }
        }
    }

    return ~crc;
}

static void bms_event_log_flash_read(u32 addr, u8 *buf, u32 len)
{
    flash_read_page(addr, (int)len, buf);
}

static int bms_event_log_flash_prog(u32 addr, const u8 *buf, u32 len)
{
    u32 page_off;
    u32 chunk;

    while (len != 0u) {
        page_off = addr % FLASH_PAGE_SIZE;
        chunk = FLASH_PAGE_SIZE - page_off;
        if (chunk > len) {
            chunk = len;
        }

        flash_write_page(addr, (int)chunk, (u8 *)buf);
        addr += chunk;
        buf += chunk;
        len -= chunk;
    }

    return 1;
}

static int bms_event_log_flash_erase_sector(u16 sector_idx)
{
    u32 addr = BMS_EVENT_LOG_FLASH_BASE + ((u32)sector_idx * BMS_EVENT_LOG_SECTOR_SIZE);

    flash_erase_sector(addr);
    return 1;
}

static u16 bms_event_log_slots_per_sector(void)
{
    return (u16)(BMS_EVENT_LOG_SECTOR_SIZE / BMS_EVENT_LOG_SNAPSHOT_BYTES);
}

static u16 bms_event_log_total_slots(void)
{
    return (u16)(bms_event_log_slots_per_sector() * BMS_EVENT_LOG_FLASH_SECTORS);
}

static u16 bms_event_log_sector_index(u16 slot_idx)
{
    return (u16)(slot_idx / bms_event_log_slots_per_sector());
}

static u16 bms_event_log_sector_first_slot(u16 sector_idx)
{
    return (u16)(sector_idx * bms_event_log_slots_per_sector());
}

static u32 bms_event_log_slot_addr(u16 slot_idx)
{
    u16 slots_per_sector = bms_event_log_slots_per_sector();
    u16 sector_idx = (u16)(slot_idx / slots_per_sector);
    u16 slot_off = (u16)(slot_idx % slots_per_sector);

    return BMS_EVENT_LOG_FLASH_BASE +
           ((u32)sector_idx * BMS_EVENT_LOG_SECTOR_SIZE) +
           ((u32)slot_off * BMS_EVENT_LOG_SNAPSHOT_BYTES);
}

static int bms_event_log_slot_is_erased(u16 slot_idx)
{
    u8 hdr[8];
    u8 i;

    bms_event_log_flash_read(bms_event_log_slot_addr(slot_idx), hdr, sizeof(hdr));
    for (i = 0; i < sizeof(hdr); ++i) {
        if (hdr[i] != 0xFFu) {
            return 0;
        }
    }

    return 1;
}

static void bms_event_log_clear_runtime_flags(void)
{
    memset(g_bms_event_log.event_latched, 0, sizeof(g_bms_event_log.event_latched));
    g_bms_event_log.cbc_last = 0u;
    g_bms_event_log.interval_s = 0u;
}

static void bms_event_log_reset_ram_only(void)
{
    memset(g_bms_event_log.records, 0, sizeof(g_bms_event_log.records));
    g_bms_event_log.write_pos = 0u;
    bms_event_log_clear_runtime_flags();
}

static void bms_event_log_build_snapshot(u8 *buf, u32 seq)
{
    u32 crc;

    memset(buf, 0xFF, BMS_EVENT_LOG_SNAPSHOT_BYTES);
    bms_event_log_put_u32le(&buf[BMS_EVENT_LOG_MAGIC_OFF], BMS_EVENT_LOG_MAGIC);
    bms_event_log_put_u16le(&buf[BMS_EVENT_LOG_VERSION_OFF], BMS_EVENT_LOG_VERSION);
    bms_event_log_put_u16le(&buf[BMS_EVENT_LOG_LENGTH_OFF], BMS_EVENT_LOG_PAYLOAD_BYTES);
    bms_event_log_put_u32le(&buf[BMS_EVENT_LOG_SEQ_OFF], seq);
    bms_event_log_put_u16le(&buf[BMS_EVENT_LOG_WRITE_POS_OFF], g_bms_event_log.write_pos);
    bms_event_log_put_u16le(&buf[BMS_EVENT_LOG_RSVD_OFF], 0u);
    memcpy(&buf[BMS_EVENT_LOG_RECORDS_OFF], g_bms_event_log.records, sizeof(g_bms_event_log.records));

    crc = bms_event_log_crc32(&buf[BMS_EVENT_LOG_SEQ_OFF], BMS_EVENT_LOG_PAYLOAD_BYTES);
    bms_event_log_put_u32le(&buf[BMS_EVENT_LOG_CRC_OFF], crc);
}

static int bms_event_log_parse_snapshot(u16 slot_idx, u32 *seq_out, u16 *write_pos_out, u8 records_out[BMS_EVENT_LOG_ENTRY_COUNT][2])
{
    u8 buf[BMS_EVENT_LOG_SNAPSHOT_BYTES];
    u32 crc_stored;
    u32 crc_calc;
    u16 write_pos;

    bms_event_log_flash_read(bms_event_log_slot_addr(slot_idx), buf, sizeof(buf));

    if (bms_event_log_get_u32le(&buf[BMS_EVENT_LOG_MAGIC_OFF]) != BMS_EVENT_LOG_MAGIC) {
        return 0;
    }
    if (bms_event_log_get_u16le(&buf[BMS_EVENT_LOG_VERSION_OFF]) != BMS_EVENT_LOG_VERSION) {
        return 0;
    }
    if (bms_event_log_get_u16le(&buf[BMS_EVENT_LOG_LENGTH_OFF]) != BMS_EVENT_LOG_PAYLOAD_BYTES) {
        return 0;
    }
    if (bms_event_log_get_u32le(&buf[BMS_EVENT_LOG_COMMIT_OFF]) != BMS_EVENT_LOG_COMMIT) {
        return 0;
    }

    write_pos = bms_event_log_get_u16le(&buf[BMS_EVENT_LOG_WRITE_POS_OFF]);
    if (write_pos >= BMS_EVENT_LOG_ENTRY_COUNT) {
        return 0;
    }

    crc_stored = bms_event_log_get_u32le(&buf[BMS_EVENT_LOG_CRC_OFF]);
    crc_calc = bms_event_log_crc32(&buf[BMS_EVENT_LOG_SEQ_OFF], BMS_EVENT_LOG_PAYLOAD_BYTES);
    if (crc_stored != crc_calc) {
        return 0;
    }

    *seq_out = bms_event_log_get_u32le(&buf[BMS_EVENT_LOG_SEQ_OFF]);
    *write_pos_out = write_pos;
    memcpy(records_out, &buf[BMS_EVENT_LOG_RECORDS_OFF], sizeof(g_bms_event_log.records));
    return 1;
}

static u16 bms_event_log_prepare_next_slot(void)
{
    u16 current_slot = g_bms_event_log.current_slot;
    u16 total_slots = bms_event_log_total_slots();
    u16 next_slot;
    u16 next_sector;
    u16 current_sector;

    if (current_slot == BMS_EVENT_LOG_INVALID_SLOT) {
        (void)bms_event_log_flash_erase_sector(0u);
        return 0u;
    }

    next_slot = (u16)(current_slot + 1u);
    if (next_slot >= total_slots) {
        next_slot = 0u;
    }

    current_sector = bms_event_log_sector_index(current_slot);
    next_sector = bms_event_log_sector_index(next_slot);
    if (next_sector != current_sector) {
        (void)bms_event_log_flash_erase_sector(next_sector);
        return bms_event_log_sector_first_slot(next_sector);
    }

    if (bms_event_log_slot_is_erased(next_slot)) {
        return next_slot;
    }

    next_sector = (u16)(next_sector + 1u);
    if (next_sector >= BMS_EVENT_LOG_FLASH_SECTORS) {
        next_sector = 0u;
    }

    (void)bms_event_log_flash_erase_sector(next_sector);
    return bms_event_log_sector_first_slot(next_sector);
}

static int bms_event_log_write_snapshot(void)
{
    u8 buf[BMS_EVENT_LOG_SNAPSHOT_BYTES];
    u8 commit[4];
    u16 slot_idx;

    if (!g_bms_event_log.ready) {
        return 0;
    }

    slot_idx = bms_event_log_prepare_next_slot();
    bms_event_log_build_snapshot(buf, g_bms_event_log.next_seq);
    bms_event_log_put_u32le(commit, BMS_EVENT_LOG_COMMIT);

    if (!bms_event_log_flash_prog(bms_event_log_slot_addr(slot_idx), buf, BMS_EVENT_LOG_COMMIT_OFF)) {
        return 0;
    }
    if (!bms_event_log_flash_prog(bms_event_log_slot_addr(slot_idx) + BMS_EVENT_LOG_COMMIT_OFF,
                                  commit,
                                  sizeof(commit))) {
        return 0;
    }

    g_bms_event_log.current_slot = slot_idx;
    g_bms_event_log.next_seq += 1u;
    return 1;
}

static u8 bms_event_log_map_interval(u32 *seconds)
{
    u8 code;
    u32 value = *seconds;

    if (value <= 60u) {
        code = 171u;
    } else if (value <= (3600u * 168u)) {
        code = (u8)((value + 3599u) / 3600u);
    } else {
        code = 170u;
    }

    *seconds = 0u;
    return code;
}

static int bms_event_log_append(bms_event_log_id_t event, int startup_event)
{
    u16 pos;

    if (!g_bms_event_log.ready) {
        return 0;
    }

    pos = g_bms_event_log.write_pos;
    if (pos >= BMS_EVENT_LOG_ENTRY_COUNT) {
        pos = 0u;
    }

    g_bms_event_log.records[pos][0] = (u8)event;
    g_bms_event_log.records[pos][1] = startup_event ? 0u : bms_event_log_map_interval(&g_bms_event_log.interval_s);

    pos += 1u;
    if (pos >= BMS_EVENT_LOG_ENTRY_COUNT) {
        pos = 0u;
    }
    g_bms_event_log.write_pos = pos;
    return bms_event_log_write_snapshot();
}

static void bms_event_log_track_edge(u8 active, bms_event_log_id_t event)
{
    if ((u32)event >= EVENT_NUM) {
        return;
    }

    if (active) {
        if (!g_bms_event_log.event_latched[event]) {
            g_bms_event_log.event_latched[event] = 1u;
            (void)bms_event_log_append(event, 0);
        }
    } else {
        g_bms_event_log.event_latched[event] = 0u;
    }
}

static void bms_event_log_track_change(u8 value, bms_event_log_id_t event)
{
    if (g_bms_event_log.cbc_last != value) {
        g_bms_event_log.cbc_last = value;
        (void)bms_event_log_append(event, 0);
    }
}

int bms_event_log_init(void)
{
    u16 slot_idx;
    u16 total_slots = bms_event_log_total_slots();
    u16 slots_per_sector = bms_event_log_slots_per_sector();
    u16 best_slot = BMS_EVENT_LOG_INVALID_SLOT;
    u16 write_pos = 0u;
    u16 best_write_pos = 0u;
    u32 seq = 0u;
    u32 best_seq = 0u;
    u8 records[BMS_EVENT_LOG_ENTRY_COUNT][2];
    u8 best_records[BMS_EVENT_LOG_ENTRY_COUNT][2];

    if ((BMS_EVENT_LOG_FLASH_BASE == 0u) ||
        (BMS_EVENT_LOG_SECTOR_SIZE == 0u) ||
        (BMS_EVENT_LOG_FLASH_SECTORS < 2u) ||
        (slots_per_sector == 0u) ||
        (total_slots == 0u)) {
        memset(&g_bms_event_log, 0, sizeof(g_bms_event_log));
        g_bms_event_log.current_slot = BMS_EVENT_LOG_INVALID_SLOT;
        return 0;
    }

    memset(&g_bms_event_log, 0, sizeof(g_bms_event_log));
    g_bms_event_log.current_slot = BMS_EVENT_LOG_INVALID_SLOT;

    for (slot_idx = 0u; slot_idx < total_slots; ++slot_idx) {
        if (!bms_event_log_parse_snapshot(slot_idx, &seq, &write_pos, records)) {
            continue;
        }

        if ((best_slot == BMS_EVENT_LOG_INVALID_SLOT) || (seq > best_seq)) {
            best_slot = slot_idx;
            best_seq = seq;
            best_write_pos = write_pos;
            memcpy(best_records, records, sizeof(best_records));
        }
    }

    if (best_slot != BMS_EVENT_LOG_INVALID_SLOT) {
        memcpy(g_bms_event_log.records, best_records, sizeof(g_bms_event_log.records));
        g_bms_event_log.write_pos = best_write_pos;
        g_bms_event_log.current_slot = best_slot;
        g_bms_event_log.next_seq = best_seq + 1u;
    } else {
        bms_event_log_reset_ram_only();
        g_bms_event_log.next_seq = 1u;
    }

    g_bms_event_log.ready = 1u;
    bms_event_log_clear_runtime_flags();
    return 1;
}

void bms_event_log_note_startup(void)
{
    if (!g_bms_event_log.ready && !bms_event_log_init()) {
        return;
    }

    (void)bms_event_log_append(BMS_START_UP, 1);
}

void bms_event_log_poll_1s(const bms_event_log_sample_t *sample)
{
    if (sample == NULL) {
        return;
    }

    if (!g_bms_event_log.ready && !bms_event_log_init()) {
        return;
    }

    g_bms_event_log.interval_s += 1u;

    bms_event_log_track_edge(sample->sleep, BMS_SLEEP);
    bms_event_log_track_edge(sample->balance, BALANCE_OPEN);
    bms_event_log_track_edge(sample->heat, HEAT_OPEN);
    bms_event_log_track_edge(sample->cool, COOL_OPEN);
    bms_event_log_track_edge(sample->vcell_ovp, VCELL_OVP);
    bms_event_log_track_edge(sample->vbus_ovp, VBUS_OVP);
    bms_event_log_track_edge(sample->chg_ocp, CHG_OCP);
    bms_event_log_track_edge(sample->vcell_uvp, VCELL_UVP);
    bms_event_log_track_edge(sample->vbus_uvp, VBUS_UVP);
    bms_event_log_track_edge(sample->dsg_ocp, DSG_OCP);
    bms_event_log_track_edge(sample->chg_utp, CHG_UTP);
    bms_event_log_track_edge(sample->dsg_utp, DSG_UTP);
    bms_event_log_track_edge(sample->chg_otp, CHG_OTP);
    bms_event_log_track_edge(sample->dsg_otp, DSG_OTP);
    bms_event_log_track_edge(sample->vdelta_op, VDELTA_OP);
    bms_event_log_track_edge(sample->afe2_err, AFE2_ERR);
    bms_event_log_track_edge(sample->eeprom_err, EEPROM_ERR);
    bms_event_log_track_change(sample->cbc_err, CBC_ERR);
}

u16 bms_event_log_read_reg(u16 reg)
{
    u16 idx;

    if (reg >= BMS_EVENT_LOG_REG_COUNT) {
        return 0u;
    }

    if (!g_bms_event_log.ready && !bms_event_log_init()) {
        return 0u;
    }

    idx = (u16)(g_bms_event_log.write_pos + BMS_EVENT_LOG_ENTRY_COUNT - 1u - reg);
    while (idx >= BMS_EVENT_LOG_ENTRY_COUNT) {
        idx = (u16)(idx - BMS_EVENT_LOG_ENTRY_COUNT);
    }

    return (u16)(((u16)g_bms_event_log.records[idx][0] << 8) | g_bms_event_log.records[idx][1]);
}

void bms_event_log_fill_protocol_bytes(u8 *buf, u16 len)
{
    u16 i;
    u16 words;
    u16 reg;
    u16 value;

    if (buf == NULL) {
        return;
    }

    words = (u16)(len / 2u);
    if (words > BMS_EVENT_LOG_REG_COUNT) {
        words = BMS_EVENT_LOG_REG_COUNT;
    }

    for (i = 0u; i < words; ++i) {
        reg = i;
        value = bms_event_log_read_reg(reg);
        buf[(u16)(i * 2u)] = (u8)(value >> 8);
        buf[(u16)(i * 2u + 1u)] = (u8)(value & 0xFFu);
    }

    if ((u16)(words * 2u) < len) {
        memset(&buf[words * 2u], 0, len - (u16)(words * 2u));
    }
}

int bms_event_log_factory_reset(void)
{
    if (!g_bms_event_log.ready && !bms_event_log_init()) {
        return 0;
    }

    bms_event_log_reset_ram_only();
    return bms_event_log_write_snapshot();
}

bms_event_log_dbg_t bms_event_log_get_dbg(void)
{
    bms_event_log_dbg_t dbg;

    dbg.last_seq = (g_bms_event_log.next_seq == 0u) ? 0u : (g_bms_event_log.next_seq - 1u);
    dbg.current_slot = g_bms_event_log.current_slot;
    dbg.slots_per_sector = bms_event_log_slots_per_sector();
    dbg.total_slots = bms_event_log_total_slots();
    dbg.write_pos = g_bms_event_log.write_pos;
    dbg.ready = g_bms_event_log.ready;
    return dbg;
}
