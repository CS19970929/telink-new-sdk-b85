#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "sh3673520_reg.h"

typedef struct { uint32_t unused; } bms_afe_aux_measurements_t;
typedef struct { uint8_t flag1, flag2, bstatus1, bstatus2; } sh3673510_control_status_t;
typedef struct { uint8_t last_error; } sh3673520_comm_stats_t;
enum { BMS_ERROR_SPI, BMS_ERROR_AFE1, BMS_ERROR_DSG_SHORT, BMS_ERROR_CBC_DSG };
enum { SH3673520_ERR_SPI, SH3673520_ERR_TIMEOUT, SH3673520_ERR_CRC, SH3673520_ERR_PROTOCOL };
static struct { struct { uint8_t b1Status_AFE1, b1Status_MOS_CHG, b1Status_MOS_DSG; } bits; } g_bms_system_status;
static uint8_t errors[4];
static unsigned failures, clears, writes;
static uint8_t clear_ok, write_ok, init_ok, actual_c, actual_d;
static uint8_t bms_error_get(unsigned e) { return errors[e]; }
static void bms_error_raise(unsigned e) { errors[e] = 1; }
static void bms_error_clear(unsigned e) { errors[e] = 0; }
static void SH3673520_GetCommStats(sh3673520_comm_stats_t *s) { s->last_error = SH3673520_ERR_CRC; }
static uint8_t bms_sw_protection_charge_blocked(void) { return 0; }
static uint8_t bms_sw_protection_discharge_blocked(void) { return 0; }
static uint8_t bms_features_charge_direction_blocked(void) { return 0; }
static uint8_t sh3673510_control_ready(void) { return 1; }
static uint8_t sh3673510_control_set_fets(uint8_t c, uint8_t d) {
    ++writes;
    if (!write_ok) return 0;
    actual_c = c; actual_d = d; return 1;
}
static uint8_t sh3673510_control_set_balance(uint16_t mask) { (void)mask; return 1; }
static uint8_t sh3673510_control_init(void) { actual_c = actual_d = 0; return init_ok; }
static uint8_t sh3673510_control_clear_flag1(uint8_t mask) {
    if (mask & SH3673520_FLAG1_SC_MASK) ++clears;
    return clear_ok;
}
static uint8_t sh3673510_control_clear_flag2(uint8_t mask) { (void)mask; return clear_ok; }
static void sh3673510_board_force_heater_fuse_safe(void) {}
static void sh3673510_control_sleep(void) {}

/* PRODUCTION */

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
static void reset(void) {
    memset(errors, 0, sizeof(errors));
    memset(s_hw_recovery_count, 0, sizeof(s_hw_recovery_count));
    s_short_latched = s_short_clear_pending = 0;
    s_short_release_count = 0;
    s_hw_charge_protect = s_hw_discharge_protect = s_hw_afe_error = 0;
    s_snapshot_valid = s_output_enabled = 1;
    s_output_inhibit = s_afe_reconfigure_required = 0;
    s_fet_command_valid = 0;
    s_requested_charge_on = s_requested_discharge_on = 1;
    s_bstatus2 = 0;
    clears = writes = 0;
    clear_ok = write_ok = init_ok = 1;
}
static void sample(uint8_t sc, uint8_t removed) {
    sh3673510_control_status_t status = {0, 0, 0, 0};
    status.flag1 = sc ? SH3673520_FLAG1_SC_MASK : 0;
    status.bstatus2 = removed ? SH3673520_BSTATUS2_LOADOFF_MASK : 0;
    publish_hw_status(&status);
#if SH3673510_HW_PROTECT_ENABLE
    service_short_recovery(&status);
#endif
}
static void short_recovery(void) {
#if SH3673510_HW_PROTECT_ENABLE
    unsigned i;
    reset();
    for (i = 0; i < 20; ++i) sample(1, 0);
    CHECK(clears == 0 && s_short_latched);
    for (i = 0; i < 9; ++i) sample(1, 1);
    CHECK(clears == 0);
    sample(1, 1);
    CHECK(clears == 1 && s_short_clear_pending && s_short_latched);
    sample(0, 1);
    CHECK(!s_short_latched && !errors[BMS_ERROR_DSG_SHORT]);

    /* A failed clear must not release the latch; later readback is mandatory. */
    reset(); clear_ok = 0;
    for (i = 0; i < 10; ++i) sample(1, 1);
    CHECK(clears == 1 && !s_short_clear_pending && s_short_latched);
    clear_ok = 1; sample(1, 1);
    CHECK(s_short_clear_pending && s_short_latched);
    sample(1, 1); /* hardware still asserts SC */
    CHECK(s_short_latched && !s_short_clear_pending);

    /* Load reattachment cancels a pending clear's qualification. */
    reset();
    for (i = 0; i < 10; ++i) sample(1, 1);
    sample(0, 0);
    CHECK(s_short_latched && !s_short_clear_pending);
    for (i = 0; i < 9; ++i) sample(0, 1);
    CHECK(s_short_latched && clears == 1);
    sample(0, 1); sample(0, 1);
    CHECK(!s_short_latched && clears == 2);

    /* Every possible gap in the release window restarts a full window. */
    for (unsigned gap = 1; gap <= 10; ++gap) {
        reset();
        for (i = 0; i < gap; ++i) sample(1, 1);
        unsigned before = clears;
        note_comm_error();
        CHECK(s_short_latched && !s_short_clear_pending && s_short_release_count == 0);
        for (i = 0; i < 9; ++i) sample(0, 1);
        CHECK(s_short_latched && clears == before);
        sample(0, 1); sample(0, 1);
        CHECK(!s_short_latched && clears == before + 1);
    }
#else
    reset(); sample(1, 1);
    CHECK(!s_short_latched && clears == 0);
#endif
}
static void recovery_continuity(void) {
    unsigned i, id;
    reset();
    for (id = 0; id < HW_REC_COUNT; ++id)
        for (i = 0; i < 9; ++i) CHECK(!hw_recovery_stable((sh3510_hw_recovery_id_t)id, 1, 200));
    note_comm_error();
    for (id = 0; id < HW_REC_COUNT; ++id)
        CHECK(!hw_recovery_stable((sh3510_hw_recovery_id_t)id, 1, 200));
    s_short_release_count = 9; s_short_clear_pending = 1; s_short_latched = 1;
    sh3673510_bms_afe_sleep();
    CHECK(s_short_release_count == 0 && !s_short_clear_pending && s_short_latched);
}
static void fet_cache(void) {
    unsigned before;
    reset();
    CHECK(sh3510_apply_requested_fets()); CHECK(actual_c && actual_d);
    before = writes;
    CHECK(sh3510_apply_requested_fets()); CHECK(writes == before);
    sh3673510_bms_afe_set_output_enabled(0);
    CHECK(!actual_c && !actual_d);
    sh3673510_bms_afe_set_output_enabled(1);
    CHECK(actual_c && actual_d);

    reset(); CHECK(sh3510_apply_requested_fets());
    s_afe_reconfigure_required = 1;
    CHECK(service_afe_reconfiguration());
    CHECK(!s_fet_command_valid);
    /* Emulate subsequent fresh-snapshot qualification by the sample owner. */
    s_snapshot_valid = 1; s_output_inhibit = 0;
    CHECK(sh3510_apply_requested_fets()); CHECK(actual_c && actual_d);
    write_ok = 0;
    sh3673510_bms_afe_set_output_enabled(0);
    CHECK(!s_fet_command_valid && s_output_inhibit);
}
int main(void) {
    short_recovery(); recovery_continuity(); fet_cache();
    printf("SH recovery/FET HW=%d: %u failures\n", SH3673510_HW_PROTECT_ENABLE, failures);
    return failures ? 1 : 0;
}
