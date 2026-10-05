#pragma once
#include "tl_common.h"

/* Inspect g_bms_rs485_tx_diag in a debugger; counters wrap naturally at u32. */
typedef struct {
    u32 tx_start_count;
    u32 tx_dma_done_count;
    u32 tx_uart_done_count;
    u32 tx_complete_count;
    u32 tx_timeout_count;
    u32 tx_busy_reject_count;
    u32 last_tx_len;
    u8 de_state;
    u8 pattern_index;
} bms_rs485_tx_diag_t;

extern volatile bms_rs485_tx_diag_t g_bms_rs485_tx_diag;

void modbus_uart_init(void);
void modbus_uart_irq_proc(void);     // 放到 DMA IRQ 里调用
/* Returned RX buffer is stable until main_loop_modbus rearms DMA. */
int  modbus_uart_poll(u8 **p, u32 *len);
u8 modbus_uart_send(const u8 *p, u32 len);
u8 modbus_uart_tx_active(void);
void main_loop_modbus(void);
