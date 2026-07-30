#include "bms_safety_log.h"
#include "tl_common.h"
#include "drivers.h"
#include "flash_store_cfg.h"
#include "flash_store_safe.h"
#include "bms_actuator.h"
#include "bms_diagnostics.h"
#include "bms_sw_protection.h"
#include "bms_supervisor.h"
#include "bms_afe.h"
#include <string.h>

#define BMS_SLOG_MAGIC       0x53414645u
#define BMS_SLOG_VERSION     0x0101u
#define BMS_SLOG_COMMIT      0x434D4954u
#define BMS_SLOG_SLOT_BYTES  80u
#define BMS_SLOG_QUEUE_LEN   4u

typedef struct {
    u32 magic;
    u16 version;
    u16 length;
    u32 seq;
    u32 time_ms;
    u16 event;
    u16 flags;
    u16 cell_max_mv;
    u16 cell_min_mv;
    u32 pack_mv;
    s32 current_ma;
    s16 battery_temp_dC;
    s16 mos_temp_dC;
    u16 afe_status;
    u16 reset_reason;
    u32 diagnostics;
    u32 active_protection;
    u32 charge_inhibit;
    u32 discharge_inhibit;
    u32 global_inhibit;
    u32 crc32;
    u32 commit;
} bms_slog_record_t;

typedef struct {
    u16 event;
    u16 flags;
    u32 now_ms;
    bms_measurement_t measurement;
} bms_slog_pending_t;

static bms_slog_pending_t g_queue[BMS_SLOG_QUEUE_LEN];
static u8 g_queue_read;
static u8 g_queue_write;
static u8 g_queue_count;
static u32 g_drop_count;
static u32 g_next_seq;
static u16 g_next_slot;
static u32 g_flash_base;
static u16 g_sector_count;
static u8 g_ready;

static u32 slog_crc32(const u8 *data, u32 len)
{
    u32 crc = 0xFFFFFFFFu;
    u32 i;
    u8 bit;
    for (i = 0u; i < len; ++i) {
        crc ^= data[i];
        for (bit = 0u; bit < 8u; ++bit)
            crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
    }
    return ~crc;
}

static u16 slog_slots_per_sector(void) { return FLASH_SECTOR_SIZE / BMS_SLOG_SLOT_BYTES; }
static u16 slog_total_slots(void) { return (u16)(slog_slots_per_sector() * g_sector_count); }
static u32 slog_slot_addr(u16 slot)
{
    u16 per = slog_slots_per_sector();
    return g_flash_base + ((u32)(slot / per) * FLASH_SECTOR_SIZE) +
           ((u32)(slot % per) * BMS_SLOG_SLOT_BYTES);
}

static u8 slog_valid(const bms_slog_record_t *r)
{
    if (r->magic != BMS_SLOG_MAGIC || r->version != BMS_SLOG_VERSION ||
        r->length != sizeof(*r) || r->commit != BMS_SLOG_COMMIT) return 0u;
    return slog_crc32((const u8 *)r, sizeof(*r) - 8u) == r->crc32;
}

void bms_safety_log_init(void)
{
    bms_slog_record_t record;
    u32 best_seq = 0u;
    u16 best_slot = 0u;
    u16 slot;
    u8 found = 0u;

    memset(g_queue, 0, sizeof(g_queue));
    g_queue_read = g_queue_write = g_queue_count = 0u;
    g_drop_count = 0u;
    g_flash_base = flash_store_cfg_get_safety_log_base();
    g_sector_count = flash_store_cfg_get_safety_log_sectors();
    g_ready = (g_flash_base != 0u && g_sector_count >= 2u) ? 1u : 0u;
    g_next_seq = 1u;
    g_next_slot = 0u;
    if (!g_ready) return;

    for (slot = 0u; slot < slog_total_slots(); ++slot) {
        flash_read_page(slog_slot_addr(slot), sizeof(record), (u8 *)&record);
        if (slog_valid(&record) && (!found || record.seq > best_seq)) {
            found = 1u;
            best_seq = record.seq;
            best_slot = slot;
        }
    }
    if (found) {
        g_next_seq = best_seq + 1u;
        g_next_slot = (u16)((best_slot + 1u) % slog_total_slots());
    }
}

void bms_safety_log_enqueue(uint16_t event, uint16_t flags,
                            const bms_measurement_t *m, uint32_t now_ms)
{
    bms_slog_pending_t *p;
    if (g_queue_count >= BMS_SLOG_QUEUE_LEN) {
        ++g_drop_count;
        return;
    }
    p = &g_queue[g_queue_write];
    memset(p, 0, sizeof(*p));
    p->event = event;
    p->flags = flags;
    p->now_ms = now_ms;
    if (m != 0) p->measurement = *m;
    g_queue_write = (u8)((g_queue_write + 1u) % BMS_SLOG_QUEUE_LEN);
    ++g_queue_count;
}

void bms_safety_log_poll(void)
{
    bms_slog_pending_t *p;
    bms_slog_record_t r;
    u8 i;
    u32 addr;
    u32 commit = BMS_SLOG_COMMIT;
    if (!g_ready || g_queue_count == 0u) return;
    p = &g_queue[g_queue_read];
    memset(&r, 0xFF, sizeof(r));
    r.magic = BMS_SLOG_MAGIC;
    r.version = BMS_SLOG_VERSION;
    r.length = sizeof(r);
    r.seq = g_next_seq;
    r.time_ms = p->now_ms;
    r.event = p->event;
    r.flags = p->flags;
    r.cell_max_mv = 0u;
    r.cell_min_mv = 0xFFFFu;
    for (i = 0u; i < p->measurement.series_count && i < BMS_CELL_MAX; ++i) {
        if (p->measurement.cell_mv[i] > r.cell_max_mv) r.cell_max_mv = p->measurement.cell_mv[i];
        if (p->measurement.cell_mv[i] < r.cell_min_mv) r.cell_min_mv = p->measurement.cell_mv[i];
    }
    r.pack_mv = p->measurement.pack_mv_adc;
    r.current_ma = p->measurement.current_ma;
    r.battery_temp_dC = p->measurement.battery_temp_dC;
    r.mos_temp_dC = p->measurement.mos_temp_dC;
    r.afe_status = (u16)bms_afe_last_error();
    r.reset_reason = bms_supervisor_reset_reason();
    r.diagnostics = bms_diagnostics_faults();
    r.active_protection = bms_sw_protection_active_mask();
    r.charge_inhibit = bms_get_charge_inhibit();
    r.discharge_inhibit = bms_get_discharge_inhibit();
    r.global_inhibit = bms_get_global_inhibit();
    r.crc32 = slog_crc32((const u8 *)&r, sizeof(r) - 8u);
    r.commit = 0xFFFFFFFFu;

    if ((g_next_slot % slog_slots_per_sector()) == 0u) {
        u32 sector_addr = g_flash_base +
            ((u32)(g_next_slot / slog_slots_per_sector()) * FLASH_SECTOR_SIZE);
        if (!flash_store_erase_sector_checked(sector_addr, FLASH_SECTOR_SIZE)) return;
    }
    addr = slog_slot_addr(g_next_slot);
    if (!flash_store_prog_checked(addr, (const u8 *)&r, sizeof(r) - 4u)) return;
    if (!flash_store_prog_checked(addr + sizeof(r) - 4u, (const u8 *)&commit, 4u)) return;

    g_next_seq++;
    g_next_slot = (u16)((g_next_slot + 1u) % slog_total_slots());
    g_queue_read = (u8)((g_queue_read + 1u) % BMS_SLOG_QUEUE_LEN);
    --g_queue_count;
}

uint32_t bms_safety_log_drop_count(void) { return g_drop_count; }
