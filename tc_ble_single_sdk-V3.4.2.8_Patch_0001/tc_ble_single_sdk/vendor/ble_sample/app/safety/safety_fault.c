#include "safety_manager.h"

#include "drivers.h"
#include "app.h"
#include "conf.h"

void close_ctlc(void);
void close_chg(void);
void close_dsg(void);

static _attribute_ram_code_ void safety_gpio_output_low(GPIO_PinTypeDef pin)
{
    u8 bit = pin & 0xffu;

    BM_SET(reg_gpio_func(pin), bit);
    BM_CLR(reg_gpio_oen(pin), bit);
    BM_CLR(reg_gpio_out(pin), bit);
}

static void safety_mark_legacy_error(SafetyFaultType fault)
{
    /* 将安全故障映射到现有错误位，便于旧上位机至少看到异常状态。 */
    switch (fault)
    {
    case SAFETY_ADC_FAULT:
        System_ErrFlag.u8ErrFlag_ADC = 1u;
        break;
    case SAFETY_AFE_COMM_FAULT:
        System_ErrFlag.u8ErrFlag_Com_AFE1 = 1u;
        break;
    case SAFETY_CLOCK_FAULT:
        System_ErrFlag.u8ErrFlag_HSE = 1u;
        break;
    default:
        System_ErrFlag.u8ErrFlag_Com_App = 1u;
        break;
    }
}

_attribute_ram_code_ void Safety_EnterFailSafeHw(SafetyFaultType fault)
{
    volatile u32 delay;

    safety_mark_legacy_error(fault);

    /* 先直接关闭硬件控制脚，避免依赖 AFE 通信成功。 */
    irq_disable();
    safety_gpio_output_low(AFE_CTL_PIN);
    safety_gpio_output_low(MCC_C_PIN);
    safety_gpio_output_low(ADC_BUSEN_PIN);
    safety_gpio_output_low(ADC_EN_PIN);

    /* 如果业务初始化已经完成，再尝试走原有 AFE MOS 关闭路径。 */
    if (Safety_IsApplicationReady())
    {
        close_ctlc();
        close_chg();
        close_dsg();
    }

    /* 不再喂狗，等待现有 WDT 将 MCU 拉回安全启动流程。 */
    while (1)
    {
        for (delay = 0u; delay < 0x2000u; ++delay)
        {
            asm("tnop");
        }
    }
}
