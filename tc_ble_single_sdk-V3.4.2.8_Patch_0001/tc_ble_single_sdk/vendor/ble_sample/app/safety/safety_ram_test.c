#include "safety_manager.h"

static volatile u32 g_safety_ram_area[SAFETY_RAM_TEST_WORDS];
static _attribute_data_retention_ u32 g_safety_ram_runtime_index;

static int safety_ram_march_word(volatile u32 *addr)
{
    *addr = 0x00000000u;
    if (*addr != 0x00000000u)
    {
        return 0;
    }
    *addr = 0xFFFFFFFFu;
    if (*addr != 0xFFFFFFFFu)
    {
        return 0;
    }
    *addr = 0xAAAAAAAAu;
    if (*addr != 0xAAAAAAAAu)
    {
        return 0;
    }
    *addr = 0x55555555u;
    if (*addr != 0x55555555u)
    {
        return 0;
    }
    *addr = 0u;
    return 1;
}

int Safety_RamStartupTest(void)
{
#if SAFETY_ENABLE
    u32 i;

#if SAFETY_TEST_ENABLE && SAFETY_INJECT_RAM_FAULT
    return 0;
#endif

    /* 当前阶段只测试安全框架自有 RAM，避免破坏栈和业务全局变量。 */
    for (i = 0u; i < SAFETY_RAM_TEST_WORDS; ++i)
    {
        if (!safety_ram_march_word(&g_safety_ram_area[i]))
        {
            return 0;
        }
    }
    g_safety_ram_runtime_index = 0u;
    return 1;
#else
    return 1;
#endif
}

int Safety_RamRuntimeTask(void)
{
#if SAFETY_ENABLE
    u32 checked;

#if SAFETY_TEST_ENABLE && SAFETY_INJECT_RAM_FAULT
    return 0;
#endif

    for (checked = 0u; checked < SAFETY_RAM_RUNTIME_WORDS; ++checked)
    {
        if (!safety_ram_march_word(&g_safety_ram_area[g_safety_ram_runtime_index]))
        {
            return 0;
        }
        g_safety_ram_runtime_index++;
        if (g_safety_ram_runtime_index >= SAFETY_RAM_TEST_WORDS)
        {
            g_safety_ram_runtime_index = 0u;
        }
    }
    return 1;
#else
    return 1;
#endif
}
