#include "bms_selftest_internal.h"
#include "bms_fault_inject.h"

typedef struct {
    u32 counter;
    u32 inverse;
} bms_flow_counter_t;

static u8 bms_flow_advance(bms_flow_counter_t *flow, u32 token)
{
    flow->counter += token;
    flow->inverse -= token;
    return ((flow->counter ^ flow->inverse) == 0xffffffffu) ? 1u : 0u;
}

static u8 bms_flow_sequence(u32 first, u32 second, u32 third)
{
    bms_flow_counter_t flow;

    flow.counter = 0u;
    flow.inverse = 0xffffffffu;
    if (!bms_flow_advance(&flow, first))
    {
        return 0u;
    }
    if (BMS_FaultInject_ShouldFail((u8)BMS_FAULT_CONTROL_FLOW))
    {
        second ^= 1u;
        flow.inverse += 1u;
    }
    if (!bms_flow_advance(&flow, second))
    {
        return 0u;
    }
    if (!bms_flow_advance(&flow, third))
    {
        return 0u;
    }
    return 1u;
}

u8 BMS_SelfTest_ControlFlowStartup(void)
{
    return bms_flow_sequence(0x13579bdfu, 0x2468ace0u, 0x10204081u);
}

u8 BMS_SelfTest_ControlFlowRuntime(void)
{
    return bms_flow_sequence(0x00010011u, 0x00020023u, 0x00040047u);
}
