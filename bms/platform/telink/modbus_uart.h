/* 文件功能：UART DMA、RS485 收发状态和主循环协议入口声明。 */
#pragma once
#include "tl_common.h"

/* 调试器可查看 g_bms_rs485_tx_diag，计数器按 u32 自然回绕。 */
typedef struct {
    u32 tx_start_count;
    u32 tx_dma_done_count;
    u32 tx_uart_done_count;
    u32 tx_complete_count;
    u32 tx_timeout_count;
    u32 tx_busy_reject_count;
    u32 last_tx_len;
    u8 de_state;
    u8 pattern_index;
} bms_rs485_tx_diag_t;

extern volatile bms_rs485_tx_diag_t g_bms_rs485_tx_diag;

/* 初始化产品 UART、DMA 缓冲区及 RS485 方向。 */
void modbus_uart_init(void);
/* D008 复用所有者在临界区调用：检查可释放条件，或停用 UART/DMA。 */
u8 modbus_uart_bus_idle(void);
void modbus_uart_disable(void);
/* SH 强制 DeepSleep：停止 UART/DMA，并释放通信域 GPIO 的上下拉和数字驱动。 */
void modbus_uart_prepare_deep_sleep(void);
/* 处理 UART DMA 中断，仅交付状态和清除标志。 */
void modbus_uart_irq_proc(void);     // 放到 DMA IRQ 里调用
/* RX 缓冲区在 main_loop_modbus 重新启用 DMA 前保持稳定。 */
int  modbus_uart_poll(u8 **p, u32 *len);
/* 检查发送状态与长度后启动 UART DMA，不覆盖活动缓冲区。 */
u8 modbus_uart_send(const u8 *p, u32 len);
/* 查询 UART 发送或 DE 保持阶段是否仍活动。 */
u8 modbus_uart_tx_active(void);
/* SH普通Suspend：D014只在485断电监听时许可，PD3高电平唤醒；
 * 其它SH保留1秒UART静默/RX唤醒。D008由bus_mux负责。 */
int modbus_uart_suspend_ready(void);
void modbus_uart_suspend_enter(void);
void modbus_uart_suspend_exit(u8 wakeup_status);
/* SH主循环在SDK返回后、采样/存储前调用；返回1表示断电、启动或隔离唤醒残帧。
 * 不解析协议。main_loop_modbus消费前仍会复核新到活动。 */
u8 modbus_uart_service_wakeup(void);
/* 主循环解析接收帧、发送协议应答并重新启用 RX。 */
void main_loop_modbus(void);
