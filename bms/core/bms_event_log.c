/* D008 Event 使用两个扇区追加已提交的 16 字节记录。 */
#include "bms_event_log.h"
#include "bms_crc.h"
#include "bms_error.h"
#include "bms_state.h"
#include "bms_storage_platform.h"
#include "bms_time.h"
#include "bms_update_policy.h"
#include <string.h>

#define EVENT_FORMAT 0xD801u
#define EVENT_COMMIT 0x45564F4Bu
#define EVENT_RECORD_BYTES 16u
#define EVENT_BODY_BYTES 12u
#define EVENT_SCAN_BYTES 256u
#define EVENT_SECTOR_COUNT 2u
#define EVENT_MAX_WRITE_FAILURES 3u
#define EVENT_MAX_HOURS 168u
#define EVENT_INTERVAL_LIMIT_MS ((EVENT_MAX_HOURS * 3600u + 1u) * 1000u)

typedef struct {
    uint32_t last_seq;
    uint16_t last_word;
    uint16_t used;
    uint16_t valid;
} event_sector_t;

static const storage_port_t *s_port;
static storage_region_t s_region;
static event_sector_t s_sector[EVENT_SECTOR_COUNT];
static uint16_t s_ring[BMS_EVENT_LOG_ENTRY_COUNT];
static uint32_t s_clock_ms, s_interval_ms, s_write_ms;
static uint16_t s_latched;
static uint8_t s_next, s_count, s_pending, s_active;
static uint8_t s_ready, s_write_failed, s_stopped, s_merge, s_sleep_noted;

static void put32(uint8_t *p, uint32_t value)
{
    unsigned i;
    for (i = 0u; i < 4u; ++i) p[i] = (uint8_t)(value >> (8u * i));
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t ring_index(unsigned age)
{
    return (uint16_t)((s_next + BMS_EVENT_LOG_ENTRY_COUNT - 1u - age) % BMS_EVENT_LOG_ENTRY_COUNT);
}

static uint32_t slot_addr(unsigned sector, unsigned slot)
{
    return s_region.base + sector * s_port->erase_size + slot * EVENT_RECORD_BYTES;
}

static unsigned slots_per_sector(void)
{
    return s_port->erase_size / EVENT_RECORD_BYTES;
}

static int blank(const uint8_t *p, unsigned size)
{
    unsigned i;
    for (i = 0; i < size; ++i)
        if (p[i] != 0xFFu) return 0;
    return 1;
}

static int native_header(const uint8_t *p)
{
    return p[6] == (uint8_t)EVENT_FORMAT && p[7] == (uint8_t)(EVENT_FORMAT >> 8);
}

static uint16_t decoded_word(const uint8_t *p)
{
    uint8_t id = p[4], time = p[5];
    if (id == 0u || id >= EVENT_NUM ||
        (!time && id != BMS_START_UP) || (time > EVENT_MAX_HOURS && time != 170u && time != 171u) ||
        !native_header(p) || get32(p + EVENT_BODY_BYTES) != EVENT_COMMIT ||
        get32(p + 8u) != ~bms_crc32_update(UINT32_MAX, p, 8u)) return 0u;
    return (uint16_t)(((uint16_t)id << 8) | time);
}

static int scan_sectors(void)
{
    uint8_t page[EVENT_SCAN_BYTES];
    unsigned sector, offset, i;

    memset(s_sector, 0, sizeof(s_sector));
    s_active = 0u;
    for (sector = 0u; sector < EVENT_SECTOR_COUNT; ++sector) {
        event_sector_t *info = &s_sector[sector];
        for (offset = 0u; offset < s_port->erase_size; offset += EVENT_SCAN_BYTES) {
            if (!s_port->read(s_port->ctx, slot_addr(sector, 0) + offset, page, sizeof(page)))
                return 0;
            for (i = 0u; i < sizeof(page); i += EVENT_RECORD_BYTES) {
                const uint8_t *p = page + i;
                uint16_t word;
                if (blank(p, EVENT_RECORD_BYTES)) continue;
                info->used = (uint16_t)((offset + i) / EVENT_RECORD_BYTES + 1u);
                word = decoded_word(p);
                if (word) {
                    ++info->valid;
                    info->last_seq = get32(p);
                    info->last_word = word;
                }
            }
        }
        if (info->valid && (!s_sector[s_active].valid ||
            (int32_t)(info->last_seq - s_sector[s_active].last_seq) > 0))
            s_active = (uint8_t)sector;
    }
    return 1;
}

static int recover_recent(void)
{
    unsigned pass, sector, used, first_slot, offset;
    uint8_t page[EVENT_SCAN_BYTES];

    s_count = 0u;
    for (pass = 0u; pass < EVENT_SECTOR_COUNT; ++pass) {
        sector = s_active ^ pass;
        used = s_sector[sector].used;
        if (!s_sector[sector].valid) continue;
        while (used && s_count < BMS_EVENT_LOG_ENTRY_COUNT) {
            unsigned slots = EVENT_SCAN_BYTES / EVENT_RECORD_BYTES;
            first_slot = (used - 1u) / slots * slots;
            if (!s_port->read(s_port->ctx, slot_addr(sector, first_slot), page, sizeof(page)))
                return 0;
            for (offset = used - first_slot; offset && s_count < BMS_EVENT_LOG_ENTRY_COUNT; --offset) {
                uint16_t word = decoded_word(page + (offset - 1u) * EVENT_RECORD_BYTES);
                if (word) s_ring[ring_index(s_count++)] = word;
            }
            used = first_slot;
        }
    }
    return 1;
}

static int open_region(void)
{
    s_port = bms_storage_platform_port();
    return s_port && bms_storage_platform_region(BMS_STORAGE_DOMAIN_EVENT, &s_region) &&
        s_port->erase_size >= EVENT_SCAN_BYTES && !(s_port->erase_size % EVENT_SCAN_BYTES) &&
        s_port->program_size == 4u && s_region.size == s_port->erase_size * EVENT_SECTOR_COUNT &&
        slots_per_sector() >= BMS_EVENT_LOG_ENTRY_COUNT;
}

int bms_event_log_init(void)
{
    if (s_ready) return 1;
    if (!open_region() || !scan_sectors()) return 0;
    if (!s_sector[s_active].valid) {
        /* 无有效历史但两区读取成功：报告损坏，从已用区域的后续空槽继续。
         * 不在初始化时擦除，后续正常轮转只访问 Event 自身。 */
        if (s_sector[0].used || s_sector[1].used)
            bms_error_raise(BMS_ERROR_EEPROM_STORE);
        if (s_sector[1].used < s_sector[0].used ||
            (!s_sector[0].used && s_sector[1].used)) s_active = 1u;
    }
    s_next = 0u;
    s_pending = 0u;
    if (!recover_recent()) return 0;
    s_clock_ms = bms_time_ms();
    s_interval_ms = 0u;
    s_latched = 0u;
    s_write_failed = s_stopped = s_merge = s_sleep_noted = 0u;
    s_ready = 1u;
    return 1;
}

static void advance_time(void)
{
    uint32_t now = bms_time_ms();
    uint32_t step = now - s_clock_ms;
    uint32_t space = EVENT_INTERVAL_LIMIT_MS - s_interval_ms;
    s_interval_ms += step < space ? step : space;
    s_clock_ms = now;
}

static void append(bms_event_log_id_t id)
{
    uint32_t sec = s_interval_ms / 1000u;
    uint16_t word = (uint16_t)id << 8;
    uint8_t ordinary = id != BMS_START_UP && id != BMS_SLEEP;

    if (s_merge && ordinary && sec <= 60u && (s_ring[ring_index(0u)] >> 8) == (uint16_t)id)
        return;
    if (id != BMS_START_UP) {
        if (sec <= 60u) word |= 171u;
        else if (sec <= EVENT_MAX_HOURS * 3600u) word |= (uint16_t)((sec + 3599u) / 3600u);
        else word |= 170u;
    }
    if (s_pending < BMS_EVENT_LOG_ENTRY_COUNT) ++s_pending;
    else bms_error_raise(BMS_ERROR_EEPROM_STORE); /* 环已满时丢最早的未提交项。 */
    if (s_count < BMS_EVENT_LOG_ENTRY_COUNT) ++s_count;
    s_ring[s_next] = word;
    s_next = (uint8_t)((s_next + 1u) % BMS_EVENT_LOG_ENTRY_COUNT);
    s_interval_ms = 0u;
    s_merge = ordinary;
}

void bms_event_log_note_startup(void)
{
    if (s_ready) {
        advance_time();
        append(BMS_START_UP);
    }
}

void bms_event_log_observe(void)
{
    static const struct { uint16_t mask; bms_event_log_id_t id; } ids[] = {
        {0x0001u, VCELL_OVP}, {0x0004u, VBUS_OVP}, {0x0010u, CHG_OCP},
        {0x0002u, VCELL_UVP}, {0x0008u, VBUS_UVP}, {0x0020u, DSG_OCP},
        {0x0100u, CHG_UTP}, {0x0200u, DSG_UTP}, {0x0040u, CHG_OTP}, {0x0080u, DSG_OTP},
        {0x0400u, VDELTA_OP}, {0x4000u, AFE1_ERR}, {0x8000u, CBC_ERR}
    };
    uint16_t active = g_bms_report.fault_third.all & 0x07FFu;
    uint16_t rising;
    unsigned i;

    if (!s_ready) return;
    if (bms_error_get(BMS_ERROR_AFE1)) active |= 0x4000u;
    if (bms_error_get(BMS_ERROR_CBC_DSG)) active |= 0x8000u;
    rising = (uint16_t)(active & ~s_latched);
    s_latched = active;
    advance_time();
    for (i = 0u; i < sizeof(ids) / sizeof(ids[0]); ++i)
        if (rising & ids[i].mask) append(ids[i].id);
}

static void write_failed(uint32_t now)
{
    s_write_ms = now;
    if (s_write_failed < EVENT_MAX_WRITE_FAILURES) ++s_write_failed;
    if (s_write_failed >= EVENT_MAX_WRITE_FAILURES) s_stopped = 1u;
    bms_error_raise(BMS_ERROR_EEPROM_STORE);
}

int bms_event_log_service(uint8_t allow_erase)
{
    uint8_t record[EVENT_RECORD_BYTES], verify[EVENT_RECORD_BYTES];
    unsigned target = s_active;
    event_sector_t *info = &s_sector[target];
    uint32_t seq, addr, now;
    uint16_t word;
    uint8_t erase = 0u;
    int read_ok, committed = 0;

    if (!s_ready || s_stopped) return 0;
    if (!s_pending) return 1;
    now = bms_time_ms();
    if (s_write_failed && now - s_write_ms < BMS_STORAGE_RETRY_INTERVAL_MS) return 0;
    seq = s_sector[s_active].valid ? s_sector[s_active].last_seq + 1u : 1u;
    if (info->used >= slots_per_sector()) {
        target ^= 1u;
        info = &s_sector[target];
        if (info->valid && s_sector[s_active].valid < BMS_EVENT_LOG_ENTRY_COUNT) {
            /* 有效记录数量决定能否擦除旧区，写中断的槽位不计入。 */
            s_stopped = 1u;
            write_failed(now);
            return 0;
        }
        erase = info->valid || info->used >= slots_per_sector();
        if (erase && !allow_erase) return 0;
    }
    if (s_port->begin && !s_port->begin(s_port->ctx)) return 0;
    if (erase) {
        if (!s_port->erase(s_port->ctx, slot_addr(target, 0u), s_port->erase_size)) {
            if (s_port->end) s_port->end(s_port->ctx);
            write_failed(now);
            return 0;
        }
        memset(info, 0, sizeof(*info));
    }
    addr = slot_addr(target, info->used++);
    word = s_ring[ring_index(s_pending - 1u)];
    put32(record, seq);
    record[4] = (uint8_t)(word >> 8);
    record[5] = (uint8_t)word;
    record[6] = (uint8_t)EVENT_FORMAT;
    record[7] = (uint8_t)(EVENT_FORMAT >> 8);
    put32(record + 8u, ~bms_crc32_update(UINT32_MAX, record, 8u));
    put32(record + EVENT_BODY_BYTES, EVENT_COMMIT);

    read_ok = 1;
    if (s_port->program(s_port->ctx, addr, record, EVENT_BODY_BYTES) &&
        s_port->read(s_port->ctx, addr, verify, EVENT_BODY_BYTES) &&
        !memcmp(record, verify, EVENT_BODY_BYTES)) {
        (void)s_port->program(s_port->ctx, addr + EVENT_BODY_BYTES,
                             record + EVENT_BODY_BYTES, EVENT_RECORD_BYTES - EVENT_BODY_BYTES);
        read_ok = s_port->read(s_port->ctx, addr, verify, sizeof(verify));
        committed = read_ok && !memcmp(record, verify, sizeof(verify));
    }
    if (s_port->end) s_port->end(s_port->ctx);
    if (!read_ok) {
        /* 提交状态不明时只扫描一次，确认前不能重复使用同一个序号。 */
        if (!scan_sectors()) s_stopped = 1u;
        else committed = s_sector[s_active].valid && s_sector[s_active].last_seq == seq &&
            s_sector[s_active].last_word == word;
    } else if (committed) {
        ++info->valid;
        info->last_seq = seq;
        info->last_word = word;
        s_active = (uint8_t)target;
    }
    if (!committed) {
        write_failed(now);
        return 0;
    }
    --s_pending;
    s_write_failed = 0u;
    return 1;
}

int bms_event_log_note_sleep(void)
{
    if (!s_ready) return 0;
    if (!s_sleep_noted) {
        advance_time();
        append(BMS_SLEEP);
        s_sleep_noted = 1u;
    }
    return bms_event_log_service(0u);
}

u16 bms_event_log_read_reg(u16 reg)
{
    return s_ready && reg < s_count ? s_ring[ring_index(reg)] : 0u;
}

int bms_event_log_factory_reset(void)
{
    unsigned sector;

    s_ready = 0u;
    if (!bms_ota_factory_reset_requested() || !open_region() || !scan_sectors()) return 0;
    for (sector = 0u; sector < EVENT_SECTOR_COUNT; ++sector) {
        int ok;
        if (!s_sector[sector].used) continue;
        if (s_port->begin && !s_port->begin(s_port->ctx)) return 0;
        ok = s_port->erase(s_port->ctx, slot_addr(sector, 0u), s_port->erase_size);
        if (s_port->end) s_port->end(s_port->ctx);
        if (!ok) return 0;
    }
    return 1; /* 全域已空，Config 的进行中标记仍保留到最终提交。 */
}
