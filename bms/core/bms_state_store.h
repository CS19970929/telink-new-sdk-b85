#pragma once

#include "bms_soc.h"

int bms_state_store_init(void);
int bms_state_store_factory_reset(void);
bms_soc_checkpoint_t bms_state_store_get(void);
int bms_state_store_save_snapshot(const bms_soc_checkpoint_t *checkpoint);
void bms_state_store_poll_snapshot(const bms_soc_checkpoint_t *checkpoint);
