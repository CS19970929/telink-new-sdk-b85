#include "bms_selftest_internal.h"
#include "bms_fault_inject.h"
#include "bms_selftest_port.h"

static volatile u32 g_bms_irq_count;
static u32 g_bms_irq_last_count;

void BMS_SelfTest_TimerIsrHook(void)
{
    ++g_bms_irq_count;
}

u32 BMS_SelfTest_InterruptCount(void)
{
    return g_bms_irq_count;
}

u8 BMS_SelfTest_InterruptRuntime(void)
{
    u32 now;
    u32 delta;

    if (BMS_Port_IsLowPower() || BMS_Port_IsOtaWorking())
    {
        g_bms_irq_last_count = g_bms_irq_count;
        return 1u;
    }
    now = g_bms_irq_count;
    delta = now - g_bms_irq_last_count;
    g_bms_irq_last_count = now;

    if (BMS_FaultInject_ShouldFail((u8)BMS_FAULT_INTERRUPT_LOST)) delta = 0u;
    if (BMS_FaultInject_ShouldFail((u8)BMS_FAULT_INTERRUPT_FREQUENT)) delta = BMS_SELFTEST_IRQ_MAX_PER_WINDOW + 1u;
    BMS_SelfTest_SetIrqDiag(now, delta);
    if (delta < BMS_SELFTEST_IRQ_MIN_PER_WINDOW)
    {
        BMS_SelfTest_RecordFailure(BMS_SELFTEST_ITEM_INTERRUPT, BMS_FAULT_INTERRUPT_LOST);
        return 0u;
    }
    if (delta > BMS_SELFTEST_IRQ_MAX_PER_WINDOW)
    {
        BMS_SelfTest_RecordFailure(BMS_SELFTEST_ITEM_INTERRUPT, BMS_FAULT_INTERRUPT_FREQUENT);
        return 0u;
    }
    return 1u;
}
