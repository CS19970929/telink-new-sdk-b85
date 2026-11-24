/*
 * 文件功能：D008 UART DMA。ISR 交付状态，主循环维护协议和恢复。
 * bms/platform/telink/modbus_uart.c；实际编译归属见各产品 sources.txt。
 */
#include "bms_time.h"
#include "bms_product.h"

#include "modbus_uart.h"
#include "bms_serial_ota.h"
#include "app_config.h"
#include "tl_common.h"
#include "drivers.h"
#include "modbus_rtu.h"
#include "bus_mux.h"

typedef struct __attribute__((aligned(4))) {
    u32 dma_len;
    u8 data[MODBUS_RTU_FRAME_CAPACITY];
} mb_dma_pkt_t;

typedef char modbus_dma_packet_size_must_be_272[
    (sizeof(mb_dma_pkt_t) == 272u) ? 1 : -1];

#if CLOCK_SYS_CLOCK_HZ != 16000000u
#error "D008 UART requires a 16 MHz system clock"
#endif

/* B85 16 MHz、115200 baud、8N1。 */
#define MODBUS_UART_CLOCK_DIVIDER 9u
#define MODBUS_UART_BWPC 13u
#define MODBUS_UART_RECOVERY_US (1000u * 1000u)


static volatile u8 s_rx_ready = 0u;
static mb_dma_pkt_t s_rx_pkt;
static mb_dma_pkt_t s_tx_pkt;
/* 正常应答和周期接收维护的时间基准。 */
static _attribute_data_retention_ u32 mb_last_ok_tick = 0u;

/* 总线检测在帧中途才取得UART所有权，首个DMA包只能用于唤醒。 */
static volatile u8 s_uart_wake_rx_pending;


/* 查询 UART 发送是否仍活动。 */
u8 modbus_uart_tx_active(void)
{
    return uart_tx_is_busy();
}


/* 初始化D008 UART 和 DMA 缓冲区。 */
void modbus_uart_init(void)
{
    u8 irq_state = irq_disable();
    s_uart_wake_rx_pending = 1u;
    /* 保留已验证的 new-new-master 初始化顺序。 */
    memset((void *)&s_rx_pkt, 0, sizeof(s_rx_pkt));
    uart_recbuff_init((u8 *)&s_rx_pkt, sizeof(s_rx_pkt));


    uart_gpio_set(BMS_BOARD_OWC_TX_PIN, BMS_BOARD_OWC_RX_PIN);
    uart_reset();
    uart_init(MODBUS_UART_CLOCK_DIVIDER,
              MODBUS_UART_BWPC,
              PARITY_NONE,
              STOP_BIT_ONE);
    uart_dma_enable(1, 1);
    uart_irq_enable(0, 0);

    irq_set_mask(FLD_IRQ_DMA_EN);
    dma_chn_irq_enable(FLD_DMA_CHN_UART_RX | FLD_DMA_CHN_UART_TX, 1);

    s_rx_ready = 0u;
    mb_last_ok_tick = clock_time();
    irq_restore(irq_state);
}

/* 调用方已关闭中断，判定与切换之间不得再交付 RX 或启动 TX。 */
u8 modbus_uart_bus_idle(void)
{
    return !bms_serial_ota_busy() && !modbus_uart_tx_active() && !s_rx_ready &&
           !(dma_chn_irq_status_get() & FLD_DMA_CHN_UART_RX) &&
           gpio_read(BMS_BOARD_OWC_RX_PIN);
}

void modbus_uart_disable(void)
{
    uart_dma_enable(0, 0);
    uart_irq_enable(0, 0);
    dma_chn_irq_enable(FLD_DMA_CHN_UART_RX | FLD_DMA_CHN_UART_TX, 0);
    dma_chn_enable(FLD_DMA_CHN_UART_RX | FLD_DMA_CHN_UART_TX, 0);
    dma_chn_irq_status_clr(FLD_DMA_CHN_UART_RX | FLD_DMA_CHN_UART_TX);
    uart_reset();
    uart_ndma_clear_tx_index();
    uart_ndma_clear_rx_index();
    s_rx_ready = 0u;
    s_rx_pkt.dma_len = 0u;
}


/* 中断交付正常RX；D008首次唤醒包只丢弃并重装DMA，不解析/发送或访问AFE。 */
void modbus_uart_irq_proc(void)
{
    u8 irqsrc = dma_chn_irq_status_get();
    if (bus_mux_get_state() != BUS_STATE_UART_MODBUS) {
        dma_chn_irq_status_clr(irqsrc & (FLD_DMA_CHN_UART_RX | FLD_DMA_CHN_UART_TX));
        return;
    }

    if (irqsrc & FLD_DMA_CHN_UART_RX)
    {
        dma_chn_enable(FLD_DMA_CHN_UART_RX, 0);
        dma_chn_irq_status_clr(FLD_DMA_CHN_UART_RX);
        if (s_rx_pkt.dma_len > 0u) bus_mux_on_uart_rx_byte();
        if (s_uart_wake_rx_pending) {
            /* 已知不交付主循环的残帧，立即重装，避免AFE/Flash期间停住RX
             * 而再丢主机重发。B85 uart_recbuff_init只写固定DMA寄存器。 */
            s_uart_wake_rx_pending = 0u;
            s_rx_pkt.dma_len = 0u;
            uart_recbuff_init((u8 *)&s_rx_pkt, sizeof(s_rx_pkt));
        } else {
            s_rx_ready = 1u;
        }
    }

    if (irqsrc & FLD_DMA_CHN_UART_TX)
    {
        dma_chn_irq_status_clr(FLD_DMA_CHN_UART_TX);
    }
}

/* 主循环处理收帧状态和长度。 */
int modbus_uart_poll(u8 **p, u32 *len)
{
    u32 l;

    if (bus_mux_get_state() != BUS_STATE_UART_MODBUS) return 0;
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

/* 检查发送状态与长度后启动 UART DMA，不覆盖活动缓冲区。 */
u8 modbus_uart_send(const u8 *p, u32 len)
{
    if (bus_mux_get_state() != BUS_STATE_UART_MODBUS) return 0u;
    if (p == NULL || len == 0u || len > sizeof(s_tx_pkt.data)) return 0u;

    /* uart_send_dma() 无条件重启 DMA，禁止覆盖其使用中的缓冲区。 */
    if (uart_tx_is_busy())
    {
        return 0u;
    }

    s_tx_pkt.dma_len = len;
    memcpy(s_tx_pkt.data, p, len);


    /* 已验证的 new-new-master TX 路径：DMA 包为 [u32 长度 + 载荷]。 */
    uart_send_dma((u8 *)&s_tx_pkt);
    return 1u;
}

/* 清除接收状态并重新启用 RX DMA。 */
static void modbus_uart_rx_reset(void)
{
    s_rx_pkt.dma_len = 0u;
    memset(s_rx_pkt.data, 0, 16u);
    uart_recbuff_init((u8 *)&s_rx_pkt, sizeof(s_rx_pkt));
}

static u8 rsp_buf[MODBUS_RTU_FRAME_CAPACITY];


/* 主循环解析帧、发送应答并恢复 RX。 */
void main_loop_modbus(void)
{
    bms_serial_ota_poll();
    /* 一线通期间不解析旧帧，也不让周期 RX 恢复重新开启 DMA。 */
    if (bus_mux_get_state() != BUS_STATE_UART_MODBUS) return;
    u8 *req = 0;
    u32 req_len = 0u;


    if (modbus_uart_poll(&req, &req_len))
    {
        u32 rsp_len = 0u;
        uint32_t ota_rsp_len = 0u;
        int ok = bms_serial_ota_on_frame(req, req_len, rsp_buf, &ota_rsp_len);
        rsp_len = ota_rsp_len;
        if (!ok) ok = modbus_on_frame(req, req_len, rsp_buf, &rsp_len);

        /* 沿用已验证实现：每帧后都重新启用 RX。 */
        modbus_uart_rx_reset();

        if (ok && rsp_len) (void)modbus_uart_send(rsp_buf, rsp_len);
        mb_last_ok_tick = clock_time();
    }

    if (clock_time_exceed(mb_last_ok_tick, MODBUS_UART_RECOVERY_US)) {
        u8 irq_state = irq_disable();
        /* 与 DMA 交付共用临界区，避免接收完成与空闲维护同时重装。 */
        if (modbus_uart_bus_idle() && !s_rx_pkt.dma_len) {
            if (uart_is_parity_error()) uart_clear_parity_error();
            modbus_uart_rx_reset();
            mb_last_ok_tick = clock_time();
        }
        irq_restore(irq_state);
    }
}
