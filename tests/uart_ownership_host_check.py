"""在 DMA 和 IRQ 替身上验证 D008 UART 与一线通的共享管脚。"""
from validation_support import read, without_includes, run_c

prefix = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef uint8_t u8;
typedef uint32_t u32;
#define _attribute_data_retention_
#define MODBUS_RTU_FRAME_CAPACITY 268u
#define CLOCK_SYS_CLOCK_HZ 16000000u
#define BMS_BOARD_OWC_TX_PIN 1
#define BMS_BOARD_OWC_RX_PIN 2
#define FLD_DMA_CHN_UART_RX 1
#define FLD_DMA_CHN_UART_TX 2
#define FLD_IRQ_DMA_EN 1
#define PARITY_NONE 0
#define STOP_BIT_ONE 1
#define BUS_STATE_UART_MODBUS 2

static u32 now;
static int bus_mode = BUS_STATE_UART_MODBUS, rx_high = 1;
static int busy, channels, irqsrc, sends, parsed, received;
static u8 irq_enabled = 1;
static u8 ota_busy;

static u32 clock_time(void) { return now; }
static int clock_time_exceed(u32 t, u32 us) { return (u32)(now-t) > us*16u; }
static int gpio_read(int pin) { assert(pin == BMS_BOARD_OWC_RX_PIN); return rx_high; }
static void uart_gpio_set(int tx, int rx) { assert(tx == 1 && rx == 2); }
static int uart_tx_is_busy(void) { return busy; }
static void uart_reset(void) { busy = 0; }
static void uart_ndma_clear_tx_index(void) {}
static void uart_ndma_clear_rx_index(void) {}
static void uart_init(int div, int bwpc, int parity, int stop)
{
    assert(div == 9 && bwpc == 13 && parity == 0 && stop == 1);
}
static void uart_dma_enable(int rx, int tx) { assert(rx == tx); }
static void uart_irq_enable(int rx, int tx) { assert(!rx && !tx); }
static void uart_recbuff_init(u8 *p, unsigned bytes) { assert(p && bytes == 272u); channels |= 1; }
static void uart_send_dma(u8 *p) { assert(p); ++sends; busy = 1; }
static void irq_set_mask(int m) { assert(m == FLD_IRQ_DMA_EN); }
static u8 irq_disable(void) { u8 previous = irq_enabled; irq_enabled = 0; return previous; }
static void irq_restore(u8 state) { irq_enabled = state; }
static int dma_chn_irq_status_get(void) { return irqsrc; }
static void dma_chn_irq_status_clr(int mask) { irqsrc &= ~mask; }
static void dma_chn_irq_enable(int mask, int en) { (void)mask; (void)en; }
static void dma_chn_enable(int mask, int en) { if (en) channels |= mask; else channels &= ~mask; }
static int uart_is_parity_error(void) { return 0; }
static void uart_clear_parity_error(void) {}
static void bus_mux_on_uart_rx_byte(void) { ++received; }
static int bus_mux_get_state(void) { return bus_mode; }
static u8 bms_serial_ota_busy(void) { return ota_busy; }
static void bms_serial_ota_poll(void) {}
static int bms_serial_ota_on_frame(const u8 *p, u32 n, u8 *r, u32 *l)
{ (void)p; (void)n; (void)r; (void)l; return 0; }
static int modbus_on_frame(const u8 *p, u32 n, u8 *r, u32 *l)
{ (void)p; (void)n; (void)r; ++parsed; *l = 0; return 0; }
'''

scenarios = r'''
static void receive(u8 value, u32 length)
{
    if (!(channels & 1)) return;
    memset(s_rx_pkt.data, value, sizeof(s_rx_pkt.data));
    s_rx_pkt.dma_len = length;
    irqsrc |= 1;
    modbus_uart_irq_proc();
}

int main(void)
{
    u8 *ptr, *saved;
    u32 len;
    u8 data[268];
    memset(data, 0x55, sizeof(data));

    modbus_uart_init();
    receive(0x11, 8);
    assert((channels & 1) && !s_rx_ready && !s_uart_wake_rx_pending && !parsed);
    assert(received == 1 && !irqsrc && irq_enabled);
    assert(modbus_uart_bus_idle());
    ota_busy = 1; assert(!modbus_uart_bus_idle()); ota_busy = 0;
    busy = 1; assert(!modbus_uart_bus_idle()); busy = 0;
    rx_high = 0; assert(!modbus_uart_bus_idle()); rx_high = 1;
    irqsrc = 1; assert(!modbus_uart_bus_idle()); irqsrc = 0;
    receive(0x11, 8);
    assert(!modbus_uart_bus_idle() && !(channels & 1) && !parsed);
    assert(modbus_uart_poll(&ptr, &len) && len == 8);
    saved = ptr;
    receive(0x22, 8);
    assert(saved[0] == 0x11);
    modbus_uart_rx_reset();
    receive(0x33, 268);
    assert(modbus_uart_poll(&ptr, &len) && len == 268 && ptr[267] == 0x33);
    modbus_uart_rx_reset();
    receive(0x44, 269);
    assert(!modbus_uart_poll(&ptr, &len) && (channels & 1));
    receive(0x44, 0);
    assert(!modbus_uart_poll(&ptr, &len) && (channels & 1));

    assert(!modbus_uart_send(0, 8) && !modbus_uart_send(data, 0) && !modbus_uart_send(data, 269));
    assert(!sends && modbus_uart_send(data, 268));
    memset(data, 0x66, sizeof(data));
    assert(!modbus_uart_send(data, 8));
    assert(sends == 1 && s_tx_pkt.dma_len == 268 && s_tx_pkt.data[0] == 0x55);
    irqsrc = 2; modbus_uart_irq_proc();
    assert(!irqsrc && modbus_uart_tx_active());
    busy = 0;

    s_rx_pkt.dma_len = 3; s_rx_pkt.data[0] = 0x77;
    now += 1000001u*16u; main_loop_modbus();
    assert(s_rx_pkt.dma_len == 3 && s_rx_pkt.data[0] == 0x77);
    receive(0x55, 8); main_loop_modbus(); assert(parsed == 1 && (channels & 1));

    receive(0x11, 8);
    bus_mode = 0;
    modbus_uart_disable();
    assert(!channels && !s_rx_ready);
    now += 2000000u*16u; main_loop_modbus();
    assert(!channels && parsed == 1);
    assert(!modbus_uart_poll(&ptr, &len) && !modbus_uart_send(data, 8));
    irqsrc = 1; modbus_uart_irq_proc(); assert(!s_rx_ready && !irqsrc);
    return 0;
}
'''

run_c(prefix + without_includes(read('bms/platform/telink/modbus_uart.c')) + scenarios,
      name='d008-uart-ownership')
print('PASS D008 UART DMA ownership, frame bounds, first-frame discard and TX buffer ownership')
