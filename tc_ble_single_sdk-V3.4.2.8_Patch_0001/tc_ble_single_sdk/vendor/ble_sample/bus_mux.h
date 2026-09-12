#pragma once

typedef enum
{
    BUS_STATE_OWC_IDLE = 0,
    BUS_STATE_OWC_TX,
    BUS_STATE_UART_MODBUS,
} bus_state_t;

void bus_mux_init(void);
void bus_mux_task(void);
void bus_mux_irq_handler(void);
void bus_mux_on_uart_rx_byte(void);
bus_state_t bus_mux_get_state(void);
void bus_mux_set_state(bus_state_t state);
