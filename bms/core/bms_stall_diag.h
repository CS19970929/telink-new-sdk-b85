/* 只观察执行边界；关闭时记录实参不求值，读取仍可发现 disabled。 */
#ifndef BMS_STALL_DIAG_H_
#define BMS_STALL_DIAG_H_
#include <stdint.h>
#include "bms_diag_config.h"

#define BMS_STALL_DIAG_BASE 0x3A00u
#define BMS_STALL_DIAG_SCHEMA 2u
#define BMS_STALL_DIAG_WORDS 160u
#define BMS_STALL_DIAG_END (BMS_STALL_DIAG_BASE + BMS_STALL_DIAG_WORDS)
/* 附加证据独立窗口，原schema2/160word完全保留，跨窗口读取仍拒绝。 */
#define BMS_STALL_DETAIL_BASE BMS_STALL_DIAG_END
#define BMS_STALL_DETAIL_WORDS 32u
#define BMS_STALL_DETAIL_END (BMS_STALL_DETAIL_BASE + BMS_STALL_DETAIL_WORDS)
/* 临睡回调时序另用16word，不改变既有两个窗口或其schema。 */
#define BMS_STALL_PREP_BASE BMS_STALL_DETAIL_END
#define BMS_STALL_PREP_WORDS 16u
#define BMS_STALL_PREP_END (BMS_STALL_PREP_BASE + BMS_STALL_PREP_WORDS)
#define BMS_STALL_AFE_WDT_OBSERVED 0x0100u

enum {
    BMS_STALL_PREP_NONE=0, BMS_STALL_PREP_RUNNING, BMS_STALL_PREP_EARLY_VETO,
    BMS_STALL_PREP_LATE_VETO, BMS_STALL_PREP_ALLOW
};

enum {
    BMS_STALL_BOOT=0, BMS_STALL_LOOP, BMS_STALL_SDK_ENTER,
    BMS_STALL_SDK_RETURN, BMS_STALL_UART, BMS_STALL_SAMPLE_BEGIN,
    BMS_STALL_SAMPLE_END, BMS_STALL_STORAGE, BMS_STALL_PROTOCOL,
    BMS_STALL_POWER, BMS_STALL_PM_ENTER, BMS_STALL_PM_RETURN,
    BMS_STALL_APP_WAKE, BMS_STALL_DEEP_COMMIT, BMS_STALL_BOOT_HOLD
};
enum {
    BMS_STALL_GAP_NONE=0, BMS_STALL_GAP_PHASE, BMS_STALL_GAP_SDK,
    BMS_STALL_GAP_PM, BMS_STALL_GAP_32K, BMS_STALL_GAP_SAMPLE
};

static inline int bms_stall_diag_overlaps(uint16_t start, uint16_t count)
{
    return count && start < BMS_STALL_PREP_END &&
        (uint32_t)start + count > BMS_STALL_DIAG_BASE;
}

#if BMS_STALL_DIAG_ENABLE
void bms_stall_diag_boot(void);
void bms_stall_diag_install_pm(void);
void bms_stall_diag_start(void);
void bms_stall_diag_sleep_reason(uint8_t reason);
void bms_stall_diag_deep_commit(void);
void bms_stall_diag_stage(uint8_t stage, uint32_t detail);
void bms_stall_diag_schedule(uint8_t due, uint32_t deadline);
void bms_stall_diag_time(uint32_t raw_tick);
void bms_stall_diag_time_begin(void);
void bms_stall_diag_bus(uint8_t writing, uint8_t reg);
void bms_stall_diag_irq(uint8_t entering);
void bms_stall_diag_afe_flags(uint8_t valid, uint8_t flag1, uint8_t flag2);
void bms_stall_diag_afe(uint16_t stage, int16_t error, uint32_t crc, uint32_t retries);
void bms_stall_diag_guard(uint16_t bits, uint32_t elapsed_ms, uint32_t wait_ms);
void bms_stall_diag_suspend_veto(int32_t remaining_ticks);
void bms_stall_diag_prepare_begin(uint32_t start_tick, uint32_t deadline, uint32_t sdk_timing_us);
void bms_stall_diag_prepare_end(uint32_t end_tick, uint8_t decision);
void bms_stall_diag_spi_mismatch(uint8_t tx, uint8_t expected, uint8_t actual);
int bms_stall_diag_read(uint16_t start, uint16_t count, uint8_t *bytes);
#else
#define bms_stall_diag_boot() ((void)0)
#define bms_stall_diag_install_pm() ((void)0)
#define bms_stall_diag_start() ((void)0)
#define bms_stall_diag_sleep_reason(reason) ((void)0)
#define bms_stall_diag_deep_commit() ((void)0)
#define bms_stall_diag_stage(stage, detail) ((void)0)
#define bms_stall_diag_schedule(due, deadline) ((void)0)
#define bms_stall_diag_time(raw_tick) ((void)0)
#define bms_stall_diag_time_begin() ((void)0)
#define bms_stall_diag_bus(writing, reg) ((void)0)
#define bms_stall_diag_irq(entering) ((void)0)
#define bms_stall_diag_afe_flags(valid, flag1, flag2) ((void)0)
#define bms_stall_diag_afe(stage, error, crc, retries) ((void)0)
#define bms_stall_diag_guard(bits, elapsed_ms, wait_ms) ((void)0)
#define bms_stall_diag_suspend_veto(remaining_ticks) ((void)0)
#define bms_stall_diag_prepare_begin(start_tick, deadline, sdk_timing_us) ((void)0)
#define bms_stall_diag_prepare_end(end_tick, decision) ((void)0)
#define bms_stall_diag_spi_mismatch(tx, expected, actual) ((void)0)
static inline int bms_stall_diag_read(uint16_t start, uint16_t count, uint8_t *bytes)
{
    uint16_t i;
    uint16_t base = start >= BMS_STALL_PREP_BASE ? BMS_STALL_PREP_BASE :
        (start >= BMS_STALL_DETAIL_BASE ? BMS_STALL_DETAIL_BASE : BMS_STALL_DIAG_BASE);
    uint16_t words = base == BMS_STALL_PREP_BASE ? BMS_STALL_PREP_WORDS :
        (base == BMS_STALL_DETAIL_BASE ? BMS_STALL_DETAIL_WORDS : BMS_STALL_DIAG_WORDS);
    if (!bytes || !count || count > 125u || start < base ||
        (uint32_t)start + count > (uint32_t)base + words) return 0;
    for (i = 0u; i < count; ++i) {
        uint16_t offset = (uint16_t)(start - base + i);
        uint16_t value = offset == 0u ? (base == BMS_STALL_PREP_BASE ? 0x5350u :
            (base == BMS_STALL_DETAIL_BASE ? 0x5358u : 0x5354u)) :
            (offset == 1u ? (base != BMS_STALL_DIAG_BASE ? 1u : BMS_STALL_DIAG_SCHEMA) :
             (offset == 3u ? words : 0u));
        bytes[2u*i] = (uint8_t)(value >> 8);
        bytes[2u*i+1u] = (uint8_t)value;
    }
    return 1;
}
#endif
#endif
