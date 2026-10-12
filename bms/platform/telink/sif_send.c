/*
 * 文件功能：SIF 单线输出及定时器中断状态；保持既有波形节拍，
 * 不在 ISR 中加入日志发送。
 * bms/platform/telink/sif_send.c；实际编译归属见各产品 sources.txt。
 */
#include <stdint.h>

#include "tl_common.h"
#include "drivers.h"
#include "sif_send.h"
#include "bms_product.h"
#include "bms_state.h"
#include "bms_config_store.h"
#include "bus_mux.h"

#define SIF_TIMER_INTERVAL_US 500u

/* 初始化 SIF 波形使用的硬件定时器。 */
void sif_timer_init(void)
{
    reg_irq_mask |= FLD_IRQ_TMR0_EN;
    reg_tmr0_tick = 0;
    reg_tmr0_capt = SIF_TIMER_INTERVAL_US * CLOCK_SYS_CLOCK_1US;
    reg_tmr_sta = FLD_TMR_STA_TMR0;
    reg_tmr_ctrl |= FLD_TMR0_EN;
    irq_enable();
}

/* 按已准备数据推进单线位波形与结束阶段。 */
_attribute_ram_code_ void sif_timer_irq_handler(void)
{
    if (reg_tmr_sta & FLD_TMR_STA_TMR0)
    {
        sif_send_data_handle();
        reg_tmr_sta = FLD_TMR_STA_TMR0;
    }
}

#if BMS_PRODUCT_SIF_ENABLE

/* 构造 SIF 私有单体电压包，只使用有效串数。 */
static void sif_send_private_cell_voltages(void);
/* 构造 SIF 私有实时测量报告包。 */
static void sif_send_private_realtime_info(void);
/* 构造公共 SIF 报告包供定时器发送。 */
static void sif_send_public_packet(void);

/* 停止单线发送并释放输出状态。 */
static inline void sif_turn_off(void)
{
    gpio_write(BMS_BOARD_OWC_TX_PIN, 0);
}

/* 启动单线发送的定时器和输出状态。 */
static inline void sif_turn_on(void)
{
    gpio_write(BMS_BOARD_OWC_TX_PIN, 1);
}

#define SIF_INVALID_U8 0xffu
#define SIF_INVALID_U16 0xffffu
#define SIF_CELL_PERIOD_TICKS (60000000u * SYSTEM_TIMER_TICK_1US)

#define SIF_SYNC (2 * (10 + 1))
#define SIF_STOP (2 * 5)
#define SIF_SEND_COUNT 4

static volatile uint8_t sif_sync_tosc = 0;
static volatile uint8_t sif_send_tosc = 0;
static volatile SIF_STATE_E state_mode = SEND_DATA_COMPLETE;
static volatile int8_t bit_cnt = 7;
static volatile uint8_t byte_cnt = 0;

static volatile uint8_t sif_send_length = 0;

/* 显式表达遗留 TC32 -fpack-struct 线格式，不直接发送本机结构体映像。 */
#define SIF_PUBLIC_BYTES 20u
#define SIF_REALTIME_BYTES 33u
#define SIF_CELL_BYTES (4u + 2u * BMS_PRODUCT_CELL_COUNT)
typedef char sif_cell_packet_fits[(SIF_CELL_BYTES <= 64u) ? 1 : -1];
static uint8_t s_packets[2][64];
static uint8_t s_packet_lengths[2];
static volatile uint8_t s_active_packet;
static volatile uint8_t s_packet_ready;
static volatile uint8_t s_prepare_packet;
static uint8_t s_iswakeup = 1u, s_public_count;
static uint32_t s_cell_packet_tick;
static uint8_t s_first_cell_packet = 1u;
static volatile uint8_t s_restart_packets;
static volatile uint8_t s_return_to_idle;
static uint32_t s_prepared_sample_tick;

/* 将 16 位值按 SIF 小端线格式写入包。 */
static void sif_put_u16le(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

/* 计算 SIF 包的累加校验值。 */
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

/* 按既有优先顺序映射 SIF 故障编号。 */
static uint8_t sif_fault_code(void)
{
    const bms_fault_bits_t *fault = &g_bms_report.fault_third.bits;
    uint8_t code = 0u;

    /* 保留 SIF 规则：后匹配的故障覆盖先匹配故障。 */
    if (fault->discharge_ocp) code = 0x01u;
    if (g_bms_report.fault_second.bits.discharge_ocp) code = 0x02u;
    if (fault->charge_utp) code = 0x03u;
    if (fault->charge_otp) code = 0x04u;
    if (fault->discharge_otp) code = 0x05u;
    if (fault->cell_uvp) code = 0x06u;
    if (fault->cell_ovp) code = 0x07u;
    if (fault->charge_ocp) code = 0x08u;
    if (fault->discharge_utp) code = 0x09u;
    return code;
}

/* 处理 SIF 包发送状态；未启用产品保留空入口。 */
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

        if (!gpio_read(BMS_BOARD_OWC_RX_PIN))
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
        if (sif_sync_tosc++ < SIF_STOP)
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

/* 仅由主循环调用；IRQ 使用另一包直至 STOP 完成。 */
/* 启用 SIF 时重建待发送包并发布 ready；关闭时为空入口。 */
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
        s_first_cell_packet = 1u;
    }
    /* 新样本后刷新等待帧；短临界区内撤销 ready，防止主循环重建时 IRQ 抢占缓冲区。 */
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
    } else if (s_first_cell_packet ||
               (uint32_t)(sample_tick - s_cell_packet_tick) >= SIF_CELL_PERIOD_TICKS) {
        s_first_cell_packet = 0u;
        s_cell_packet_tick = sample_tick;
        sif_send_private_cell_voltages();
    } else {
        sif_send_private_realtime_info();
    }
    s_prepared_sample_tick = sample_tick;
    /* 最后发布 ready；IRQ 使用前载荷与长度须全部完成。 */
    __asm__ __volatile__("" ::: "memory");
    s_packet_ready = 1u;
}

/* A*10 保留小数；正放电、负充电，线值 = A*10 + 5000。 */
static uint16_t sif_encoded_current(void)
{
    uint16_t discharge = g_bms_report.discharge_current_a10;
    uint16_t charge = g_bms_report.charge_current_a10;
    if (discharge > 5000u || charge > 5000u || (discharge && charge))
        return SIF_INVALID_U16;
    return (uint16_t)(5000u + discharge - charge);
}

/* 协议没有静置状态：用范围内保留值 0x0f，不能把零电流标成回馈。 */
static uint8_t sif_work_status(void)
{
    if (g_bms_report.discharge_current_a10 && g_bms_report.charge_current_a10) return 0x0fu;
    return g_bms_report.discharge_current_a10 ? 0u :
           (g_bms_report.charge_current_a10 ? 1u : 0x0fu);
}

/* 报告的 0 表示温度无效；有效编码已带 +40 偏移，只需由 0.1℃ 转为 1℃。 */
static uint8_t sif_temperature(uint16_t encoded_x10)
{
    return encoded_x10 && encoded_x10 <= 1900u ? (uint8_t)(encoded_x10 / 10u) : SIF_INVALID_U8;
}

static uint8_t sif_soc(void)
{
    return g_bms_report.soc.soc_percent <= 100u ?
           (uint8_t)(g_bms_report.soc.soc_percent * 2u) : SIF_INVALID_U8;
}

static uint16_t sif_extreme_cell(uint16_t mv)
{
    return mv && mv <= 10000u ? mv : SIF_INVALID_U16;
}

static uint8_t sif_cell_index(uint16_t index)
{
    return index && index <= BMS_PRODUCT_CELL_COUNT && index <= 25u ?
           (uint8_t)index : SIF_INVALID_U8;
}

/* 构造公共 SIF 报告包供定时器发送。 */
static void sif_send_public_packet(void)
{
    uint8_t *p = s_packets[s_prepare_packet];
    bms_config_system_params_t system;
    uint16_t capacity = SIF_INVALID_U16;
    if (bms_config_store_get_system(&system) && system.capacity_factory > 0u &&
        system.capacity_factory <= 10000u) capacity = (uint16_t)system.capacity_factory;
    memset(p, 0, SIF_PUBLIC_BYTES);
    p[0] = 1u; p[1] = 1u;
    /* 无已配置的协议厂家代码或电池型号，不沿用占位品牌/型号。 */
    p[2] = SIF_INVALID_U8; p[3] = SIF_INVALID_U8;
    p[4] = BMS_PRODUCT_CHEMISTRY == BMS_SOC_CHEMISTRY_LFP ? 1u : 3u;
    /* 没有独立的额定电压参数，不能拿实测电压或保护阈值代替。 */
    sif_put_u16le(p + 5, SIF_INVALID_U16);
    sif_put_u16le(p + 7, capacity);
    p[9] = sif_soc();
    sif_put_u16le(p + 10, g_bms_report.pack_voltage_10mv / 10u);
    sif_put_u16le(p + 12, sif_encoded_current());
    p[14] = sif_temperature(g_bms_report.temperature_max_x10);
    p[15] = sif_temperature(g_bms_report.temperature_min_x10);
    p[16] = sif_temperature(g_bms_report.temperature_x10[MOS_TEMP1]);
    p[17] = sif_fault_code(); p[18] = sif_work_status();
    p[19] = sum_verify(p, SIF_PUBLIC_BYTES - 1u);
    s_packet_lengths[s_prepare_packet] = SIF_PUBLIC_BYTES;
}

/* 构造 SIF 私有实时测量报告包。 */
static void sif_send_private_realtime_info(void)
{
    uint8_t *p = s_packets[s_prepare_packet];
    memset(p, 0, SIF_REALTIME_BYTES);
    p[0] = 0x3au; p[1] = 1u; p[2] = SIF_REALTIME_BYTES - 4u;
    p[3] = sif_soc();
    sif_put_u16le(p + 4, g_bms_report.pack_voltage_10mv / 10u);
    sif_put_u16le(p + 6, sif_encoded_current());
    p[8] = sif_temperature(g_bms_report.temperature_max_x10);
    p[9] = sif_temperature(g_bms_report.temperature_min_x10);
    p[10] = sif_temperature(g_bms_report.temperature_x10[MOS_TEMP1]);
    p[11] = sif_fault_code(); p[12] = sif_work_status();
    sif_put_u16le(p + 14, g_bms_report.soc.cycle_count <= 60000u ?
                 g_bms_report.soc.cycle_count : SIF_INVALID_U16);
    sif_put_u16le(p + 16, sif_extreme_cell(g_bms_report.cell_max_mv));
    sif_put_u16le(p + 18, sif_extreme_cell(g_bms_report.cell_min_mv));
    p[20] = sif_cell_index(g_bms_report.cell_max_index);
    p[21] = sif_cell_index(g_bms_report.cell_min_index);
    /* 未实现充电协商，按协议发送无效值，不能广播硬编码的充电请求。 */
    p[22] = SIF_INVALID_U8;
    sif_put_u16le(p + 23, SIF_INVALID_U16);
    p[25] = SIF_INVALID_U8; p[26] = SIF_INVALID_U8;
    /* 解码/加密算法另行提供；key=0 不属于合法 1..255，明确不提供认证结果。
     * p[13] 和 p[27..31] 保留零，不得把这些字节当成真实 MOS/许可状态。 */
    p[32] = sum_verify(p, SIF_REALTIME_BYTES - 1u);
    s_packet_lengths[s_prepare_packet] = SIF_REALTIME_BYTES;
}

/* 构造 SIF 私有单体电压包，只使用有效串数。 */
static void sif_send_private_cell_voltages(void)
{
    uint8_t i;
    uint8_t *p = s_packets[s_prepare_packet];
    memset(p, 0, SIF_CELL_BYTES);
    p[0] = 0x3bu; p[1] = 1u; p[2] = 2u * BMS_PRODUCT_CELL_COUNT;
    for (i = 0u; i < BMS_PRODUCT_CELL_COUNT; ++i) {
        uint16_t mv = g_bms_report.cell_voltage_mv[i];
        /* 保留全项目的 61001 空串约定；其它超协议范围值不得回绕或冒充电压。 */
        if (mv > 60000u && mv != 61001u) mv = SIF_INVALID_U16;
        sif_put_u16le(p + 3u + i * 2u, mv);
    }
    p[SIF_CELL_BYTES - 1u] = sum_verify(p, SIF_CELL_BYTES - 1u);
    s_packet_lengths[s_prepare_packet] = SIF_CELL_BYTES;
}

#else
/* 主循环准备待发送 SIF 包；未启用产品保留空入口。 */
/* 启用 SIF 时重建待发送包并发布 ready；关闭时为空入口。 */
void sif_prepare_task(uint32_t sample_tick) { (void)sample_tick; }
/* 处理 SIF 包发送状态；未启用产品保留空入口。 */
void sif_send_data_handle(void)
{
    /* 当前产品配置未启用 SIF，保留空入口以维持调度接口。 */
}
#endif /* 条件编译结束： BMS_PRODUCT_SIF_ENABLE */
