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
/* 处理 UART DMA 中断，仅交付状态和清除标志。 */
void modbus_uart_irq_proc(void);     // 放到 DMA IRQ 里调用
/* RX 缓冲区在 main_loop_modbus 重新启用 DMA 前保持稳定。 */
int  modbus_uart_poll(u8 **p, u32 *len);
/* 检查发送状态与长度后启动 UART DMA，不覆盖活动缓冲区。 */
u8 modbus_uart_send(const u8 *p, u32 len);
/* 查询 UART 发送或 DE 保持阶段是否仍活动。 */
u8 modbus_uart_tx_active(void);
/* SH 普通Suspend：30秒静默资格、SDK进入/退出通知；D008不调用。 */
int modbus_uart_suspend_ready(void);
void modbus_uart_suspend_enter(void);
void modbus_uart_suspend_exit(u8 wakeup_status);
/* 主循环解析接收帧、发送协议应答并重新启用 RX。 */
void main_loop_modbus(void);
