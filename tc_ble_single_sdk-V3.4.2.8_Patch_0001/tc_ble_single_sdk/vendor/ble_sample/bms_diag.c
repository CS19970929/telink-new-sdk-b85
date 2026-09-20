#include "bms_diag.h"

#include "SocEnhance.h"
#include "bms_afe.h"
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
    s_words[29] = BMS_DIAG_INFO_GENERIC_FET_BITS;
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

static uint16_t capacity_0p1ah(uint32_t as10)
{
    uint32_t value = (as10 + BMS_DIAG_CAPACITY_AS10_PER_0P1AH / 2u) /
                     BMS_DIAG_CAPACITY_AS10_PER_0P1AH;
    return (uint16_t)((value > 65535u) ? 65535u : value);
}

static void poll_fets(void)
{
    uint8_t requested_c = 0u, requested_d = 0u;
    uint8_t command = 0u, command_valid = 0u;
    uint8_t driver = 0u, driver_valid = 0u;
    uint16_t requested;
    uint32_t charge_reason = 0u, discharge_reason = 0u;

    bms_afe_get_requested_fets(&requested_c, &requested_d);
    (void)sh3673510_bms_afe_get_fet_diagnostics(
        &command, &command_valid, &driver, &driver_valid);
    requested = (uint16_t)((requested_c ? 1u : 0u) |
                           (requested_d ? 2u : 0u));
    if (requested_c && (!command_valid || !(command & 1u))) charge_reason = 1024u;
    if (requested_d && (!command_valid || !(command & 2u))) discharge_reason = 1024u;

    if (s_words[128] != requested || s_words[130] != command ||
        s_words[131] != command_valid || s_words[132] != driver ||
        s_words[133] != driver_valid || get32(&s_words[136]) != charge_reason ||
        get32(&s_words[138]) != discharge_reason) {
        s_words[128] = requested;
        s_words[129] = command_valid ? command : 0u;
        s_words[130] = command;
        s_words[131] = command_valid;
        s_words[132] = driver;
        s_words[133] = driver_valid;
        put32(&s_words[136], charge_reason);
        put32(&s_words[138], discharge_reason);
        put32(&s_words[140], pm_get_32k_tick());
        trace(DIAG_EV_MOS, (uint32_t)requested | ((uint32_t)command << 16), driver);
    }
}

void bms_diag_poll_runtime(uint8_t sample_valid, int32_t current_ma,
                           uint32_t sample_tick_32k, uint8_t factory_mode)
{
    bms_soc_diag_t soc;
    uint16_t flags;
    uint16_t soh_source;
    uint8_t dirty = 0u;
    uint16_t old_sample = s_words[193];

    memset(&soc, 0, sizeof(soc));
    bms_soc_get_diag(&soc);

    dirty |= update16(193u, sample_valid ? 1u : 0u);
    dirty |= update32(194u, (uint32_t)current_ma);
    dirty |= update32(196u, (uint32_t)current_ma);
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
    flags = soc.capacity_learning_enable ? 1u : 0u;
    dirty |= update16(230u, flags);
    dirty |= update16(231u, capacity_0p1ah(SOC_Calculate_Element.u32CapFactory));
    dirty |= update16(232u, capacity_0p1ah(SOC_Calculate_Element.u32CapFull));
    dirty |= update16(233u, capacity_0p1ah(SOC_Calculate_Element.u32CapNow));
    dirty |= update32(234u, (uint32_t)current_ma);
    dirty |= update16(236u, 0u);
    dirty |= update16(237u, 0xFFFFu);
    dirty |= update16(238u, 0xFFFFu);
    dirty |= update16(239u, 0u);
    dirty |= update16(240u, SOC_Calculate_Element.soh);
    soh_source = soc.capacity_learned ? 2u : 1u;
    dirty |= update16(241u, soh_source);
    dirty |= update16(242u, soc.capacity_learned ? 100u : 50u);
    dirty |= update16(248u, soc.ocv_cell_mv);

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
