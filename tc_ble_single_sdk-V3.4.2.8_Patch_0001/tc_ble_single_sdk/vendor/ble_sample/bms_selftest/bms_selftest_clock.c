#include "bms_selftest_internal.h"
#include "bms_fault_inject.h"

#include "drivers.h"

#define BMS_CLOCK_RUNTIME_MAX_GAP_TICKS (CLOCK_SYS_CLOCK_1MS * 2000u)

static u32 g_bms_clock_last_tick;

u8 BMS_SelfTest_ClockStartup(void)
{
    u32 before;
    u32 after;
    volatile u8 i;

    if ((u8)clock_get_system_clk() != (u8)SYS_CLK_TYPE)
    {
        return 0u;
    }
    before = clock_time();
    for (i = 0u; i < 64u; ++i)
    {
        asm("tnop");
    }
    after = clock_time();
    if (BMS_FaultInject_ShouldFail((u8)BMS_FAULT_CLOCK))
    {
        after = before;
    }
    g_bms_clock_last_tick = after;
    return (after != before) ? 1u : 0u;
}

u8 BMS_SelfTest_ClockRuntime(void)
{
    u32 now = clock_time();
    u32 delta = now - g_bms_clock_last_tick;

    g_bms_clock_last_tick = now;
    if (BMS_FaultInject_ShouldFail((u8)BMS_FAULT_CLOCK))
    {
        delta = 0u;
    }
    return ((delta != 0u) && (delta <= BMS_CLOCK_RUNTIME_MAX_GAP_TICKS)) ? 1u : 0u;
}
