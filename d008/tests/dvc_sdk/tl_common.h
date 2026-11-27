#pragma once
#include <stdbool.h>
#include <stdint.h>
#define CLOCK_SYS_CLOCK_HZ 16000000u
uint32_t clock_time(void);
bool clock_time_exceed(uint32_t start,uint32_t microseconds);
