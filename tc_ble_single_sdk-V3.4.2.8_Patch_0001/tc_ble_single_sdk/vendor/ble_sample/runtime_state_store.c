#include "runtime_state_store.h"

#include "drivers.h"
#include "flash_blob_store.h"
#include "flash_layout.h"
#include "flash_store_cfg.h"
#include "runtime.h"

#define RUNTIME_STATE_STORE_VERSION             1u
#define RUNTIME_STATE_SOC_MAX_SAFE              16382u

typedef struct
{
    u32 runtime_min;
    u16 crc;
    u16 flag;
} runtime_legacy_store_t;

static const flash_blob_store_cfg_t g_runtime_store_cfg = {
    FLASH_ADR_STATE_EXT_SLOT_A,
    FLASH_ADR_STATE_EXT_SLOT_B,
    FLASH_APP_SECTOR_SIZE,
    FLASH_RUNTIME_MAGIC,
    RUNTIME_STATE_STORE_VERSION,
    sizeof(flash_runtime_state_t),
};

static flash_blob_store_state_t g_runtime_store_state;
static u8 g_runtime_store_ready;

static u16 runtime_state_crc16(const u8 *data, u16 len)
{
    u16 crc = 0;

    for (u16 i = 0; i < len; ++i) {
        crc = (u16)(crc + data[i]);
    }

    return crc;
}

static void runtime_state_fill_defaults(flash_runtime_state_t *state)
{
    memset(state, 0, sizeof(*state));
    state->runtime_min = 0;
    state->cycle = 1;
    state->soc = 60;
    state->dsg_int = 0;
    state->soh = 0;
    state->bms_mode = MODE_FACTORY;
    state->factory_expired = 0;
    state->shutdown_reason = 0;
    state->fault_summary = 0;
    state->update_counter = 0;
}

static void runtime_state_fixup(flash_runtime_state_t *state)
{
    if (state->cycle == 0) {
        state->cycle = 1;
    }
    if (state->soc > RUNTIME_STATE_SOC_MAX_SAFE) {
        state->soc = 60;
    }
    if (state->dsg_int > RUNTIME_STATE_SOC_MAX_SAFE) {
        state->dsg_int = 0;
    }
    if (state->cycle > RUNTIME_STATE_SOC_MAX_SAFE) {
        state->cycle = RUNTIME_STATE_SOC_MAX_SAFE;
    }

    state->factory_expired = (state->runtime_min >= FACTORY_TIME_LIMIT_MIN) ? 1u : 0u;
    state->bms_mode = state->factory_expired ? MODE_NORMAL : MODE_FACTORY;
}

static int runtime_state_load_legacy_runtime(flash_runtime_state_t *state)
{
    runtime_legacy_store_t legacy;

    flash_read_page(FLASH_ADR_RUNTIME, sizeof(legacy), (u8 *)&legacy);
    if (legacy.flag != RUNTIME_FLAG) {
        return 0;
    }

    if (runtime_state_crc16((const u8 *)&legacy, sizeof(legacy) - 4) != legacy.crc) {
        return 0;
    }

    state->runtime_min = legacy.runtime_min;
    runtime_state_fixup(state);
    return 1;
}

int runtime_state_store_init(void)
{
    if (!g_runtime_store_ready)
    {
        memset(&g_runtime_store_state, 0, sizeof(g_runtime_store_state));
        g_runtime_store_ready = 1;
    }

    return 1;
}

int runtime_state_store_load(flash_runtime_state_t *state)
{
    if (!state) {
        return 0;
    }

    runtime_state_store_init();

    if (flash_blob_store_load(&g_runtime_store_cfg, state, &g_runtime_store_state))
    {
        runtime_state_fixup(state);
        return 1;
    }

    runtime_state_fill_defaults(state);
    if (runtime_state_load_legacy_runtime(state)) {
        return runtime_state_store_save(state);
    }

    return 0;
}

int runtime_state_store_save(const flash_runtime_state_t *state)
{
    flash_runtime_state_t local_state;

    if (!state) {
        return 0;
    }

    runtime_state_store_init();

    local_state = *state;
    local_state.update_counter += 1;
    runtime_state_fixup(&local_state);

    return flash_blob_store_save(&g_runtime_store_cfg, &local_state, &g_runtime_store_state);
}

int runtime_state_store_save_soc_snapshot(u16 soc, u16 dsg_int, u16 cycle)
{
    flash_runtime_state_t state;

    runtime_state_store_load(&state);
    state.soc = soc;
    state.dsg_int = dsg_int;
    state.cycle = cycle;

    return runtime_state_store_save(&state);
}

int runtime_state_store_log_event(const flash_event_record_t *event_rec)
{
    (void)event_rec;
    return 0;
}

int runtime_state_store_factory_reset(void)
{
    flash_runtime_state_t state;

    runtime_state_store_init();
    if (!flash_blob_store_reset(&g_runtime_store_cfg)) {
        return 0;
    }

    memset(&g_runtime_store_state, 0, sizeof(g_runtime_store_state));
    runtime_state_fill_defaults(&state);
    return runtime_state_store_save(&state);
}
