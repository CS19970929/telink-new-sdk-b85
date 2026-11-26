#pragma once
#include <stdint.h>
#include <stdbool.h>
bool update_active(void);
void product_radio_init(void);
void product_radio_poll(uint32_t ms);
void wired_init(void);
void wired_irq(void);
void wired_poll(uint32_t ms);
bool wired_busy(void);
void wired_prepare_suspend(void);
void wired_stop(void);
uint16_t rtu_reply(const uint8_t *req,unsigned n,uint8_t *reply,uint32_t ms);
