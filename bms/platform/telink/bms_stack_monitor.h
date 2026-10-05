/*
 * 文件功能：可选栈水位观察；扫描预填充区帮助评估 SRAM 余量，不代表完整最坏栈深证明。
 * bms/platform/telink/bms_stack_monitor.h；实际编译归属见各产品 sources.txt。
 */
#ifndef BMS_STACK_MONITOR_H_
#define BMS_STACK_MONITOR_H_
#include <stdint.h>
#define BMS_STACK_PAINT 0xA5A5A5A5u
/*
 * 填涂字节数是实际观测值，非形式化最坏界限；IRQ 预留独立。
 * 标志 bit0/bit1 表示主栈/IRQ 栈底被触及，保持到复位；
 * valid_mask 的 bit0/bit1 表示主栈/IRQ 扫描完成。
 */
typedef struct {
    uint32_t main_free_min_bytes;
    uint32_t irq_free_min_bytes;
    uint32_t valid_mask;
    uint32_t overflow_flags;
} bms_stack_watermark_t;
extern volatile bms_stack_watermark_t g_bms_stack_watermark;
/* 在主循环中有界扫描栈水位并发布观测值。 */
void bms_stack_monitor_poll(void);
#endif
