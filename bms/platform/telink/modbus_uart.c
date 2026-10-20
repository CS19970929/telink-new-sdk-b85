/*
 * 文件功能：UART DMA 与可选 RS485 方向控制；ISR 交付状态，主循环维护协议和恢复，
 * 保留产品通信差异。
 * bms/platform/telink/modbus_uart.c；实际编译归属见各产品 sources.txt。
 */
#include "bms_time.h"
#include "bms_debug_log.h"
#include "bms_afe_backend.h"

#include "modbus_uart.h"
#include "app_config.h"
#include "tl_common.h"
#include "drivers.h"
#include "modbus_rtu.h"
#include "bms_product.h"
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
#include "bus_mux.h"
#endif

typedef struct __attribute__((aligned(4))) {
    u32 dma_len;
    u8 data[MODBUS_RTU_FRAME_CAPACITY];
} mb_dma_pkt_t;

typedef char modbus_dma_packet_size_must_be_272[
    (sizeof(mb_dma_pkt_t) == 272u) ? 1 : -1];

#ifndef BMS_PRODUCT_UART_BAUD_RATE
#define BMS_PRODUCT_UART_BAUD_RATE 115200u
#endif

#if CLOCK_SYS_CLOCK_HZ != 16000000u
#error "Modbus UART divider configuration requires a 16 MHz system clock"
#endif

/* B85 SDK 的 16 MHz 配置表；初始化与超时恢复使用同一组参数。 */
#if BMS_PRODUCT_UART_BAUD_RATE == 9600u
#define MODBUS_UART_CLOCK_DIVIDER 118u
#define MODBUS_UART_BWPC          13u
/* 268 字节最长帧约 279 ms，必须允许完整发送后再判超时。 */
#define MODBUS_RS485_TX_TIMEOUT_US 350000u
#elif BMS_PRODUCT_UART_BAUD_RATE == 19200u
#define MODBUS_UART_CLOCK_DIVIDER 118u
#define MODBUS_UART_BWPC          6u
/* 268 字节最长帧约 140 ms，保留发送完成与主循环调度余量。 */
#define MODBUS_RS485_TX_TIMEOUT_US 200000u
#elif BMS_PRODUCT_UART_BAUD_RATE == 115200u
#define MODBUS_UART_CLOCK_DIVIDER 9u
#define MODBUS_UART_BWPC          13u
#define MODBUS_RS485_TX_TIMEOUT_US 50000u
#else
#error "Unsupported BMS_PRODUCT_UART_BAUD_RATE"
#endif
#define MODBUS_UART_BITS_PER_CHAR 10u /* 8N1：起始位 + 8 个数据位 + 停止位。 */
#define MODBUS_UART_RECOVERY_US   (1u * 1000u * 1000u)

/*
 * 理论整帧时间后的附加余量；禁止当作固定毫秒延时。
 * DE 主要保持时间按实际应答长度及B85 UART divider/BWPC 配置计算。
 */
#define MODBUS_RS485_TX_EXTRA_GUARD_US 200u

#if ((BMS_PRODUCT_RS485_ENABLE != 0) && (BMS_PRODUCT_RS485_ENABLE != 1))
#error "BMS_PRODUCT_RS485_ENABLE must be 0 or 1"
#endif
#if ((BMS_RS485_TX_DIAG_ENABLE != 0) && (BMS_RS485_TX_DIAG_ENABLE != 1))
#error "BMS_RS485_TX_DIAG_ENABLE must be 0 or 1"
#endif
#if BMS_RS485_TX_DIAG_ENABLE && !BMS_PRODUCT_RS485_ENABLE
#error "RS485 TX diagnostics require BMS_PRODUCT_RS485_ENABLE"
#endif
#ifndef BMS_PRODUCT_RS485_POWER_GATING_ENABLE
#define BMS_PRODUCT_RS485_POWER_GATING_ENABLE 0
#endif
#if BMS_PRODUCT_RS485_POWER_GATING_ENABLE != 0 && BMS_PRODUCT_RS485_POWER_GATING_ENABLE != 1
#error "RS485 power gating must be 0 or 1"
#endif
#if BMS_PRODUCT_RS485_POWER_GATING_ENABLE && \
    (!BMS_PRODUCT_RS485_ENABLE || BMS_AFE_BACKEND != BMS_AFE_BACKEND_SH3673510)
#error "RS485 power gating requires the reviewed SH RS485 board"
#endif
/* 主动线路诊断保持原有常供电，不能因没有主机请求而停止发送。 */
#define MODBUS_RS485_POWER_GATING (BMS_PRODUCT_RS485_POWER_GATING_ENABLE && !BMS_RS485_TX_DIAG_ENABLE)
#if MODBUS_RS485_POWER_GATING
#if BMS_PRODUCT_RS485_POWER_SETTLE_MS < 1u || BMS_PRODUCT_RS485_POWER_SETTLE_MS > 100u
#error "RS485 power settle time must be 1..100 ms"
#endif
typedef enum {
    MODBUS_COMM_POWER_OFF,
    MODBUS_COMM_POWER_STARTING,
    MODBUS_COMM_POWER_READY,
    MODBUS_COMM_POWER_STOPPED
} modbus_comm_power_state_t;
static volatile modbus_comm_power_state_t s_comm_power_state;
static volatile u8 s_comm_wake_pending;
static u32 s_comm_power_start_tick;
#endif

volatile bms_rs485_tx_diag_t g_bms_rs485_tx_diag;

static volatile u8 s_rx_ready = 0u;
static mb_dma_pkt_t s_rx_pkt;
static mb_dma_pkt_t s_tx_pkt;
#if !BMS_RS485_TX_DIAG_ENABLE
/* 正常应答/周期接收维护的时间基准；D014完成上电隔离时也建立新基准。 */
static _attribute_data_retention_ u32 mb_last_ok_tick = 0u;
static _attribute_data_retention_ u32 mb_bad_cnt = 0u;
#endif

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
/* 总线检测在帧中途才取得UART所有权，首个DMA包只能用于唤醒。 */
static volatile u8 s_uart_wake_rx_pending;
#endif

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
#define MODBUS_UART_IDLE_MS 1000u
#define MODBUS_UART_WAKE_GAP_US 5000u /* 大于最低9600波特的3.5字符间隔。 */
static volatile u8 s_uart_activity, s_uart_suspending, s_uart_resume_pending;
static volatile u8 s_uart_wake_discard;
static u8 s_uart_idle_ready;
static u32 s_uart_last_activity_ms, s_uart_last_edge_tick;

/* 强制 DeepSleep 不保留通信唤醒；关闭数字输入，避免未供电隔离器经 IO 反向取电。 */
static void modbus_uart_float_comm_pin(GPIO_PinTypeDef pin)
{
    gpio_set_func(pin, AS_GPIO);
    gpio_set_input_en(pin, 0);
    gpio_set_output_en(pin, 0);
    gpio_setup_up_down_resistor(pin, PM_PIN_UP_DOWN_FLOAT);
}
#endif

#if BMS_PRODUCT_RS485_ENABLE
static volatile u8 s_rs485_tx_dma_done = 0u;
static volatile u8 s_rs485_tx_active = 0u;
static volatile u8 s_rs485_tx_uart_done = 0u;
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
     * 16 MHz 下：9/13 约 114285.7 波特，118/13 约 9603.8 波特，118/6 约 19207.7 波特。
     * 按系统 tick 计算 DE 最小保持时间，以跟随当前产品的实际分频配置。
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

    if (clock_time_exceed(s_rs485_tx_start_tick, MODBUS_RS485_TX_TIMEOUT_US))
    {
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
    s_rs485_tx_start_tick = 0u;
    s_rs485_tx_min_hold_us = 0u;
}
#endif

/* 查询 UART 发送或 DE 保持阶段是否仍活动。 */
u8 modbus_uart_tx_active(void)
{
#if BMS_PRODUCT_RS485_ENABLE
    return s_rs485_tx_active;
#else
    return uart_tx_is_busy();
#endif
}

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
/* RISC0仅在SH产品归UART RX所有。每轮最多一个边沿IRQ，避免按每个数据位中断。 */
static int modbus_uart_rx_idle(void)
{
    return s_uart_idle_ready && !s_uart_activity && !s_rx_ready &&
        !s_uart_resume_pending && !s_uart_wake_discard &&
        !(reg_irq_src & FLD_IRQ_GPIO_RISC0_EN) &&
        !(dma_chn_irq_status_get() & FLD_DMA_CHN_UART_RX) &&
        gpio_read(BMS_BOARD_SCI1_RX_PIN) &&
        !uart_tx_is_busy() && !modbus_uart_tx_active();
}

int modbus_uart_suspend_ready(void)
{
#if MODBUS_RS485_POWER_GATING
    /* 掉电RX没有有效电平；只依据常供PD3和它的独立事件判断。 */
    return s_comm_power_state == MODBUS_COMM_POWER_OFF && !s_comm_wake_pending &&
        !(reg_irq_src & FLD_IRQ_GPIO_RISC1_EN) && !gpio_read(BMS_BOARD_CMNT_WK_PIN);
#else
    return modbus_uart_rx_idle();
#endif
}

void modbus_uart_suspend_enter(void)
{
    s_uart_suspending = 1u;
#if MODBUS_RS485_POWER_GATING
    cpu_set_gpio_wakeup(BMS_BOARD_CMNT_WK_PIN, Level_High, 1);
#else
    cpu_set_gpio_wakeup(BMS_BOARD_SCI1_RX_PIN, Level_Low, 1);
#endif
}

/* SDK回调只交付唤醒状态；PAD不能定位具体管脚，保守按通信唤醒保持运行。 */
void modbus_uart_suspend_exit(u8 wakeup_status)
{
    if (!s_uart_suspending) return;
    s_uart_suspending = 0u;
#if MODBUS_RS485_POWER_GATING
    /* PAD状态不区分引脚，保守接纳短PD3脉冲（回调时可能已变低）。
     * 其它PAD变化也可临时开启通信；纯采样/BLE定时唤醒绝不置位。 */
    if (s_comm_power_state == MODBUS_COMM_POWER_OFF && (wakeup_status & WAKEUP_STATUS_PAD))
        s_comm_wake_pending = 1u;
#else
    s_uart_resume_pending = 1u;
    if ((wakeup_status & WAKEUP_STATUS_PAD) || s_uart_activity ||
        (reg_irq_src & FLD_IRQ_GPIO_RISC0_EN) || !gpio_read(BMS_BOARD_SCI1_RX_PIN)) {
        s_uart_wake_discard = 1u;
        s_uart_activity = 1u;
    }
#endif
}

/* Suspend前没有TX；只在主循环恢复UART/DMA，不在SDK回调中解析或发送。 */
static void modbus_uart_resume_rx(void)
{
    u8 irq_state = irq_disable();
    dma_chn_enable(FLD_DMA_CHN_UART_RX | FLD_DMA_CHN_UART_TX, 0);
    uart_dma_enable(0, 0);
    uart_reset();
    uart_ndma_clear_tx_index();
    uart_ndma_clear_rx_index();
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
#if BMS_PRODUCT_RS485_ENABLE
    modbus_rs485_receive_mode();
#endif
    irq_restore(irq_state);
}

#if MODBUS_RS485_POWER_GATING
/* 调用方关闭中断且确认收发已空闲。PD3保持输入/PAD，不能用强制深睡收尾代替。 */
static void modbus_rs485_power_off(void)
{
    cpu_set_gpio_wakeup(BMS_BOARD_SCI1_RX_PIN, Level_Low, 0);
    gpio_en_interrupt_risc0(BMS_BOARD_SCI1_RX_PIN, 0);
    reg_irq_src = FLD_IRQ_GPIO_RISC0_EN;
    dma_chn_enable(FLD_DMA_CHN_UART_RX | FLD_DMA_CHN_UART_TX, 0);
    dma_chn_irq_enable(FLD_DMA_CHN_UART_RX | FLD_DMA_CHN_UART_TX, 0);
    dma_chn_irq_status_clr(FLD_DMA_CHN_UART_RX | FLD_DMA_CHN_UART_TX);
    uart_dma_enable(0, 0);
    uart_irq_enable(0, 0);
    uart_reset();
    uart_ndma_clear_tx_index();
    uart_ndma_clear_rx_index();
    modbus_rs485_receive_mode();
    modbus_uart_float_comm_pin(BMS_BOARD_SCI1_TX_PIN);
    modbus_uart_float_comm_pin(BMS_BOARD_SCI1_RX_PIN);
    s_rx_pkt.dma_len = 0u;
    s_rx_ready = s_uart_activity = s_uart_resume_pending = s_uart_wake_discard = 0u;
    s_uart_suspending = 0u;
    s_uart_idle_ready = 1u;
    cpu_set_gpio_wakeup(BMS_BOARD_CMNT_WK_PIN, Level_High, 1);
    gpio_write(BMS_BOARD_CMNT_EN_PIN, 0);
    s_comm_power_state = MODBUS_COMM_POWER_OFF;
    /* 关断边界来的活动仍由常供光耦/IRQ交付，下一轮立即上电。 */
    if (gpio_read(BMS_BOARD_CMNT_WK_PIN) || (reg_irq_src & FLD_IRQ_GPIO_RISC1_EN))
        s_comm_wake_pending = 1u;
}

/* 主循环非阻塞推进电源；返回1表示尚不能交付协议帧。 */
static u8 modbus_rs485_service_power(void)
{
    u8 irq_state = irq_disable();
    u8 activity;
    u8 gpio_pending;
    if (s_comm_power_state == MODBUS_COMM_POWER_STOPPED) {
        irq_restore(irq_state);
        return 1u;
    }
    gpio_pending = (reg_irq_src & FLD_IRQ_GPIO_RISC1_EN) ? 1u : 0u;
    activity = s_comm_wake_pending || gpio_read(BMS_BOARD_CMNT_WK_PIN) || gpio_pending;
    s_comm_wake_pending = 0u;
    /* 只清已观察到的事件，避免先读到0、后来的短脉冲被无条件W1C抹掉。 */
    if (gpio_pending) reg_irq_src = FLD_IRQ_GPIO_RISC1_EN;
    gpio_en_interrupt_risc1(BMS_BOARD_CMNT_WK_PIN, 1);
    s_uart_suspending = 0u; /* SDK许可后取消入睡也必须撤销。 */
    if (s_comm_power_state == MODBUS_COMM_POWER_OFF) {
        if (activity) {
            gpio_write(BMS_BOARD_CMNT_EN_PIN, 1);
            s_comm_power_start_tick = clock_time();
            s_comm_power_state = MODBUS_COMM_POWER_STARTING;
            s_uart_idle_ready = 0u;
        }
        irq_restore(irq_state);
        return 1u;
    }
    if (s_comm_power_state == MODBUS_COMM_POWER_STARTING) {
        if (!clock_time_exceed(s_comm_power_start_tick, BMS_PRODUCT_RS485_POWER_SETTLE_MS * 1000u)) {
            irq_restore(irq_state);
            return 1u;
        }
        /* CA-IS2092A v1.01 p20要求上电期间不传数据；稳定后才恢复复用及DMA。
         * 20ms是板级工程初值，非芯片保证上限，后续仍须帧尾静默隔离。 */
        uart_gpio_set(BMS_BOARD_SCI1_TX_PIN, BMS_BOARD_SCI1_RX_PIN);
        gpio_set_interrupt_risc0(BMS_BOARD_SCI1_RX_PIN, POL_FALLING);
        irq_set_mask(FLD_IRQ_DMA_EN);
        dma_chn_irq_enable(FLD_DMA_CHN_UART_RX | FLD_DMA_CHN_UART_TX, 1);
        modbus_uart_resume_rx();
        s_uart_wake_discard = 1u;
        s_uart_activity = 1u;
        s_comm_power_state = MODBUS_COMM_POWER_READY;
    } else if (activity) {
        s_uart_activity = 1u;
    }
    irq_restore(irq_state);
    return 0u;
}
#endif

/* SDK返回后先恢复接收；协议消费前再次处理期间新到的活动与残帧。
 * 这里只维护UART/DMA和唤醒资格，不解析命令、不访问AFE或Flash。 */
u8 modbus_uart_service_wakeup(void)
{
#if MODBUS_RS485_POWER_GATING
    if (modbus_rs485_service_power()) return 1u;
#endif
    u8 irq_state = irq_disable();
    u8 activity = s_uart_activity;
    u8 resume = s_uart_resume_pending;
    s_uart_activity = 0u;
    /* SDK许可后也可能取消入睡且不交付exit；返回主循环必须撤销临睡状态，
     * 否则后续每个RX边沿都会被当作唤醒残帧，导致持续通信一直被丢弃。 */
    if (s_uart_suspending || resume) {
        s_uart_suspending = 0u;
        cpu_set_gpio_wakeup(BMS_BOARD_SCI1_RX_PIN, Level_Low, 0);
    }
    gpio_en_interrupt_risc0(BMS_BOARD_SCI1_RX_PIN, 1);
    if (resume) {
        modbus_uart_resume_rx();
        /* 覆盖定时唤醒后、UART恢复期间到来的起始位；恢复完成前不解除标记。 */
        if (activity || s_uart_wake_discard ||
            (reg_irq_src & FLD_IRQ_GPIO_RISC0_EN) || !gpio_read(BMS_BOARD_SCI1_RX_PIN)) {
            s_uart_wake_discard = 1u;
            activity = 1u;
        }
        s_uart_resume_pending = 0u;
    }
    irq_restore(irq_state);
    if (activity || !gpio_read(BMS_BOARD_SCI1_RX_PIN) ||
        uart_tx_is_busy() || modbus_uart_tx_active()) {
        s_uart_last_activity_ms = bms_time_ms();
        s_uart_last_edge_tick = clock_time();
        s_uart_idle_ready = 0u;
    }
    if (s_uart_wake_discard) {
        /* 等待帧尾静默再重新装载RX；整个唤醒帧均不执行Modbus写命令。 */
        irq_state = irq_disable();
        if (!s_uart_activity && !(reg_irq_src & FLD_IRQ_GPIO_RISC0_EN) &&
#if MODBUS_RS485_POWER_GATING
            !s_comm_wake_pending && !(reg_irq_src & FLD_IRQ_GPIO_RISC1_EN) &&
            !gpio_read(BMS_BOARD_CMNT_WK_PIN) &&
#endif
            gpio_read(BMS_BOARD_SCI1_RX_PIN) &&
            clock_time_exceed(s_uart_last_edge_tick, MODBUS_UART_WAKE_GAP_US)) {
            modbus_uart_resume_rx();
            /* 重新装载期间若又出现起始位，仍按残帧处理，不能提前交给协议。 */
            s_uart_wake_discard = (reg_irq_src & FLD_IRQ_GPIO_RISC0_EN) ||
                !gpio_read(BMS_BOARD_SCI1_RX_PIN);
#if MODBUS_RS485_POWER_GATING
            if (s_comm_wake_pending || (reg_irq_src & FLD_IRQ_GPIO_RISC1_EN) ||
                gpio_read(BMS_BOARD_CMNT_WK_PIN)) s_uart_wake_discard = 1u;
            if (!s_uart_wake_discard) {
                /* OFF可能持续很久；不能让旧的1秒维护期限立即重置刚恢复的RX。 */
                mb_last_ok_tick = clock_time();
                mb_bad_cnt = 0u;
            }
#endif
            if (s_uart_wake_discard) s_uart_activity = 1u;
        }
        irq_restore(irq_state);
        return 1u;
    }
    if ((u32)(bms_time_ms() - s_uart_last_activity_ms) >= MODBUS_UART_IDLE_MS)
        s_uart_idle_ready = 1u;
#if MODBUS_RS485_POWER_GATING
    irq_state = irq_disable();
    if (modbus_uart_rx_idle() && !s_comm_wake_pending &&
        !(reg_irq_src & FLD_IRQ_GPIO_RISC1_EN) && !gpio_read(BMS_BOARD_CMNT_WK_PIN)) {
        modbus_rs485_power_off();
        irq_restore(irq_state);
        return 1u; /* 禁止后段周期RX维护重新启动已掉电DMA。 */
    }
    irq_restore(irq_state);
#endif
    return 0u;
}
#endif

/* 初始化产品 UART、DMA 缓冲区及 RS485 方向。 */
void modbus_uart_init(void)
{
#if MODBUS_RS485_POWER_GATING
    u8 irq_state = irq_disable();
    s_comm_wake_pending = 0u;
    gpio_set_func(BMS_BOARD_RS485_EN_PIN, AS_GPIO);
    gpio_write(BMS_BOARD_RS485_EN_PIN, 0);
    gpio_set_input_en(BMS_BOARD_RS485_EN_PIN, 0);
    gpio_set_output_en(BMS_BOARD_RS485_EN_PIN, 1);
    s_rs485_tx_active = s_rs485_tx_dma_done = s_rs485_tx_uart_done = 0u;
    s_rs485_tx_start_tick = s_rs485_tx_min_hold_us = 0u;
    gpio_set_func(BMS_BOARD_CMNT_WK_PIN, AS_GPIO);
    gpio_set_input_en(BMS_BOARD_CMNT_WK_PIN, 1);
    gpio_set_output_en(BMS_BOARD_CMNT_WK_PIN, 0);
    gpio_setup_up_down_resistor(BMS_BOARD_CMNT_WK_PIN, PM_PIN_UP_DOWN_FLOAT);
    gpio_set_interrupt_risc1(BMS_BOARD_CMNT_WK_PIN, POL_RISING);
    modbus_rs485_power_off();
    irq_restore(irq_state);
#else
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    u8 irq_state = irq_disable();
    s_uart_wake_rx_pending = 1u;
#endif
#if !BMS_RS485_TX_DIAG_ENABLE
    /* 保留已验证的 new-new-master 初始化顺序。 */
    memset((void *)&s_rx_pkt, 0, sizeof(s_rx_pkt));
    uart_recbuff_init((u8 *)&s_rx_pkt, sizeof(s_rx_pkt));
#endif

#if BMS_PRODUCT_RS485_ENABLE
    /* D014：PC2=TX，PC3=RX，PA1 控制 485 DE 和 /RE。 */
    gpio_set_func(BMS_BOARD_RS485_EN_PIN, AS_GPIO);
    gpio_write(BMS_BOARD_RS485_EN_PIN, 0);
    gpio_set_input_en(BMS_BOARD_RS485_EN_PIN, 0);
    gpio_set_output_en(BMS_BOARD_RS485_EN_PIN, 1);
    s_rs485_tx_active = 0u;
    s_rs485_tx_dma_done = 0u;
    s_rs485_tx_uart_done = 0u;
    s_rs485_tx_start_tick = 0u;
    s_rs485_tx_min_hold_us = 0u;
#endif

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    uart_gpio_set(BMS_BOARD_OWC_TX_PIN, BMS_BOARD_OWC_RX_PIN);
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

#if BMS_PRODUCT_RS485_ENABLE
    modbus_rs485_receive_mode();
#endif
    s_rx_ready = 0u;
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
    s_uart_activity = s_uart_suspending = s_uart_resume_pending = s_uart_wake_discard = 0u;
    s_uart_idle_ready = 0u;
    s_uart_last_activity_ms = bms_time_ms();
    s_uart_last_edge_tick = clock_time();
    gpio_set_interrupt_risc0(BMS_BOARD_SCI1_RX_PIN, POL_FALLING);
    cpu_set_gpio_wakeup(BMS_BOARD_SCI1_RX_PIN, Level_Low, 0);
#endif
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    irq_restore(irq_state);
#else
    irq_enable();
#endif
#endif
}

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
/* 调用方已关闭中断，判定与切换之间不得再交付 RX 或启动 TX。 */
u8 modbus_uart_bus_idle(void)
{
    return !modbus_uart_tx_active() && !s_rx_ready &&
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
#endif

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
/*
 * SH 强制 DeepSleep 会关闭通信隔离器电源。先停止 UART/DMA，再释放与隔离器相连的
 * PC2/PC3/PD3，避免 SDK uart_gpio_set() 配置的 10 kOhm 上拉继续向掉电通信域供电。
 * PA1 保持低电平，使 RS485 DE 在 CMNT_EN 拉低前始终处于接收/禁发状态。
 */
void modbus_uart_prepare_deep_sleep(void)
{
    u8 irq_state = irq_disable();
#if MODBUS_RS485_POWER_GATING
    s_comm_power_state = MODBUS_COMM_POWER_STOPPED;
    s_comm_wake_pending = 0u;
    gpio_en_interrupt_risc1(BMS_BOARD_CMNT_WK_PIN, 0);
    cpu_set_gpio_wakeup(BMS_BOARD_CMNT_WK_PIN, Level_High, 0);
    reg_irq_src = FLD_IRQ_GPIO_RISC1_EN;
#endif

    gpio_en_interrupt_risc0(BMS_BOARD_SCI1_RX_PIN, 0);
    cpu_set_gpio_wakeup(BMS_BOARD_SCI1_RX_PIN, Level_Low, 0);
    reg_irq_src = FLD_IRQ_GPIO_RISC0_EN;

#if BMS_PRODUCT_RS485_ENABLE
    modbus_rs485_receive_mode();
#endif
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
    s_uart_activity = 0u;
    s_uart_suspending = 0u;
    s_uart_resume_pending = 0u;
    s_uart_wake_discard = 0u;
    s_uart_idle_ready = 0u;
#if BMS_PRODUCT_RS485_ENABLE
    s_rs485_tx_active = 0u;
    s_rs485_tx_dma_done = 0u;
    s_rs485_tx_uart_done = 0u;
#endif

    modbus_uart_float_comm_pin(BMS_BOARD_SCI1_TX_PIN);
    modbus_uart_float_comm_pin(BMS_BOARD_SCI1_RX_PIN);
    modbus_uart_float_comm_pin(BMS_BOARD_CMNT_WK_PIN);
#if MODBUS_RS485_POWER_GATING
    gpio_write(BMS_BOARD_CMNT_EN_PIN, 0);
#endif

    irq_restore(irq_state);
}
#endif

/* 中断交付正常RX；D008首次唤醒包只丢弃并重装DMA，不解析/发送或访问AFE。 */
void modbus_uart_irq_proc(void)
{
    u8 irqsrc = dma_chn_irq_status_get();
#if MODBUS_RS485_POWER_GATING
    if (reg_irq_src & FLD_IRQ_GPIO_RISC1_EN) {
        gpio_en_interrupt_risc1(BMS_BOARD_CMNT_WK_PIN, 0);
        reg_irq_src = FLD_IRQ_GPIO_RISC1_EN;
        if (s_comm_power_state != MODBUS_COMM_POWER_STOPPED)
            s_comm_wake_pending = 1u; /* 一轮最多一次，避免每个光耦脉冲都进入ISR。 */
    }
    if (s_comm_power_state != MODBUS_COMM_POWER_READY) {
        dma_chn_irq_status_clr(irqsrc & (FLD_DMA_CHN_UART_RX | FLD_DMA_CHN_UART_TX));
        return;
    }
#endif
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    if (bus_mux_get_state() != BUS_STATE_UART_MODBUS) {
        dma_chn_irq_status_clr(irqsrc & (FLD_DMA_CHN_UART_RX | FLD_DMA_CHN_UART_TX));
        return;
    }
#endif
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
    if (reg_irq_src & FLD_IRQ_GPIO_RISC0_EN) {
        gpio_en_interrupt_risc0(BMS_BOARD_SCI1_RX_PIN, 0);
        reg_irq_src = FLD_IRQ_GPIO_RISC0_EN;
        s_uart_activity = 1u;
        if (s_uart_suspending || s_uart_resume_pending) s_uart_wake_discard = 1u;
    }
#endif

    if (irqsrc & FLD_DMA_CHN_UART_RX)
    {
        dma_chn_enable(FLD_DMA_CHN_UART_RX, 0);
        dma_chn_irq_status_clr(FLD_DMA_CHN_UART_RX);
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
        /* 无效/零长度也必须经过poll，以重新启用RX。 */
        s_rx_ready = 1u;
        s_uart_activity = 1u;
#else
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
#endif
    }

    if (irqsrc & FLD_DMA_CHN_UART_TX)
    {
        dma_chn_irq_status_clr(FLD_DMA_CHN_UART_TX);
#if BMS_PRODUCT_RS485_ENABLE
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
#if MODBUS_RS485_POWER_GATING
    if (s_comm_power_state != MODBUS_COMM_POWER_READY || s_uart_wake_discard) return 0;
#endif

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    if (bus_mux_get_state() != BUS_STATE_UART_MODBUS) return 0;
#endif
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
#if MODBUS_RS485_POWER_GATING
    if (s_comm_power_state != MODBUS_COMM_POWER_READY || s_uart_wake_discard) return 0u;
#endif
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    if (bus_mux_get_state() != BUS_STATE_UART_MODBUS) return 0u;
#endif
    if (p == NULL || len == 0u || len > sizeof(s_tx_pkt.data)) return 0u;

    /* uart_send_dma() 无条件重启 DMA，禁止覆盖其使用中的缓冲区。 */
#if BMS_PRODUCT_RS485_ENABLE
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

#if BMS_PRODUCT_RS485_ENABLE
    modbus_rs485_transmit_mode();
    s_rs485_tx_dma_done = 0u;
    s_rs485_tx_uart_done = 0u;
    s_rs485_tx_active = 1u;
    s_rs485_tx_start_tick = clock_time();
    s_rs485_tx_min_hold_us = modbus_rs485_min_hold_us(len);
#endif

    g_bms_rs485_tx_diag.last_tx_len = len;
    ++g_bms_rs485_tx_diag.tx_start_count;

    /* 已验证的 new-new-master TX 路径：DMA 包为 [u32 长度 + 载荷]。 */
    uart_send_dma((u8 *)&s_tx_pkt);
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
    s_uart_activity = 1u;
#endif
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
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    /* 一线通期间不解析旧帧，也不让周期 RX 恢复重新开启 DMA。 */
    if (bus_mux_get_state() != BUS_STATE_UART_MODBUS) return;
#endif
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
    if (modbus_uart_service_wakeup()) return;
#endif
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

#if BMS_PRODUCT_RS485_ENABLE
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

#if BMS_PRODUCT_RS485_ENABLE
    modbus_rs485_service_tx_done();
#endif
#endif
}
