#pragma once
#include <stdint.h>
#include <stdbool.h>
bool update_active(void);
void product_radio_init(void);
void product_radio_poll(uint32_t ms);
bool product_radio_reply_pending(void);
void wired_init(void);
void wired_irq(void);
void wired_poll(uint32_t ms);
bool wired_busy(void);
bool wired_transmitting(void);
bool wired_reply_pending(void);
void wired_pause_frame(void);
uint32_t wired_wakeup(uint32_t sample_tick);
void wired_prepare_suspend(void);
void wired_stop(void);
