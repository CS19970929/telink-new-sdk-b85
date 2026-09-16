#include "bms_state_store.h"

#include "bms_storage_platform.h"
#include "storage_record.h"
#include "drivers.h"
#include "bms_error.h"
#include <string.h>

#define BMS_STATE_RECORD_MAGIC        0x53544131u /* STA1 */
#define BMS_STATE_SCHEMA_VERSION      2u
#define BMS_STATE_PAYLOAD_WORDS       8u
#define BMS_STATE_PAYLOAD_BYTES       (BMS_STATE_PAYLOAD_WORDS * 4u)

typedef struct {
    u32 soc;
    u32 dsg;
    u32 cycle;
    u32 learned_capacity_0p1ah;
    u32 flags;
    u32 runtime_min;
    u32 soc_revision;
    u32 runtime_revision;
} bms_state_persist_t;

static storage_record_store_t g_bms_state_store;
static bms_state_persist_t g_bms_state;
static u8 g_bms_state_ready;
static bms_state_persist_t g_bms_state_pending;
static u32 g_bms_state_last_attempt_32k;
static u8 g_bms_state_last_failed;
static u8 g_bms_state_attempted;

static void bms_state_put_u32le(u8 *buf, u32 value)
{
    buf[0] = (u8)(value & 0xFFu);
    buf[1] = (u8)((value >> 8) & 0xFFu);
    buf[2] = (u8)((value >> 16) & 0xFFu);
    buf[3] = (u8)((value >> 24) & 0xFFu);
}

static u32 bms_state_get_u32le(const u8 *buf)
{
    return ((u32)buf[0]) | ((u32)buf[1] << 8) |
           ((u32)buf[2] << 16) | ((u32)buf[3] << 24);
}

bms_state_store_data_t bms_state_store_get_default_data(void)
{
    bms_state_store_data_t data;
    data.soc = BMS_STATE_DEFAULT_SOC;
    data.dsg = BMS_STATE_DEFAULT_DSG;
    data.cycle = BMS_STATE_DEFAULT_CYCLE;
    data.learned_capacity_0p1ah = BMS_STATE_DEFAULT_LEARNED_CAPACITY;
    data.flags = BMS_STATE_DEFAULT_FLAGS;
    return data;
}

static void bms_state_defaults(bms_state_persist_t *state)
{
    bms_state_store_data_t soc = bms_state_store_get_default_data();
    state->soc = soc.soc;
    state->dsg = soc.dsg;
    state->cycle = soc.cycle;
    state->learned_capacity_0p1ah = soc.learned_capacity_0p1ah;
    state->flags = soc.flags;
    state->runtime_min = 0u;
    state->soc_revision = FW_UPGRADE_RESET_SOC_EPOCH;
    state->runtime_revision = FW_UPGRADE_RESET_RUNTIME_EPOCH;
}

static void bms_state_encode(const bms_state_persist_t *state, u8 *payload)
{
    bms_state_put_u32le(&payload[0], state->soc);
    bms_state_put_u32le(&payload[4], state->dsg);
    bms_state_put_u32le(&payload[8], state->cycle);
    bms_state_put_u32le(&payload[12], state->learned_capacity_0p1ah);
    bms_state_put_u32le(&payload[16], state->flags);
    bms_state_put_u32le(&payload[20], state->runtime_min);
    bms_state_put_u32le(&payload[24], state->soc_revision);
    bms_state_put_u32le(&payload[28], state->runtime_revision);
}

static void bms_state_decode(bms_state_persist_t *state, const u8 *payload)
{
    state->soc = bms_state_get_u32le(&payload[0]);
    state->dsg = bms_state_get_u32le(&payload[4]);
    state->cycle = bms_state_get_u32le(&payload[8]);
    state->learned_capacity_0p1ah = bms_state_get_u32le(&payload[12]);
    state->flags = bms_state_get_u32le(&payload[16]);
    state->runtime_min = bms_state_get_u32le(&payload[20]);
    state->soc_revision = bms_state_get_u32le(&payload[24]);
    state->runtime_revision = bms_state_get_u32le(&payload[28]);
}

static int bms_state_save(const bms_state_persist_t *next)
{
    u8 payload[BMS_STATE_PAYLOAD_BYTES];
    u32 now = pm_get_32k_tick();
    if (g_bms_state_store.has_latest && memcmp(&g_bms_state, next, sizeof(*next)) == 0) return 1;
    /* Forced shutdown writes bypass the normal interval, never failure backoff. */
    if (g_bms_state_attempted && g_bms_state_last_failed &&
        (u32)(now - g_bms_state_last_attempt_32k) < BMS_STORAGE_RETRY_INTERVAL_32K) return 0;
    g_bms_state_attempted = 1u;
    g_bms_state_last_attempt_32k = now;
    bms_state_encode(next, payload);
    if (!storage_record_save(&g_bms_state_store, payload)) {
        g_bms_state_last_failed = 1u;
        bms_error_raise(BMS_ERROR_EEPROM_STORE);
        return 0;
    }
    g_bms_state_last_failed = 0u;
    g_bms_state = *next;
    return 1;
}

int bms_state_store_init(void)
{
    const storage_port_t *port;
    storage_region_t region;
    u8 payload[BMS_STATE_PAYLOAD_BYTES];
    bms_state_persist_t next, defaults;
    if (g_bms_state_ready) return 1;
    if (g_bms_state_attempted && g_bms_state_last_failed &&
        (u32)(pm_get_32k_tick() - g_bms_state_last_attempt_32k) < BMS_STORAGE_RETRY_INTERVAL_32K) return 0;
    port = bms_storage_platform_port();
    if ((port == 0) || !bms_storage_platform_region(BMS_STORAGE_DOMAIN_STATE, &region) ||
        !storage_record_open(&g_bms_state_store, port, region, BMS_STATE_RECORD_MAGIC,
                             BMS_STATE_SCHEMA_VERSION, BMS_STATE_PAYLOAD_BYTES)) goto invalid;
    if (storage_record_load(&g_bms_state_store, payload)) bms_state_decode(&g_bms_state, payload);
    else bms_state_defaults(&g_bms_state);
    next = g_bms_state;
    bms_state_defaults(&defaults);
    if (next.soc_revision != FW_UPGRADE_RESET_SOC_EPOCH) {
        next.soc = defaults.soc; next.dsg = defaults.dsg; next.cycle = defaults.cycle;
        next.learned_capacity_0p1ah = defaults.learned_capacity_0p1ah;
        next.flags = defaults.flags;
        next.soc_revision = FW_UPGRADE_RESET_SOC_EPOCH;
    }
    if (next.runtime_revision != FW_UPGRADE_RESET_RUNTIME_EPOCH) {
        next.runtime_min = 0u;
        next.runtime_revision = FW_UPGRADE_RESET_RUNTIME_EPOCH;
    }
    if (next.soc > 100u || next.dsg > 100u || next.learned_capacity_0p1ah > 10000u ||
        (next.flags & ~BMS_STATE_FLAG_CAPACITY_LEARNED) != 0u) goto invalid;
    if (!bms_state_save(&next)) return 0;
    g_bms_state_pending = g_bms_state;
    g_bms_state_last_attempt_32k = pm_get_32k_tick();
    g_bms_state_attempted = 1u;
    g_bms_state_ready = 1u;
    return 1;
invalid:
    g_bms_state_attempted = 1u;
    g_bms_state_last_failed = 1u;
    g_bms_state_last_attempt_32k = pm_get_32k_tick();
    bms_error_raise(BMS_ERROR_EEPROM_STORE);
    return 0;
}

bms_state_store_data_t bms_state_store_get(void)
{
    bms_state_store_data_t data = bms_state_store_get_default_data();
    if (!bms_state_store_init()) return data;
    data.soc = g_bms_state.soc;
    data.dsg = g_bms_state.dsg;
    data.cycle = g_bms_state.cycle;
    data.learned_capacity_0p1ah = g_bms_state.learned_capacity_0p1ah;
    data.flags = g_bms_state.flags;
    return data;
}

int bms_state_store_write_all(u32 soc, u32 dsg, u32 cycle)
{
    bms_state_persist_t next;
    if (!bms_state_store_init()) return 0;
    next = g_bms_state_pending;
    next.soc = soc; next.dsg = dsg; next.cycle = cycle;
    g_bms_state_pending = next;
    return bms_state_save(&next);
}

int bms_state_store_write_learning(u32 learned_capacity_0p1ah, u32 flags)
{
    bms_state_persist_t next;
    if (!bms_state_store_init()) return 0;
    next = g_bms_state_pending;
    next.learned_capacity_0p1ah = learned_capacity_0p1ah;
    next.flags = flags;
    g_bms_state_pending = next;
    /* Queued checkpoint; the main loop persists it, shutdown flush includes it. */
    return 1;
}

void bms_state_store_update_and_log_if_changed(u32 soc, u32 dsg, u32 cycle)
{
    u32 interval;
    if (!bms_state_store_init()) return;
    g_bms_state_pending.soc = soc;
    g_bms_state_pending.dsg = dsg;
    g_bms_state_pending.cycle = cycle;
    interval = g_bms_state_last_failed ? BMS_STORAGE_RETRY_INTERVAL_32K : BMS_STATE_SAVE_INTERVAL_32K;
    if ((u32)(pm_get_32k_tick() - g_bms_state_last_attempt_32k) < interval) return;
    (void)bms_state_save(&g_bms_state_pending);
}

u32 bms_state_store_get_runtime_min(void)
{
    if (!bms_state_store_init()) return 0u;
    return g_bms_state.runtime_min;
}

int bms_state_store_write_runtime_min(u32 runtime_min)
{
    bms_state_persist_t next;
    if (!bms_state_store_init()) return 0;
    next = g_bms_state_pending;
    next.runtime_min = runtime_min;
    g_bms_state_pending = next;
    return bms_state_save(&next);
}

int bms_state_store_reset_runtime(void)
{
    return bms_state_store_write_runtime_min(0u);
}
