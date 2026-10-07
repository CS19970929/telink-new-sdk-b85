/*
 * 文件功能：UART DMA 与可选 RS485 方向控制；ISR 交付状态，主循环维护协议和恢复，
 * 保留产品通信差异。
 * bms/platform/telink/modbus_uart.c；实际编译归属见各产品 sources.txt。
 */
#include "bms_debug_log.h"
#include "bms_afe_backend.h"

#include "modbus_uart.h"
#include "app_config.h"
#include "tl_common.h"
#include "drivers.h"
#include "modbus_rtu.h"
#include "bms_product.h"
#include "bms_afe_backend.h"
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
#include "bus_mux.h"
#endif

typedef struct __attribute__((aligned(4))) {
    u32 dma_len;
    u8 data[MODBUS_RTU_FRAME_CAPACITY];
} mb_dma_pkt_t;

typedef char modbus_dma_packet_size_must_be_272[
    (sizeof(mb_dma_pkt_t) == 272u) ? 1 : -1];

#define MODBUS_UART_CLOCK_DIVIDER 9u
#define MODBUS_UART_BWPC          13u
#define MODBUS_UART_BITS_PER_CHAR 10u /* 8N1：起始位 + 8 个数据位 + 停止位。 */
#define MODBUS_UART_RECOVERY_US   (1u * 1000u * 1000u)

/*
 * 理论整帧时间后的附加余量；禁止当作固定毫秒延时。
 * DE 主要保持时间按实际应答长度及B85 UART divider/BWPC 配置计算。
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

/* 将 RS485 方向切换为接收。 */
static void modbus_rs485_receive_mode(void)
{
    /* D014 CA-IS2092A：DE 和 /RE 共用 PA1，0 表示接收。 */
    gpio_write(BMS_BOARD_RS485_EN_PIN, 0);
    g_bms_rs485_tx_diag.de_state = 0u;
}

/* 将 RS485 方向切换为发送。 */
static void modbus_rs485_transmit_mode(void)
{
    /* D014 CA-IS2092A：DE 和 /RE 共用 PA1，1 表示发送。 */
    gpio_write(BMS_BOARD_RS485_EN_PIN, 1);
    g_bms_rs485_tx_diag.de_state = 1u;
}

/* 根据实际 UART 配置与帧长度计算 DE 最短保持微秒数。 */
static u32 modbus_rs485_min_hold_us(u32 len)
{
    /*
     * B85 UART 位时钟：baud = SYSCLK / ((divider + 1) * (BWPC + 1))。
     * 16 MHz、divider=9、BWPC=13 时实际约 114285.7 波特，每个 8N1 字符占 87.5 us。
     * 按系统 tick 计算 DE 最小保持时间以跟随实际配置，不假定理想 115200 波特率。
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

/*
 * DMA 完成不代表停止位离开引脚；
 * 同时满足 UART TX_DONE 与计算的最短线时长后才释放 DE。
 */
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
        /* 超时表示帧被中止，绝不能视为成功完成。 */
        {
            u8 irq_state = irq_disable();
            dma_chn_enable(FLD_DMA_CHN_UART_RX | FLD_DMA_CHN_UART_TX, 0);
            uart_dma_enable(0, 0);
            uart_reset();
            uart_ndma_clear_tx_index();
            uart_ndma_clear_rx_index();
            modbus_rs485_receive_mode();
            uart_init(MODBUS_UART_CLOCK_DIVIDER, MODBUS_UART_BWPC, PARITY_NONE, STOP_BIT_ONE);
            uart_irq_enable(0, 0);
            dma_chn_irq_status_clr(FLD_DMA_CHN_UART_RX | FLD_DMA_CHN_UART_TX);
            s_rx_ready = 0u;
            s_rx_pkt.dma_len = 0u;
#if BMS_RS485_TX_DIAG_ENABLE
            uart_dma_enable(0, 1);
#else
            uart_recbuff_init((u8 *)&s_rx_pkt, sizeof(s_rx_pkt));
            uart_dma_enable(1, 1);
#endif
            s_rs485_tx_active = 0u;
            s_rs485_tx_dma_done = s_rs485_tx_uart_done = 0u;
            irq_restore(irq_state);
        }
        return;
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
     * B85 官方 UART TX_DONE 是发送完成依据，
     * 同时必须满足从启动 uart_send_dma() 起算的理论整帧时间；两者满足后才释放 DE，
     * 防止异常提前的状态/IRQ 观测导致过早切换。
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

/* 查询 UART 发送或 DE 保持阶段是否仍活动。 */
u8 modbus_uart_tx_active(void)
{
#if MODBUS_RS485_ENABLE
    return s_rs485_tx_active;
#else
    return uart_tx_is_busy();
#endif
}

/* 初始化产品 UART、DMA 缓冲区及 RS485 方向。 */
void modbus_uart_init(void)
{
#if !BMS_RS485_TX_DIAG_ENABLE
    /* 保留已验证的 new-new-master 初始化顺序。 */
    memset((void *)&s_rx_pkt, 0, sizeof(s_rx_pkt));
    uart_recbuff_init((u8 *)&s_rx_pkt, sizeof(s_rx_pkt));
#endif

#if MODBUS_RS485_ENABLE
    /* D014：PC2=TX，PC3=RX，PA1 控制 485 DE 和 /RE。 */
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

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    uart_gpio_set(OWC_TX_PIN, OWC_RX_PIN);
#else
    uart_gpio_set(BMS_BOARD_SCI1_TX_PIN, BMS_BOARD_SCI1_RX_PIN);
#endif
    uart_reset();
    uart_init(MODBUS_UART_CLOCK_DIVIDER,
              MODBUS_UART_BWPC,
              PARITY_NONE,
              STOP_BIT_ONE);
#if BMS_RS485_TX_DIAG_ENABLE
    /* 保持 RX 关闭，避免主动请求影响线路测试。 */
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
    s_rx_ready = 0u;
    irq_enable();
}

/* 中断只清 DMA 标志并交付状态；运行日志在主循环生成，避免格式化和额外发送影响时序。 */
void modbus_uart_irq_proc(void)
{
    u8 irqsrc = dma_chn_irq_status_get();

    if (irqsrc & FLD_DMA_CHN_UART_RX)
    {
        dma_chn_enable(FLD_DMA_CHN_UART_RX, 0);
        dma_chn_irq_status_clr(FLD_DMA_CHN_UART_RX);
        /* 无效/零长度也必须经过 poll，以重新启用 RX。 */
        s_rx_ready = 1u;
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
        if (s_rx_pkt.dma_len > 0u) bus_mux_on_uart_rx_byte();
#endif
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
        /* 主循环负责 UART 状态、超时恢复和 DE 释放。 */
#endif
    }
}

/* 主循环处理收发状态、超时恢复与 DE 释放。 */
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

/* 检查发送状态与长度后启动 UART DMA，不覆盖活动缓冲区。 */
u8 modbus_uart_send(const u8 *p, u32 len)
{
    if (p == NULL || len == 0u || len > sizeof(s_tx_pkt.data)) return 0u;

    /* uart_send_dma() 无条件重启 DMA，禁止覆盖其使用中的缓冲区。 */
#if MODBUS_RS485_ENABLE
    modbus_rs485_service_tx_done();
    if (s_rs485_tx_active || uart_tx_is_busy())
#else
    if (uart_tx_is_busy())
#endif
    {
        ++g_bms_rs485_tx_diag.tx_busy_reject_count;
        BMS_LOG(BMS_LOG_WARN, BMS_LOG_COMM, BMS_LOG_TX_BUSY, len, g_bms_rs485_tx_diag.tx_busy_reject_count);
        return 0u;
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

    /* 已验证的 new-new-master TX 路径：DMA 包为 [u32 长度 + 载荷]。 */
    uart_send_dma((u8 *)&s_tx_pkt);
    return 1u;
}

#if !BMS_RS485_TX_DIAG_ENABLE
/* 清除接收状态并重新启用 RX DMA。 */
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

/* 20 种图案/长度组合后发送 01 03 4C + 00..4B + C9 B8。 */
static const u8 s_diag_lengths[5] = {8u, 32u, 64u, 81u, 128u};
static u8 s_diag_frame[128];
static u32 s_diag_last_start_tick;

/* 线路测试构建中发送下一组诊断图案。 */
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
        s_diag_frame[79] = 0xc9u; /* 字节 0..78 的 CRC16/Modbus。 */
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

/* 接收帧解析、应答与 RX 恢复的主循环所有者；日志读取走同一 TX 路径，不插入裸文本。 */
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
#if BMS_DEBUG_LOG_ENABLE
        if (!bms_debug_log_is_read(req, req_len))
            BMS_LOG(ok ? BMS_LOG_DEBUG : BMS_LOG_WARN, BMS_LOG_COMM, BMS_LOG_UART_FRAME,
                    (req_len << 16) | (req_len >= 2u ? req[1] : 0u), rsp_len);
#endif

        /* 沿用已验证实现：每帧后都重新启用 RX。 */
        modbus_uart_rx_reset();

        if (ok && rsp_len)
        {
            if (modbus_uart_send(rsp_buf, rsp_len)) {
                mb_last_ok_tick = clock_time();
                mb_bad_cnt = 0u;
            } else {
                ++mb_bad_cnt;
            }
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
