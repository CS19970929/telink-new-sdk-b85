#include "safety_manager.h"

static volatile u32 g_safety_flow_counter;
static volatile u32 g_safety_flow_counter_inv = 0xFFFFFFFFu;

int Safety_FlowCheckInit(void)
{
#if SAFETY_ENABLE
    g_safety_flow_counter = 0u;
    g_safety_flow_counter_inv = 0xFFFFFFFFu;
#endif
    return 1;
}

void Safety_FlowCheckPoint(u32 id)
{
#if SAFETY_ENABLE
    u32 next;

    if (!Safety_FlowCheckValidate())
    {
        Safety_EnterFailSafe(SAFETY_FLOW_FAULT);
    }

    next = g_safety_flow_counter + (id ^ 0x5A5Au);
    g_safety_flow_counter = next;
    g_safety_flow_counter_inv = ~next;
#else
    (void)id;
#endif
}

int Safety_FlowCheckValidate(void)
{
#if SAFETY_ENABLE
    return (g_safety_flow_counter ^ g_safety_flow_counter_inv) == 0xFFFFFFFFu;
#else
    return 1;
#endif
}
