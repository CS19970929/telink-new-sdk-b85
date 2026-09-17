#include "runtime.h"
#include "bms_error.h"
#include "drivers.h"
#include "bms_state_store.h"

#define RUNTIME_SAVE_INTERVAL_MIN  1u
#define RUNTIME_PM_TICKS_PER_SEC   32000u
#define RUNTIME_PM_TICKS_PER_MIN   (RUNTIME_PM_TICKS_PER_SEC * 60u)

static u32 g_runtime_min = 0u;
static u32 g_runtime_last_saved_min = 0u;
static u8 g_runtime_store_ready = 0u;
static bms_mode_t g_mode = MODE_NORMAL;
static u32 g_runtime_last_tick_32k = 0u;
static u32 g_runtime_pending_tick_32k = 0u;
static u8 g_runtime_tick_ready = 0u;

static void runtime_set_initial_state(void)
{
    g_runtime_min = 0u;
    g_runtime_last_saved_min = 0u;
    g_runtime_store_ready = 0u;
    g_mode = MODE_FACTORY;
    g_runtime_last_tick_32k = 0u;
    g_runtime_pending_tick_32k = 0u;
    g_runtime_tick_ready = 0u;
}

static void runtime_note_store_error(void) { bms_error_raise(BMS_ERROR_EEPROM_STORE); }

static int runtime_state_save(void)
{
    if (!g_runtime_store_ready || !bms_state_store_write_runtime_min(g_runtime_min)) return 0;
    g_runtime_last_saved_min = g_runtime_min;
    return 1;
}

static void runtime_finish_factory_mode(void)
{
    g_mode = MODE_NORMAL;
    if (g_runtime_store_ready && !runtime_state_save()) runtime_note_store_error();
}

static void runtime_apply_elapsed_minutes(u32 elapsed_min)
{
    u32 remain_min;
    if ((elapsed_min == 0u) || (g_mode == MODE_NORMAL)) return;
    remain_min = (FACTORY_TIME_LIMIT_MIN > g_runtime_min) ?
                 (FACTORY_TIME_LIMIT_MIN - g_runtime_min) : 0u;
    if (elapsed_min >= remain_min) {
        g_runtime_min = FACTORY_TIME_LIMIT_MIN;
        runtime_finish_factory_mode();
        return;
    }
    g_runtime_min += elapsed_min;
    if (g_runtime_store_ready &&
        ((g_runtime_min - g_runtime_last_saved_min) >= RUNTIME_SAVE_INTERVAL_MIN) &&
        !runtime_state_save()) runtime_note_store_error();
}

static void runtime_apply_elapsed_ticks(u32 elapsed_tick_32k)
{
    u32 total_tick_32k;
    u32 elapsed_min;
    if ((elapsed_tick_32k == 0u) || (g_mode == MODE_NORMAL)) return;
    total_tick_32k = g_runtime_pending_tick_32k + elapsed_tick_32k;
    elapsed_min = total_tick_32k / RUNTIME_PM_TICKS_PER_MIN;
    g_runtime_pending_tick_32k = total_tick_32k % RUNTIME_PM_TICKS_PER_MIN;
    runtime_apply_elapsed_minutes(elapsed_min);
}

void Runtime_Init(void)
{
    runtime_set_initial_state();
    if (bms_state_store_init()) {
        g_runtime_store_ready = 1u;
        g_runtime_min = bms_state_store_get_runtime_min();
        g_runtime_last_saved_min = g_runtime_min;
    }
    g_mode = (g_runtime_min >= FACTORY_TIME_LIMIT_MIN) ? MODE_NORMAL : MODE_FACTORY;
    g_runtime_last_tick_32k = pm_get_32k_tick();
    g_runtime_tick_ready = 1u;
}

void Runtime_Poll(void)
{
    u32 now_tick_32k;
    /* Factory aging is finished. Reentry/reset initializes its own time base;
     * normal operation needs neither a PM-clock read nor elapsed arithmetic. */
    if (g_mode == MODE_NORMAL) return;
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

bms_mode_t Runtime_GetMode(void) { return g_mode; }

int Runtime_FactoryReset(void)
{
    if (!bms_state_store_init() || !bms_state_store_reset_runtime()) return 0;
    runtime_set_initial_state();
    g_runtime_store_ready = 1u;
    g_runtime_last_tick_32k = pm_get_32k_tick();
    g_runtime_tick_ready = 1u;
    return 1;
}

int Runtime_ReenterFactoryMode(void) { return Runtime_FactoryReset(); }
