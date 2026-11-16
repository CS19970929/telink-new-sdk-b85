#include "ct_watchdog.h"
#include "ct_config.h"
#include "stm32f10x.h"
#include <stdint.h>

#ifndef CT_WATCHDOG_ENABLE
#define CT_WATCHDOG_ENABLE              1u
#endif

#ifndef CT_WATCHDOG_RELOAD_VALUE
#define CT_WATCHDOG_RELOAD_VALUE        1875u
#endif

#define CT_IWDG_KEY_ENABLE_WRITE        0x5555u
#define CT_IWDG_KEY_RELOAD              0xAAAAu
#define CT_IWDG_KEY_START               0xCCCCu
#define CT_IWDG_PRESCALER_256           6u
#define CT_IWDG_LSI_READY_TIMEOUT       1000000u
#define CT_IWDG_UPDATE_TIMEOUT          1000000u

static uint8_t s_watchdog_started;

void CtWatchdog_Init(void)
{
#if (CT_WATCHDOG_ENABLE != 0u)
    uint32_t wait;

    RCC->CSR |= RCC_CSR_LSION;
    wait = CT_IWDG_LSI_READY_TIMEOUT;
    while (((RCC->CSR & RCC_CSR_LSIRDY) == 0u) && (wait > 0u))
    {
        wait--;
    }
    if ((RCC->CSR & RCC_CSR_LSIRDY) == 0u)
    {
        return;
    }

    IWDG->KR = CT_IWDG_KEY_ENABLE_WRITE;
    IWDG->PR = CT_IWDG_PRESCALER_256;
    IWDG->RLR = CT_WATCHDOG_RELOAD_VALUE & 0x0FFFu;

    wait = CT_IWDG_UPDATE_TIMEOUT;
    while (((IWDG->SR & (IWDG_SR_PVU | IWDG_SR_RVU)) != 0u) && (wait > 0u))
    {
        wait--;
    }

    IWDG->KR = CT_IWDG_KEY_RELOAD;
    IWDG->KR = CT_IWDG_KEY_START;
    s_watchdog_started = 1u;
#endif
}

void CtWatchdog_Feed(void)
{
#if (CT_WATCHDOG_ENABLE != 0u)
    if (s_watchdog_started != 0u)
    {
        IWDG->KR = CT_IWDG_KEY_RELOAD;
    }
#endif
}
