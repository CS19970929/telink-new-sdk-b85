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
#include "bms_afe.h"
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
/* 使用已提交的配置 SN 构造电池编码；不使用共享的编译默认身份。 */
static void sif_send_private_battery_code(void);
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
/* 高4位主版本1、低4位次版本3；文档A1.3.3的末位修订号不另占线字节。 */
#define SIF_PROTOCOL_VERSION 0x13u
#define SIF_CELL_PERIOD_TICKS (60000000u * SYSTEM_TIMER_TICK_1US)
#define SIF_STARTUP_CODE_PACKETS 3u

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
static uint8_t s_code_packet_due;
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

/* A1.3.3 表 A.4：保护优先于异常报警，再优先于预警。
 * 只映射已有状态，不从 MOS 过温猜测 MOS 损坏，也不新增保护阈值。 */
static uint8_t sif_fault_code(void)
{
    const bms_fault_bits_t *warning = &g_bms_report.fault_first.bits;
    const bms_fault_bits_t *alarm = &g_bms_report.fault_second.bits;
    const bms_fault_bits_t *fault = &g_bms_report.fault_third.bits;
    uint8_t code = 0u;

    /* 表中没有独立异常报警码的 Second 项仍报告对应预警。 */
    if (warning->discharge_ocp) code = 0x22u;
    if (warning->charge_utp || alarm->charge_utp) code = 0x23u;
    if (warning->charge_otp) code = 0x24u;
    if (warning->discharge_otp) code = 0x25u;
    if (warning->cell_uvp || warning->pack_uvp || alarm->cell_uvp || alarm->pack_uvp) code = 0x26u;
    if (warning->cell_ovp || warning->pack_ovp || alarm->cell_ovp || alarm->pack_ovp) code = 0x27u;
    if (warning->charge_ocp || alarm->charge_ocp) code = 0x28u;
    if (warning->discharge_utp || alarm->discharge_utp) code = 0x29u;
    if (alarm->charge_otp) code = 0x14u;
    if (alarm->discharge_otp) code = 0x15u;
    /* 保留本项目 Second 放电过流对应 DOC1P、Third 对应 DOC2P 的业务映射。 */
    if (alarm->discharge_ocp) code = 0x02u;
    if (fault->discharge_ocp) code = 0x01u;
    if (fault->charge_utp) code = 0x03u;
    if (fault->charge_otp) code = 0x04u;
    if (fault->discharge_otp) code = 0x05u;
    if (fault->cell_uvp || fault->pack_uvp) code = 0x06u;
    if (fault->cell_ovp || fault->pack_ovp) code = 0x07u;
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
        sif_sync_tosc = 0u;
        sif_send_tosc = 0u;
        bit_cnt = 0;
        byte_cnt = 0u;
        state_mode = SIF_IDLE;
        s_packet_ready = 0u;
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
        s_code_packet_due = 0u;
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
    } else if (refresh_type == 0x3cu) {
        sif_send_private_battery_code();
    } else if (s_iswakeup) {
        sif_send_public_packet();
        if (++s_public_count >= 3u) {
            s_public_count = 0u;
            s_iswakeup = 0u;
        }
    } else if (s_code_packet_due) {
        --s_code_packet_due;
        sif_send_private_battery_code();
    } else if (s_first_cell_packet ||
               (uint32_t)(sample_tick - s_cell_packet_tick) >= SIF_CELL_PERIOD_TICKS) {
        s_code_packet_due = s_first_cell_packet ? SIF_STARTUP_CODE_PACKETS : 1u;
        s_first_cell_packet = 0u;
        s_cell_packet_tick = sample_tick;
        sif_send_private_cell_voltages();
        /* 接入先发数帧编码；后续编码紧随每周期单体包，共用周期计时器。 */
    } else {
        sif_send_private_realtime_info();
    }
    s_prepared_sample_tick = sample_tick;
    /* 最后发布 ready；IRQ 使用前载荷与长度须全部完成。 */
    __asm__ __volatile__("" ::: "memory");
    s_packet_ready = 1u;
}

/* A*10 保留小数；正放电、负充电，线值 = A*10 + 5000。 */
static uint16_t sif_encoded_current(uint8_t valid)
{
    uint16_t discharge = g_bms_report.discharge_current_a10;
    uint16_t charge = g_bms_report.charge_current_a10;
    if (!valid || discharge > 5000u || charge > 5000u || (discharge && charge))
        return SIF_INVALID_U16;
    return (uint16_t)(5000u + discharge - charge);
}

/* 未实现应答业务，高三位固定为 0（不请求应答）。文档没有静置状态，
 * 静置/测量无效使用 FF 表示本项目未提供状态，不能伪造回馈或占用保留位。 */
static uint8_t sif_work_status(uint8_t valid)
{
    if (sif_encoded_current(valid) == SIF_INVALID_U16) return SIF_INVALID_U8;
    return g_bms_report.discharge_current_a10 ? 0u :
           (g_bms_report.charge_current_a10 ? 1u : SIF_INVALID_U8);
}

/* 只查询后端 RAM 快照与已有总线门禁，不访问 AFE，也不改变保护报告。 */
static uint8_t sif_measurements_valid(void)
{
    bms_afe_aux_measurements_t sample;
    return bms_afe_get_aux_measurements(&sample);
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
    uint8_t valid = sif_measurements_valid();
    uint16_t capacity = SIF_INVALID_U16;
    if (bms_config_store_get_system(&system) && system.capacity_factory > 0u &&
        system.capacity_factory <= 10000u) capacity = (uint16_t)system.capacity_factory;
    memset(p, 0, SIF_PUBLIC_BYTES);
    p[0] = 1u; p[1] = SIF_PROTOCOL_VERSION;
    /* 无已配置的协议厂家代码或电池型号，不沿用占位品牌/型号。 */
    p[2] = SIF_INVALID_U8; p[3] = SIF_INVALID_U8;
    p[4] = BMS_PRODUCT_CHEMISTRY == BMS_SOC_CHEMISTRY_LFP ? 1u : 3u;
    /* 没有独立的额定电压参数，不能拿实测电压或保护阈值代替。 */
    sif_put_u16le(p + 5, SIF_INVALID_U16);
    sif_put_u16le(p + 7, capacity);
    p[9] = valid ? sif_soc() : SIF_INVALID_U8;
    sif_put_u16le(p + 10, valid ? g_bms_report.pack_voltage_10mv / 10u : SIF_INVALID_U16);
    sif_put_u16le(p + 12, sif_encoded_current(valid));
    p[14] = sif_temperature(valid ? g_bms_report.temperature_max_x10 : 0u);
    p[15] = sif_temperature(valid ? g_bms_report.temperature_min_x10 : 0u);
    p[16] = sif_temperature(valid ? g_bms_report.temperature_x10[MOS_TEMP1] : 0u);
    p[17] = sif_fault_code(); p[18] = sif_work_status(valid);
    p[19] = sum_verify(p, SIF_PUBLIC_BYTES - 1u);
    s_packet_lengths[s_prepare_packet] = SIF_PUBLIC_BYTES;
}

/* 构造 SIF 私有实时测量报告包。 */
static void sif_send_private_realtime_info(void)
{
    uint8_t *p = s_packets[s_prepare_packet];
    uint8_t valid = sif_measurements_valid();
    memset(p, SIF_INVALID_U8, SIF_REALTIME_BYTES);
    p[0] = 0x3au; p[1] = SIF_PROTOCOL_VERSION; p[2] = SIF_REALTIME_BYTES - 4u;
    p[3] = valid ? sif_soc() : SIF_INVALID_U8;
    sif_put_u16le(p + 4, valid ? g_bms_report.pack_voltage_10mv / 10u : SIF_INVALID_U16);
    sif_put_u16le(p + 6, sif_encoded_current(valid));
    p[8] = sif_temperature(valid ? g_bms_report.temperature_max_x10 : 0u);
    p[9] = sif_temperature(valid ? g_bms_report.temperature_min_x10 : 0u);
    p[10] = sif_temperature(valid ? g_bms_report.temperature_x10[MOS_TEMP1] : 0u);
    p[11] = sif_fault_code(); p[12] = sif_work_status(valid);
    sif_put_u16le(p + 14, g_bms_report.soc.cycle_count <= 60000u ?
                 g_bms_report.soc.cycle_count : SIF_INVALID_U16);
    sif_put_u16le(p + 16, valid ? sif_extreme_cell(g_bms_report.cell_max_mv) : SIF_INVALID_U16);
    sif_put_u16le(p + 18, valid ? sif_extreme_cell(g_bms_report.cell_min_mv) : SIF_INVALID_U16);
    p[20] = valid ? sif_cell_index(g_bms_report.cell_max_index) : SIF_INVALID_U8;
    p[21] = valid ? sif_cell_index(g_bms_report.cell_min_index) : SIF_INVALID_U8;
    /* 未实现充电协商，按协议发送无效值，不能广播硬编码的充电请求。 */
    p[22] = SIF_INVALID_U8;
    sif_put_u16le(p + 23, SIF_INVALID_U16);
    p[25] = SIF_INVALID_U8; p[26] = SIF_INVALID_U8;
    /* 用户明确不实现加密/充电策略/互认。key=0 标记未实现，不是假随机数；
     * 状态与加密结果为 FF，不得解读为认证或许可结果。 */
    p[27] = 0u;
    p[32] = sum_verify(p, SIF_REALTIME_BYTES - 1u);
    s_packet_lengths[s_prepare_packet] = SIF_REALTIME_BYTES;
}

/* 构造 SIF 私有单体电压包，只使用有效串数。 */
static void sif_send_private_cell_voltages(void)
{
    uint8_t i;
    uint8_t valid = sif_measurements_valid();
    uint8_t *p = s_packets[s_prepare_packet];
    memset(p, 0, SIF_CELL_BYTES);
    p[0] = 0x3bu; p[1] = SIF_PROTOCOL_VERSION; p[2] = 2u * BMS_PRODUCT_CELL_COUNT;
    for (i = 0u; i < BMS_PRODUCT_CELL_COUNT; ++i) {
        uint16_t mv = g_bms_report.cell_voltage_mv[i];
        /* 仅在一线通边界转为 A1.3.3 无效值；内部 61001 及其它协议保持原约定。 */
        if (!valid || mv > 60000u) mv = SIF_INVALID_U16;
        sif_put_u16le(p + 3u + i * 2u, mv);
    }
    p[SIF_CELL_BYTES - 1u] = sum_verify(p, SIF_CELL_BYTES - 1u);
    s_packet_lengths[s_prepare_packet] = SIF_CELL_BYTES;
}

/* A1.3.3 表 A.7：ASCII 不带 NUL，len 为字符数，SUM 覆盖整个头和载荷。 */
static void sif_send_private_battery_code(void)
{
    bms_user_params_t user;
    uint8_t length = 0u;
    uint8_t has_code = 0u;
    uint8_t *p = s_packets[s_prepare_packet];
    p[0] = 0x3cu;
    p[1] = SIF_PROTOCOL_VERSION;
    /* getter 只读已提交的 Config RAM 缓存；暂存 SN 和 Flash 写入不属于发送路径。
     * SN 当前最多32字节；若以后扩大存储字段，不能静默截断成另一条编码。 */
    if (sizeof(user.serial) <= sizeof(s_packets[0]) - 4u && bms_config_get_user(&user)) {
        while (length < sizeof(user.serial)) {
            uint8_t value = (uint8_t)user.serial[length];
            if (value == 0u) break;
            if (value < 0x20u || value > 0x7eu) {
                has_code = 0u;
                break;
            }
            p[3u + length++] = value;
            if (value != 0x20u) has_code = 1u;
        }
    }
    /* 空白/无效身份使用一个无效字节，不能发 len=0，也不伪造默认唯一编码。 */
    if (!has_code) {
        length = 1u;
        p[3] = SIF_INVALID_U8;
    }
    p[2] = length;
    p[3u + length] = sum_verify(p, 3u + length);
    s_packet_lengths[s_prepare_packet] = (uint8_t)(length + 4u);
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
