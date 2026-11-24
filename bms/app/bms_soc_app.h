#pragma once

#include "bms_afe.h"
#include "bms_soc.h"

void bms_soc_app_init(void);
void bms_soc_app_sample(const bms_afe_aux_measurements_t *measurement);
void bms_soc_app_reload(void);
bms_soc_checkpoint_t bms_soc_app_checkpoint(void);
uint8_t bms_soc_app_percent(void);
