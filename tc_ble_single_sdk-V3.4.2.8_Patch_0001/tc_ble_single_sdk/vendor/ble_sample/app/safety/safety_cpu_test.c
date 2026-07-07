#include "safety_manager.h"

extern int safety_cpu_register_test_asm(void);

static int safety_cpu_stack_pointer_check(void)
{
    volatile u32 stack_probe = 0xA5A55A5Au;
    volatile u32 *sp_sample = &stack_probe;

    /* 通过自动变量地址确认当前栈可读写且非空指针。 */
    if (sp_sample == 0)
    {
        return 0;
    }
    if (stack_probe != 0xA5A55A5Au)
    {
        return 0;
    }
    stack_probe = 0x5A5AA5A5u;
    return stack_probe == 0x5A5AA5A5u;
}

int Safety_CpuStartupTest(void)
{
#if SAFETY_ENABLE
#if SAFETY_TEST_ENABLE && SAFETY_INJECT_CPU_FAULT
    return 0;
#else
    return (safety_cpu_register_test_asm() != 0) && safety_cpu_stack_pointer_check();
#endif
#else
    return 1;
#endif
}

int Safety_CpuRuntimeTest(void)
{
#if SAFETY_ENABLE
#if SAFETY_TEST_ENABLE && SAFETY_INJECT_CPU_FAULT
    return 0;
#else
    /* 运行期复用轻量寄存器模式检查，避免长时间阻塞 BLE 时序。 */
    return safety_cpu_register_test_asm() != 0;
#endif
#else
    return 1;
#endif
}
