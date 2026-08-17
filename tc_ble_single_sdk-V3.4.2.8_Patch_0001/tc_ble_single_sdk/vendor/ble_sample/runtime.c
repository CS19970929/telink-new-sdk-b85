#include "runtime.h"
#include "drivers.h"
#include "flash_store_cfg.h"
#include "flash_store_safe.h"
#include "sci_upper.h"
#include "sh367309_datadeal.h"

extern struct stCell_Info g_stCellInfoReport;
extern void enter_fac_mode(bool on);

#define RUNTIME_COMMIT_MAGIC       0x544D4F43u
// #define RUNTIME_SAVE_INTERVAL_MIN  10u
#define RUNTIME_SAVE_INTERVAL_MIN  1u
#define RUNTIME_PM_TICKS_PER_SEC   32000u
#define RUNTIME_PM_TICKS_PER_MIN   (RUNTIME_PM_TICKS_PER_SEC * 60u)

typedef struct
{
    u32 seq;
    u32 runtime_min;
    u16 flag;
    u16 crc;
    u32 commit;
} runtime_record_t;

#define RUNTIME_OFFSETOF(type, field)  ((u32)&(((type *)0)->field))
#define RUNTIME_RECORD_BYTES         ((u16)sizeof(runtime_record_t))
#define RUNTIME_RECORDS_PER_SECTOR   (FLASH_SECTOR_SIZE / RUNTIME_RECORD_BYTES)
#define RUNTIME_TOTAL_RECORDS        (RUNTIME_RECORDS_PER_SECTOR * FLASH_ADDR_RUNTIME_SECTORS)

static u32 g_runtime_min = 0u;
static u32 g_runtime_last_saved_min = 0u;
static u32 g_runtime_next_seq = 1u;
static u16 g_runtime_next_index = 0u;
static u8 g_runtime_store_ready = 0u;
static bms_mode_t g_mode = MODE_NORMAL;
static u32 g_runtime_last_tick_32k = 0u;
static u32 g_runtime_pending_tick_32k = 0u;
static u8 g_runtime_tick_ready = 0u;

static void runtime_set_initial_state(void)
{
    g_runtime_min = 0u;
    g_runtime_last_saved_min = 0u;
    g_runtime_next_seq = 1u;
    g_runtime_next_index = 0u;
    g_runtime_store_ready = 0u;
    g_mode = MODE_FACTORY;
    g_runtime_last_tick_32k = 0u;
    g_runtime_pending_tick_32k = 0u;
    g_runtime_tick_ready = 0u;
}

static u16 runtime_crc_bytes(const u8 *data, u16 len)
{
    u16 crc = 0u;
    u16 i;

    for (i = 0u; i < len; ++i) {
        crc = (u16)(crc + data[i]);
    }

    return crc;
}

static u16 runtime_crc_for_record(u32 seq, u32 runtime_min, u16 flag)
{
    u8 buf[10];

    buf[0] = (u8)(seq & 0xFFu);
    buf[1] = (u8)((seq >> 8) & 0xFFu);
    buf[2] = (u8)((seq >> 16) & 0xFFu);
    buf[3] = (u8)((seq >> 24) & 0xFFu);
    buf[4] = (u8)(runtime_min & 0xFFu);
    buf[5] = (u8)((runtime_min >> 8) & 0xFFu);
    buf[6] = (u8)((runtime_min >> 16) & 0xFFu);
    buf[7] = (u8)((runtime_min >> 24) & 0xFFu);
    buf[8] = (u8)(flag & 0xFFu);
    buf[9] = (u8)(flag >> 8);
    return runtime_crc_bytes(buf, sizeof(buf));
}

static int runtime_record_valid(const runtime_record_t *record)
{
    if ((record->flag != RUNTIME_FLAG) || (record->commit != RUNTIME_COMMIT_MAGIC)) {
        return 0;
    }

    return (runtime_crc_for_record(record->seq, record->runtime_min, record->flag) == record->crc);
}

static u32 runtime_flash_base(void)
{
    return flash_store_cfg_get_runtime_base();
}

static u32 runtime_record_addr(u16 index)
{
    u32 base = runtime_flash_base();
    u16 sector_idx = (u16)(index / RUNTIME_RECORDS_PER_SECTOR);
    u16 slot_idx = (u16)(index % RUNTIME_RECORDS_PER_SECTOR);

    return base +
           ((u32)sector_idx * FLASH_SECTOR_SIZE) +
           ((u32)slot_idx * RUNTIME_RECORD_BYTES);
}

static int runtime_record_is_erased(u16 index)
{
    u8 buf[RUNTIME_RECORD_BYTES];
    u16 i;

    flash_read_page(runtime_record_addr(index), RUNTIME_RECORD_BYTES, buf);
    for (i = 0u; i < RUNTIME_RECORD_BYTES; ++i) {
        if (buf[i] != 0xFFu) {
            return 0;
        }
    }
    return 1;
}

static int runtime_scan_store(void)
{
    runtime_record_t record;
    u16 index;
    u16 best_index = 0u;
    u32 best_seq = 0u;
    u8 found = 0u;

    if (runtime_flash_base() == 0u) {
        return 0;
    }

    for (index = 0u; index < RUNTIME_TOTAL_RECORDS; ++index) {
        flash_read_page(runtime_record_addr(index), sizeof(record), (u8 *)&record);
        if (!runtime_record_valid(&record)) {
            continue;
        }

        if ((!found) || (record.seq > best_seq)) {
            found = 1u;
            best_seq = record.seq;
            best_index = index;
            g_runtime_min = record.runtime_min;
        }
    }

    if (!found) {
        return 0;
    }

    g_runtime_last_saved_min = g_runtime_min;
    g_runtime_next_seq = (best_seq == 0xFFFFFFFFu) ? 1u : (best_seq + 1u);
    g_runtime_next_index = (u16)(best_index + 1u);
    if (g_runtime_next_index >= RUNTIME_TOTAL_RECORDS) {
        g_runtime_next_index = 0u;
    }
    g_runtime_store_ready = 1u;
    return 1;
}

static int runtime_prepare_slot(u16 *index)
{
    u16 slot_index;
    u16 sector_idx;
    u16 slot_off;
    u32 sector_addr;

    if ((index == NULL) || (runtime_flash_base() == 0u)) {
        return 0;
    }

    slot_index = *index;
    if (slot_index >= RUNTIME_TOTAL_RECORDS) {
        slot_index = 0u;
    }

    sector_idx = (u16)(slot_index / RUNTIME_RECORDS_PER_SECTOR);
    slot_off = (u16)(slot_index % RUNTIME_RECORDS_PER_SECTOR);
    sector_addr = runtime_flash_base() + ((u32)sector_idx * FLASH_SECTOR_SIZE);

    if (slot_off == 0u) {
        if (!flash_store_erase_sector_checked(sector_addr, FLASH_SECTOR_SIZE)) {
            return 0;
        }
        *index = slot_index;
        return 1;
    }

    if (runtime_record_is_erased(slot_index)) {
        *index = slot_index;
        return 1;
    }

    sector_idx = (u16)(sector_idx + 1u);
    if (sector_idx >= flash_store_cfg_get_runtime_sectors()) {
        sector_idx = 0u;
    }

    slot_index = (u16)(sector_idx * RUNTIME_RECORDS_PER_SECTOR);
    sector_addr = runtime_flash_base() + ((u32)sector_idx * FLASH_SECTOR_SIZE);
    if (!flash_store_erase_sector_checked(sector_addr, FLASH_SECTOR_SIZE)) {
        return 0;
    }

    *index = slot_index;
    return 1;
}

static int runtime_flash_save(void)
{
    runtime_record_t record;
    u16 slot_index = g_runtime_next_index;
    u32 record_addr;
    u32 commit_addr;

    if (!g_runtime_store_ready) {
        return 0;
    }

    if (!runtime_prepare_slot(&slot_index)) {
        return 0;
    }

    record.seq = g_runtime_next_seq;
    record.runtime_min = g_runtime_min;
    record.flag = RUNTIME_FLAG;
    record.crc = runtime_crc_for_record(record.seq, record.runtime_min, record.flag);
    record.commit = RUNTIME_COMMIT_MAGIC;

    record_addr = runtime_record_addr(slot_index);
    commit_addr = record_addr + RUNTIME_OFFSETOF(runtime_record_t, commit);
    if (!flash_store_prog_checked(record_addr,
                                  (const u8 *)&record,
                                  RUNTIME_OFFSETOF(runtime_record_t, commit))) {
        return 0;
    }
    if (!flash_store_prog_checked(commit_addr, (const u8 *)&record.commit, sizeof(record.commit))) {
        return 0;
    }

    g_runtime_last_saved_min = g_runtime_min;
    g_runtime_next_seq += 1u;
    if (g_runtime_next_seq == 0u) {
        g_runtime_next_seq = 1u;
    }
    g_runtime_next_index = (u16)(slot_index + 1u);
    if (g_runtime_next_index >= RUNTIME_TOTAL_RECORDS) {
        g_runtime_next_index = 0u;
    }
    return 1;
}

static void runtime_note_store_error(void)
{
    System_ERROR_UserCallback(ERROR_EEPROM_STORE);
}

static void runtime_finish_factory_mode(void)
{
    g_mode = MODE_NORMAL;
    if (g_runtime_store_ready && !runtime_flash_save()) {
        runtime_note_store_error();
    }
    enter_fac_mode(false);
}

static void runtime_apply_elapsed_minutes(u32 elapsed_min)
{
    u32 remain_min;

    if ((elapsed_min == 0u) || (g_mode == MODE_NORMAL)) {
        return;
    }

    remain_min = (FACTORY_TIME_LIMIT_MIN > g_runtime_min) ?
                 (FACTORY_TIME_LIMIT_MIN - g_runtime_min) : 0u;
    if (elapsed_min >= remain_min) {
        g_runtime_min = FACTORY_TIME_LIMIT_MIN;
        runtime_finish_factory_mode();
        return;
    }

    g_runtime_min += elapsed_min;
    if (g_runtime_store_ready &&
        ((g_runtime_min - g_runtime_last_saved_min) >= RUNTIME_SAVE_INTERVAL_MIN)) {
        if (!runtime_flash_save()) {
            runtime_note_store_error();
        }
    }
}

static void runtime_apply_elapsed_ticks(u32 elapsed_tick_32k)
{
    u32 total_tick_32k;
    u32 elapsed_min;

    if ((elapsed_tick_32k == 0u) || (g_mode == MODE_NORMAL)) {
        return;
    }

    total_tick_32k = g_runtime_pending_tick_32k + elapsed_tick_32k;
    elapsed_min = total_tick_32k / RUNTIME_PM_TICKS_PER_MIN;
    g_runtime_pending_tick_32k = total_tick_32k % RUNTIME_PM_TICKS_PER_MIN;
    runtime_apply_elapsed_minutes(elapsed_min);
}

void Runtime_Init(void)
{
    u32 now_tick_32k;

    runtime_set_initial_state();

    if (!runtime_scan_store()) {
        if (runtime_flash_base() == 0u) {
            g_runtime_last_tick_32k = pm_get_32k_tick();
            g_runtime_tick_ready = 1u;
            return;
        }
        g_runtime_store_ready = 1u;
    }

    if (g_runtime_min >= FACTORY_TIME_LIMIT_MIN) {
        g_mode = MODE_NORMAL;
    } else {
        g_mode = MODE_FACTORY;
    }

    now_tick_32k = pm_get_32k_tick();
    g_runtime_last_tick_32k = now_tick_32k;
    g_runtime_tick_ready = 1u;
}

void Runtime_Poll(void)
{
    u32 now_tick_32k;

    now_tick_32k = pm_get_32k_tick();
    if (!g_runtime_tick_ready) {
        g_runtime_last_tick_32k = now_tick_32k;
        g_runtime_tick_ready = 1u;
        return;
    }

    runtime_apply_elapsed_ticks(now_tick_32k - g_runtime_last_tick_32k);
    g_runtime_last_tick_32k = now_tick_32k;
}

void Runtime_PrepareForDeepSleep(void)
{
    /* Aging runtime counts awake BMS execution only; deep sleep is not compensated. */
    g_runtime_last_tick_32k = pm_get_32k_tick();
    g_runtime_tick_ready = 1u;
}

void Runtime_CancelPendingDeepSleep(void)
{
    g_runtime_last_tick_32k = pm_get_32k_tick();
    g_runtime_tick_ready = 1u;
}

bms_mode_t Runtime_GetMode(void)
{
    return g_mode;
}

int Runtime_FactoryReset(void)
{
    u16 sector_idx;
    u32 base = runtime_flash_base();

    if (base == 0u) {
        return 0;
    }

    for (sector_idx = 0u; sector_idx < flash_store_cfg_get_runtime_sectors(); ++sector_idx) {
        if (!flash_store_erase_sector_checked(base + ((u32)sector_idx * FLASH_SECTOR_SIZE),
                                              FLASH_SECTOR_SIZE)) {
            return 0;
        }
    }

    runtime_set_initial_state();
    g_runtime_store_ready = 1u;
    g_runtime_last_tick_32k = pm_get_32k_tick();
    g_runtime_tick_ready = 1u;
    return 1;
}

int Runtime_ReenterFactoryMode(void)
{
    if (!Runtime_FactoryReset()) {
        return 0;
    }

    enter_fac_mode(true);
    return 1;
}
