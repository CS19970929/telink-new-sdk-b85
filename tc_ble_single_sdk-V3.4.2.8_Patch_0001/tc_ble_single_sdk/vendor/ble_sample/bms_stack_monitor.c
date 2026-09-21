#include "bms_stack_monitor.h"

/* Linker symbols delimit memory regions, not single C objects. */
extern uint32_t _bms_main_stack_bottom_[], __SRAM_SIZE[];
extern uint32_t bms_irq_stack_bottom[], bms_irq_stack_top[];
volatile bms_stack_watermark_t g_bms_stack_watermark;
static uint32_t s_main_cursor, s_irq_cursor;

/* Read at most 64 words per stack per main-loop turn. Never paint a live stack.
 * A later IRQ can only make the next pass report a smaller observed free span.
 * Bottom-up scan retains evidence left by short-lived deep calls. */
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

void bms_stack_monitor_poll(void)
{
    bms_stack_scan(_bms_main_stack_bottom_,
        ((uintptr_t)__SRAM_SIZE - (uintptr_t)_bms_main_stack_bottom_) / 4u,
        &s_main_cursor, &g_bms_stack_watermark.main_free_min_bytes, 1u);
    bms_stack_scan(bms_irq_stack_bottom,
        ((uintptr_t)bms_irq_stack_top - (uintptr_t)bms_irq_stack_bottom) / 4u,
        &s_irq_cursor, &g_bms_stack_watermark.irq_free_min_bytes, 2u);
}
