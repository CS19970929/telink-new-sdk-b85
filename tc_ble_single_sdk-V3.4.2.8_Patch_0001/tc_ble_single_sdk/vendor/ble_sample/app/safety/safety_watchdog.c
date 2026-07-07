#include "safety_manager.h"

#include "drivers.h"
#include "app_config.h"

int Safety_WatchdogStartupTest(void)
{
#if SAFETY_ENABLE
#if SAFETY_TEST_ENABLE && SAFETY_INJECT_WDT_FAULT
    return 0;
#elif SAFETY_WATCHDOG_STARTUP_RESET_TEST_ENABLE
    /* 破坏性 WDT 复位验证需要台架流程配合，默认不在量产启动路径执行。 */
    while (1)
    {
        asm("tnop");
    }
#else
    return 1;
#endif
#else
    return 1;
#endif
}

int Safety_WatchdogRuntimeTest(void)
{
#if SAFETY_ENABLE
#if SAFETY_TEST_ENABLE && SAFETY_INJECT_WDT_FAULT
    return 0;
#elif MODULE_WATCHDOG_ENABLE
    return (reg_wd_ctrl1 & FLD_WD_EN) != 0u;
#else
    return 1;
#endif
#else
    return 1;
#endif
}
