#pragma once
#include "tl_common.h"
typedef unsigned GPIO_PinTypeDef;
enum { AS_GPIO, Level_Low = 0, Level_High = 1 };
/* Model keys, deliberately not claims about electrical pin encodings. */
enum { GPIO_PA0, GPIO_PA1, GPIO_PA2, GPIO_PA3, GPIO_PA4, GPIO_PA5, GPIO_PA6, GPIO_PA7,
       GPIO_PB0, GPIO_PB1, GPIO_PB2, GPIO_PB3, GPIO_PB4, GPIO_PB5, GPIO_PB6, GPIO_PB7,
       GPIO_PC0, GPIO_PC1, GPIO_PC2, GPIO_PC3, GPIO_PC4, GPIO_PC5, GPIO_PC6, GPIO_PC7,
       GPIO_PD0, GPIO_PD1, GPIO_PD2, GPIO_PD3, GPIO_PD4, GPIO_PD5, GPIO_PD6, GPIO_PD7 };
void gpio_set_func(GPIO_PinTypeDef pin, unsigned v);
void gpio_write(GPIO_PinTypeDef pin, unsigned v);
void gpio_set_input_en(GPIO_PinTypeDef pin, unsigned v);
void gpio_set_output_en(GPIO_PinTypeDef pin, unsigned v);
int gpio_read(GPIO_PinTypeDef pin);
void cpu_set_gpio_wakeup(GPIO_PinTypeDef pin, unsigned level, unsigned enabled);
