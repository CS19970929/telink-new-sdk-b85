#pragma once

/* Storage V1 State domain: SOC/cycle/learned-capacity/runtime checkpoints. */

#include "tl_common.h"
#include "conf.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BMS_STATE_DEFAULT_SOC
#define BMS_STATE_DEFAULT_SOC    ((u32)FAC_INIT_soc)
#endif
#ifndef BMS_STATE_DEFAULT_DSG
#define BMS_STATE_DEFAULT_DSG    0u
#endif
#ifndef BMS_STATE_DEFAULT_CYCLE
#define BMS_STATE_DEFAULT_CYCLE  0u
#endif
#ifndef BMS_STATE_DEFAULT_LEARNED_CAPACITY
#define BMS_STATE_DEFAULT_LEARNED_CAPACITY 0u
#endif
#ifndef BMS_STATE_DEFAULT_FLAGS
#define BMS_STATE_DEFAULT_FLAGS 0u
#endif

#define BMS_STATE_FLAG_CAPACITY_LEARNED 0x00000001u
#define BMS_STATE_FLAG_LEARNING_META    0x00000002u
#define BMS_STATE_FLAG_LEARNING_ACTIVE  0x00000004u
#define BMS_STATE_FLAG_LOW_MASK         0x0000FFFFu
#define BMS_STATE_FLAG_NOMINAL_MASK     0xFFFF0000u
#define BMS_STATE_FLAG_NOMINAL_SHIFT    16u

typedef struct {
    u32 soc;
    u32 dsg;
    u32 cycle;
    u32 learned_capacity_0p1ah;
    u32 flags;
    u32 candidate_capacity_0p1ah;
    u32 valid_learning_count;
    u32 rejected_learning_count;
    u32 last_learning_reject_reason;
    u32 candidate_match_count;
} bms_state_store_data_t;

int  bms_state_store_init(void);
bms_state_store_data_t bms_state_store_get_default_data(void);
bms_state_store_data_t bms_state_store_get(void);
int  bms_state_store_write_all(u32 soc, u32 dsg, u32 cycle);
/* Queue learning in RAM; periodic checkpoint and write_all flush include it. */
int  bms_state_store_write_learning(u32 learned_capacity_0p1ah, u32 flags);
int  bms_state_store_write_learning_meta(u32 learned_capacity_0p1ah, u32 flags,
                                         u32 candidate_capacity_0p1ah,
                                         u32 valid_learning_count,
                                         u32 rejected_learning_count,
                                         u32 last_learning_reject_reason,
                                         u32 candidate_match_count);
void bms_state_store_update_and_log_if_changed(u32 soc, u32 dsg, u32 cycle);
u32 bms_state_store_get_runtime_min(void);
int bms_state_store_write_runtime_min(u32 runtime_min);
int bms_state_store_reset_runtime(void);

int bms_state_store_set_soc_cycle(u32 soc, u32 dsg, u32 cycle);

#ifdef __cplusplus
}
#endif
