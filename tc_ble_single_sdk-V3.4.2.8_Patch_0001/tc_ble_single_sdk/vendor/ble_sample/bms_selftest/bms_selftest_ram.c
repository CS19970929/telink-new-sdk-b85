#include "bms_selftest_internal.h"
#include "bms_fault_inject.h"

static volatile u32 g_bms_ram_test[BMS_SELFTEST_RAM_WORDS];
static u16 g_bms_ram_runtime_index;

static u8 bms_ram_compare(volatile u32 *address, u32 expected)
{
    u32 actual = *address;

    if (BMS_FaultInject_ShouldFail((u8)BMS_FAULT_RAM))
    {
        actual ^= 1u;
    }
    return (actual == expected) ? 1u : 0u;
}

u8 BMS_SelfTest_RamStartup(void)
{
    u16 i;

    for (i = 0u; i < BMS_SELFTEST_RAM_WORDS; ++i)
    {
        g_bms_ram_test[i] = 0u;
    }
    for (i = 0u; i < BMS_SELFTEST_RAM_WORDS; ++i)
    {
        if (!bms_ram_compare(&g_bms_ram_test[i], 0u)) return 0u;
        g_bms_ram_test[i] = 0xffffffffu;
    }
    for (i = BMS_SELFTEST_RAM_WORDS; i > 0u; --i)
    {
        if (!bms_ram_compare(&g_bms_ram_test[i - 1u], 0xffffffffu)) return 0u;
        g_bms_ram_test[i - 1u] = 0u;
    }
    for (i = 0u; i < BMS_SELFTEST_RAM_WORDS; ++i)
    {
        if (!bms_ram_compare(&g_bms_ram_test[i], 0u)) return 0u;
        g_bms_ram_test[i] = 0xaaaaaaaau;
    }
    for (i = BMS_SELFTEST_RAM_WORDS; i > 0u; --i)
    {
        if (!bms_ram_compare(&g_bms_ram_test[i - 1u], 0xaaaaaaaau)) return 0u;
        g_bms_ram_test[i - 1u] = 0x55555555u;
    }
    for (i = 0u; i < BMS_SELFTEST_RAM_WORDS; ++i)
    {
        if (!bms_ram_compare(&g_bms_ram_test[i], 0x55555555u)) return 0u;
        g_bms_ram_test[i] = 0u;
    }
    g_bms_ram_runtime_index = 0u;
    return 1u;
}

u8 BMS_SelfTest_RamRuntimeStep(void)
{
    u32 saved[BMS_SELFTEST_RAM_RUNTIME_WORDS];
    u16 i;
    u16 base = g_bms_ram_runtime_index;
    u8 ok = 1u;

    for (i = 0u; i < BMS_SELFTEST_RAM_RUNTIME_WORDS; ++i)
    {
        saved[i] = g_bms_ram_test[base + i];
        g_bms_ram_test[base + i] = 0xaaaaaaaau;
        if (!bms_ram_compare(&g_bms_ram_test[base + i], 0xaaaaaaaau)) ok = 0u;
        g_bms_ram_test[base + i] = 0x55555555u;
        if (!bms_ram_compare(&g_bms_ram_test[base + i], 0x55555555u)) ok = 0u;
        g_bms_ram_test[base + i] = saved[i];
        if (!bms_ram_compare(&g_bms_ram_test[base + i], saved[i])) ok = 0u;
    }

    base = (u16)(base + BMS_SELFTEST_RAM_RUNTIME_WORDS);
    if (base >= BMS_SELFTEST_RAM_WORDS)
    {
        base = 0u;
    }
    g_bms_ram_runtime_index = base;
    return ok;
}
