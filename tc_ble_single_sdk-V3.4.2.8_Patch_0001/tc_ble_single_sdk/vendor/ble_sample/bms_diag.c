#include "bms_diag.h"
#include <string.h>

static uint16_t s_words[256];
static uint16_t s_trace[BMS_DIAG_TRACE_COUNT][BMS_DIAG_TRACE_WORDS];
static uint32_t s_sequence, s_trace_sequence;
static uint16_t s_next;
static uint8_t s_frozen;

static void put32(uint16_t *p, uint32_t value)
{
    p[0] = (uint16_t)value; p[1] = (uint16_t)(value >> 16);
}
static uint32_t get32(const uint16_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 16);
}
static void changed(void) { ++s_sequence; put32(&s_words[4], s_sequence); }
static uint8_t update16(uint16_t offset, uint16_t value)
{
    if (s_words[offset] == value) return 0u;
    s_words[offset] = value;
    return 1u;
}
static uint8_t update32(uint16_t offset, uint32_t value)
{
    if (get32(&s_words[offset]) == value) return 0u;
    put32(&s_words[offset], value);
    return 1u;
}

void bms_diag_trace(uint16_t event, uint32_t arg0, uint32_t arg1)
{
    uint16_t *p = s_trace[s_next];
    put32(p, ++s_trace_sequence); put32(p + 2, bms_diag_tick());
    p[4] = event; p[5] = 0u; put32(p + 6, arg0); put32(p + 8, arg1);
    p[10] = 0u; p[11] = 0u;
    s_next = (uint16_t)((s_next + 1u) % BMS_DIAG_TRACE_COUNT);
    if (s_words[12] < BMS_DIAG_TRACE_COUNT) ++s_words[12];
    else if (get32(&s_words[10]) != 0xFFFFFFFFu)
        put32(&s_words[10], get32(&s_words[10]) + 1u);
    put32(&s_words[8], s_trace_sequence);
    changed();
}
void bms_diag_init(void)
{
    memset(s_words, 0, sizeof(s_words)); memset(s_trace, 0, sizeof(s_trace));
    s_sequence = 0u; s_trace_sequence = 0u; s_next = 0u; s_frozen = 0u;
    s_words[0] = 0x4447u; s_words[1] = 1u; s_words[2] = BMS_DIAG_CAPABILITIES;
    s_words[BMS_DIAG_RUNTIME_OFFSET] = BMS_DIAG_RUNTIME_VERSION;
    put32(&s_words[22], BMS_DIAG_BUILD_ID);
    s_words[14] = 0x1124u; s_words[15] = 0x8251u;
    bms_diag_trace(DIAG_EV_BOOT, 0u, 0u);
}
void bms_diag_boot_word(uint16_t offset, uint16_t value)
{
    if (!s_frozen && offset >= 13u && offset < 128u) { s_words[offset] = value; changed(); }
}
void bms_diag_boot_u32(uint16_t offset, uint32_t value)
{
    if (!s_frozen && offset >= 13u && offset < 127u) { put32(&s_words[offset], value); changed(); }
}
void bms_diag_upgrade(uint16_t stage, uint16_t invalid_mask)
{
    if (s_frozen) return;
    s_words[27] = stage; s_words[28] = invalid_mask;
    bms_diag_trace(DIAG_EV_UPGRADE, stage, invalid_mask);
}
void bms_diag_freeze_boot(void)
{
    if (s_frozen) return;
    s_frozen = 1u; s_words[3] = 1u;
    bms_diag_trace(DIAG_EV_BOOT_DONE, s_words[24], s_words[25]);
}
void bms_diag_attempt(uint8_t domain)
{
    uint16_t offset;
    if (domain >= 4u) return;
    offset = (uint16_t)(32u + 16u * domain);
    if (!s_frozen) {
        if (s_words[offset + 4u] != 0xFFFFu) ++s_words[offset + 4u];
        s_words[offset + 6u] = DIAG_STARTED;
    }
    bms_diag_trace(DIAG_EV_INIT, domain, DIAG_STARTED);
}
void bms_diag_result(uint8_t domain, uint16_t result)
{
    uint16_t offset;
    if (domain >= 4u) return;
    offset = (uint16_t)(32u + 16u * domain);
    if (!s_frozen) {
        s_words[offset + 6u] = result;
        if (result == DIAG_DEFAULTS) s_words[offset + 7u] = 1u;
        else if (result != DIAG_OK && s_words[offset + 5u] == 0u) {
            s_words[offset + 5u] = result;
            put32(&s_words[offset + 8u], bms_diag_tick());
        }
    }
    s_words[180u + domain] = result;
    bms_diag_trace(DIAG_EV_INIT, domain, result);
}
void bms_diag_storage_error(uint16_t reason, uint32_t address)
{
    /* Keep the first error independently of ring overwrites and boot freeze. */
    if (s_words[176] == 0u) { s_words[176] = reason; put32(&s_words[184], address); }
    s_words[177] = reason; put32(&s_words[178], address);
    bms_diag_trace(DIAG_EV_STORAGE, reason, address);
}
void bms_diag_params(uint8_t valid, uint8_t upgrade)
{
    uint16_t bits = (uint16_t)((valid ? 1u : 0u) | (upgrade ? 2u : 0u));
    if (!s_frozen) s_words[24] = bits;
    if (s_words[144] != bits) { s_words[144] = bits; bms_diag_trace(DIAG_EV_PARAMS, bits, 0u); }
}
void bms_diag_mos(uint16_t requested, uint32_t charge, uint32_t discharge)
{
    if (s_words[128] == requested && get32(&s_words[136]) == charge &&
        get32(&s_words[138]) == discharge) return;
    s_words[128] = requested;
    s_words[129] = (uint16_t)((charge == 0u ? requested & 1u : 0u) |
                             (discharge == 0u ? requested & 2u : 0u));
    put32(&s_words[136], charge); put32(&s_words[138], discharge);
    bms_diag_trace(DIAG_EV_MOS, charge | ((uint32_t)requested << 16), discharge);
}
void bms_diag_command(uint8_t command, uint8_t valid)
{
    if (s_words[130] != command || s_words[131] != valid) {
        s_words[130] = command; s_words[131] = valid;
        bms_diag_trace(DIAG_EV_AFE, command, valid);
    }
}
void bms_diag_driver(uint8_t flags, uint8_t valid)
{
    if (!valid && !s_words[133]) return;
    if ((s_words[132] & 3u) != (flags & 3u) || s_words[133] != valid)
        bms_diag_trace(DIAG_EV_DRIVER, flags & 3u, valid);
    s_words[132] = flags; s_words[133] = valid;
    put32(&s_words[140], bms_diag_tick()); changed();
}
void bms_diag_counter(uint16_t index, uint32_t value)
{
    if (index < 6u && get32(&s_words[160u + 2u * index]) != value) {
        put32(&s_words[160u + 2u * index], value); changed();
    }
}

void bms_diag_runtime_sample(uint8_t valid, int32_t raw_current_ma,
                             int32_t current_ma, uint32_t sample_tick_32k,
                             uint8_t current_recovery_pending)
{
    uint16_t old_flags = s_words[193];
    uint16_t flags = (uint16_t)((valid ? 1u : 0u) |
                                (current_recovery_pending ? 2u : 0u));
    uint8_t dirty = 0u;
    dirty |= update16(193u, flags);
    dirty |= update32(194u, (uint32_t)raw_current_ma);
    dirty |= update32(196u, (uint32_t)current_ma);
    dirty |= update32(198u, sample_tick_32k);
    if ((old_flags & 1u) != (flags & 1u))
        bms_diag_trace(DIAG_EV_SAMPLE_STATE, flags & 1u, (uint32_t)current_ma);
    else if (dirty)
        changed();
}

void bms_diag_runtime_soc(uint8_t soc_estimate, uint8_t soc_display,
                          uint8_t ocv_state, uint8_t ocv_center,
                          uint8_t ocv_low, uint8_t ocv_high,
                          uint8_t ocv_confidence, uint16_t rest_seconds,
                          uint8_t learning_state, uint8_t capacity_learned,
                          uint16_t learned_capacity_0p1ah,
                          uint16_t current_deadband_ma)
{
    uint8_t dirty = 0u;
    dirty |= update16(200u, current_deadband_ma);
    dirty |= update16(202u, soc_estimate);
    dirty |= update16(203u, soc_display);
    dirty |= update16(204u, ocv_state);
    dirty |= update16(205u, ocv_center);
    dirty |= update16(206u, ocv_low);
    dirty |= update16(207u, ocv_high);
    dirty |= update16(208u, ocv_confidence);
    dirty |= update16(209u, rest_seconds);
    dirty |= update16(210u, learning_state);
    dirty |= update16(211u, capacity_learned);
    dirty |= update16(212u, learned_capacity_0p1ah);
    if (dirty) changed();
}

void bms_diag_runtime_soc_extended(const bms_soc_diag_t *soc)
{
    uint16_t flags;
    uint16_t eta;
    uint8_t dirty = 0u;
    if (soc == 0) return;
    flags = (uint16_t)((soc->capacity_learning_enable ? 1u : 0u) |
                       (soc->capacity_learning_candidate_valid ? 2u : 0u) |
                       (soc->eta_valid ? 4u : 0u) |
                       ((soc->endpoint_event_flags & 0x00FFu) << 8));
    eta = (uint16_t)((soc->eta_state & 0x000Fu) |
                     ((soc->eta_direction & 0x000Fu) << 4) |
                     ((soc->eta_confidence & 0x00FFu) << 8));
    dirty |= update16(226u, soc->chemistry);
    dirty |= update16(227u, soc->profile_id);
    dirty |= update16(228u, soc->profile_version);
    dirty |= update16(229u, soc->endpoint_state);
    dirty |= update16(230u, flags);
    dirty |= update16(231u, soc->nominal_capacity_0p1ah);
    dirty |= update16(232u, soc->effective_capacity_0p1ah);
    dirty |= update16(233u, soc->remaining_capacity_0p1ah);
    dirty |= update32(234u, (uint32_t)soc->filtered_current_ma);
    dirty |= update16(236u, soc->current_variation_ma);
    dirty |= update16(237u, soc->time_to_empty_min);
    dirty |= update16(238u, soc->time_to_full_min);
    dirty |= update16(239u, eta);
    dirty |= update16(240u, soc->soh);
    dirty |= update16(241u, soc->soh_source);
    dirty |= update16(242u, soc->soh_confidence);
    dirty |= update16(243u, soc->candidate_capacity_0p1ah);
    dirty |= update16(244u, soc->valid_learning_count);
    dirty |= update16(245u, soc->rejected_learning_count);
    dirty |= update16(246u, soc->last_learning_reject_reason);
    dirty |= update16(247u, soc->capacity_learning_confidence);
    dirty |= update16(248u, soc->ocv_cell_mv);
    if (dirty) changed();
}

void bms_diag_runtime_pm(uint8_t suspend_allowed, uint32_t block_mask,
                         uint8_t low_voltage_region, uint32_t low_voltage_seconds,
                         uint8_t ble_connected, uint8_t sample_pending,
                         uint16_t suspend_current_threshold_ma)
{
    uint32_t previous_mask = get32(&s_words[213]);
    uint32_t previous_trace_mask = previous_mask & (uint32_t)~DIAG_PM_BLOCK_SAMPLE_PENDING;
    uint32_t trace_mask = block_mask & (uint32_t)~DIAG_PM_BLOCK_SAMPLE_PENDING;
    uint8_t dirty = 0u;
    dirty |= update32(213u, block_mask);
    dirty |= update16(215u, suspend_allowed ? 1u : 0u);
    dirty |= update16(216u, low_voltage_region);
    dirty |= update32(217u, low_voltage_seconds);
    dirty |= update16(219u, ble_connected ? 1u : 0u);
    dirty |= update16(220u, sample_pending ? 1u : 0u);
    dirty |= update16(221u, suspend_current_threshold_ma);
    /* sample_pending is a normal 200 ms scheduling edge. Keep it visible in
     * the live snapshot, but do not let it churn the 64-entry trace. */
    if (previous_trace_mask != trace_mask)
        bms_diag_trace(DIAG_EV_PM_STATE,
            (uint32_t)(trace_mask == 0u ? 1u : 0u) |
            ((uint32_t)low_voltage_region << 8) |
            ((uint32_t)(ble_connected ? 1u : 0u) << 16),
            trace_mask);
    else if (dirty)
        changed();
}

void bms_diag_runtime_faults(uint16_t level1, uint16_t level2, uint16_t level3)
{
    if (s_words[222] == level1 && s_words[223] == level2 && s_words[224] == level3)
        return;
    s_words[222] = level1;
    s_words[223] = level2;
    s_words[224] = level3;
    bms_diag_trace(DIAG_EV_PROTECTION,
                   (uint32_t)level1 | ((uint32_t)level2 << 16),
                   level3);
}

void bms_diag_runtime_mode(uint8_t factory_mode)
{
    if (update16(225u, factory_mode ? 1u : 0u)) changed();
}

int bms_diag_overlaps(uint16_t start, uint16_t count)
{
    uint32_t end = (uint32_t)start + count;
    return count != 0u && start < BMS_DIAG_END && end > BMS_DIAG_BASE;
}
int bms_diag_read(uint16_t start, uint16_t count, uint8_t *bytes)
{
    uint16_t i;
    uint32_t end = (uint32_t)start + count;
    uint32_t tick = bms_diag_tick();
    if (!bytes || !count || count > 125u || start < BMS_DIAG_BASE ||
        end > BMS_DIAG_END || (start < BMS_DIAG_TRACE_BASE && end > BMS_DIAG_TRACE_BASE)) return 0;
    /* Caller and producers all run on the main loop; no interrupt masking or
     * a second 2KB copy is necessary. Explicit encoding avoids ABI packing. */
    for (i = 0u; i < count; ++i) {
        uint16_t word, offset = (uint16_t)(start + i - BMS_DIAG_BASE);
        if (offset == 6u) word = (uint16_t)tick;
        else if (offset == 7u) word = (uint16_t)(tick >> 16);
        else if (offset < 256u) word = s_words[offset];
        else {
            offset = (uint16_t)(offset - 256u);
            word = s_trace[offset / BMS_DIAG_TRACE_WORDS][offset % BMS_DIAG_TRACE_WORDS];
        }
        bytes[2u*i] = (uint8_t)(word >> 8); bytes[2u*i+1u] = (uint8_t)word;
    }
    return 1;
}

uint16_t bms_diag_cached_word(uint16_t offset)
{
    return offset < 256u ? s_words[offset] : 0u;
}
void bms_diag_backend(uint16_t charge, uint16_t discharge)
{
    if (s_words[146] != charge || s_words[147] != discharge) {
        s_words[146] = charge; s_words[147] = discharge; changed();
    }
}
