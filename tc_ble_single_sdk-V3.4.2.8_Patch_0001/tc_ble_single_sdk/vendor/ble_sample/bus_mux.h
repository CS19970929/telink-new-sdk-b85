#pragma once
#include <stdint.h>

typedef enum {
    BUS_STATE_OWC_IDLE = 0,
    BUS_STATE_OWC_TX,
    BUS_STATE_UART_MODBUS,
} bus_state_t;

void bus_mux_init(void);
void bus_mux_task(void);

// 放到中断入口里调用（或直接把下面 handler 代码合并到你的 irq_handler）
void bus_mux_irq_handler(void);

// UART RX 收到任意字节时调用
void bus_mux_on_uart_rx_byte(void);

bus_state_t bus_mux_get_state(void);

// 业务对接：你工程里实现
void owc_start_tx_only(void);
void owc_stop_tx(void);

void modbus_uart_slave_enable(void);
void modbus_uart_slave_disable(void);
