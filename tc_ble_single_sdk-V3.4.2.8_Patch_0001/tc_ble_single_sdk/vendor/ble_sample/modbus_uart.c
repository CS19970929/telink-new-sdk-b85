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
#define MODBUS_UART_RECOVERY_US   (1u * 1000u * 1000u)

/*
 * D011 RS485 tail guard.
 * UART is 115200 8N1, so one complete character is about 86.8 us.
 * Keep DE asserted for a little more than one character after DMA has
 * completed and UART busy has cleared, so the final byte/stop bit is not
 * truncated when switching the transceiver back to receive mode.
 */
#define MODBUS_RS485_TX_TAIL_GUARD_US 120u

#if ((MODBUS_RS485_ENABLE != 0) && (MODBUS_RS485_ENABLE != 1))
#error "MODBUS_RS485_ENABLE must be 0 or 1"
#endif

static volatile u8 s_rx_ready = 0u;
static mb_dma_pkt_t s_rx_pkt;
static mb_dma_pkt_t s_tx_pkt;

#if MODBUS_RS485_ENABLE
static volatile u8 s_rs485_tx_dma_done = 0u;
static volatile u8 s_rs485_tx_active = 0u;
static volatile u8 s_rs485_tx_tail_wait = 0u;
static volatile u32 s_rs485_tx_tail_tick = 0u;

static void modbus_rs485_receive_mode(void)
{
    /* D011 CA-IS2092A: DE and /RE share PA1, 0=receive. */
    gpio_write(D011_RS485_EN_PIN, 0);
}

static void modbus_rs485_transmit_mode(void)
{
    /* D011 CA-IS2092A: DE and /RE share PA1, 1=transmit. */
    gpio_write(D011_RS485_EN_PIN, 1);
}

static void modbus_rs485_service_tx_done(void)
{
    if (!s_rs485_tx_active || !s_rs485_tx_dma_done)
    {
        return;
    }

    /*
     * DMA done does not mean the last UART byte has physically left the pin.
     * Also do not switch DE immediately when uart_tx_is_busy() first clears:
     * keep the transceiver in TX for one additional character-time margin.
     */
    if (!s_rs485_tx_tail_wait)
    {
        if (uart_tx_is_busy())
        {
            return;
        }

        s_rs485_tx_tail_tick = clock_time();
        s_rs485_tx_tail_wait = 1u;
        return;
    }

    if (!clock_time_exceed(s_rs485_tx_tail_tick, MODBUS_RS485_TX_TAIL_GUARD_US))
    {
        return;
    }

    modbus_rs485_receive_mode();
    s_rs485_tx_active = 0u;
    s_rs485_tx_dma_done = 0u;
    s_rs485_tx_tail_wait = 0u;
}
#endif

void modbus_uart_init(void)
{
    /* Keep the proven new-new-master initialization order. */
    memset((void *)&s_rx_pkt, 0, sizeof(s_rx_pkt));
    uart_recbuff_init((u8 *)&s_rx_pkt, sizeof(s_rx_pkt));

#if MODBUS_RS485_ENABLE
    /* D011: PC2=TX, PC3=RX, PA1=485 DE//RE direction control. */
    gpio_set_func(D011_RS485_EN_PIN, AS_GPIO);
    gpio_write(D011_RS485_EN_PIN, 0);
    gpio_set_input_en(D011_RS485_EN_PIN, 0);
    gpio_set_output_en(D011_RS485_EN_PIN, 1);
    s_rs485_tx_active = 0u;
    s_rs485_tx_dma_done = 0u;
    s_rs485_tx_tail_wait = 0u;
    s_rs485_tx_tail_tick = 0u;
#endif

    uart_gpio_set(D011_SCI1_TX_PIN, D011_SCI1_RX_PIN);
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
    s_rs485_tx_tail_wait = 0u;
    s_rs485_tx_active = 1u;
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
