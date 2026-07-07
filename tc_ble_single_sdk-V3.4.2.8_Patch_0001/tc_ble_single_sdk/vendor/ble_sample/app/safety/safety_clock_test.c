#include "safety_manager.h"

#include "drivers.h"
#include "app_config.h"

static int safety_clock_tick_moves(void)
{
    u32 t0 = clock_time();
    volatile u32 i;

    for (i = 0u; i < 128u; ++i)
    {
        asm("tnop");
    }

    return clock_time() != t0;
}

int Safety_ClockStartupTest(void)
{
#if SAFETY_ENABLE
#if SAFETY_TEST_ENABLE && SAFETY_INJECT_CLOCK_FAULT
    return 0;
#else
    if ((u8)clock_get_system_clk() != (u8)SYS_CLK_TYPE)
    {
        return 0;
    }
    return safety_clock_tick_moves();
#endif
#else
    return 1;
#endif
}

int Safety_ClockRuntimeTest(void)
{
    return Safety_ClockStartupTest();
}
