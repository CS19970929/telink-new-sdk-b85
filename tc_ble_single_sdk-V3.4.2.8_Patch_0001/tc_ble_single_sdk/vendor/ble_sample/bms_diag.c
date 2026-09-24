#include "bms_diag.h"

#include "SocEnhance.h"
#include "bms_afe.h"
#include "bms_features.h"
#include "bms_state.h"
#include "bms_storage_platform.h"
#include "drivers.h"
#include "sh3673510_project_config.h"
#include <string.h>

#define BMS_DIAG_RUNTIME_OFFSET 192u
#define BMS_DIAG_CAPACITY_AS10_PER_0P1AH 3600u

static uint16_t s_words[256];
static uint16_t s_trace[BMS_DIAG_TRACE_COUNT][BMS_DIAG_TRACE_WORDS];
static uint32_t s_sequence;
static uint32_t s_trace_sequence;
static uint16_t s_next;
static uint8_t s_frozen;

static void put32(uint16_t *p, uint32_t value)
{
    p[0] = (uint16_t)value;
    p[1] = (uint16_t)(value >> 16);
}

static uint32_t get32(const uint16_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 16);
}

static void changed(void)
{
    ++s_sequence;
    put32(&s_words[4], s_sequence);
}

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

static void trace(uint16_t event, uint32_t arg0, uint32_t arg1)
{
    uint16_t *p = s_trace[s_next];
    put32(p, ++s_trace_sequence);
    put32(p + 2, pm_get_32k_tick());
    p[4] = event;
    p[5] = 0u;
    put32(p + 6, arg0);
    put32(p + 8, arg1);
    p[10] = 0u;
    p[11] = 0u;
    s_next = (uint16_t)((s_next + 1u) % BMS_DIAG_TRACE_COUNT);
    if (s_words[12] < BMS_DIAG_TRACE_COUNT) ++s_words[12];
    else if (get32(&s_words[10]) != 0xFFFFFFFFu)
        put32(&s_words[10], get32(&s_words[10]) + 1u);
    put32(&s_words[8], s_trace_sequence);
    changed();
}

static void populate_storage_layout(void)
{
    uint8_t domain;
    for (domain = 0u; domain < 4u; ++domain) {
        storage_region_t region;
        uint16_t offset = (uint16_t)(32u + 16u * domain);
        if (bms_storage_platform_region((bms_storage_domain_t)domain, &region)) {
            put32(&s_words[offset], region.base);
            put32(&s_words[offset + 2u], region.size);
        }
    }
}

void bms_diag_init(void)
{
    memset(s_words, 0, sizeof(s_words));
    memset(s_trace, 0, sizeof(s_trace));
    s_sequence = 0u;
    s_trace_sequence = 0u;
    s_next = 0u;
    s_frozen = 0u;
    s_words[0] = 0x4447u;
    s_words[1] = 1u;
    s_words[2] = BMS_DIAG_CAPABILITIES;
    s_words[14] = 0x3510u;
    s_words[15] = 0x8251u;
    s_words[17] = 1u;
    put32(&s_words[22], BMS_DIAG_BUILD_ID);
    s_words[29] = BMS_DIAG_INFO_GENERIC_FET_BITS | BMS_DIAG_INFO_SH_FET_DETAIL;
    s_words[BMS_DIAG_RUNTIME_OFFSET] = BMS_DIAG_RUNTIME_VERSION;
    s_words[237] = 0xFFFFu;
    s_words[238] = 0xFFFFu;
    populate_storage_layout();
    trace(DIAG_EV_BOOT, 0u, 0u);
}

void bms_diag_set_build_flags(uint16_t flags)
{
    if (!s_frozen) {
        s_words[13] = flags;
        changed();
    }
}

void bms_diag_set_boot_result(uint16_t afe_result, uint16_t parameter_result)
{
    if (!s_frozen) {
        s_words[25] = afe_result;
        s_words[26] = parameter_result;
        changed();
    }
}

void bms_diag_freeze_boot(void)
{
    if (s_frozen) return;
    s_frozen = 1u;
    s_words[3] = 1u;
    trace(DIAG_EV_BOOT_DONE, s_words[25], s_words[26]);
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
    trace(DIAG_EV_INIT, domain, DIAG_STARTED);
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
            put32(&s_words[offset + 8u], pm_get_32k_tick());
        }
    }
    s_words[180u + domain] = result;
    trace(DIAG_EV_INIT, domain, result);
}

void bms_diag_storage_error(uint16_t reason, uint32_t address)
{
    if (s_words[176] == 0u) {
        s_words[176] = reason;
        put32(&s_words[184], address);
    }
    s_words[177] = reason;
    put32(&s_words[178], address);
    trace(DIAG_EV_STORAGE, reason, address);
}

static void poll_fets(void)
{
    uint8_t requested_c = 0u, requested_d = 0u;
    uint8_t command = 0u, command_valid = 0u;
    uint8_t driver = 0u, driver_valid = 0u;
    uint16_t requested;
    uint16_t guard_state;
    sh3673510_fet_diag_detail_t detail;
    uint32_t charge_reason = 0u, discharge_reason = 0u;

    bms_afe_get_requested_fets(&requested_c, &requested_d);
    (void)sh3673510_bms_afe_get_fet_diagnostics(
        &command, &command_valid, &driver, &driver_valid);
    (void)sh3673510_bms_afe_get_fet_diag_detail(&detail);
    guard_state = bms_afe_get_guard_diagnostic_bits();
    requested = (uint16_t)((requested_c ? 1u : 0u) |
                           (requested_d ? 2u : 0u));
    if (requested_c && (!command_valid || !(command & 1u))) {
        charge_reason = detail.charge_block_reasons |
                        bms_features_diag_reasons(1u);
        if (!(guard_state & DIAG_GUARD_OUTPUT_ENABLED)) charge_reason |= DIAG_BLOCK_OUTPUT;
        if (guard_state & (DIAG_GUARD_COMM_INHIBIT | DIAG_GUARD_BUS_SILENCED))
            charge_reason |= DIAG_BLOCK_COMM;
        if (charge_reason == 0u) charge_reason = DIAG_BLOCK_BACKEND;
    }
    if (requested_d && (!command_valid || !(command & 2u))) {
        discharge_reason = detail.discharge_block_reasons |
                           bms_features_diag_reasons(0u);
        if (!(guard_state & DIAG_GUARD_OUTPUT_ENABLED)) discharge_reason |= DIAG_BLOCK_OUTPUT;
        if (guard_state & (DIAG_GUARD_COMM_INHIBIT | DIAG_GUARD_BUS_SILENCED))
            discharge_reason |= DIAG_BLOCK_COMM;
        if (discharge_reason == 0u) discharge_reason = DIAG_BLOCK_BACKEND;
    }

    if (s_words[128] != requested || s_words[130] != command ||
        s_words[131] != command_valid || s_words[132] != driver ||
        s_words[133] != driver_valid || get32(&s_words[136]) != charge_reason ||
        get32(&s_words[138]) != discharge_reason ||
        s_words[142] != detail.flag1 || s_words[143] != detail.flag2 ||
        s_words[145] != detail.bstatus2 ||
        s_words[146] != detail.backend_state || s_words[147] != guard_state ||
        s_words[148] != detail.sensor_state) {
        s_words[128] = requested;
        s_words[129] = command_valid ? command : 0u;
        s_words[130] = command;
        s_words[131] = command_valid;
        s_words[132] = driver;
        s_words[133] = driver_valid;
        put32(&s_words[136], charge_reason);
        put32(&s_words[138], discharge_reason);
        s_words[142] = detail.flag1;
        s_words[143] = detail.flag2;
        s_words[145] = detail.bstatus2;
        s_words[146] = detail.backend_state;
        s_words[147] = guard_state;
        s_words[148] = detail.sensor_state;
        put32(&s_words[140], pm_get_32k_tick());
        trace(DIAG_EV_MOS, (uint32_t)requested | ((uint32_t)command << 16), driver);
    }
}

void bms_diag_poll_runtime(uint8_t sample_valid, int32_t current_ma,
                           uint32_t sample_tick_32k, uint8_t factory_mode)
{
    bms_soc_diag_t soc;
    bms_storage_diagnostics_t flash_diag;
    uint8_t counter_changed = 0u;
    uint16_t flags;
    uint16_t eta;
    uint8_t dirty = 0u;
    uint16_t old_sample = s_words[193];

    bms_storage_platform_get_diagnostics(&flash_diag);
    counter_changed |= update32(160u, flash_diag.program_calls);
    counter_changed |= update32(162u, flash_diag.erase_calls);
    counter_changed |= update32(164u, flash_diag.verify_failures);
    counter_changed |= update32(166u, flash_diag.deferred_writes);
    counter_changed |= update32(168u, flash_diag.max_program_ticks_32k);
    counter_changed |= update32(170u, flash_diag.max_erase_ticks_32k);
    if (counter_changed) changed();
    memset(&soc, 0, sizeof(soc));
    bms_soc_get_diag(&soc);

    dirty |= update16(193u, sample_valid ? 1u : 0u);
    dirty |= update32(194u, (uint32_t)current_ma);
    dirty |= update32(196u, (uint32_t)bms_afe_current_to_soc_ma(current_ma));
    dirty |= update32(198u, sample_tick_32k);
    dirty |= update16(200u, soc.current_deadband_ma);
    dirty |= update16(202u, soc.soc_estimate);
    dirty |= update16(203u, soc.soc_display);
    dirty |= update16(204u, soc.ocv_state);
    dirty |= update16(205u, soc.ocv_center);
    dirty |= update16(206u, soc.ocv_low);
    dirty |= update16(207u, soc.ocv_high);
    dirty |= update16(208u, soc.ocv_confidence);
    dirty |= update16(209u, soc.rest_seconds);
    dirty |= update16(210u, soc.learning_state);
    dirty |= update16(211u, soc.capacity_learned);
    dirty |= update16(212u, soc.learned_capacity_0p1ah);
    dirty |= update16(225u, factory_mode ? 1u : 0u);
    dirty |= update16(226u, soc.chemistry);
    dirty |= update16(227u, soc.profile_id);
    dirty |= update16(228u, soc.profile_version);
    dirty |= update16(229u, soc.endpoint_state);
    flags = (uint16_t)((soc.capacity_learning_enable ? 1u : 0u) |
                       (soc.capacity_learning_candidate_valid ? 2u : 0u) |
                       (soc.eta_valid ? 4u : 0u) |
                       ((soc.endpoint_event_flags & 0x00FFu) << 8));
    dirty |= update16(230u, flags);
    dirty |= update16(231u, soc.nominal_capacity_0p1ah);
    dirty |= update16(232u, soc.effective_capacity_0p1ah);
    dirty |= update16(233u, soc.remaining_capacity_0p1ah);
    dirty |= update32(234u, (uint32_t)soc.filtered_current_ma);
    dirty |= update16(236u, soc.current_variation_ma);
    dirty |= update16(237u, soc.time_to_empty_min);
    dirty |= update16(238u, soc.time_to_full_min);
    eta = (uint16_t)((soc.eta_state & 0x0Fu) |
                     ((soc.eta_direction & 0x0Fu) << 4) |
                     ((uint16_t)soc.eta_confidence << 8));
    dirty |= update16(239u, eta);
    dirty |= update16(240u, soc.soh);
    dirty |= update16(241u, soc.soh_source);
    dirty |= update16(242u, soc.soh_confidence);
    dirty |= update16(243u, soc.candidate_capacity_0p1ah);
    dirty |= update16(244u, soc.valid_learning_count);
    dirty |= update16(245u, soc.rejected_learning_count);
    dirty |= update16(246u, soc.last_learning_reject_reason);
    dirty |= update16(247u, soc.capacity_learning_confidence);
    dirty |= update16(248u, soc.ocv_cell_mv);
    dirty |= update16(249u, (uint16_t)((soc.last_sample_state & 0x0Fu) |
                                       ((soc.last_integral_direction & 0x0Fu) << 4) |
                                       ((uint16_t)soc.last_soc_action << 8)));
    dirty |= update32(250u, soc.last_sample_elapsed_32k);
    dirty |= update32(252u, soc.last_integral_delta_as10);
    dirty |= update16(254u, (uint16_t)(soc.last_soc_before |
                                       ((uint16_t)soc.last_soc_after << 8)));
    dirty |= update16(255u, (uint16_t)(soc.last_soc_target |
                                       ((uint16_t)soc.last_decision_detail << 8)));

    if (s_words[222] != g_stCellInfoReport.unMdlFault_First.all ||
        s_words[223] != g_stCellInfoReport.unMdlFault_Second.all ||
        s_words[224] != g_stCellInfoReport.unMdlFault_Third.all) {
        s_words[222] = g_stCellInfoReport.unMdlFault_First.all;
        s_words[223] = g_stCellInfoReport.unMdlFault_Second.all;
        s_words[224] = g_stCellInfoReport.unMdlFault_Third.all;
        trace(DIAG_EV_PROTECTION,
              (uint32_t)s_words[222] | ((uint32_t)s_words[223] << 16),
              s_words[224]);
        dirty = 0u;
    }

    if ((old_sample & 1u) != (s_words[193] & 1u)) {
        trace(DIAG_EV_SAMPLE_STATE, s_words[193] & 1u, (uint32_t)current_ma);
        dirty = 0u;
    }
    poll_fets();
    if (dirty) changed();
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
    uint32_t tick = pm_get_32k_tick();
    if (!bytes || !count || count > 125u || start < BMS_DIAG_BASE ||
        end > BMS_DIAG_END ||
        (start < BMS_DIAG_TRACE_BASE && end > BMS_DIAG_TRACE_BASE)) return 0;
    for (i = 0u; i < count; ++i) {
        uint16_t word;
        uint16_t offset = (uint16_t)(start + i - BMS_DIAG_BASE);
        if (offset == 6u) word = (uint16_t)tick;
        else if (offset == 7u) word = (uint16_t)(tick >> 16);
        else if (offset < 256u) word = s_words[offset];
        else {
            offset = (uint16_t)(offset - 256u);
            word = s_trace[offset / BMS_DIAG_TRACE_WORDS]
                          [offset % BMS_DIAG_TRACE_WORDS];
        }
        bytes[2u * i] = (uint8_t)(word >> 8);
        bytes[2u * i + 1u] = (uint8_t)word;
    }
    return 1;
}
