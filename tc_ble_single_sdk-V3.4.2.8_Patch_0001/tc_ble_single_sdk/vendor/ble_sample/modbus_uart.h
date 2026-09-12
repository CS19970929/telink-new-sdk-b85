#pragma once
#include "tl_common.h"

void modbus_uart_init(void);
void modbus_uart_irq_proc(void);     // 放到 DMA IRQ 里调用
int  modbus_uart_poll(u8 **p, u32 *len);  // 主循环取一帧
void modbus_uart_send(const u8 *p, u32 len);
void main_loop_modbus(void);
