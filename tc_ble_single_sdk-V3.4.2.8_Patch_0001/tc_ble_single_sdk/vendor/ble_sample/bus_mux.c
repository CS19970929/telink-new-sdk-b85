#include "bus_mux.h"
#include "modbus_uart.h"

/*
 * D011/D013 no longer multiplex the SCI pins with a one-wire/SIF protocol.
 * Keep this tiny compatibility layer because app.c/main.c still reference the
 * historical bus_mux API, but the bus is permanently Modbus UART.
 */
static bus_state_t s_bus_state = BUS_STATE_UART_MODBUS;

void bus_mux_init(void)
{
    modbus_uart_init();
    s_bus_state = BUS_STATE_UART_MODBUS;
}

void bus_mux_task(void)
{
    /* Fixed Modbus UART: no protocol detection or runtime switching. */
}

_attribute_ram_code_ void bus_mux_irq_handler(void)
{
    /* No GPIO edge detector is used when the UART is permanently selected. */
}

void bus_mux_on_uart_rx_byte(void)
{
    /* Legacy no-op. UART is already the permanent communication interface. */
}

bus_state_t bus_mux_get_state(void)
{
    return s_bus_state;
}

void bus_mux_return_to_owc_idle(void)
{
    /* One-wire/SIF mode is intentionally unavailable on D011/D013. */
    s_bus_state = BUS_STATE_UART_MODBUS;
}
