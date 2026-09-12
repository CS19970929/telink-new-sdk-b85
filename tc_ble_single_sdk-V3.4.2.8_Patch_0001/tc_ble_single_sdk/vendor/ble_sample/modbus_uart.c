#include "modbus_uart.h"

#include "drivers.h"
#include "modbus_rtu.h"
#include "conf.h"
#include "bus_mux.h"

#define MODBUS_DMA_DATA_SIZE 268u
#define MODBUS_RSP_BUF_SIZE  512u

typedef struct __attribute__((aligned(4)))
{
    u32 dma_len;
    u8 data[MODBUS_DMA_DATA_SIZE];
} modbus_dma_packet_t;

static volatile u8 s_rx_ready;
static modbus_dma_packet_t s_rx_pkt;
static modbus_dma_packet_t s_tx_pkt;
static u8 s_rsp_buf[MODBUS_RSP_BUF_SIZE];
static _attribute_data_retention_ u32 s_last_ok_tick;
static _attribute_data_retention_ u32 s_bad_frame_count;

static void modbus_uart_rx_reset(void)
{
    s_rx_pkt.dma_len = 0u;
    memset(s_rx_pkt.data, 0, 16u);
    uart_recbuff_init((u8 *)&s_rx_pkt, sizeof(s_rx_pkt));
}

void modbus_uart_init(void)
{
    memset((void *)&s_rx_pkt, 0, sizeof(s_rx_pkt));
    uart_recbuff_init((u8 *)&s_rx_pkt, sizeof(s_rx_pkt));

    uart_gpio_set(OWC_TX_PIN, OWC_RX_PIN);
    uart_reset();
    uart_init(9, 13, PARITY_NONE, STOP_BIT_ONE);
    uart_dma_enable(1, 1);
    uart_irq_enable(0, 0);

    irq_set_mask(FLD_IRQ_DMA_EN);
    dma_chn_irq_enable(FLD_DMA_CHN_UART_RX | FLD_DMA_CHN_UART_TX, 1);
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
        dma_chn_irq_status_clr(FLD_DMA_CHN_UART_TX);
    }
}

int modbus_uart_poll(u8 **data, u32 *len)
{
    u32 frame_len;

    if (!s_rx_ready)
    {
        return 0;
    }

    frame_len = s_rx_pkt.dma_len;
    if ((frame_len == 0u) || (frame_len > sizeof(s_rx_pkt.data)))
    {
        s_rx_ready = 0u;
        modbus_uart_rx_reset();
        return 0;
    }

    *data = s_rx_pkt.data;
    *len = frame_len;
    s_rx_ready = 0u;
    return 1;
}

void modbus_uart_send(const u8 *data, u32 len)
{
    if (len > sizeof(s_tx_pkt.data))
    {
        len = sizeof(s_tx_pkt.data);
    }

    s_tx_pkt.dma_len = len;
    memcpy(s_tx_pkt.data, data, len);
    uart_send_dma((u8 *)&s_tx_pkt);
}

void main_loop_modbus(void)
{
    u8 *req = 0;
    u32 req_len = 0u;

    if (modbus_uart_poll(&req, &req_len))
    {
        u32 rsp_len = 0u;
        int ok = modbus_on_frame(req, req_len, s_rsp_buf, &rsp_len);

        modbus_uart_rx_reset();
        if (ok && rsp_len)
        {
            modbus_uart_send(s_rsp_buf, rsp_len);
            s_last_ok_tick = clock_time();
            s_bad_frame_count = 0u;
        }
        else
        {
            ++s_bad_frame_count;
        }
    }

    if (clock_time_exceed(s_last_ok_tick, 1000u * 1000u))
    {
        if (uart_is_parity_error())
        {
            uart_clear_parity_error();
        }
        modbus_uart_rx_reset();
        s_last_ok_tick = clock_time();
    }

    if (s_bad_frame_count > 50u)
    {
        if (uart_is_parity_error())
        {
            uart_clear_parity_error();
        }
        modbus_uart_rx_reset();
        s_bad_frame_count = 0u;
    }
}
