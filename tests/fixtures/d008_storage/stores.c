#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bms_soc_app.h"
#include "bms_afe_driver.h"
#include "bms_state.h"
#include "bms_parameter_access.h"
#include "bms_product.h"

#define SECTOR 4096u
#define DOMAIN (2u * SECTOR)
#define TOTAL (3u * DOMAIN)
static uint8_t memory[TOTAL];
static uint8_t backup[TOTAL];
static uint32_t clock_ms;
static int budget = -1;
static unsigned writes, erases, reloads;
static int read_failed, fail_after_event_commit, fail_after_config_commit, locked;
static uint32_t fail_erase_at = UINT32_MAX;
static int fail_legacy_purge;
static unsigned legacy_calls;
static uint16_t last_charge;
static unsigned apply_calls, finish_calls, inhibit_calls, apply_failures;

void bms_product_info_refresh(void) {}

uint32_t bms_time_ms(void) { return clock_ms; }
uint8_t bms_afe_hw_access_is_active(void) { return 1u; }
void bms_afe_hw_access_close(void) {}
uint8_t bms_afe_begin_protection_update(void) { return 1u; }
void bms_afe_finish_protection_update(void) { ++finish_calls; }
void bms_afe_invalidate_configuration(void) { ++inhibit_calls; }
uint8_t bms_afe_driver_apply_protection_config(const bms_afe_hw_profile_t *profile)
{
    ++apply_calls;
    if (apply_failures) { --apply_failures; return 0u; }
    last_charge = profile->cov_mv;
    return 1u;
}
uint8_t DVC1124_ReadProtectionEffective(bms_afe_hw_profile_t *profile)
{ profile->cov_mv = last_charge; return 1u; }
void bms_soc_app_reload(void) { ++reloads; }
uint8_t bms_soc_app_percent(void) { return 61u; }
uint8_t bms_afe_get_aux_measurements(bms_afe_aux_measurements_t *sample)
{ memset(sample, 0, sizeof(*sample)); return 1u; }
uint8_t bms_board_heater_supported(void) { return 1u; }
u8 bms_reset_software_parameters(void) { return 0u; }
u8 bms_reset_afe_parameters(void) { return 0u; }

#include "bms_storage_platform.h"
static int begin_flash(void *ctx)
{
    (void)ctx;
    return !locked;
}

static int flash_read(void *ctx, uint32_t addr, uint8_t *dst, uint32_t len)
{
    (void)ctx;
    assert(addr + len <= TOTAL);
    if (read_failed == 1) return 0;
    if (read_failed == 2) { read_failed = 0; return 0; }
    memcpy(dst, memory + addr, len);
    return 1;
}

static int flash_program(void *ctx, uint32_t addr, const uint8_t *src, uint32_t len)
{
    uint32_t i;
    (void)ctx;
    assert(addr + len <= TOTAL && !(addr % 4u) && !(len % 4u));
    ++writes;
    for (i = 0u; i < len; ++i) {
        if (budget == 0) return 0;
        if (budget > 0) --budget;
        assert((memory[addr + i] & src[i]) == src[i]);
        memory[addr + i] &= src[i];
    }
    if (fail_after_event_commit && addr >= 2u * DOMAIN && len == 4u) read_failed = 2;
    if (fail_after_config_commit && addr < DOMAIN && addr % 512u == 24u) read_failed = 1;
    return 1;
}

static int flash_erase(void *ctx, uint32_t addr, uint32_t len)
{
    (void)ctx;
    assert(addr % SECTOR == 0u && len == SECTOR && addr + len <= TOTAL);
    if (addr == fail_erase_at) return 0;
    ++erases;
    memset(memory + addr, 0xFF, len);
    return 1;
}

static const storage_port_t port = {0, SECTOR, 4u, 0xFFu, begin_flash, 0,
                                    flash_read, flash_program, flash_erase};

const storage_port_t *bms_storage_platform_port(void) { return &port; }
int bms_storage_platform_erase_factory_legacy(void)
{
    ++legacy_calls;
    return !fail_legacy_purge;
}

int bms_storage_platform_region(bms_storage_domain_t domain, storage_region_t *region)
{
    region->base = (uint32_t)domain * DOMAIN;
    region->size = DOMAIN;
    return 1;
}

/* 仅测试夹具保存与恢复 RAM 对象，Flash 记录由生产实现读写。 */
#define put32 state_put32
#define get32 state_get32
#define s_ready state_ready
#define s_write_failed state_write_failed
#ifdef D008_TEST_FACTORY
#undef BMS_OTA_UPDATE_FACTORY_RESET
#define BMS_OTA_UPDATE_FACTORY_RESET 1u
#undef BMS_OTA_UPDATE_ID_1
#define BMS_OTA_UPDATE_ID_1 0x1234u
#endif
#include "bms_state_store.c"
#undef s_write_failed
#undef s_ready
#undef get32
#undef put32
#include "bms_event_log.c"
#include "bms_config_store.c"
#include "bms_parameters.c"

static void reboot(void)
{
    state_ready = s_ready = g_bms_config_ready = 0u;
    s_write_failed = state_write_failed = 0u;
    memset(&g_bms_config_store, 0, sizeof(g_bms_config_store));
    memset(&s_saved, 0, sizeof(s_saved));
    memset(&s_store, 0, sizeof(s_store));
    memset(&g_bms_report, 0, sizeof(g_bms_report));
    budget = -1;
    read_failed = locked = fail_after_event_commit = fail_after_config_commit = fail_legacy_purge = 0;
    fail_erase_at = UINT32_MAX;
    clock_ms += 6000u;
}

static void fresh(void)
{
    memset(memory, 0xFF, sizeof(memory));
    writes = erases = reloads = 0u;
    reboot();
}

static void config(void)
{
    bms_soc_limits_t pair, stored;
    bms_config_system_params_t system;
    const u8 valid[] = {0x0D, 0xBB, 0x0B, 0x22}; /* 3515 / 2850 mV */
    const u8 invalid[] = {0x0F, 0xA0, 0x0B, 0x22}; /* 超出 LFP 窗口。 */
    u8 response[4];
    unsigned initial, i;

    fresh();
    assert(bms_config_store_validate_startup());
    assert(g_bms_config_store.slot_size == 512u && erases == 0u);
    assert(bms_config_get_soc_limits(&pair) && pair.full_mv == 3500u && pair.empty_mv == 2850u);
    assert(bms_parameter_read_block(BMS_PARAM_REG_SOC100_MV, 2u, response));
    assert(response[0] == 0x0Du && response[1] == 0xACu);
    assert(bms_parameter_write(BMS_PARAM_REG_SOC100_MV, 1u, valid) == 3u);
    assert(bms_parameter_write(BMS_PARAM_REG_SOC0_MV, 1u, valid) == 2u);
    assert(bms_parameter_write(BMS_PARAM_REG_SOC100_MV, 2u, invalid) == 3u);
    assert(bms_parameter_write(BMS_PARAM_REG_SOC, 1u, valid) == 2u);
    locked = 1;
    assert(bms_parameter_write(BMS_PARAM_REG_SOC100_MV, 2u, valid) == 4u && !reloads);
    locked = 0;
    assert(bms_parameter_write(BMS_PARAM_REG_SOC100_MV, 2u, valid) == 0u && reloads == 1u);
    initial = writes;
    assert(bms_parameter_write(BMS_PARAM_REG_SOC100_MV, 2u, valid) == 0u && writes == initial);
    assert(bms_config_get_soc_limits(&pair));
    assert(pair.full_mv == 3515u && pair.empty_mv == 2850u);
    memcpy(backup, memory, sizeof(memory));
    for (i = 0u; i <= 352u; ++i) {
        memcpy(memory, backup, sizeof(memory));
        reboot();
        assert(bms_config_store_validate_startup());
        assert(bms_config_store_get_system(&system));
        system.capacity_factory = 750u;
        budget = (int)i;
        {
            int ok = bms_config_store_set_system(&system);
            reboot();
            assert(bms_config_store_validate_startup());
            assert(bms_config_store_get_system(&system));
            assert(system.capacity_factory == (ok ? 750u : BMS_PRODUCT_DEFAULT_CAPACITY_0P1AH));
        }
        assert(bms_config_get_soc_limits(&stored) && stored.full_mv == pair.full_mv);
    }
    assert(bms_config_store_get_system(&system));
    system.capacity_factory = 800u;
    fail_after_config_commit = 1;
    assert(!bms_config_store_set_system(&system) && bms_config_store_commit_unknown());
    initial = writes;
    assert(!bms_config_store_set_system(&system) && initial == writes);
    reboot();
    assert(bms_config_store_validate_startup());
    assert(bms_config_store_get_system(&system) && system.capacity_factory == 800u);

    fresh();
    memory[SECTOR + 31u] = 0x00u;
    assert(!bms_config_store_validate_startup() && !writes && !erases);
    fresh();
    read_failed = 1;
    assert(!bms_config_store_validate_startup() && !writes && !erases);
}

static void state(void)
{
    bms_soc_checkpoint_t cp = {1251685u, 82463u, 0u, 600u, 88u, 57u, 0u, 1u};
    bms_soc_checkpoint_t stored;
    unsigned initial, cut;

    fresh();
    assert(bms_state_store_init());
    assert(s_store.slot_size == 64u);
    assert(bms_state_store_save_snapshot(&cp, 0u));
    assert(writes && !erases);
    initial = writes;
    assert(bms_state_store_save_snapshot(&cp, 0u) && writes == initial);
    cp.charge_as10 -= 50000u;
    assert(!bms_state_store_poll_snapshot(&cp, 1u) && writes == initial);
    clock_ms += 300000u;
    assert(bms_state_store_poll_snapshot(&cp, 1u));
    memcpy(backup, memory, sizeof(memory));
    for (cut = 0u; cut <= 64u; ++cut) {
        int saved;
        memcpy(memory, backup, sizeof(memory));
        reboot();
        assert(bms_state_store_init());
        cp.cycles = 89u;
        cp.shown = 56u;
        budget = (int)cut;
        saved = bms_state_store_save_snapshot(&cp, 1u);
        reboot();
        assert(bms_state_store_init());
        stored = bms_state_store_get();
        assert(stored.shown == (saved ? 56u : 57u) && stored.cycles == (saved ? 89u : 88u));
        assert(stored.charge_as10 == 1201685u && stored.discharge_as10 == 82463u);
    }
    fresh();
    memory[DOMAIN + 210u] = 0xB3u;
    assert(!bms_state_store_init() && !bms_state_store_get().capacity_0p1ah);
    assert(!bms_state_store_save_snapshot(&cp, 1u) && !writes && !erases);
}

static void afe_transaction(void)
{
    bms_afe_hw_profile_t profile, observed;
    uint8_t words[70];
    unsigned i, before_writes;

    fresh();
    apply_calls = finish_calls = inhibit_calls = apply_failures = 0u;
    assert(bms_config_store_validate_startup() && bms_config_store_get_afe_hw_profile(&profile));
    last_charge = profile.cov_mv;
    profile.cov_mv += 5u;
    for (i = 0u; i < BMS_AFE_HW_PROFILE_WORD_COUNT; ++i) {
        uint16_t word;
        memcpy(&word, (const u8 *)&profile + i * 2u, sizeof(word));
        words[i * 2u] = (u8)(word >> 8);
        words[i * 2u + 1u] = (u8)word;
    }
    before_writes = writes;
    apply_failures = 1;
    assert(bms_afe_hw_profile_commit_be(words, 35u) == BMS_AFE_HW_ERROR_APPLY_VERIFY);
    assert(apply_calls == 2u && finish_calls == 1u && !inhibit_calls && writes == before_writes);
    assert(last_charge == profile.cov_mv - 5u);

    locked = 1;
    assert(bms_afe_hw_profile_commit_be(words, 35u) == BMS_AFE_HW_ERROR_STORE);
    assert(apply_calls == 4u && finish_calls == 2u && last_charge == profile.cov_mv - 5u);
    locked = 0;
    fail_after_config_commit = 1;
    assert(bms_afe_hw_profile_commit_be(words, 35u) == BMS_AFE_HW_ERROR_STORE_UNKNOWN);
    assert(apply_calls == 5u && finish_calls == 2u && inhibit_calls == 1u);
    assert(bms_config_store_get_afe_hw_profile(&observed) && observed.cov_mv != last_charge);
    reboot();
    assert(bms_config_store_validate_startup());
    assert(bms_config_store_get_afe_hw_profile(&observed) && observed.cov_mv == last_charge);
}

static void add_event(unsigned index)
{
    if (index % 3u == 0u) bms_event_log_note_startup();
    else {
        g_bms_report.fault_third.all = 0u;
        bms_event_log_observe();
        g_bms_report.fault_third.all = index % 3u == 1u ? 0x0001u : 0x0002u;
        bms_event_log_observe();
    }
}

static u16 expected_event(unsigned index)
{
    if (index % 3u == 0u) return BMS_START_UP << 8;
    return (u16)(((index % 3u == 1u ? VCELL_OVP : VCELL_UVP) << 8) | 171u);
}

static void events(void)
{
    unsigned i, cut;

    fresh();
    assert(bms_event_log_init());
    for (i = 0u; i < 530u; ++i) {
        clock_ms += 200u;
        add_event(i);
        if (i == 100u) {
            fail_after_event_commit = 1;
            assert(bms_event_log_service(1u));
            fail_after_event_commit = 0;
        } else assert(bms_event_log_service(1u));
    }
    assert(erases == 1u);
    reboot();
    assert(bms_event_log_init());
    for (i = 0u; i < 100u; ++i)
        assert(bms_event_log_read_reg((u16)i) == expected_event(529u - i));
    assert(!bms_event_log_read_reg(100u));
    memcpy(backup, memory, sizeof(memory));
    for (cut = 0u; cut <= 16u; ++cut) {
        memcpy(memory, backup, sizeof(memory));
        reboot();
        assert(bms_event_log_init());
        add_event(530u);
        budget = (int)cut;
        assert(bms_event_log_service(1u) == (cut == 16u));
        reboot();
        assert(bms_event_log_init());
        assert(bms_event_log_read_reg(0u) == expected_event(cut == 16u ? 530u : 529u));
    }

    fresh();
    clock_ms = UINT32_MAX - 900u;
    assert(bms_event_log_init());
    add_event(1u);
    assert(bms_event_log_service(1u));
    clock_ms += 60099u;
    add_event(1u);
    assert(!bms_event_log_read_reg(1u));
    clock_ms += 1000u;
    add_event(1u);
    assert((bms_event_log_read_reg(0u) & 255u) == 1u);
    assert(bms_event_log_read_reg(1u) == expected_event(1u));
}

static void factory(void)
{
    bms_soc_checkpoint_t cp = {1251685u, 82463u, 0u, 600u, 88u, 57u, 0u, 1u};
    unsigned prior_erases, prior_writes;
    fresh();
    memory[11] = 0x23u; /* 旧 Config 残留。 */
    assert(bms_event_log_init());
    add_event(1u);
    assert(bms_event_log_service(1u));
    assert(bms_state_store_init() && bms_state_store_save_snapshot(&cp, 1u));
    assert(bms_config_store_validate_startup() && bms_config_store_factory_reset_pending());
    assert(bms_config_store_purge_factory_history());
    assert(bms_state_store_factory_reset());
    reboot(); /* State 已提交，Event 和完成标记尚未提交。 */
    assert(bms_config_store_validate_startup() && bms_config_store_factory_reset_pending());
    prior_erases = erases;
    prior_writes = writes;
    assert(bms_config_store_purge_factory_history() && bms_state_store_factory_reset());
    assert(erases == prior_erases && writes == prior_writes);
    fail_erase_at = 2u * DOMAIN;
    bms_parameters_init();
    assert(!bms_protection_params_valid() && bms_config_store_factory_reset_pending());
    assert(bms_error_get(BMS_ERROR_EEPROM_STORE));
    assert(erases == prior_erases && writes == prior_writes && legacy_calls == 0u);

    reboot();
    fail_legacy_purge = 1;
    bms_parameters_init();
    assert(!bms_protection_params_valid() && bms_config_store_factory_reset_pending());
    assert(legacy_calls == 1u && erases == prior_erases + 1u && writes == prior_writes);

    reboot();
    bms_parameters_init();
    assert(bms_protection_params_valid() && !bms_config_store_factory_reset_pending());
    assert(legacy_calls == 2u);
    assert(bms_state_store_init() && !bms_state_store_get().capacity_0p1ah);
    assert(bms_event_log_init() && !bms_event_log_read_reg(0u));
    prior_erases = erases;
    prior_writes = writes;
    reboot();
    bms_parameters_init();
    assert(bms_protection_params_valid() && erases == prior_erases && writes == prior_writes && legacy_calls == 2u);
}

static void boot_qualification(void)
{
    fresh();
    bms_error_clear(BMS_ERROR_EEPROM_STORE);
    bms_parameters_init();
    assert(bms_protection_params_valid() && !bms_error_get(BMS_ERROR_EEPROM_STORE));
    memory[DOMAIN + 11u] = 0x23u;
    reboot();
    bms_parameters_init();
    assert(bms_protection_params_valid() && bms_error_get(BMS_ERROR_EEPROM_STORE));
    assert(!bms_state_store_get().capacity_0p1ah);

    fresh();
    memory[11] = 0x23u;
    bms_parameters_init();
    assert(!bms_protection_params_valid() && bms_error_get(BMS_ERROR_EEPROM_STORE));
    assert(!writes && !erases && !legacy_calls);
}

int main(void)
{
    if (bms_ota_factory_reset_requested()) factory();
    else {
        config();
        state();
        events();
        afe_transaction();
        boot_qualification();
    }
    puts("PASS D008 Config/State power cuts, write acknowledgement and last 100 events");
    return 0;
}
