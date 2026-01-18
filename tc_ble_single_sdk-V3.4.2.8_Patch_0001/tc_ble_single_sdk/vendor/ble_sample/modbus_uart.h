#pragma once
#include "tl_common.h"
#include "drivers.h"

#define MODBUS_BAUD        115200
#define MODBUS_SYSCLK_HZ   CLOCK_SYS_CLOCK_HZ   // 你工程里一般有这个
#define MODBUS_SLAVE_ADDR  0x01

// Modbus RTU 最大一帧通常不超过 256 字节（含CRC）
// DMA包要：4 + payload，并且总大小建议 16 对齐
#define MB_RX_MAX          256
#define MB_TX_MAX          256

typedef struct __attribute__((aligned(4))) {
    u32 dma_len;
    u8  data[MB_RX_MAX]; // 4 + 256 = 260，不是16倍数；我们会用更大对齐
} mb_dma_pkt_raw_t;

// 为了满足 (len+4) 16对齐，直接做 272：4 + 268 = 272（16倍数）
typedef struct __attribute__((aligned(4))) {
    u32 dma_len;
    u8  data[268];
} mb_dma_pkt_t;

void modbus_uart_init(void);
void modbus_uart_irq_proc(void);     // 放到 DMA IRQ 里调用
int  modbus_uart_poll(u8 **p, u32 *len);  // 主循环取一帧
void modbus_uart_send(const u8 *p, u32 len);
void main_loop_modbus(void);
