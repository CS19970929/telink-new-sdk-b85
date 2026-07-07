#include "safety_manager.h"

static void safety_startup_fail_if(int ok, SafetyFaultType fault)
{
    if (!ok)
    {
        Safety_EnterFailSafe(fault);
    }
}

void Safety_StartUpTest(void)
{
#if SAFETY_ENABLE && SAFETY_STARTUP_TEST_ENABLE
    Safety_Init();
    Safety_FlowCheckPoint(SAFETY_FLOW_ID_STARTUP_BEGIN);

    safety_startup_fail_if(Safety_CpuStartupTest(), SAFETY_CPU_FAULT);
    safety_startup_fail_if(Safety_FlowCheckValidate(), SAFETY_FLOW_FAULT);
    safety_startup_fail_if(Safety_FlashStartupTest(), SAFETY_FLASH_FAULT);
    safety_startup_fail_if(Safety_RamStartupTest(), SAFETY_RAM_FAULT);
    safety_startup_fail_if(Safety_ClockStartupTest(), SAFETY_CLOCK_FAULT);
    safety_startup_fail_if(Safety_WatchdogStartupTest(), SAFETY_WATCHDOG_FAULT);

    Safety_FlowCheckPoint(SAFETY_FLOW_ID_STARTUP_END);
    safety_startup_fail_if(Safety_FlowCheckValidate(), SAFETY_FLOW_FAULT);
#endif
}
