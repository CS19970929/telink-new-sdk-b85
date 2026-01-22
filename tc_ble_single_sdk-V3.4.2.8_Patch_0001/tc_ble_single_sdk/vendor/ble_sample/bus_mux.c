#include "bus_mux.h"
#include "tl_common.h"
#include "drivers.h"
#include "modbus_uart.h"
#include "conf.h"
#include "sh367309_datadeal.h"
#include "app.h"

// ===== pins =====
#define PIN_OWC_RX OWC_RX_PIN
#define PIN_OWC_TX OWC_TX_PIN

// ===== params =====
#define RX_HIGH_STABLE_MS (1000 * 50)
// #define UART_DETECT_WINDOW_MS   30
#define UART_DETECT_WINDOW_MS (600 * 30)
#define UART_FALL_MIN_CNT 3
#define UART_IDLE_BACK_MS (1000 * 1000 * 2) // 0 关闭回退

static inline uint32_t tick_now(void) { return clock_time(); }
// static inline int exceed_ms(uint32_t ref, uint32_t ms){ return clock_time_exceed(ref, ms * 1000 * 16); }

static volatile bus_state_t g_state = BUS_STATE_OWC_IDLE;

// UART活动检测：下降沿计数窗口
static volatile uint16_t g_fall_cnt = 0;
static volatile uint32_t g_win_start = 0;

// OWC接入检测：RX高稳定
static uint32_t g_rx_high_since = 0;

// UART空闲回退
static volatile uint32_t g_last_uart_rx = 0;

static inline int rx_is_high(void) { return gpio_read(PIN_OWC_RX) ? 1 : 0; }

void owc_start_tx_only(void)
{
    gpio_set_func(OWC_TX_PIN, AS_GPIO); // PA4 姒涙顓绘稉锟� GPIO 閸旂喕鍏橀敍灞藉讲娴犮儰绗夌拋鍓х枂
    gpio_set_input_en(OWC_TX_PIN, 0);
    gpio_set_output_en(OWC_TX_PIN, 1);
    gpio_write(OWC_TX_PIN, 1);
    gpio_en_interrupt_risc0(PIN_OWC_RX, 0);
}
void owc_stop_tx(void)
{
}

void modbus_uart_slave_enable(void)
{
}
void modbus_uart_slave_disable(void)
{
}

/* 监听态GPIO初始化：不发，RX下拉，开启 RISC0 下降沿中断 */
static void owc_listen_init(void)
{
    // RX input + pulldown
    gpio_set_func(PIN_OWC_RX, AS_GPIO);
    gpio_set_input_en(PIN_OWC_RX, 1);
    gpio_set_output_en(PIN_OWC_RX, 0);
    gpio_setup_up_down_resistor(PIN_OWC_RX, PM_PIN_PULLDOWN_100K);
    // todo 下拉100K，确认功耗测试等等

    // TX 高阻释放
    gpio_set_func(PIN_OWC_TX, AS_GPIO);
    gpio_set_input_en(PIN_OWC_TX, 0);
    gpio_set_output_en(PIN_OWC_TX, 0);
    gpio_write(PIN_OWC_TX, 0);
    gpio_setup_up_down_resistor(PIN_OWC_TX, PM_PIN_UP_DOWN_FLOAT);
    // gpio_setup_up_down_resistor(PIN_OWC_TX, PM_PIN_UP_DOWN_FLOAT);

    // 用 RISC0 专用中断（下降沿：UART start bit）
    gpio_set_interrupt_risc0(PIN_OWC_RX, POL_FALLING);

    g_fall_cnt = 0;
    g_win_start = tick_now();
    gpio_en_interrupt_risc0(PIN_OWC_RX, 1);
}
/* UART pin mux: 你SDK里 UART init 可能还需要 uart_init(115200) */
static void uart_pin_mux_enable(void)
{
    modbus_uart_init();
}

static void enter_owc_idle(void)
{
    owc_stop_tx();
    modbus_uart_slave_disable();

    // 退出UART复用回GPIO监听
    owc_listen_init();

    g_rx_high_since = 0;
    // System_ERROR_UserCallback(ERROR_EEPROM_STORE);
    // System_ERROR_UserCallback(ERROR_REMOVE_EEPROM_COM);
    // System_ERROR_UserCallback(ERROR_REMOVE_EEPROM_STORE);
    g_state = BUS_STATE_OWC_IDLE;
}

static void enter_owc_tx(void)
{
    owc_start_tx_only();
    g_state = BUS_STATE_OWC_TX;
    // System_ERROR_UserCallback(ERROR_AFE2);
    // System_ERROR_UserCallback(ERROR_REMOVE_EEPROM_COM);
    // System_ERROR_UserCallback(ERROR_REMOVE_EEPROM_STORE);
}

static void enter_uart_modbus(void)
{
    owc_stop_tx();

    // 关RISC0对该pin的中断使能，避免UART期间误进
    gpio_en_interrupt_risc0(PIN_OWC_RX, 0);

    uart_pin_mux_enable();
    modbus_uart_slave_enable();

    g_last_uart_rx = tick_now();
    g_state = BUS_STATE_UART_MODBUS;
    // System_ERROR_UserCallback(ERROR_EEPROM_COM);
    // System_ERROR_UserCallback(ERROR_REMOVE_AFE2);
    // System_ERROR_UserCallback(ERROR_REMOVE_EEPROM_STORE);
}

bus_state_t bus_mux_get_state(void) { return g_state; }
void bus_mux_set_state(bus_state_t _state)
{
    g_state = _state;
    if(g_state == BUS_STATE_OWC_IDLE)
    {
        enter_owc_idle();
    }
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

        System_ERROR_UserCallback(ERROR_AFE1);

        // if(g_state == BUS_STATE_OWC_IDLE || g_state == BUS_STATE_OWC_TX){
        if (g_state == BUS_STATE_OWC_IDLE)
        {
            uint32_t now = tick_now();
            if (clock_time_exceed(g_win_start, UART_DETECT_WINDOW_MS))
            {
                g_win_start = now;
                g_fall_cnt = 0;
            }
            g_fall_cnt++;
        }
    }
}

void bus_mux_init(void)
{
    // 默认：OWC_IDLE（监听不发）
    enter_owc_idle();
    // owc_start_tx_only();
    // g_state = BUS_STATE_OWC_TX;
    // enter_uart_modbus();

    // 如果你希望“睡眠也能被串口/外部唤醒”，可以用 wakeup：
    // 低电平唤醒还是高电平唤醒取决于你的电路空闲态
    // cpu_set_gpio_wakeup(PIN_OWC_RX, Level_Low/Level_High, 1);
    // 但注意：wakeup 只保证“醒来”，醒来后仍要靠本模块检测是否切UART。
}

void bus_mux_task(void)
{
    // 1) OWC相关状态：优先UART活动判定
    // if((g_state == BUS_STATE_OWC_IDLE || g_state == BUS_STATE_OWC_TX) &&
    if ((g_state == BUS_STATE_OWC_IDLE) &&
        g_fall_cnt >= UART_FALL_MIN_CNT)
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
            if (clock_time_exceed(g_rx_high_since, RX_HIGH_STABLE_MS))
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
    if (g_state == BUS_STATE_UART_MODBUS && UART_IDLE_BACK_MS > 0)
    {
        if (clock_time_exceed(g_last_uart_rx, UART_IDLE_BACK_MS))
        {
            enter_owc_idle();
            return;
        }
    }
    // extern volatile struct SYSTEM_ERROR System_ErrFlag;
    // switch (g_state)
    // {
    // case BUS_STATE_OWC_IDLE:
    //     System_ErrFlag.u8ErrFlag_Com_AFE2 = 1;
    //     ;
    //     System_ErrFlag.u8ErrFlag_Com_Can = 0;
    //     System_ErrFlag.u8ErrFlag_Com_EEPROM = 0;
    //     break;
    // case BUS_STATE_OWC_TX:
    //     System_ErrFlag.u8ErrFlag_Com_Can = 1;
    //     System_ErrFlag.u8ErrFlag_Com_AFE2 = 0;
    //     ;
    //     System_ErrFlag.u8ErrFlag_Com_EEPROM = 0;
    //     break;
    // case BUS_STATE_UART_MODBUS:
    //     System_ErrFlag.u8ErrFlag_Com_Can = 0;
    //     System_ErrFlag.u8ErrFlag_Com_AFE2 = 0;
    //     ;
    //     System_ErrFlag.u8ErrFlag_Com_EEPROM = 1;
    //     break;
    // default:
    //     break;
    // }
}
