#include "modbus_uart.h"
#include "app_config.h"
#include "tl_common.h"
#include "drivers.h"
#include "modbus_rtu.h"
#include "conf.h"

typedef struct __attribute__((aligned(4))) {
    u32 dma_len;
    u8 data[MODBUS_RTU_FRAME_CAPACITY];
} mb_dma_pkt_t;

typedef char modbus_dma_packet_size_must_be_272[
    (sizeof(mb_dma_pkt_t) == 272u) ? 1 : -1];

#define MODBUS_UART_CLOCK_DIVIDER 9u
#define MODBUS_UART_BWPC          13u
#define MODBUS_UART_BITS_PER_CHAR 10u /* 8N1: start + 8 data + stop */
#define MODBUS_UART_RECOVERY_US   (1u * 1000u * 1000u)

/*
 * Extra margin after the theoretical complete frame time.  Do not use this as
 * a blind millisecond delay: the dominant DE hold time is calculated from the
 * actual response length and the configured B85 UART divider/BWPC.
 */
#define MODBUS_RS485_TX_EXTRA_GUARD_US 200u

#if ((MODBUS_RS485_ENABLE != 0) && (MODBUS_RS485_ENABLE != 1))
#error "MODBUS_RS485_ENABLE must be 0 or 1"
#endif

static volatile u8 s_rx_ready = 0u;
static mb_dma_pkt_t s_rx_pkt;
static mb_dma_pkt_t s_tx_pkt;

#if MODBUS_RS485_ENABLE
static volatile u8 s_rs485_tx_dma_done = 0u;
static volatile u8 s_rs485_tx_active = 0u;
static volatile u32 s_rs485_tx_start_tick = 0u;
static volatile u32 s_rs485_tx_min_hold_us = 0u;

static void modbus_rs485_receive_mode(void)
{
    /* D014 CA-IS2092A: DE and /RE share PA1, 0=receive. */
    gpio_write(D014_RS485_EN_PIN, 0);
}

static void modbus_rs485_transmit_mode(void)
{
    /* D014 CA-IS2092A: DE and /RE share PA1, 1=transmit. */
    gpio_write(D014_RS485_EN_PIN, 1);
}

static u32 modbus_rs485_min_hold_us(u32 len)
{
    /*
     * B85 UART bit clock:
     *   baud = SYSCLK / ((divider + 1) * (BWPC + 1))
     *
     * At 16 MHz, divider=9 and BWPC=13, the real baud is about 114285.7.
     * One 8N1 character therefore occupies 87.5 us.  Calculate with system
     * clock ticks so the DE minimum hold follows the actual configured UART,
     * rather than assuming an ideal 115200 baud.
     */
    const u32 ticks_per_us = CLOCK_SYS_CLOCK_HZ / 1000000u;
    const u32 ticks_per_bit =
        (MODBUS_UART_CLOCK_DIVIDER + 1u) * (MODBUS_UART_BWPC + 1u);
    u32 frame_ticks;
    u32 frame_us;

    if (ticks_per_us == 0u) return MODBUS_RS485_TX_EXTRA_GUARD_US;

    frame_ticks = len * MODBUS_UART_BITS_PER_CHAR * ticks_per_bit;
    frame_us = (frame_ticks + ticks_per_us - 1u) / ticks_per_us;
    return frame_us + MODBUS_RS485_TX_EXTRA_GUARD_US;
}

static void modbus_rs485_service_tx_done(void)
{
    if (!s_rs485_tx_active || !s_rs485_tx_dma_done)
    {
        return;
    }

    /*
     * Official B85 UART TX_DONE is the authoritative completion indication,
     * but also enforce the theoretical full-frame line time from the instant
     * uart_send_dma() is started.  Both conditions must be true before DE is
     * released.  This makes a premature DE transition impossible even if a
     * status/IRQ observation is unexpectedly early.
     */
    if (uart_tx_is_busy())
    {
        return;
    }

    if (!clock_time_exceed(s_rs485_tx_start_tick, s_rs485_tx_min_hold_us))
    {
        return;
    }

    modbus_rs485_receive_mode();
    s_rs485_tx_active = 0u;
    s_rs485_tx_dma_done = 0u;
    s_rs485_tx_start_tick = 0u;
    s_rs485_tx_min_hold_us = 0u;
}
#endif

void modbus_uart_init(void)
{
    /* Keep the proven new-new-master initialization order. */
    memset((void *)&s_rx_pkt, 0, sizeof(s_rx_pkt));
    uart_recbuff_init((u8 *)&s_rx_pkt, sizeof(s_rx_pkt));

#if MODBUS_RS485_ENABLE
    /* D014: PC2=TX, PC3=RX, PA1=485 DE//RE direction control. */
    gpio_set_func(D014_RS485_EN_PIN, AS_GPIO);
    gpio_write(D014_RS485_EN_PIN, 0);
    gpio_set_input_en(D014_RS485_EN_PIN, 0);
    gpio_set_output_en(D014_RS485_EN_PIN, 1);
    s_rs485_tx_active = 0u;
    s_rs485_tx_dma_done = 0u;
    s_rs485_tx_start_tick = 0u;
    s_rs485_tx_min_hold_us = 0u;
#endif

    uart_gpio_set(D014_SCI1_TX_PIN, D014_SCI1_RX_PIN);
    uart_reset();
    uart_init(MODBUS_UART_CLOCK_DIVIDER,
              MODBUS_UART_BWPC,
              PARITY_NONE,
              STOP_BIT_ONE);
    uart_dma_enable(1, 1);
    uart_irq_enable(0, 0);

    irq_set_mask(FLD_IRQ_DMA_EN);
    dma_chn_irq_enable(FLD_DMA_CHN_UART_RX | FLD_DMA_CHN_UART_TX, 1);

#if MODBUS_RS485_ENABLE
    modbus_rs485_receive_mode();
#endif
    irq_enable();
}

void modbus_uart_irq_proc(void)
{
    u8 irqsrc = dma_chn_irq_status_get();

    if (irqsrc & FLD_DMA_CHN_UART_RX)
    {
        dma_chn_irq_status_clr(FLD_DMA_CHN_UART_RX);
        if (s_rx_pkt.dma_len > 0u)
        {
            s_rx_ready = 1u;
        }
    }

    if (irqsrc & FLD_DMA_CHN_UART_TX)
    {
        dma_chn_irq_status_clr(FLD_DMA_CHN_UART_TX);
#if MODBUS_RS485_ENABLE
        s_rs485_tx_dma_done = 1u;
        modbus_rs485_service_tx_done();
#endif
    }
}

int modbus_uart_poll(u8 **p, u32 *len)
{
    u32 l;

    if (p == NULL || len == NULL) return 0;
    if (!s_rx_ready) return 0;

    l = s_rx_pkt.dma_len;
    if (l == 0u || l > sizeof(s_rx_pkt.data))
    {
        s_rx_ready = 0u;
        s_rx_pkt.dma_len = 0u;
        uart_recbuff_init((u8 *)&s_rx_pkt, sizeof(s_rx_pkt));
        return 0;
    }

    *p = (u8 *)s_rx_pkt.data;
    *len = l;
    s_rx_ready = 0u;
    return 1;
}

void modbus_uart_send(const u8 *p, u32 len)
{
    if (p == NULL || len == 0u) return;
    if (len > sizeof(s_tx_pkt.data)) len = sizeof(s_tx_pkt.data);

    s_tx_pkt.dma_len = len;
    memcpy(s_tx_pkt.data, p, len);

#if MODBUS_RS485_ENABLE
    modbus_rs485_transmit_mode();
    s_rs485_tx_dma_done = 0u;
    s_rs485_tx_active = 1u;
    s_rs485_tx_start_tick = clock_time();
    s_rs485_tx_min_hold_us = modbus_rs485_min_hold_us(len);
#endif

    /* Proven new-new-master TX path: DMA packet is [u32 len + payload]. */
    uart_send_dma((u8 *)&s_tx_pkt);
}

static void modbus_uart_rx_reset(void)
{
    s_rx_pkt.dma_len = 0u;
    memset(s_rx_pkt.data, 0, 16u);
    uart_recbuff_init((u8 *)&s_rx_pkt, sizeof(s_rx_pkt));
}

static u8 rsp_buf[MODBUS_RTU_FRAME_CAPACITY];
static _attribute_data_retention_ u32 mb_last_ok_tick = 0u;
static _attribute_data_retention_ u32 mb_bad_cnt = 0u;

void main_loop_modbus(void)
{
    u8 *req = 0;
    u32 req_len = 0u;

#if MODBUS_RS485_ENABLE
    modbus_rs485_service_tx_done();
#endif

    if (modbus_uart_poll(&req, &req_len))
    {
        u32 rsp_len = 0u;
        int ok = modbus_on_frame(req, req_len, rsp_buf, &rsp_len);

        /* Match the proven implementation: always re-arm RX after a frame. */
        modbus_uart_rx_reset();

        if (ok && rsp_len)
        {
            modbus_uart_send(rsp_buf, rsp_len);
            mb_last_ok_tick = clock_time();
            mb_bad_cnt = 0u;
        }
        else
        {
            ++mb_bad_cnt;
        }
    }

    if (clock_time_exceed(mb_last_ok_tick, MODBUS_UART_RECOVERY_US))
    {
        if (uart_is_parity_error()) uart_clear_parity_error();
        modbus_uart_rx_reset();
        mb_last_ok_tick = clock_time();
    }

    if (mb_bad_cnt > 50u)
    {
        if (uart_is_parity_error()) uart_clear_parity_error();
        modbus_uart_rx_reset();
        mb_bad_cnt = 0u;
    }

#if MODBUS_RS485_ENABLE
    modbus_rs485_service_tx_done();
#endif
}
