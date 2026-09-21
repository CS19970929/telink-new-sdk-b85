#ifndef BMS_STACK_MONITOR_H_
#define BMS_STACK_MONITOR_H_
#include <stdint.h>
#define BMS_STACK_PAINT 0xA5A5A5A5u
/* Observed painted bytes, not a formal worst-case bound. IRQ reserve is separate.
 * Flags: bit0 main bottom touched, bit1 IRQ bottom touched. Sticky until reset.
 * valid_mask: bit0 main scan complete, bit1 IRQ scan complete. */
typedef struct {
    uint32_t main_free_min_bytes;
    uint32_t irq_free_min_bytes;
    uint32_t valid_mask;
    uint32_t overflow_flags;
} bms_stack_watermark_t;
extern volatile bms_stack_watermark_t g_bms_stack_watermark;
void bms_stack_monitor_poll(void);
#endif
