#ifndef BOARD_OWC_H
#define BOARD_OWC_H
#include <stdint.h>
void BoardOwc_Init(void);
int BoardOwc_Enable(uint8_t enable);
int BoardOwc_EnableMode(uint8_t enable, uint8_t mode);
uint8_t BoardOwc_IsEnabled(void);
void BoardOwc_KeepAlive(void);
void BoardOwc_Poll(void);
#endif
