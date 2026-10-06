#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
/* PROFILE_HEADER */
#define MB_EXCEPTION_NONE 0u
#define MB_EX_ILLEGAL_ADDRESS 2u
#define MB_EX_ILLEGAL_VALUE 3u
#define MB_EX_DEVICE_FAILURE 4u

static bms_afe_hw_profile_t stored;
static unsigned get_count, set_count, apply_count, effective_count, close_count;
static unsigned fail_get, fail_set, fail_apply, fail_effective, mismatch_get;
static u8 session_active, candidate_valid;
static char trace[64];
static unsigned trace_size;
static void event(char e) { assert(trace_size + 1u < sizeof(trace)); trace[trace_size++] = e; }
u8 bms_afe_hw_access_is_active(void) { event('A'); return session_active; }
void bms_afe_hw_access_close(void) { event('C'); session_active = 0u; ++close_count; }
u8 bms_afe_hw_profile_get(bms_afe_hw_profile_t *p) {
    event('G'); ++get_count;
    if (get_count == fail_get) return 0u;
    *p = stored;
    if (get_count == mismatch_get) p->cov_mv ^= 1u;
    return 1u;
}
u8 bms_afe_hw_profile_set(const bms_afe_hw_profile_t *p) {
    event('S'); ++set_count;
    if (set_count == fail_set) return 0u;
    stored = *p;
    return 1u;
}
u8 bms_afe_hw_profile_validate_write(const bms_afe_hw_profile_t *p) {
    event('V'); assert(p->cov_mv == 4200u); return candidate_valid;
}
u8 bms_afe_apply_protection_config(void) {
    event('P'); ++apply_count; return apply_count != fail_apply;
}
void bms_afe_invalidate_configuration(void) {}
u8 bms_afe_hw_profile_get_effective(bms_afe_hw_profile_t *p) {
    event('E'); ++effective_count; *p = stored; return effective_count != fail_effective;
}
/* PRODUCTION_SOURCE */

typedef struct {
    unsigned get_fail, set_fail, apply_fail, effective_fail, get_mismatch;
    u8 active, valid, null_payload;
    u16 qty, exception, error, state;
    unsigned closed;
    const char *events;
} scenario_t;
#define KEEP_STATE 0xffffu
static const scenario_t scenarios[] = {
    {0,0,0,0,0,1,1,0,35,0,0,1,1,"AGVSPGEC"},
    {0,0,0,0,0,0,1,1,34,2,1,KEEP_STATE,0,"A"},
    {0,0,0,0,0,1,1,1,35,3,2,KEEP_STATE,0,"A"},
    {0,0,0,0,0,1,1,0,34,3,2,KEEP_STATE,0,"A"},
    {1,0,0,0,0,1,1,0,35,4,3,KEEP_STATE,0,"AG"},
    {0,0,0,0,0,1,0,0,35,3,2,KEEP_STATE,0,"AGV"},
    {0,1,0,0,0,1,1,0,35,4,3,0,0,"AGVS"},
    {0,0,1,0,0,1,1,0,35,4,4,2,0,"AGVSPSPGE"},
    {2,0,0,0,0,1,1,0,35,4,4,2,0,"AGVSPGSPGE"},
    {0,0,0,0,2,1,1,0,35,4,4,2,0,"AGVSPGSPGE"},
    {0,0,0,1,0,1,1,0,35,4,4,2,0,"AGVSPGESPGE"},
    {0,2,1,0,0,1,1,0,35,4,5,3,1,"AGVSPSC"},
    {0,0,2,1,0,1,1,0,35,4,5,3,1,"AGVSPGESPC"},
    {3,0,0,1,0,1,1,0,35,4,5,3,1,"AGVSPGESPGC"},
    {0,0,0,1,3,1,1,0,35,4,5,3,1,"AGVSPGESPGC"},
    {0,0,1,1,0,1,1,0,35,4,5,3,1,"AGVSPSPGEC"},
};

int main(void) {
    unsigned i, prior_state;
    u8 payload[70];
    for (i = 0u; i < sizeof(payload); i += 2u) {
        payload[i] = 0x12u; payload[i + 1u] = 0x34u;
    }
    payload[4] = 0x10u; payload[5] = 0x68u; /* cov_mv=4200, BE */
    for (prior_state = 0u; prior_state <= 3u; ++prior_state) {
        for (i = 0u; i < sizeof(scenarios)/sizeof(scenarios[0]); ++i) {
            const scenario_t *s = &scenarios[i];
            u8 exception;
            memset(&stored, 0, sizeof(stored)); stored.cov_mv = 4100u;
            memset(trace, 0, sizeof(trace)); trace_size = 0u;
            get_count = set_count = apply_count = effective_count = close_count = 0u;
            fail_get = s->get_fail; fail_set = s->set_fail; fail_apply = s->apply_fail;
            fail_effective = s->effective_fail; mismatch_get = s->get_mismatch;
            session_active = s->active; candidate_valid = s->valid;
            s_afe_hw_apply_state = (u16)prior_state;
            s_afe_hw_last_error = BMS_AFE_HW_ERROR_NONE;
            exception = afe_hw_profile_write_block(s->null_payload ? 0 : payload, s->qty);
            assert(exception == s->exception);
            assert(s_afe_hw_last_error == s->error);
            assert(s_afe_hw_apply_state == (s->state == KEEP_STATE ? prior_state : s->state));
            assert(close_count == s->closed);
            assert(session_active == (s->closed ? 0u : s->active));
            assert(strcmp(trace, s->events) == 0);
            /* Successful apply publishes candidate; successful rollback restores old words. */
            if (s->exception == 0u) assert(stored.cov_mv == 4200u);
            if (s->state == BMS_AFE_HW_APPLY_ROLLBACK_OK) assert(stored.cov_mv == 4100u);
            printf("%u/%u:%u/%u/%u/%u/%u/%u:%s\n", prior_state, i, exception,
                s_afe_hw_apply_state, s_afe_hw_last_error, close_count, session_active,
                stored.cov_mv, trace);
        }
    }
    puts("AFE hardware transaction: 64 fault/state scenarios PASS");
    return 0;
}
