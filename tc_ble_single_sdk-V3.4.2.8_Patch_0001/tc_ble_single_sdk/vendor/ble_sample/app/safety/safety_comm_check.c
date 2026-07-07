#include "safety_manager.h"

#include "app.h"

static _attribute_data_retention_ u8 g_safety_afe_comm_fault_count;

int Safety_CommRuntimeCheck(void)
{
#if SAFETY_ENABLE
#if SAFETY_TEST_ENABLE && SAFETY_INJECT_AFE_COMM_FAULT
    return 0;
#endif

    if (System_ErrFlag.u8ErrFlag_Com_AFE1 != 0u)
    {
        if (g_safety_afe_comm_fault_count < SAFETY_AFE_COMM_FAULT_LIMIT)
        {
            g_safety_afe_comm_fault_count++;
        }
    }
    else
    {
        g_safety_afe_comm_fault_count = 0u;
    }

    return g_safety_afe_comm_fault_count < SAFETY_AFE_COMM_FAULT_LIMIT;
#else
    return 1;
#endif
}
