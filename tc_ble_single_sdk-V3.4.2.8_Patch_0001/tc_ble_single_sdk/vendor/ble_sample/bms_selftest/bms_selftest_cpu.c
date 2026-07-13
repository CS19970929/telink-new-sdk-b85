#include "bms_selftest_internal.h"
#include "bms_fault_inject.h"

extern u32 BMS_SelfTest_Tc32CpuRegPatternAsm(u32 pattern);
extern u32 BMS_SelfTest_Tc32ControlFlowAsm(void);

static const u32 g_bms_cpu_patterns[] = {
    0x00000000u,
    0xffffffffu,
    0xaaaaaaaau,
    0x55555555u,
    0x00000001u,
    0x80000000u,
    0xfffffffeu,
    0x7fffffffu
};

static u8 bms_selftest_cpu_pattern(u32 pattern)
{
    if (BMS_FaultInject_ShouldFail((u8)BMS_FAULT_CPU_REGISTER))
    {
        pattern ^= 1u;
    }
    return (BMS_SelfTest_Tc32CpuRegPatternAsm(pattern) == 1u) ? 1u : 0u;
}

u8 BMS_SelfTest_CpuStartup(void)
{
    u8 i;

    for (i = 0u; i < (u8)(sizeof(g_bms_cpu_patterns) / sizeof(g_bms_cpu_patterns[0])); ++i)
    {
        if (!bms_selftest_cpu_pattern(g_bms_cpu_patterns[i]))
        {
            return 0u;
        }
    }
    if (BMS_FaultInject_ShouldFail((u8)BMS_FAULT_CPU_FLAG))
    {
        return 0u;
    }
    return (BMS_SelfTest_Tc32ControlFlowAsm() == 1u) ? 1u : 0u;
}

u8 BMS_SelfTest_CpuRuntime(void)
{
    static u8 pattern_index;
    u32 pattern = g_bms_cpu_patterns[pattern_index];

    pattern_index++;
    if (pattern_index >= (u8)(sizeof(g_bms_cpu_patterns) / sizeof(g_bms_cpu_patterns[0])))
    {
        pattern_index = 0u;
    }
    if (!bms_selftest_cpu_pattern(pattern))
    {
        return 0u;
    }
    return (BMS_SelfTest_Tc32ControlFlowAsm() == 1u) ? 1u : 0u;
}
