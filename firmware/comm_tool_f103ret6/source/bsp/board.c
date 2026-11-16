#include "board.h"
#include "board_can.h"
#include "board_uart.h"
#include "board_owc.h"
#include "ct_board_port.h"
#include "ct_config.h"
#include "ct_watchdog.h"

#define BOARD_APP_LED_PERIOD_MS 200u

static volatile uint32_t s_tick_ms;

static void board_gpio_init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO |
                           RCC_APB2Periph_GPIOA |
                           RCC_APB2Periph_GPIOB |
                           RCC_APB2Periph_GPIOC |
                           RCC_APB2Periph_GPIOD,
                           ENABLE);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;

    gpio.GPIO_Pin = BOARD_DEBUG_LED_PIN;
    GPIO_Init(BOARD_DEBUG_LED_GPIO, &gpio);
    GPIO_ResetBits(BOARD_DEBUG_LED_GPIO, BOARD_DEBUG_LED_PIN);

    gpio.GPIO_Pin = BOARD_CAN_POWER_PIN;
    GPIO_Init(BOARD_CAN_POWER_GPIO, &gpio);
    GPIO_ResetBits(BOARD_CAN_POWER_GPIO, BOARD_CAN_POWER_PIN);

    gpio.GPIO_Pin = BOARD_PWSV_CTRL_PIN;
    GPIO_Init(BOARD_PWSV_CTRL_GPIO, &gpio);
    GPIO_ResetBits(BOARD_PWSV_CTRL_GPIO, BOARD_PWSV_CTRL_PIN);

    gpio.GPIO_Pin = BOARD_PWSV_STB_PIN;
    GPIO_Init(BOARD_PWSV_STB_GPIO, &gpio);
    GPIO_SetBits(BOARD_PWSV_STB_GPIO, BOARD_PWSV_STB_PIN);

    gpio.GPIO_Pin = BOARD_OFFLINE_UPGRADE_BUTTON_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(BOARD_OFFLINE_UPGRADE_BUTTON_GPIO, &gpio);
}

void Board_Init(void)
{
    SystemCoreClockUpdate();
    SCB->VTOR = CT_SELF_APP_BASE;
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    board_gpio_init();
    BoardUart_Init(CT_UART_DEFAULT_BAUD);
    BoardBmsUart_Init(CT_BMS_UART_BAUD);
    BoardCan_Init(CT_CAN_DEFAULT_BITRATE);
    BoardOwc_Init();
    (void)SysTick_Config(SystemCoreClock / 1000u);
    CtWatchdog_Init();
}

void Board_Poll(void)
{
    static uint32_t last_led_tick;
    uint32_t now;

    BoardOwc_Poll();
    now = Board_GetTickMs();
    if ((uint32_t)(now - last_led_tick) >= BOARD_APP_LED_PERIOD_MS)
    {
        last_led_tick = now;
        Board_DebugLedToggle();
    }
}

void Board_DebugLedToggle(void)
{
    if (GPIO_ReadOutputDataBit(BOARD_DEBUG_LED_GPIO, BOARD_DEBUG_LED_PIN) != Bit_RESET)
    {
        GPIO_ResetBits(BOARD_DEBUG_LED_GPIO, BOARD_DEBUG_LED_PIN);
    }
    else
    {
        GPIO_SetBits(BOARD_DEBUG_LED_GPIO, BOARD_DEBUG_LED_PIN);
    }
}

uint32_t Board_GetTickMs(void)
{
    return s_tick_ms;
}

int Board_OfflineUpgradeButtonActive(void)
{
    return (GPIO_ReadInputDataBit(BOARD_OFFLINE_UPGRADE_BUTTON_GPIO,
                                  BOARD_OFFLINE_UPGRADE_BUTTON_PIN) == Bit_RESET) ? 1 : 0;
}

void SysTick_Handler(void)
{
    s_tick_ms++;
}

uint32_t CtBoard_GetTickMs(void)
{
    return Board_GetTickMs();
}

void CtBoard_Reset(void)
{
    NVIC_SystemReset();
}

int CtBoard_OfflineUpgradeButtonActive(void)
{
    return Board_OfflineUpgradeButtonActive();
}

int CtBoard_SetBmsUartBaud(uint32_t baudrate)
{
    if ((baudrate < 1200u) || (baudrate > 460800u))
    {
        return 0;
    }
    BoardBmsUart_Init(baudrate);
    return 1;
}
