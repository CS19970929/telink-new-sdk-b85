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

typedef struct {
    u32 soc;
    u32 dsg;
    u32 cycle;
    u32 learned_capacity_0p1ah;
    u32 flags;
} bms_state_store_data_t;

int  bms_state_store_init(void);
bms_state_store_data_t bms_state_store_get_default_data(void);
bms_state_store_data_t bms_state_store_get(void);
int  bms_state_store_write_all(u32 soc, u32 dsg, u32 cycle);
/* Queue learning in RAM; periodic checkpoint and write_all flush include it. */
int  bms_state_store_write_learning(u32 learned_capacity_0p1ah, u32 flags);
void bms_state_store_update_and_log_if_changed(u32 soc, u32 dsg, u32 cycle);
u32 bms_state_store_get_runtime_min(void);
int bms_state_store_write_runtime_min(u32 runtime_min);
int bms_state_store_reset_runtime(void);

#ifdef __cplusplus
}
#endif
