#pragma once

#include "tl_common.h"

void modbus_uart_init(void);
void modbus_uart_irq_proc(void);
int modbus_uart_poll(u8 **data, u32 *len);
void modbus_uart_send(const u8 *data, u32 len);
void main_loop_modbus(void);
