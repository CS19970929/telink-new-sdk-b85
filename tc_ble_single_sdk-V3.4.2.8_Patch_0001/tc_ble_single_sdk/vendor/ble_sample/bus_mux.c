#include "bus_mux.h"

#include "tl_common.h"
#include "drivers.h"
#include "modbus_uart.h"
#include "conf.h"

#define OWC_RX_PIN_LOCAL             OWC_RX_PIN
#define OWC_TX_PIN_LOCAL             OWC_TX_PIN

#define OWC_RX_HIGH_STABLE_US        50000u
#define UART_DETECT_WINDOW_US        18000u
#define UART_FALL_MIN_COUNT          3u
#define UART_IDLE_TIMEOUT_US         5000000u

static volatile bus_state_t s_state = BUS_STATE_OWC_IDLE;
static volatile uint16_t s_uart_fall_count;
static volatile uint32_t s_uart_detect_start_tick;
static uint32_t s_owc_rx_high_since;
static volatile uint32_t s_last_uart_rx_tick;

static int bus_mux_rx_is_high(void)
{
    return gpio_read(OWC_RX_PIN_LOCAL) ? 1 : 0;
}

static void bus_mux_start_owc_tx(void)
{
    gpio_set_func(OWC_TX_PIN_LOCAL, AS_GPIO);
    gpio_set_input_en(OWC_TX_PIN_LOCAL, 0);
    gpio_set_output_en(OWC_TX_PIN_LOCAL, 1);
    gpio_write(OWC_TX_PIN_LOCAL, 1);
    gpio_en_interrupt_risc0(OWC_RX_PIN_LOCAL, 0);
}

static void bus_mux_start_owc_listen(void)
{
    gpio_set_func(OWC_RX_PIN_LOCAL, AS_GPIO);
    gpio_set_input_en(OWC_RX_PIN_LOCAL, 1);
    gpio_set_output_en(OWC_RX_PIN_LOCAL, 0);
    gpio_setup_up_down_resistor(OWC_RX_PIN_LOCAL, PM_PIN_PULLDOWN_100K);

    gpio_set_func(OWC_TX_PIN_LOCAL, AS_GPIO);
    gpio_set_input_en(OWC_TX_PIN_LOCAL, 0);
    gpio_set_output_en(OWC_TX_PIN_LOCAL, 0);
    gpio_write(OWC_TX_PIN_LOCAL, 0);
    gpio_setup_up_down_resistor(OWC_TX_PIN_LOCAL, PM_PIN_UP_DOWN_FLOAT);

    gpio_set_interrupt_risc0(OWC_RX_PIN_LOCAL, POL_FALLING);
    s_uart_fall_count = 0u;
    s_uart_detect_start_tick = clock_time();
    gpio_en_interrupt_risc0(OWC_RX_PIN_LOCAL, 1);
}

static void bus_mux_enter_owc_idle(void)
{
    bus_mux_start_owc_listen();
    s_owc_rx_high_since = 0u;
    s_state = BUS_STATE_OWC_IDLE;
}

static void bus_mux_enter_owc_tx(void)
{
    bus_mux_start_owc_tx();
    s_state = BUS_STATE_OWC_TX;
}

static void bus_mux_enter_uart_modbus(void)
{
    gpio_en_interrupt_risc0(OWC_RX_PIN_LOCAL, 0);
    modbus_uart_init();
    s_last_uart_rx_tick = clock_time();
    s_state = BUS_STATE_UART_MODBUS;
}

bus_state_t bus_mux_get_state(void)
{
    return s_state;
}

void bus_mux_set_state(bus_state_t state)
{
    if (state == BUS_STATE_OWC_IDLE)
    {
        bus_mux_enter_owc_idle();
        return;
    }

    s_state = state;
}

void bus_mux_on_uart_rx_byte(void)
{
    s_last_uart_rx_tick = clock_time();
}

_attribute_ram_code_ void bus_mux_irq_handler(void)
{
    if (reg_irq_src & FLD_IRQ_GPIO_RISC0_EN)
    {
        uint32_t now;

        reg_irq_src = FLD_IRQ_GPIO_RISC0_EN;
        if (s_state != BUS_STATE_OWC_IDLE)
        {
            return;
        }

        now = clock_time();
        if (clock_time_exceed(s_uart_detect_start_tick, UART_DETECT_WINDOW_US))
        {
            s_uart_detect_start_tick = now;
            s_uart_fall_count = 0u;
        }
        ++s_uart_fall_count;
    }
}

void bus_mux_init(void)
{
    bus_mux_enter_owc_idle();
}

void bus_mux_task(void)
{
    if ((s_state == BUS_STATE_OWC_IDLE) &&
        (s_uart_fall_count >= UART_FALL_MIN_COUNT))
    {
        bus_mux_enter_uart_modbus();
        return;
    }

    if (s_state == BUS_STATE_OWC_IDLE)
    {
        if (bus_mux_rx_is_high())
        {
            if (s_owc_rx_high_since == 0u)
            {
                s_owc_rx_high_since = clock_time();
            }
            if (clock_time_exceed(s_owc_rx_high_since, OWC_RX_HIGH_STABLE_US))
            {
                bus_mux_enter_owc_tx();
                return;
            }
        }
        else
        {
            s_owc_rx_high_since = 0u;
        }
    }

    if ((s_state == BUS_STATE_UART_MODBUS) &&
        clock_time_exceed(s_last_uart_rx_tick, UART_IDLE_TIMEOUT_US))
    {
        bus_mux_enter_owc_idle();
    }
}
