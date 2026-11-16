#ifndef BOARD_UART_H
#define BOARD_UART_H

#include <stdint.h>

void BoardUart_Init(uint32_t baudrate);
int BoardUart_ReadByte(uint8_t *byte);
void BoardBmsUart_Init(uint32_t baudrate);
int BoardBmsUart_ReadByte(uint8_t *byte);
int BoardBmsUart_Write(const uint8_t *data, uint16_t length);

#endif
