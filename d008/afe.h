#pragma once
#include "product.h"

typedef enum { AFE_WAIT, AFE_OK, AFE_ERROR } AfeResult;
void afe_begin(const Config *config);
AfeResult afe_setup_step(void);
AfeResult afe_sample(const Config *config, uint32_t ms, Sample *out);
bool afe_outputs(uint8_t drive, uint32_t balance, uint32_t ms);
bool afe_ack(uint8_t flags);
bool afe_openwire(bool on);
bool afe_shutdown(void);
