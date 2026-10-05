/* 文件功能：SIF 单线输出及定时器中断状态；保持既有波形节拍，不在 ISR 中加入日志发送。
 * bms/platform/telink/sif_send.c；实际编译归属见各产品 sources.txt。
 */
#include <stdint.h>

#include "tl_common.h"
#include "drivers.h"
#include "sif_send.h"
#include "bms_state.h"
#include "bus_mux.h"

#define SIF_TIMER_INTERVAL_US 500u

void sif_timer_init(void)
{
    reg_irq_mask |= FLD_IRQ_TMR0_EN;
    reg_tmr0_tick = 0;
    reg_tmr0_capt = SIF_TIMER_INTERVAL_US * CLOCK_SYS_CLOCK_1US;
    reg_tmr_sta = FLD_TMR_STA_TMR0;
    reg_tmr_ctrl |= FLD_TMR0_EN;
    irq_enable();
}

_attribute_ram_code_ void sif_timer_irq_handler(void)
{
    if (reg_tmr_sta & FLD_TMR_STA_TMR0)
    {
        sif_send_data_handle();
        reg_tmr_sta = FLD_TMR_STA_TMR0;
    }
}

#ifdef _FUNC_SIF_

static void sif_send_private_cell_voltages(void);
static void sif_send_private_realtime_info(void);
static void sif_send_public_packet(void);

static inline void sif_turn_off(void)
{
    gpio_write(OWC_TX_PIN, 0);
}

static inline void sif_turn_on(void)
{
    gpio_write(OWC_TX_PIN, 1);
}

#define __TODO__ 0Xaa

#define SIF_VERSION 1
#define SIF_SYNC (2 * (10 + 1))
#define SIF_STOP (2 * 5)
#define SIF_SEND_COUNT 4

static volatile uint8_t sif_sync_tosc = 0;
static volatile uint8_t sif_send_tosc = 0;
static volatile SIF_STATE_E state_mode = SEND_DATA_COMPLETE;
static volatile int8_t bit_cnt = 7;
static volatile uint8_t byte_cnt = 0;

static volatile uint8_t sif_send_length = 0;

/* Explicit legacy TC32 -fpack-struct wire layout; no native struct image. */
#define SIF_PUBLIC_BYTES 20u
#define SIF_REALTIME_BYTES 33u
#define SIF_CELL_BYTES (4u + 2u * SeriesNum)
typedef char sif_cell_packet_fits[(SIF_CELL_BYTES <= 64u) ? 1 : -1];
static uint8_t s_packets[2][64];
static uint8_t s_packet_lengths[2];
static volatile uint8_t s_active_packet;
static volatile uint8_t s_packet_ready;
static volatile uint8_t s_prepare_packet;
static uint8_t s_iswakeup = 1u, s_public_count;
static uint16_t s_private_count;
static volatile uint8_t s_restart_packets;
static volatile uint8_t s_return_to_idle;
static uint32_t s_prepared_sample_tick;

static void sif_put_u16le(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static uint8_t sum_verify(const uint8_t *data, uint16_t length)
{
    uint16_t i;
    uint16_t res = 0;

    for (i = 0; i < length; i++)
    {
        res += data[i];
    }

    return (uint8_t)(res & 0xff);
}

static uint8_t sif_fault_code(void)
{
    const bms_fault_bits_t *fault = &g_stCellInfoReport.unMdlFault_Third.bits;
    uint8_t code = 0u;

    /* Preserve the established SIF rule: a later matching fault wins. */
    if (fault->b1IdischgOcp) code = 0x01u;
    if (g_stCellInfoReport.unMdlFault_Second.bits.b1IdischgOcp) code = 0x02u;
    if (fault->b1CellChgUtp) code = 0x03u;
    if (fault->b1CellChgOtp) code = 0x04u;
    if (fault->b1CellDischgOtp) code = 0x05u;
    if (fault->b1CellUvp) code = 0x06u;
    if (fault->b1CellOvp) code = 0x07u;
    if (fault->b1IchgOcp) code = 0x08u;
    if (fault->b1CellDischgUtp) code = 0x09u;
    return code;
}

void sif_send_data_handle(void)
{
    static uint16_t cnt = 0;
    uint8_t count = SIF_SEND_COUNT;
    const uint8_t *p = s_packets[s_active_packet];

    if (BUS_STATE_OWC_TX != bus_mux_get_state())
    {
        cnt = 0;
        state_mode = SIF_IDLE;
        s_restart_packets = 1u;
        return;
    }

    if ((state_mode == SIF_IDLE || state_mode == SEND_DATA_COMPLETE) && !s_packet_ready)
    {
        sif_turn_on();
        return;
    }

    switch (state_mode)
    {
    case SIF_IDLE:

        sif_turn_on();

        if (!gpio_read(OWC_RX_PIN))
        {
            s_return_to_idle = 1u;
            cnt = 0;
            state_mode = SIF_IDLE;
            s_restart_packets = 1u;
            return;
        }

        if (++cnt >= 2 * 1000)
        {
            cnt = 0;
            state_mode = SYNC_SIGNAL;
        }
        // sif_turn_off();
        break;
    case SYNC_SIGNAL:

        if (sif_sync_tosc < SIF_SYNC - 1 * 2)
        {
            sif_turn_off();
        }
        else
        {
            sif_turn_on();
        }
        sif_sync_tosc++;
        if (sif_sync_tosc >= SIF_SYNC)
        {
            sif_sync_tosc = 0;
            bit_cnt = 0;
            byte_cnt = 0;
            sif_send_tosc = 0;

            state_mode = SEND_DATA;

            s_active_packet = s_prepare_packet;
            sif_send_length = s_packet_lengths[s_active_packet];
            s_packet_ready = 0u;

        }
        break;
    case SEND_DATA:

        sif_send_tosc = sif_send_tosc % count;

        uint8_t data = (p[byte_cnt] >> bit_cnt) & 0x1;

        if (data)
        {
            if (sif_send_tosc == 0)
            {
                sif_turn_off();
                sif_send_tosc++;
            }
            else if (sif_send_tosc == 1)
            {
                sif_turn_on();
                sif_send_tosc++;
            }
            else if (sif_send_tosc == 3)
            {
                sif_send_tosc = 0;
            }
            else
            {
                sif_send_tosc++;
            }
        }
        else
        {
            if (sif_send_tosc == 0)
            {
                sif_turn_off();
                sif_send_tosc++;
            }
            else if (sif_send_tosc == 3)
            {
                sif_turn_on();
                sif_send_tosc = 0;
            }
            else
            {
                sif_send_tosc++;
            }
        }
        if (sif_send_tosc == 0)
        {
            if (++bit_cnt > 7)
            {
                byte_cnt++;
                bit_cnt = 0;
            }
            if (byte_cnt >= sif_send_length)
            {
                state_mode = STOP_SIGNAL;
                break;
            }
        }

        break;
    case STOP_SIGNAL:
    {
        if (sif_sync_tosc++ <= SIF_STOP)
        {
            sif_turn_off();
        }
        else
        {
            state_mode = SIF_IDLE;
            sif_turn_on();

            sif_sync_tosc = 0;
        }

        break;
    }
    case SEND_DATA_COMPLETE:
        if (++cnt >= 2 * 1500)
        {
            cnt = 0;
            state_mode = SYNC_SIGNAL;
        }
        sif_turn_on();
        break;
    default:
        break;
    }

}

/* Called only by main. IRQ consumes the other packet until STOP completes. */
void sif_prepare_task(uint32_t sample_tick)
{
    uint8_t refresh_type = 0u;
    uint8_t irq_state;
    if (s_return_to_idle) {
        s_return_to_idle = 0u;
        bus_mux_return_to_owc_idle();
    }
    if (BUS_STATE_OWC_TX != bus_mux_get_state()) return;
    if (s_restart_packets) {
        s_restart_packets = 0u;
        s_packet_ready = 0u;
        s_iswakeup = 1u;
        s_public_count = 0u;
        s_private_count = 0u;
    }
    /* Refresh a waiting frame after a new sample. Withdraw ready in a short
     * critical section so IRQ cannot claim a buffer while main rebuilds it. */
    irq_state = irq_disable();
    if (s_packet_ready) {
        if ((state_mode != SIF_IDLE && state_mode != SEND_DATA_COMPLETE) ||
            sample_tick == s_prepared_sample_tick) {
            irq_restore(irq_state);
            return;
        }
        refresh_type = s_packets[s_prepare_packet][0];
        s_packet_ready = 0u;
    }
    irq_restore(irq_state);
    s_prepare_packet = (uint8_t)(s_active_packet ^ 1u);
    if (refresh_type == 1u) {
        sif_send_public_packet();
    } else if (refresh_type == 0x3au) {
        sif_send_private_realtime_info();
    } else if (refresh_type == 0x3bu) {
        sif_send_private_cell_voltages();
    } else if (s_iswakeup) {
        sif_send_public_packet();
        if (++s_public_count >= 3u) {
            s_public_count = 0u;
            s_iswakeup = 0u;
        }
    } else if (++s_private_count <= 15u) {
        sif_send_private_realtime_info();
    } else {
        s_private_count = 0u;
        sif_send_private_cell_voltages();
    }
    s_prepared_sample_tick = sample_tick;
    /* Publish ready last; all payload bytes/length are complete before IRQ uses them. */
    __asm__ __volatile__("" ::: "memory");
    s_packet_ready = 1u;
}

static uint16_t sif_encoded_current(void)
{
    uint16_t current = g_stCellInfoReport.u16IDischg ?
                       g_stCellInfoReport.u16IDischg : g_stCellInfoReport.u16Ichg;
    return (uint16_t)((current / 10u + 500u) * 10u);
}

static uint8_t sif_work_status(void)
{
    return g_stCellInfoReport.u16IDischg ? 0u :
           (g_stCellInfoReport.u16Ichg ? 1u : 2u);
}

static void sif_send_public_packet(void)
{
    uint8_t *p = s_packets[s_prepare_packet];
    memset(p, 0, SIF_PUBLIC_BYTES);
    p[0] = 1u; p[1] = 1u; p[2] = 1u; p[3] = __TODO__; p[4] = 3u;
    sif_put_u16le(p + 5, SeriesNum * 36u);
    sif_put_u16le(p + 7, CapacityFactory);
    p[9] = (uint8_t)(g_stCellInfoReport.SocElement.u16Soc * 2u);
    sif_put_u16le(p + 10, g_stCellInfoReport.u16VCellTotle / 10u);
    sif_put_u16le(p + 12, sif_encoded_current());
    p[14] = (uint8_t)(g_stCellInfoReport.u16TempMax / 10u);
    p[15] = (uint8_t)(g_stCellInfoReport.u16TempMin / 10u);
    /* Preserve legacy public MOS truncation on the wire. */
    p[16] = (uint8_t)g_stCellInfoReport.u16Temperature[MOS_TEMP1];
    p[17] = sif_fault_code(); p[18] = sif_work_status();
    p[19] = sum_verify(p, SIF_PUBLIC_BYTES - 1u);
    s_packet_lengths[s_prepare_packet] = SIF_PUBLIC_BYTES;
}

static void sif_send_private_realtime_info(void)
{
    uint8_t *p = s_packets[s_prepare_packet];
    memset(p, 0, SIF_REALTIME_BYTES);
    p[0] = 0x3au; p[1] = 1u; p[2] = SIF_REALTIME_BYTES - 4u;
    p[3] = (uint8_t)(g_stCellInfoReport.SocElement.u16Soc * 2u);
    sif_put_u16le(p + 4, g_stCellInfoReport.u16VCellTotle / 10u);
    sif_put_u16le(p + 6, sif_encoded_current());
    p[8] = (uint8_t)(g_stCellInfoReport.u16TempMax / 10u);
    p[9] = (uint8_t)(g_stCellInfoReport.u16TempMin / 10u);
    p[10] = (uint8_t)(g_stCellInfoReport.u16Temperature[MOS_TEMP1] / 10u);
    p[11] = sif_fault_code(); p[12] = sif_work_status();
    sif_put_u16le(p + 14, g_stCellInfoReport.SocElement.u16Cycle_times);
    sif_put_u16le(p + 16, g_stCellInfoReport.u16VCellMax);
    sif_put_u16le(p + 18, g_stCellInfoReport.u16VCellMin);
    p[20] = (uint8_t)g_stCellInfoReport.u16VCellMaxPosition;
    p[21] = (uint8_t)g_stCellInfoReport.u16VCellMinPosition;
    p[22] = __TODO__;
    sif_put_u16le(p + 23, 43u * SeriesNum);
    p[25] = 20u; p[27] = __TODO__; p[28] = __TODO__;
    p[32] = sum_verify(p, SIF_REALTIME_BYTES - 1u);
    s_packet_lengths[s_prepare_packet] = SIF_REALTIME_BYTES;
}

static void sif_send_private_cell_voltages(void)
{
    uint8_t i;
    uint8_t *p = s_packets[s_prepare_packet];
    memset(p, 0, SIF_CELL_BYTES);
    p[0] = 0x3bu; p[1] = 1u; p[2] = 2u * SeriesNum;
    for (i = 0u; i < SeriesNum; ++i)
        sif_put_u16le(p + 3u + i * 2u, g_stCellInfoReport.u16VCell[i]);
    p[3u + SeriesNum * 2u] = __TODO__;
    s_packet_lengths[s_prepare_packet] = SIF_CELL_BYTES;
}

#else
void sif_prepare_task(uint32_t sample_tick) { (void)sample_tick; }
void sif_send_data_handle(void)
{
    /* 当前产品配置未启用 SIF，保留空入口以维持调度接口。 */
}
#endif /* _FUNC_SIF_ */
