#include "bus_mux.h"
#include "tl_common.h"
#include "drivers.h"
#include "modbus_uart.h"
#include "conf.h"
#include "app.h"

// ===== pins =====
#define PIN_OWC_RX OWC_RX_PIN
#define PIN_OWC_TX OWC_TX_PIN

// ===== params =====
#define RX_HIGH_STABLE_US      (50u * 1000u)
#define UART_DETECT_WINDOW_US  (30u * 600u) /* Preserve the existing 18 ms window. */
#define UART_FALL_MIN_COUNT    3u
#define UART_IDLE_BACK_US      (5u * 1000u * 1000u) /* 0 disables fallback. */

static inline uint32_t tick_now(void) { return clock_time(); }

static volatile bus_state_t g_state = BUS_STATE_OWC_IDLE;

// UART活动检测：下降沿计数窗口
static volatile uint8_t g_fall_cnt = 0;
static volatile uint32_t g_win_start = 0;

// OWC接入检测：RX高稳定
static uint32_t g_rx_high_since = 0;

// UART空闲回退
static volatile uint32_t g_last_uart_rx = 0;

static inline int rx_is_high(void) { return gpio_read(PIN_OWC_RX) ? 1 : 0; }

static void owc_start_tx_only(void)
{
    gpio_set_func(OWC_TX_PIN, AS_GPIO); /* OWC-TX is PC2 on HS-D008. */
    gpio_set_input_en(OWC_TX_PIN, 0);
    gpio_set_output_en(OWC_TX_PIN, 1);
    gpio_write(OWC_TX_PIN, 1);
    gpio_en_interrupt_risc0(PIN_OWC_RX, 0);
}
/* 监听态GPIO初始化：不发，RX下拉，开启 RISC0 下降沿中断 */
static void owc_listen_init(void)
{
    // RX input + pulldown
    gpio_set_func(PIN_OWC_RX, AS_GPIO);
    gpio_set_input_en(PIN_OWC_RX, 1);
    gpio_set_output_en(PIN_OWC_RX, 0);
    gpio_setup_up_down_resistor(PIN_OWC_RX, PM_PIN_PULLDOWN_100K);
    // TX 高阻释放
    gpio_set_func(PIN_OWC_TX, AS_GPIO);
    gpio_set_input_en(PIN_OWC_TX, 0);
    gpio_set_output_en(PIN_OWC_TX, 0);
    gpio_write(PIN_OWC_TX, 0);
    gpio_setup_up_down_resistor(PIN_OWC_TX, PM_PIN_UP_DOWN_FLOAT);
    // 用 RISC0 专用中断（下降沿：UART start bit）
    gpio_set_interrupt_risc0(PIN_OWC_RX, POL_FALLING);

    g_fall_cnt = 0;
    g_win_start = tick_now();
    gpio_en_interrupt_risc0(PIN_OWC_RX, 1);
}
static void enter_owc_idle(void)
{
    // 退出UART复用回GPIO监听
    owc_listen_init();

    g_rx_high_since = 0;
    g_state = BUS_STATE_OWC_IDLE;
}

static void enter_owc_tx(void)
{
    owc_start_tx_only();
    g_state = BUS_STATE_OWC_TX;
}

static void enter_uart_modbus(void)
{
    // 关RISC0对该pin的中断使能，避免UART期间误进
    gpio_en_interrupt_risc0(PIN_OWC_RX, 0);

    modbus_uart_init();

    g_last_uart_rx = tick_now();
    g_state = BUS_STATE_UART_MODBUS;
}

bus_state_t bus_mux_get_state(void) { return g_state; }
void bus_mux_return_to_owc_idle(void)
{
    enter_owc_idle();
}

void bus_mux_on_uart_rx_byte(void)
{
    g_last_uart_rx = tick_now();
}

/**
 * 放到你的总 irq_handler 里调用
 * 只处理 RISC0 这个源（我们约定 RISC0 只给 OWC_RX 用）
 */
_attribute_ram_code_ void bus_mux_irq_handler(void)
{
    // RISC0 GPIO interrupt
    if (reg_irq_src & FLD_IRQ_GPIO_RISC0_EN)
    {
        reg_irq_src = FLD_IRQ_GPIO_RISC0_EN;

        if (g_state == BUS_STATE_OWC_IDLE)
        {
            uint32_t now = tick_now();
            if (clock_time_exceed(g_win_start, UART_DETECT_WINDOW_US))
            {
                g_win_start = now;
                g_fall_cnt = 0;
            }
            if (g_fall_cnt < UART_FALL_MIN_COUNT) g_fall_cnt++;
        }
    }
}

void bus_mux_init(void)
{
    /* Default to passive OWC listen mode. */
    enter_owc_idle();
}

void bus_mux_task(void)
{
    // 1) OWC相关状态：优先UART活动判定
    if ((g_state == BUS_STATE_OWC_IDLE) &&
        g_fall_cnt >= UART_FALL_MIN_COUNT)
    {
        enter_uart_modbus();
        return;
    }
    // 2) OWC_IDLE：RX高稳定才允许发
    if (g_state == BUS_STATE_OWC_IDLE)
    {
        if (rx_is_high())
        {
            if (g_rx_high_since == 0)
                g_rx_high_since = tick_now();
            if (clock_time_exceed(g_rx_high_since, RX_HIGH_STABLE_US))
            {
                enter_owc_tx();
                return;
            }
        }
        else
        {
            g_rx_high_since = 0;
        }
    }
    // 3) UART模式：长时间无通信回OWC_IDLE（可选）
    if (g_state == BUS_STATE_UART_MODBUS && UART_IDLE_BACK_US > 0u)
    {
        if (clock_time_exceed(g_last_uart_rx, UART_IDLE_BACK_US))
        {
            enter_owc_idle();
            return;
        }
    }
}
