#pragma once
#include <stdint.h>
#include <stdbool.h>
bool uart_update_busy(void);
unsigned uart_update_frame(const uint8_t *p,unsigned n,uint8_t *out,uint32_t ms);
void uart_update_poll(uint32_t ms);
bool update_layout(void);
void update_lock(bool lock);
