#include "bms_selftest_internal.h"
#include "bms_fault_inject.h"

#define BMS_STACK_GUARD_PATTERN 0x5aa55aa5u
#define BMS_STACK_FILL_PATTERN  0xa5a5a5a5u

extern u32 BMS_SelfTest_Tc32GetSp(void);
extern u8 _ram_use_end_[];
extern u8 __SRAM_SIZE[];

__attribute__((section(".bms_stack_guard"), used))
static volatile u32 g_bms_stack_guard[BMS_SELFTEST_STACK_GUARD_WORDS];

static u32 g_bms_stack_fill_begin;
static u32 g_bms_stack_fill_end;

static u8 bms_stack_guard_ok(void)
{
    u8 i;
    u32 value;

    for (i = 0u; i < BMS_SELFTEST_STACK_GUARD_WORDS; ++i)
    {
        value = g_bms_stack_guard[i];
        if (BMS_FaultInject_ShouldFail((u8)BMS_FAULT_STACK) && (i == 0u))
        {
            value ^= 1u;
        }
        if (value != BMS_STACK_GUARD_PATTERN)
        {
            return 0u;
        }
    }
    return 1u;
}

u8 BMS_SelfTest_StackStartup(void)
{
    u8 i;
    u32 sp = BMS_SelfTest_Tc32GetSp();
    u32 lower = (u32)_ram_use_end_;
    u32 upper = (u32)__SRAM_SIZE;
    volatile u32 *fill;
    volatile u32 *fill_end;

    if ((sp <= lower + BMS_SELFTEST_STACK_FILL_MARGIN) || (sp > upper))
    {
        return 0u;
    }
    for (i = 0u; i < BMS_SELFTEST_STACK_GUARD_WORDS; ++i)
    {
        g_bms_stack_guard[i] = BMS_STACK_GUARD_PATTERN;
    }

    g_bms_stack_fill_begin = lower;
    g_bms_stack_fill_end = (sp - BMS_SELFTEST_STACK_FILL_MARGIN) & ~3u;
    fill = (volatile u32 *)g_bms_stack_fill_begin;
    fill_end = (volatile u32 *)g_bms_stack_fill_end;
    while (fill < fill_end)
    {
        *fill++ = BMS_STACK_FILL_PATTERN;
    }
    BMS_SelfTest_SetStackHighWater(upper - g_bms_stack_fill_end);
    return bms_stack_guard_ok();
}

u8 BMS_SelfTest_StackRuntime(void)
{
    u32 sp = BMS_SelfTest_Tc32GetSp();
    u32 lower = (u32)_ram_use_end_;
    u32 upper = (u32)__SRAM_SIZE;
    volatile u32 *scan;
    volatile u32 *scan_end;

    if ((sp <= lower) || (sp > upper) || !bms_stack_guard_ok())
    {
        return 0u;
    }

    scan = (volatile u32 *)g_bms_stack_fill_begin;
    scan_end = (volatile u32 *)g_bms_stack_fill_end;
    while ((scan < scan_end) && (*scan == BMS_STACK_FILL_PATTERN))
    {
        ++scan;
    }
    BMS_SelfTest_SetStackHighWater(upper - (u32)scan);
    return 1u;
}
