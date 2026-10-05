/*
 * 文件功能：通信引脚复用状态机；依据当前产品编译配置在 UART 与单线总线之间协调所有权
 * 。
 * bms/platform/telink/bus_mux.h；实际编译归属见各产品 sources.txt。
 */
#pragma once
#include <stdint.h>

typedef enum {
    BUS_STATE_OWC_IDLE = 0,
    BUS_STATE_OWC_TX,
    BUS_STATE_UART_MODBUS,
} bus_state_t;

/* 初始化通信复用状态、GPIO 监听和时间基准。 */
void bus_mux_init(void);
/* 按活动边沿、稳定电平和超时推进通信复用状态。 */
void bus_mux_task(void);

// 放到中断入口里调用（或直接把下面 handler 代码合并到你的 irq_handler）
void bus_mux_irq_handler(void);

// UART RX 收到任意字节时调用
void bus_mux_on_uart_rx_byte(void);

/* 取得当前通信引脚复用状态。 */
bus_state_t bus_mux_get_state(void);
/* 撤销当前发送并返回单线监听状态。 */
void bus_mux_return_to_owc_idle(void);
