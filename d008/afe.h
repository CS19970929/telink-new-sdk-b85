#pragma once
#include "product.h"

typedef enum { AFE_WAIT, AFE_OK, AFE_ERROR } AfeResult;
void afe_begin(const Config *config);
AfeResult afe_setup_step(void);
bool afe_setup_safe(void);
bool afe_confirm_off(uint32_t ms);
AfeResult afe_sample(const Config *config, uint32_t ms, Sample *out);
bool afe_outputs(uint8_t drive, uint32_t balance, uint32_t ms);
bool afe_disable_outputs(uint32_t ms);
bool afe_ack(uint8_t flags, uint8_t *remaining);
bool afe_shutdown(void);
