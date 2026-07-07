#include "safety_manager.h"

#include "drivers.h"
#include "conf.h"

static _attribute_data_retention_ u32 g_safety_runtime_tick;

static void safety_runtime_fail_if(int ok, SafetyFaultType fault)
{
    if (!ok)
    {
        Safety_EnterFailSafe(fault);
    }
}

void Safety_RuntimeTask(void)
{
#if SAFETY_ENABLE && SAFETY_RUNTIME_TEST_ENABLE
    if (sys_time.low_power_mode)
    {
        return;
    }

    if (!clock_time_exceed(g_safety_runtime_tick, SAFETY_RUNTIME_PERIOD_US))
    {
        return;
    }
    g_safety_runtime_tick = clock_time();

    Safety_FlowCheckPoint(SAFETY_FLOW_ID_RUNTIME_BEGIN);
    safety_runtime_fail_if(Safety_CpuRuntimeTest(), SAFETY_CPU_FAULT);
    safety_runtime_fail_if(Safety_FlowCheckValidate(), SAFETY_FLOW_FAULT);
    safety_runtime_fail_if(Safety_ClockRuntimeTest(), SAFETY_CLOCK_FAULT);
    safety_runtime_fail_if(Safety_FlashRuntimeTask(), SAFETY_FLASH_FAULT);
    safety_runtime_fail_if(Safety_RamRuntimeTask(), SAFETY_RAM_FAULT);
    safety_runtime_fail_if(Safety_WatchdogRuntimeTest(), SAFETY_WATCHDOG_FAULT);
    safety_runtime_fail_if(Safety_AdcRuntimeCheck(), SAFETY_ADC_FAULT);
    safety_runtime_fail_if(Safety_CommRuntimeCheck(), SAFETY_AFE_COMM_FAULT);
    safety_runtime_fail_if(Safety_MosRuntimeCheck(), SAFETY_MOS_FAULT);
    Safety_FlowCheckPoint(SAFETY_FLOW_ID_RUNTIME_END);
    safety_runtime_fail_if(Safety_FlowCheckValidate(), SAFETY_FLOW_FAULT);
#endif
}
