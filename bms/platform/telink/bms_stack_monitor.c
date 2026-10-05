/*
 * 文件功能：可选栈水位观察；扫描预填充区帮助评估 SRAM 余量，不代表完整最坏栈深证明。
 * bms/platform/telink/bms_stack_monitor.c；实际编译归属见各产品 sources.txt。
 */
#include "bms_stack_monitor.h"

/* 链接器符号界定内存区域，不代表单个 C 对象。 */
extern uint32_t _bms_main_stack_bottom_[], __SRAM_SIZE[];
extern uint32_t bms_irq_stack_bottom[], bms_irq_stack_top[];
volatile bms_stack_watermark_t g_bms_stack_watermark;
static uint32_t s_main_cursor, s_irq_cursor;

/*
 * 每轮主循环每个栈最多读取 64 个字；禁止填涂活动栈。
 * 后续 IRQ 只能让下一轮观测空闲范围缩小；自底向上扫描保留短时深调用证据。
 */
static void bms_stack_scan(volatile const uint32_t *bottom, uint32_t words,
                           uint32_t *cursor, volatile uint32_t *minimum,
                           uint32_t mask)
{
    uint32_t budget = 64u;
    uint32_t i;
    if (words == 0u) return;
    for (i = 0u; i < 4u && i < words; ++i) {
        if (bottom[i] != BMS_STACK_PAINT)
            g_bms_stack_watermark.overflow_flags |= mask;
    }
    while (budget-- && *cursor < words && bottom[*cursor] == BMS_STACK_PAINT)
        ++*cursor;
    if (*cursor == words || bottom[*cursor] != BMS_STACK_PAINT) {
        uint32_t free_bytes = *cursor * 4u;
        if (!(g_bms_stack_watermark.valid_mask & mask) || free_bytes < *minimum)
            *minimum = free_bytes;
        g_bms_stack_watermark.valid_mask |= mask;
        *cursor = 0u;
    }
}

/* 在主循环中有界扫描栈水位并发布观测值。 */
void bms_stack_monitor_poll(void)
{
    bms_stack_scan(_bms_main_stack_bottom_,
        ((uintptr_t)__SRAM_SIZE - (uintptr_t)_bms_main_stack_bottom_) / 4u,
        &s_main_cursor, &g_bms_stack_watermark.main_free_min_bytes, 1u);
    bms_stack_scan(bms_irq_stack_bottom,
        ((uintptr_t)bms_irq_stack_top - (uintptr_t)bms_irq_stack_bottom) / 4u,
        &s_irq_cursor, &g_bms_stack_watermark.irq_free_min_bytes, 2u);
}
