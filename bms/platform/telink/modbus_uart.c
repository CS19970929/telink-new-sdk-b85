#include "bms_afe_backend.h"

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
#include "modbus_uart.h"
#include "app_config.h"
#include "tl_common.h"
#include "drivers.h"
#include "bus_mux.h"
#include "modbus_rtu.h"
#include "conf.h"


#else
#include "modbus_uart.h"
#include "app_config.h"
#include "tl_common.h"
#include "drivers.h"
#include "modbus_rtu.h"
#include "conf.h"


#endif
typedef struct __attribute__((aligned(4))) {
    u32 dma_len;
    u8 data[MODBUS_RTU_FRAME_CAPACITY];
} mb_dma_pkt_t;

typedef char modbus_dma_packet_size_must_be_272[
    (sizeof(mb_dma_pkt_t) == 272u) ? 1 : -1];


#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
#define MODBUS_UART_CLOCK_DIVIDER 9u
#define MODBUS_UART_BWPC          13u
#define MODBUS_UART_RECOVERY_US   (1u * 1000u * 1000u)

static volatile u8 s_rx_ready = 0;

static mb_dma_pkt_t s_rx_pkt;  // DMA RX：硬件写这里（dma_len + data）
static mb_dma_pkt_t s_tx_pkt;  // DMA TX：我们写这里

void modbus_uart_init(void)
{
    // 1) 先配 DMA RX 缓冲（非常关键：照例程的顺序）
    memset((void*)&s_rx_pkt, 0, sizeof(s_rx_pkt));
    uart_recbuff_init((u8*)&s_rx_pkt, sizeof(s_rx_pkt));

    // 2) 配引脚（PC2 TX / PC3 RX）
    uart_gpio_set(OWC_TX_PIN, OWC_RX_PIN);

    // 3) reset uart（会清 0x90~0x9f），所以 init 要在 reset 后
    uart_reset();

    // 4) 现有产品协议使用当前固定分频参数和 8N1
    uart_init(MODBUS_UART_CLOCK_DIVIDER,
              MODBUS_UART_BWPC,
              PARITY_NONE,
              STOP_BIT_ONE);

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
        // 此时 s_rx_pkt.dma_len 会被硬件更新
        // 做一个最轻量的标记：主循环再去读长度和内容
        if (s_rx_pkt.dma_len > 0) {
            s_rx_ready = 1;

            bus_mux_on_uart_rx_byte();
        }
    }

    if (irqsrc & FLD_DMA_CHN_UART_TX) {
        dma_chn_irq_status_clr(FLD_DMA_CHN_UART_TX);
    }
}

// 主循环：拿到一帧（指针直接指向 s_rx_pkt.data）
int modbus_uart_poll(u8 **p, u32 *len)
{
    if (p == NULL || len == NULL) return 0;
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
    if (p == NULL || len == 0u || len > sizeof(s_tx_pkt.data)) return;

    // 注意：DMA TX 包必须 “前4字节长度 + payload”
    s_tx_pkt.dma_len = len;
    memcpy(s_tx_pkt.data, p, len);

    // 推荐用 uart_send_dma（它会先 clr_tx_done，更稳）
    uart_send_dma((u8*)&s_tx_pkt);
}

// 处理完一帧后调用：重新 arm RX（简单粗暴但稳）
static void modbus_uart_rx_reset(void)
{
    s_rx_pkt.dma_len = 0;
    memset(s_rx_pkt.data, 0, 16); // 可选：只清头部，别全清浪费
    uart_recbuff_init((u8*)&s_rx_pkt, sizeof(s_rx_pkt));
}

static u8 rsp_buf[MODBUS_RTU_FRAME_CAPACITY];

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
    if (clock_time_exceed(mb_last_ok_tick, MODBUS_UART_RECOVERY_US))
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

#else
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
#if ((BMS_RS485_TX_DIAG_ENABLE != 0) && (BMS_RS485_TX_DIAG_ENABLE != 1))
#error "BMS_RS485_TX_DIAG_ENABLE must be 0 or 1"
#endif
#if BMS_RS485_TX_DIAG_ENABLE && !MODBUS_RS485_ENABLE
#error "RS485 TX diagnostics require MODBUS_RS485_ENABLE"
#endif

#define MODBUS_RS485_TX_TIMEOUT_US 50000u

volatile bms_rs485_tx_diag_t g_bms_rs485_tx_diag;

static volatile u8 s_rx_ready = 0u;
static mb_dma_pkt_t s_rx_pkt;
static mb_dma_pkt_t s_tx_pkt;

#if MODBUS_RS485_ENABLE
static volatile u8 s_rs485_tx_dma_done = 0u;
static volatile u8 s_rs485_tx_active = 0u;
static volatile u8 s_rs485_tx_uart_done = 0u;
static volatile u8 s_rs485_tx_timeout_seen = 0u;
static volatile u32 s_rs485_tx_start_tick = 0u;
static volatile u32 s_rs485_tx_min_hold_us = 0u;

static void modbus_rs485_receive_mode(void)
{
    /* D014 CA-IS2092A: DE and /RE share PA1, 0=receive. */
    gpio_write(BMS_BOARD_RS485_EN_PIN, 0);
    g_bms_rs485_tx_diag.de_state = 0u;
}

static void modbus_rs485_transmit_mode(void)
{
    /* D014 CA-IS2092A: DE and /RE share PA1, 1=transmit. */
    gpio_write(BMS_BOARD_RS485_EN_PIN, 1);
    g_bms_rs485_tx_diag.de_state = 1u;
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
    if (!s_rs485_tx_active)
    {
        return;
    }

    if (!s_rs485_tx_timeout_seen &&
        clock_time_exceed(s_rs485_tx_start_tick, MODBUS_RS485_TX_TIMEOUT_US))
    {
        s_rs485_tx_timeout_seen = 1u;
        ++g_bms_rs485_tx_diag.tx_timeout_count;
        /* Keep DE asserted while hardware still reports TX busy. */
    }

    if (s_rs485_tx_dma_done && !uart_tx_is_busy() && !s_rs485_tx_uart_done)
    {
        s_rs485_tx_uart_done = 1u;
        ++g_bms_rs485_tx_diag.tx_uart_done_count;
    }

    if (!s_rs485_tx_dma_done || !s_rs485_tx_uart_done)
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
    ++g_bms_rs485_tx_diag.tx_complete_count;
    s_rs485_tx_active = 0u;
    s_rs485_tx_dma_done = 0u;
    s_rs485_tx_uart_done = 0u;
    s_rs485_tx_timeout_seen = 0u;
    s_rs485_tx_start_tick = 0u;
    s_rs485_tx_min_hold_us = 0u;
}
#endif

u8 modbus_uart_tx_active(void)
{
#if MODBUS_RS485_ENABLE
    return s_rs485_tx_active;
#else
    return 0u;
#endif
}

void modbus_uart_init(void)
{
#if !BMS_RS485_TX_DIAG_ENABLE
    /* Keep the proven new-new-master initialization order. */
    memset((void *)&s_rx_pkt, 0, sizeof(s_rx_pkt));
    uart_recbuff_init((u8 *)&s_rx_pkt, sizeof(s_rx_pkt));
#endif

#if MODBUS_RS485_ENABLE
    /* D014: PC2=TX, PC3=RX, PA1=485 DE//RE direction control. */
    gpio_set_func(BMS_BOARD_RS485_EN_PIN, AS_GPIO);
    gpio_write(BMS_BOARD_RS485_EN_PIN, 0);
    gpio_set_input_en(BMS_BOARD_RS485_EN_PIN, 0);
    gpio_set_output_en(BMS_BOARD_RS485_EN_PIN, 1);
    s_rs485_tx_active = 0u;
    s_rs485_tx_dma_done = 0u;
    s_rs485_tx_uart_done = 0u;
    s_rs485_tx_timeout_seen = 0u;
    s_rs485_tx_start_tick = 0u;
    s_rs485_tx_min_hold_us = 0u;
#endif

    uart_gpio_set(BMS_BOARD_SCI1_TX_PIN, BMS_BOARD_SCI1_RX_PIN);
    uart_reset();
    uart_init(MODBUS_UART_CLOCK_DIVIDER,
              MODBUS_UART_BWPC,
              PARITY_NONE,
              STOP_BIT_ONE);
#if BMS_RS485_TX_DIAG_ENABLE
    /* Keep RX disabled: unsolicited requests cannot affect the line test. */
    uart_dma_enable(0, 1);
#else
    uart_dma_enable(1, 1);
#endif
    uart_irq_enable(0, 0);

    irq_set_mask(FLD_IRQ_DMA_EN);
#if BMS_RS485_TX_DIAG_ENABLE
    dma_chn_irq_enable(FLD_DMA_CHN_UART_TX, 1);
#else
    dma_chn_irq_enable(FLD_DMA_CHN_UART_RX | FLD_DMA_CHN_UART_TX, 1);
#endif

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
        if (s_rs485_tx_active && !s_rs485_tx_dma_done)
        {
            s_rs485_tx_dma_done = 1u;
            ++g_bms_rs485_tx_diag.tx_dma_done_count;
        }
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

    /* uart_send_dma() unconditionally restarts DMA; never overwrite its buffer. */
#if MODBUS_RS485_ENABLE
    modbus_rs485_service_tx_done();
    if (s_rs485_tx_active || uart_tx_is_busy())
#else
    if (uart_tx_is_busy())
#endif
    {
        ++g_bms_rs485_tx_diag.tx_busy_reject_count;
        return;
    }

    s_tx_pkt.dma_len = len;
    memcpy(s_tx_pkt.data, p, len);

#if MODBUS_RS485_ENABLE
    modbus_rs485_transmit_mode();
    s_rs485_tx_dma_done = 0u;
    s_rs485_tx_uart_done = 0u;
    s_rs485_tx_timeout_seen = 0u;
    s_rs485_tx_active = 1u;
    s_rs485_tx_start_tick = clock_time();
    s_rs485_tx_min_hold_us = modbus_rs485_min_hold_us(len);
#endif

    g_bms_rs485_tx_diag.last_tx_len = len;
    ++g_bms_rs485_tx_diag.tx_start_count;

    /* Proven new-new-master TX path: DMA packet is [u32 len + payload]. */
    uart_send_dma((u8 *)&s_tx_pkt);
}

#if !BMS_RS485_TX_DIAG_ENABLE
static void modbus_uart_rx_reset(void)
{
    s_rx_pkt.dma_len = 0u;
    memset(s_rx_pkt.data, 0, 16u);
    uart_recbuff_init((u8 *)&s_rx_pkt, sizeof(s_rx_pkt));
}

static u8 rsp_buf[MODBUS_RTU_FRAME_CAPACITY];
static _attribute_data_retention_ u32 mb_last_ok_tick = 0u;
static _attribute_data_retention_ u32 mb_bad_cnt = 0u;
#endif

#if BMS_RS485_TX_DIAG_ENABLE
#define RS485_DIAG_PERIOD_US 500000u
#define RS485_DIAG_FRAME_COUNT 21u

/* 20 pattern/length combinations, then 01 03 4C + 00..4B + C9 B8. */
static const u8 s_diag_lengths[5] = {8u, 32u, 64u, 81u, 128u};
static u8 s_diag_frame[128];
static u32 s_diag_last_start_tick;

static void modbus_uart_diag_send_next(void)
{
    u8 index = g_bms_rs485_tx_diag.pattern_index;
    u32 len;
    u32 i;
    u32 started = g_bms_rs485_tx_diag.tx_start_count;

    if (index >= RS485_DIAG_FRAME_COUNT) index = 0u;
    if (index == RS485_DIAG_FRAME_COUNT - 1u)
    {
        len = 81u;
        s_diag_frame[0] = 0x01u;
        s_diag_frame[1] = 0x03u;
        s_diag_frame[2] = 0x4cu;
        for (i = 0u; i < 76u; ++i) s_diag_frame[3u + i] = (u8)i;
        s_diag_frame[79] = 0xc9u; /* CRC16/Modbus of bytes 0..78 */
        s_diag_frame[80] = 0xb8u;
    }
    else
    {
        len = s_diag_lengths[index % 5u];
        for (i = 0u; i < len; ++i)
        {
            switch (index / 5u)
            {
            case 0u: s_diag_frame[i] = 0x55u; break;
            case 1u: s_diag_frame[i] = 0xaau; break;
            case 2u: s_diag_frame[i] = (u8)i; break;
            default: s_diag_frame[i] = (i & 1u) ? 0x49u : 0xeeu; break;
            }
        }
    }

    modbus_uart_send(s_diag_frame, len);
    if (g_bms_rs485_tx_diag.tx_start_count != started)
    {
        g_bms_rs485_tx_diag.pattern_index = (u8)((index + 1u) % RS485_DIAG_FRAME_COUNT);
        s_diag_last_start_tick = clock_time();
    }
}
#endif

void main_loop_modbus(void)
{
#if BMS_RS485_TX_DIAG_ENABLE
    modbus_rs485_service_tx_done();
    if (!modbus_uart_tx_active() && !uart_tx_is_busy() &&
        clock_time_exceed(s_diag_last_start_tick, RS485_DIAG_PERIOD_US))
    {
        modbus_uart_diag_send_next();
    }
#else
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
#endif
}

#endif
