#pragma once
#include "../registers.h"
extern bool hil_awake;
extern uint32_t hil_pm_exits, hil_samples, hil_missed, hil_last_ms, hil_largest_ticks;
void hil_sample(uint32_t tick,uint32_t ms);
unsigned hil_command(RtuSession *owner,const uint8_t *req,unsigned n,uint8_t *out,uint32_t ms);
bool hil_read(unsigned offset,uint16_t *v);
unsigned hil_control(unsigned offset,unsigned value);
