#pragma once
#include <stdbool.h>
#include <stdint.h>
bool history_note(unsigned id,uint32_t ms);
bool history_observe(uint16_t active,uint32_t ms);
bool history_clear(uint32_t update_id,uint32_t factory_id);
bool history_flush(uint32_t ms);
