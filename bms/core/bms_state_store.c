/* D008 State 只保存 SOC 检查点和选更/出厂的一次性标记。 */
#include "bms_state_store.h"
#include "bms_storage_platform.h"
#include "bms_update_policy.h"
#include "bms_product.h"
#include "storage_record.h"
#include "bms_time.h"
#include "bms_error.h"
#include <string.h>

#define STATE_MAGIC 0x44385331u
#define STATE_SCHEMA 1u
#define STATE_CHECKPOINT_BYTES 20u
#define STATE_PAYLOAD_BYTES (STATE_CHECKPOINT_BYTES + BMS_OTA_UPDATE_ID_WORDS * 2u)
#define STATE_INTERVAL_MS (5u * 60u * 1000u)

typedef struct {
    bms_soc_checkpoint_t soc;
    uint16_t update_id[BMS_OTA_UPDATE_ID_WORDS];
} state_record_t;

static storage_record_store_t s_store __attribute__((aligned(4)));
static state_record_t s_saved;
static uint32_t s_last_write_ms;
static uint8_t s_ready;
static uint8_t s_write_failed;

static void put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *p, uint32_t value)
{
    put16(p, (uint16_t)value);
    put16(p + 2, (uint16_t)(value >> 16));
}

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)get16(p) | ((uint32_t)get16(p + 2) << 16);
}

static void encode(const state_record_t *record, uint8_t *p)
{
    const bms_soc_checkpoint_t *cp = &record->soc;
    unsigned i;

    put32(p, cp->charge_as10);
    put32(p + 4, cp->discharge_as10);
    put32(p + 8, cp->endpoint_basis);
    put16(p + 12, cp->capacity_0p1ah);
    put16(p + 14, cp->cycles);
    p[16] = cp->shown;
    p[17] = cp->endpoint;
    p[18] = cp->estimated;
    p[19] = 0u;
    for (i = 0u; i < BMS_OTA_UPDATE_ID_WORDS; ++i)
        put16(p + STATE_CHECKPOINT_BYTES + i * 2u, record->update_id[i]);
}

static int decode(state_record_t *record, const uint8_t *p)
{
    bms_soc_checkpoint_t *cp = &record->soc;
    unsigned i;

    memset(record, 0, sizeof(*record));
    cp->charge_as10 = get32(p);
    cp->discharge_as10 = get32(p + 4);
    cp->endpoint_basis = get32(p + 8);
    cp->capacity_0p1ah = get16(p + 12);
    cp->cycles = get16(p + 14);
    cp->shown = p[16];
    cp->endpoint = p[17];
    cp->estimated = p[18];
    for (i = 0u; i < BMS_OTA_UPDATE_ID_WORDS; ++i)
        record->update_id[i] = get16(p + STATE_CHECKPOINT_BYTES + i * 2u);

    if (p[19] != 0u) return 0;
    if (cp->capacity_0p1ah != 0u) return bms_soc_checkpoint_valid(cp);
    /* 空检查点只用于记录本包的一次性初始化。 */
    for (i = 0u; i < STATE_CHECKPOINT_BYTES; ++i)
        if (p[i] != 0u) return 0;
    return 1;
}

static int open_store(void)
{
    const storage_port_t *port = bms_storage_platform_port();
    storage_region_t region;

    return port && bms_storage_platform_region(BMS_STORAGE_DOMAIN_STATE, &region) &&
        storage_record_open(&s_store, port, region, STATE_MAGIC, STATE_SCHEMA, STATE_PAYLOAD_BYTES, 64u);
}

static int save_record(const state_record_t *next)
{
    uint8_t payload[STATE_PAYLOAD_BYTES];
    uint32_t now = bms_time_ms();

    if (s_write_failed && now - s_last_write_ms < BMS_STORAGE_RETRY_INTERVAL_MS) return 0;
    s_last_write_ms = now;
    encode(next, payload);
    s_write_failed = !storage_record_save(&s_store, payload);
    if (s_write_failed) {
        bms_error_raise(BMS_ERROR_EEPROM_STORE);
        return 0;
    }
    s_saved = *next;
    return 1;
}

int bms_state_store_init(void)
{
    uint8_t payload[STATE_PAYLOAD_BYTES];
    state_record_t next = {{0}, {0}};

    if (s_ready) return 1;
    if (!open_store()) return 0;
    if (storage_record_load(&s_store, payload)) {
        if (!decode(&next, payload)) return 0;
    } else if (s_store.load_status != STORAGE_RECORD_LOAD_EMPTY) {
        return 0;
    }
    if (bms_ota_update_selected(BMS_UPDATE_SOC_STATE) &&
        !bms_ota_update_id_matches(next.update_id)) {
        s_saved = next; /* 更新失败仍可报告此前已验证的检查点。 */
        memset(&next.soc, 0, sizeof(next.soc));
        bms_ota_update_copy_id(next.update_id);
        if (!save_record(&next)) return 0;
    }
    s_saved = next;
    s_last_write_ms = bms_time_ms();
    s_ready = 1u;
    return 1;
}

bms_soc_checkpoint_t bms_state_store_get(void)
{
    return s_saved.soc;
}

static int same_checkpoint(const bms_soc_checkpoint_t *a, const bms_soc_checkpoint_t *b)
{
    return a->charge_as10 == b->charge_as10 && a->discharge_as10 == b->discharge_as10 &&
        a->endpoint_basis == b->endpoint_basis && a->capacity_0p1ah == b->capacity_0p1ah &&
        a->cycles == b->cycles && a->shown == b->shown && a->endpoint == b->endpoint &&
        a->estimated == b->estimated;
}

int bms_state_store_save_snapshot(const bms_soc_checkpoint_t *checkpoint, uint8_t allow_erase)
{
    state_record_t next;

    if (!s_ready || s_store.save_status == STORAGE_RECORD_SAVE_UNKNOWN ||
        !bms_soc_checkpoint_valid(checkpoint)) return 0;
    if (same_checkpoint(checkpoint, &s_saved.soc)) return 1;
    if (!allow_erase && !storage_record_can_append(&s_store)) return 0;
    next = s_saved;
    next.soc = *checkpoint;
    return save_record(&next);
}

static uint32_t distance_u32(uint32_t a, uint32_t b)
{
    return a >= b ? a - b : b - a;
}

int bms_state_store_poll_snapshot(const bms_soc_checkpoint_t *cp, uint8_t allow_erase)
{
    const bms_soc_checkpoint_t *previous = &s_saved.soc;
    uint32_t threshold = (uint32_t)cp->capacity_0p1ah * BMS_SOC_UNITS_PER_0P1AH / 50u;
    uint8_t end = cp->endpoint;
    uint8_t target = end == BMS_SOC_FULL ? 100u : 0u;

    uint32_t since_write = bms_time_ms() - s_last_write_ms;
    if (!s_ready || cp->capacity_0p1ah == 0u ||
        (s_write_failed && since_write < BMS_STORAGE_RETRY_INTERVAL_MS)) return 0;
    if (end != BMS_SOC_NO_ENDPOINT &&
        (end != previous->endpoint || cp->endpoint_basis != previous->endpoint_basis ||
         (cp->shown == target && previous->shown != target))) {
        return bms_state_store_save_snapshot(cp, allow_erase);
    }
    if (since_write < STATE_INTERVAL_MS) return 0;
    if (!previous->capacity_0p1ah || cp->capacity_0p1ah != previous->capacity_0p1ah ||
        cp->cycles != previous->cycles || distance_u32(cp->charge_as10, previous->charge_as10) >= threshold ||
        distance_u32(cp->discharge_as10, previous->discharge_as10) >= threshold)
        return bms_state_store_save_snapshot(cp, allow_erase);
    return 0;
}

int bms_state_store_factory_reset(void)
{
    uint8_t payload[STATE_PAYLOAD_BYTES];
    state_record_t next = {{0}, {0}};

    if (!bms_ota_factory_reset_requested() || !open_store()) return 0;
    if (storage_record_load(&s_store, payload) && decode(&next, payload) &&
        next.soc.capacity_0p1ah == 0u && bms_ota_update_id_matches(next.update_id)) {
        s_saved = next;
        s_ready = 1u;
        return 1;
    }
    if (s_store.load_status == STORAGE_RECORD_LOAD_IO_ERROR ||
        !storage_record_erase_all(&s_store)) return 0;
    memset(&next, 0, sizeof(next));
    bms_ota_update_copy_id(next.update_id);
    if (!save_record(&next)) return 0;
    s_ready = 1u;
    return 1;
}
