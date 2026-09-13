#include "modbus_uart.h"
#include "app_config.h"
#include "tl_common.h"
#include "drivers.h"
#include "bus_mux.h"
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

static volatile u8 s_rx_ready = 0u;
static volatile u8 s_tx_active = 0u;
static mb_dma_pkt_t s_rx_pkt;
static mb_dma_pkt_t s_tx_pkt;

static void modbus_rs485_receive_mode(void)
{
    gpio_write(D011_RS485_EN_PIN, 0);
}

static void modbus_rs485_transmit_mode(void)
{
    gpio_write(D011_RS485_EN_PIN, 1);
}

static void modbus_uart_service_tx_done(void)
{
    if (s_tx_active && !uart_tx_is_busy())
    {
        /* CA-IS2092A DE and /RE share 485-EN: 0=RX, 1=TX. */
        modbus_rs485_receive_mode();
        s_tx_active = 0u;
        uart_clr_tx_done();
    }
}

void modbus_uart_init(void)
{
    memset((void *)&s_rx_pkt, 0, sizeof(s_rx_pkt));
    memset((void *)&s_tx_pkt, 0, sizeof(s_tx_pkt));
    uart_recbuff_init((u8 *)&s_rx_pkt, sizeof(s_rx_pkt));

    /* HS-D011: PC2=SCI1-TX, PC3=SCI1-RX, PA1=485-EN. */
    gpio_set_func(D011_RS485_EN_PIN, AS_GPIO);
    gpio_write(D011_RS485_EN_PIN, 0);
    gpio_set_input_en(D011_RS485_EN_PIN, 0);
    gpio_set_output_en(D011_RS485_EN_PIN, 1);

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
    modbus_rs485_receive_mode();
    s_tx_active = 0u;
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
            bus_mux_on_uart_rx_byte();
        }
    }

    if (irqsrc & FLD_DMA_CHN_UART_TX)
    {
        /* DMA completion can precede the UART stop bit. Direction is released
         * only after FLD_UART_TX_DONE via modbus_uart_service_tx_done(). */
        dma_chn_irq_status_clr(FLD_DMA_CHN_UART_TX);
    }

    modbus_uart_service_tx_done();
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
    if (p == NULL || len == 0u || len > sizeof(s_tx_pkt.data)) return;
    modbus_uart_service_tx_done();
    if (s_tx_active || uart_tx_is_busy()) return;

    s_tx_pkt.dma_len = len;
    memcpy(s_tx_pkt.data, p, len);

    modbus_rs485_transmit_mode();
    s_tx_active = 1u;
    uart_send_dma((u8 *)&s_tx_pkt);
}

static void modbus_uart_rx_reset(void)
{
    s_rx_pkt.dma_len = 0u;
    memset(s_rx_pkt.data, 0, 16u);
    uart_recbuff_init((u8 *)&s_rx_pkt, sizeof(s_rx_pkt));
}

static u8 rsp_buf[MODBUS_RTU_FRAME_CAPACITY];
static _attribute_data_retention_ u32 mb_last_ok_tick = 0;
static _attribute_data_retention_ u32 mb_bad_cnt = 0;

void main_loop_modbus(void)
{
    u8 *req = 0;
    u32 req_len = 0;

    modbus_uart_service_tx_done();

    if (modbus_uart_poll(&req, &req_len))
    {
        u32 rsp_len = 0;
        int ok = modbus_on_frame(req, req_len, rsp_buf, &rsp_len);
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

    modbus_uart_service_tx_done();
}
