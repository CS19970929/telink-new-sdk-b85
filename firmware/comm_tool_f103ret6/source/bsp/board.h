#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>
#include "stm32f10x.h"

#define BOARD_DEBUG_LED_GPIO        GPIOB
#define BOARD_DEBUG_LED_PIN         GPIO_Pin_15

#define BOARD_CAN_POWER_GPIO        GPIOC
#define BOARD_CAN_POWER_PIN         GPIO_Pin_12
#define BOARD_PWSV_CTRL_GPIO        GPIOC
#define BOARD_PWSV_CTRL_PIN         GPIO_Pin_13
#define BOARD_PWSV_STB_GPIO         GPIOD
#define BOARD_PWSV_STB_PIN          GPIO_Pin_2
#define BOARD_OFFLINE_UPGRADE_BUTTON_GPIO GPIOA
#define BOARD_OFFLINE_UPGRADE_BUTTON_PIN  GPIO_Pin_6

void Board_Init(void);
void Board_Poll(void);
void Board_DebugLedToggle(void);
uint32_t Board_GetTickMs(void);
int Board_OfflineUpgradeButtonActive(void);

#endif
