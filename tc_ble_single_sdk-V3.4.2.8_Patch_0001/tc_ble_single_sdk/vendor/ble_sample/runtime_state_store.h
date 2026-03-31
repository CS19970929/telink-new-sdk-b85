#ifndef RUNTIME_STATE_STORE_H_
#define RUNTIME_STATE_STORE_H_

#include "tl_common.h"
#include "flash_storage_types.h"

#ifdef __cplusplus
extern "C" {
#endif

int runtime_state_store_init(void);
int runtime_state_store_load(flash_runtime_state_t *state);
int runtime_state_store_save(const flash_runtime_state_t *state);
int runtime_state_store_save_soc_snapshot(u16 soc, u16 dsg_int, u16 cycle);
int runtime_state_store_log_event(const flash_event_record_t *event_rec);
int runtime_state_store_factory_reset(void);

#ifdef __cplusplus
}
#endif

#endif
