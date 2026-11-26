#pragma once
#include <stdbool.h>
#include <stdint.h>
extern volatile bool wire_probe_requested,wire_probe_frame;
unsigned wire_probe_arm(unsigned key,uint32_t ms);
void wire_probe_expire(uint32_t ms);
void wire_probe_begin(const uint8_t *frame);
void wire_probe_tick(uint32_t tick,bool level);
void wire_probe_end(bool level);
bool wire_probe_read(unsigned index,uint16_t *word);
