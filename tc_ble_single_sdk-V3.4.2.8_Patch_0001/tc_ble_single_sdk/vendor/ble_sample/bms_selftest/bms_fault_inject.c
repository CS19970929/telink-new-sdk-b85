#include "bms_fault_inject.h"
#include "bms_selftest_cfg.h"

u8 BMS_FaultInject_ShouldFail(u8 fault_id)
{
#if BMS_DIAG_FAULT_INJECT_ENABLE
    if (fault_id < 32u)
    {
        return (BMS_FAULT_INJECT_MASK & (1u << fault_id)) ? 1u : 0u;
    }
#else
    (void)fault_id;
#endif
    return 0u;
}

u32 BMS_FaultInject_GetMask(void)
{
#if BMS_DIAG_FAULT_INJECT_ENABLE
    return BMS_FAULT_INJECT_MASK;
#else
    return 0u;
#endif
}
