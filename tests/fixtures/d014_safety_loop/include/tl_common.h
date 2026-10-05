#pragma once
#include "common/types.h"
#include <stddef.h>
#include <string.h>
u32 pm_get_32k_tick(void);
u32 clock_time(void);
int clock_time_exceed(u32 tick, u32 us);
