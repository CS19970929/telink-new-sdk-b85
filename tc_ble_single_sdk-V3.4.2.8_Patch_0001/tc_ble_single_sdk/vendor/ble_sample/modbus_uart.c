#include "modbus_uart.h"
// #include <string.h>
#include "app_config.h"
#include "tl_common.h"
#include "drivers.h"
#include "modbus_rtu.h"
#include "conf.h"

static volatile u8  s_rx_ready = 0;

static mb_dma_pkt_t s_rx_pkt;  // DMA RX：硬件写这里（dma_len + data）
static mb_dma_pkt_t s_tx_pkt;  // DMA TX：我们写这里
volatile unsigned char uart_dmairq_tx_cnt2=0;
volatile unsigned char uart_dmairq_rx_cnt2=0;

void modbus_uart_init(void)
{
    // WaitMs(50);

    // 1) 先配 DMA RX 缓冲（非常关键：照例程的顺序）
    memset((void*)&s_rx_pkt, 0, sizeof(s_rx_pkt));
    uart_recbuff_init((u8*)&s_rx_pkt, sizeof(s_rx_pkt));

    // 2) 配引脚（PC2 TX / PC3 RX）
    // uart_gpio_set(UART_TX_PC2, UART_RX_PC3);
    uart_gpio_set(OWC_TX_PIN, OWC_RX_PIN);

    // 3) reset uart（会清 0x90~0x9f），所以 init 要在 reset 后
    uart_reset();

    // 4) Modbus RTU 常用 8E1：PARITY_EVEN + STOP_BIT_ONE
    // uart_init_baudrate(MODBUS_BAUD, MODBUS_SYSCLK_HZ, PARITY_EVEN, STOP_BIT_ONE);
	uart_init(9, 13, PARITY_NONE, STOP_BIT_ONE);

    // 5) 开 DMA
    uart_dma_enable(1, 1);

    // 6) 只用 DMA IRQ，不用 UART IRQ（照例程）
    uart_irq_enable(0, 0);

    // 7) 使能 DMA IRQ
    irq_set_mask(FLD_IRQ_DMA_EN);
    dma_chn_irq_enable(FLD_DMA_CHN_UART_RX | FLD_DMA_CHN_UART_TX, 1);

    irq_enable();
}

void modbus_uart_irq_proc(void)
{
    u8 irqsrc = dma_chn_irq_status_get();

    if (irqsrc & FLD_DMA_CHN_UART_RX) {
        dma_chn_irq_status_clr(FLD_DMA_CHN_UART_RX);
		uart_dmairq_rx_cnt2++;

        // 此时 s_rx_pkt.dma_len 会被硬件更新
        // 做一个最轻量的标记：主循环再去读长度和内容
        if (s_rx_pkt.dma_len > 0) {
            s_rx_ready = 1;

            bus_mux_on_uart_rx_byte();
        }
    }

    if (irqsrc & FLD_DMA_CHN_UART_TX) {
        dma_chn_irq_status_clr(FLD_DMA_CHN_UART_TX);
        // 可选：记录 TX done
		uart_dmairq_tx_cnt2++;
    }
}

// 主循环：拿到一帧（指针直接指向 s_rx_pkt.data）
int modbus_uart_poll(u8 **p, u32 *len)
{
    if (!s_rx_ready) return 0;

    // 读一次长度
    u32 l = s_rx_pkt.dma_len;
    if (l == 0 || l > sizeof(s_rx_pkt.data)) {
        // 异常：清掉重来
        s_rx_ready = 0;
        s_rx_pkt.dma_len = 0;
        uart_recbuff_init((u8*)&s_rx_pkt, sizeof(s_rx_pkt));
        return 0;
    }

    *p = (u8*)s_rx_pkt.data;
    *len = l;

    // 把 ready 清掉：这帧交给上层处理
    s_rx_ready = 0;
    return 1;
}

void modbus_uart_send(const u8 *p, u32 len)
{
    if (len > sizeof(s_tx_pkt.data)) {
        len = sizeof(s_tx_pkt.data);
    }

    // 注意：DMA TX 包必须 “前4字节长度 + payload”
    s_tx_pkt.dma_len = len;
    memcpy(s_tx_pkt.data, p, len);

    // 推荐用 uart_send_dma（它会先 clr_tx_done，更稳）
    uart_send_dma((u8*)&s_tx_pkt);
}

// 处理完一帧后调用：重新 arm RX（简单粗暴但稳）
void modbus_uart_rx_reset(void)
{
    s_rx_pkt.dma_len = 0;
    memset(s_rx_pkt.data, 0, 16); // 可选：只清头部，别全清浪费
    uart_recbuff_init((u8*)&s_rx_pkt, sizeof(s_rx_pkt));
}

static u8  rsp_buf[512];

static _attribute_data_retention_ u32 mb_last_ok_tick = 0;
static _attribute_data_retention_ u32 mb_bad_cnt = 0;

void main_loop_modbus(void)
{
    u8 *req = 0;
    u32 req_len = 0;

    if (modbus_uart_poll(&req, &req_len))
    {
        u32 rsp_len = 0;
        int ok = modbus_on_frame(req, req_len, rsp_buf, &rsp_len);

        // ✅关键：不管 ok 与否，必须清RX状态机/重新arm
        modbus_uart_rx_reset();

        if (ok && rsp_len)
        {
            modbus_uart_send(rsp_buf, rsp_len);
            mb_last_ok_tick = clock_time();
            mb_bad_cnt = 0;
        }
        else
        {
            mb_bad_cnt++;
        }
    }

    // ✅温和自愈：长时间没成功回应，就做一次“软恢复”（不reset uart）
    if (clock_time_exceed(mb_last_ok_tick, 1000 * 1000)) // 1秒都没成功回包
    {
        // 只做：清错误位 + 重新arm RX，不动 UART 配置
        if (uart_is_parity_error()) uart_clear_parity_error();
        modbus_uart_rx_reset();

        mb_last_ok_tick = clock_time(); // 防止每次循环都触发
    }

    // ✅如果连续失败很多次，再考虑更强恢复（可选）
    if (mb_bad_cnt > 50)
    {
        // 可选：再做一次稍强的恢复（仍不reset uart）
        if (uart_is_parity_error()) uart_clear_parity_error();
        modbus_uart_rx_reset();
        mb_bad_cnt = 0;
    }
}
