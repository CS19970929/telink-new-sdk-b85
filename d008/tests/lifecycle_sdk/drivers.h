#pragma once
#include <stdint.h>
enum { GPIO_PD7, GPIO_PC4, GPIO_PA0, GPIO_PB1, D008_RUN_LED_PIN, Level_Low,
       DEEPSLEEP_MODE, PM_WAKEUP_PAD };
void gpio_write(unsigned pin,unsigned value);
unsigned gpio_read(unsigned pin);
void start_reboot(void);
void cpu_set_gpio_wakeup(unsigned pin,unsigned level,unsigned enabled);
void cpu_sleep_wakeup(unsigned mode,unsigned source,unsigned tick);
